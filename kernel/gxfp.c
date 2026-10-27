// SPDX-License-Identifier: GPL-2.0-only

#include <linux/acpi.h>
#include <linux/atomic.h>
#include <linux/align.h>
#include <linux/cleanup.h>
#include <linux/capability.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/io.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/kfifo.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/poll.h>
#include <linux/kref.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/ktime.h>
#include <linux/types.h>
#include <linux/unaligned.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#include <linux/gxfp.h>

#define GXFP_DRV_NAME		"gxfp"
#define GXFP_WINDOW_SIZE		0x1000u
#define GXFP_RX_OFFSET		0x200u
#define GXFP_RX_FIFO_SIZE		SZ_256K

#define GXFP_WRITE_DONE_PRE_US	50
#define GXFP_WRITE_DONE_HIGH_US	4000
#define GXFP_WRITE_DONE_POST_US	200
#define GXFP_READ_DONE_HIGH_US	50
#define GXFP_READ_DONE_POST_US	200

struct gxfp {
	struct device *dev;
	struct miscdevice misc;

	void __iomem *window;

	struct gpio_desc *gpio_write_done;
	struct gpio_desc *gpio_read_done;
	struct gpio_desc *gpio_irq;
	int irq;
	unsigned int irq_flags;

	u8 *tx_buf;
	u8 *rx_buf;
	u8 *rx_read_buf;
	void *rx_fifo_buf;

	/* Protects opened/removed and open against remove. */
	struct mutex open_lock;
	/* Serializes TX with remove/suspend. */
	struct mutex tx_lock;
	/* Serializes record-oriented read operations. */
	struct mutex rx_read_lock;

	/* Protects rx_fifo and the fast-path flags. */
	spinlock_t rx_lock;
	struct kfifo rx_fifo;
	wait_queue_head_t rx_wait;

	bool rx_fifo_ready;
	bool rx_overflow;
	atomic_t queued_records;
	bool opened;
	bool removed;
	bool suspended;

	struct kref refs;
};

/* --------------------------------------------------------------------------
 * ACPI _CRS GPIO resolution
 */

struct gxfp_crs {
	int gpio_seen;
	int io_seen;
	int int_seen;
	u8 io_pol[2];
	int io_index[2];
	int int_index;
	u8 int_triggering;
	u8 int_polarity;
};

static int gxfp_crs_cb(struct acpi_resource *ares, void *context)
{
	struct gxfp_crs *st = context;
	struct acpi_resource_gpio *gpio;

	if (!ares || ares->type != ACPI_RESOURCE_TYPE_GPIO)
		return 0;

	gpio = &ares->data.gpio;
	st->gpio_seen++;

	if (gpio->connection_type == ACPI_RESOURCE_GPIO_TYPE_INT)
		st->int_seen++;
	else if (gpio->connection_type == ACPI_RESOURCE_GPIO_TYPE_IO)
		st->io_seen++;

	if (!gpio->pin_table_length)
		return 0;

	if (gpio->connection_type == ACPI_RESOURCE_GPIO_TYPE_INT) {
		if (st->int_index < 0) {
			st->int_index = st->gpio_seen - 1;
			st->int_triggering = gpio->triggering;
			st->int_polarity = gpio->polarity;
		}
	} else if (gpio->connection_type == ACPI_RESOURCE_GPIO_TYPE_IO &&
		   st->io_seen <= ARRAY_SIZE(st->io_index)) {
		st->io_pol[st->io_seen - 1] = gpio->polarity;
		st->io_index[st->io_seen - 1] = st->gpio_seen - 1;
	}

	return 0;
}

static unsigned int gxfp_acpi_irq_type(u8 triggering, u8 polarity)
{
	bool low = polarity == ACPI_ACTIVE_LOW;

	if (polarity == ACPI_ACTIVE_BOTH)
		return triggering == ACPI_EDGE_SENSITIVE ?
			IRQ_TYPE_EDGE_BOTH : IRQ_TYPE_NONE;

	if (triggering == ACPI_LEVEL_SENSITIVE)
		return low ? IRQ_TYPE_LEVEL_LOW : IRQ_TYPE_LEVEL_HIGH;

	if (triggering == ACPI_EDGE_SENSITIVE)
		return low ? IRQ_TYPE_EDGE_FALLING : IRQ_TYPE_EDGE_RISING;

	return IRQ_TYPE_NONE;
}

static int gxfp_add_gpio_mapping(struct platform_device *pdev,
				 unsigned int *irq_flags)
{
	struct acpi_device *adev = ACPI_COMPANION(&pdev->dev);
	struct gxfp_crs st = {
		.int_index = -1,
		.io_index = { -1, -1 },
	};
	struct acpi_gpio_params *params;
	struct acpi_gpio_mapping *map;
	LIST_HEAD(res_list);
	bool active_low;
	int ret;

	if (!adev)
		return -ENODEV;

	ret = acpi_dev_get_resources(adev, &res_list, gxfp_crs_cb, &st);
	acpi_dev_free_resource_list(&res_list);
	if (ret < 0)
		return ret;

	if (st.int_index < 0 || st.int_seen != 1 || st.io_seen != 2 ||
	    st.io_index[0] < 0 || st.io_index[1] < 0)
		return -ENOENT;

	if (st.int_polarity != ACPI_ACTIVE_HIGH &&
	    st.int_polarity != ACPI_ACTIVE_LOW)
		return -EINVAL;
	if ((st.io_pol[0] != ACPI_ACTIVE_HIGH &&
	     st.io_pol[0] != ACPI_ACTIVE_LOW) ||
	    (st.io_pol[1] != ACPI_ACTIVE_HIGH &&
	     st.io_pol[1] != ACPI_ACTIVE_LOW))
		return -EINVAL;
	if (st.io_pol[0] != st.io_pol[1]) {
		dev_warn(&pdev->dev,
			 "ACPI _CRS: strobe GPIO polarities differ\n");
		return -EINVAL;
	}
	active_low = st.io_pol[0] == ACPI_ACTIVE_LOW;

	if (irq_flags)
		*irq_flags = gxfp_acpi_irq_type(st.int_triggering,
						st.int_polarity);

	params = devm_kcalloc(&pdev->dev, 3, sizeof(*params), GFP_KERNEL);
	map = devm_kcalloc(&pdev->dev, 4, sizeof(*map), GFP_KERNEL);
	if (!params || !map)
		return -ENOMEM;

	params[0].crs_entry_index = st.int_index;
	params[0].line_index = 0;
	params[0].active_low = false;
	map[0].name = "irq-gpios";
	map[0].data = &params[0];
	map[0].size = 1;

	params[1].crs_entry_index = st.io_index[0];
	params[1].line_index = 0;
	params[1].active_low = active_low;
	map[1].name = "write-done-gpios";
	map[1].data = &params[1];
	map[1].size = 1;

	params[2].crs_entry_index = st.io_index[1];
	params[2].line_index = 0;
	params[2].active_low = active_low;
	map[2].name = "read-done-gpios";
	map[2].data = &params[2];
	map[2].size = 1;

	/* map[3] is the terminator */

	return devm_acpi_dev_add_driver_gpios(&pdev->dev, map);
}

static int gxfp_init_gpios(struct platform_device *pdev,
			   struct gxfp *g)
{
	struct gpio_desc *desc;
	bool mapped = false;
	int ret;

	desc = devm_gpiod_get(&pdev->dev, "write-done", GPIOD_OUT_LOW);
	if (IS_ERR(desc) && PTR_ERR(desc) == -ENOENT) {
		ret = gxfp_add_gpio_mapping(pdev, &g->irq_flags);
		if (ret)
			return dev_err_probe(&pdev->dev, ret,
					     "failed to add ACPI GPIO mapping\n");
		mapped = true;
		desc = devm_gpiod_get(&pdev->dev, "write-done", GPIOD_OUT_LOW);
	}
	if (IS_ERR(desc))
		return dev_err_probe(&pdev->dev, PTR_ERR(desc),
				     "failed to get write-done GPIO\n");
	g->gpio_write_done = desc;

	desc = devm_gpiod_get(&pdev->dev, "read-done", GPIOD_OUT_LOW);
	if (!mapped && IS_ERR(desc) && PTR_ERR(desc) == -ENOENT) {
		ret = gxfp_add_gpio_mapping(pdev, &g->irq_flags);
		if (ret)
			return dev_err_probe(&pdev->dev, ret,
					     "failed to add ACPI GPIO mapping\n");
		mapped = true;
		desc = devm_gpiod_get(&pdev->dev, "read-done", GPIOD_OUT_LOW);
	}
	if (IS_ERR(desc))
		return dev_err_probe(&pdev->dev, PTR_ERR(desc),
				     "failed to get read-done GPIO\n");
	g->gpio_read_done = desc;

	desc = devm_gpiod_get(&pdev->dev, "irq", GPIOD_IN);
	if (!mapped && IS_ERR(desc) && PTR_ERR(desc) == -ENOENT) {
		ret = gxfp_add_gpio_mapping(pdev, &g->irq_flags);
		if (ret)
			return dev_err_probe(&pdev->dev, ret,
					     "failed to add ACPI GPIO mapping\n");
		desc = devm_gpiod_get(&pdev->dev, "irq", GPIOD_IN);
	}
	if (IS_ERR(desc))
		return dev_err_probe(&pdev->dev, PTR_ERR(desc),
				     "failed to get irq GPIO\n");
	g->gpio_irq = desc;

	gpiod_set_value_cansleep(g->gpio_write_done, 0);
	gpiod_set_value_cansleep(g->gpio_read_done, 0);

	return 0;
}

/* --------------------------------------------------------------------------
 * Mailbox access
 */

static void gxfp_write_tx(struct gxfp *g, size_t len)
{
	size_t offset;

	for (offset = 0; offset < len; offset += sizeof(u64)) {
		u64 value = get_unaligned_le64(g->tx_buf + offset);

		writeq(value, g->window + offset);
	}
}

static void gxfp_read_rx(struct gxfp *g)
{
	size_t offset;

	for (offset = 0; offset < GXFP_RX_MAX; offset += sizeof(u64)) {
		u64 value = readq(g->window + GXFP_RX_OFFSET + offset);

		put_unaligned_le64(value, g->rx_buf + offset);
	}
	/* Complete the mailbox snapshot before parsing or acknowledging it. */
	rmb();
}

static void gxfp_pulse_write_done(struct gxfp *g)
{
	gpiod_set_value_cansleep(g->gpio_write_done, 0);
	fsleep(GXFP_WRITE_DONE_PRE_US);
	gpiod_set_value_cansleep(g->gpio_write_done, 1);
	fsleep(GXFP_WRITE_DONE_HIGH_US);
	gpiod_set_value_cansleep(g->gpio_write_done, 0);
	fsleep(GXFP_WRITE_DONE_POST_US);
}

static void gxfp_pulse_read_done(struct gxfp *g)
{
	gpiod_set_value_cansleep(g->gpio_read_done, 1);
	fsleep(GXFP_READ_DONE_HIGH_US);
	gpiod_set_value_cansleep(g->gpio_read_done, 0);
	fsleep(GXFP_READ_DONE_POST_US);
}

/* --------------------------------------------------------------------------
 * Interrupt handling
 */

static irqreturn_t gxfp_irq_thread(int irq, void *data)
{
	struct gxfp *g = data;
	struct gxfp_rx_record header;
	unsigned long flags;
	bool dropped = false;
	bool queue = !READ_ONCE(g->removed) && !READ_ONCE(g->suspended);

	memset(&header, 0, sizeof(header));
	header.len = GXFP_RX_MAX;
	header.timestamp_ns = ktime_get_ns();

	gxfp_read_rx(g);
	gxfp_pulse_read_done(g);

	if (!queue)
		return IRQ_HANDLED;

	spin_lock_irqsave(&g->rx_lock, flags);
	if (g->rx_fifo_ready &&
	    kfifo_avail(&g->rx_fifo) >= GXFP_RX_RECORD_SIZE) {
		kfifo_in(&g->rx_fifo, &header, sizeof(header));
		kfifo_in(&g->rx_fifo, g->rx_buf, GXFP_RX_MAX);
		atomic_inc(&g->queued_records);
	} else {
		g->rx_overflow = true;
		kfifo_reset(&g->rx_fifo);
		atomic_set(&g->queued_records, 0);
		dropped = true;
	}
	spin_unlock_irqrestore(&g->rx_lock, flags);

	if (dropped)
		dev_warn_ratelimited(g->dev,
				     "RX queue full; dropping %u-byte record\n",
				     GXFP_RX_MAX);

	wake_up_interruptible(&g->rx_wait);
	return IRQ_HANDLED;
}

/* --------------------------------------------------------------------------
 * Character device
 */

static int gxfp_open(struct inode *inode, struct file *file)
{
	struct miscdevice *m = file->private_data;
	struct gxfp *g = container_of(m, struct gxfp, misc);

	(void)inode;

	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;

	guard(mutex)(&g->open_lock);

	if (READ_ONCE(g->removed))
		return -ENODEV;
	if (g->opened)
		return -EBUSY;

	g->opened = true;
	kref_get(&g->refs);

	file->private_data = g;
	return 0;
}

static void gxfp_release_dev(struct kref *ref)
{
	struct gxfp *g = container_of(ref, struct gxfp, refs);

	kfree(g->tx_buf);
	kfree(g->rx_buf);
	kfree(g->rx_read_buf);
	kvfree(g->rx_fifo_buf);
	mutex_destroy(&g->open_lock);
	mutex_destroy(&g->tx_lock);
	mutex_destroy(&g->rx_read_lock);
	kfree(g);
}

static void gxfp_put(struct gxfp *g)
{
	kref_put(&g->refs, gxfp_release_dev);
}

static void gxfp_put_action(void *data)
{
	gxfp_put(data);
}

static int gxfp_release(struct inode *inode, struct file *file)
{
	struct gxfp *g = file->private_data;

	(void)inode;

	if (!g)
		return 0;

	mutex_lock(&g->open_lock);
	g->opened = false;
	mutex_unlock(&g->open_lock);

	gxfp_put(g);
	return 0;
}

static ssize_t gxfp_write(struct file *file, const char __user *buf,
			  size_t count, loff_t *ppos)
{
	struct gxfp *g = file->private_data;
	size_t tx_len;

	(void)ppos;

	if (!g)
		return -ENODEV;
	if (count == 0 || count > GXFP_TX_MAX)
		return -EMSGSIZE;

	guard(mutex)(&g->tx_lock);

	if (READ_ONCE(g->removed))
		return -ENODEV;
	if (READ_ONCE(g->suspended))
		return -EHOSTDOWN;

	memset(g->tx_buf, 0, GXFP_TX_MAX);
	if (copy_from_user(g->tx_buf, buf, count))
		return -EFAULT;

	tx_len = ALIGN(count, sizeof(u64));
	gxfp_write_tx(g, tx_len);
	/* Publish every qword before asserting the write-done doorbell. */
	wmb();
	gxfp_pulse_write_done(g);

	return count;
}

static ssize_t gxfp_read(struct file *file, char __user *buf,
			 size_t count, loff_t *ppos)
{
	struct gxfp *g = file->private_data;
	unsigned long flags;
	int ret;

	(void)ppos;

	if (!g)
		return -ENODEV;
	if (count < GXFP_RX_RECORD_SIZE)
		return -EMSGSIZE;

	ret = mutex_lock_interruptible(&g->rx_read_lock);
	if (ret)
		return ret;

	for (;;) {
		spin_lock_irqsave(&g->rx_lock, flags);

		if (READ_ONCE(g->removed)) {
			spin_unlock_irqrestore(&g->rx_lock, flags);
			ret = -ENODEV;
			break;
		}
		if (READ_ONCE(g->suspended)) {
			spin_unlock_irqrestore(&g->rx_lock, flags);
			ret = -EHOSTDOWN;
			break;
		}
		if (!g->rx_fifo_ready) {
			spin_unlock_irqrestore(&g->rx_lock, flags);
			ret = -ENODEV;
			break;
		}
		if (g->rx_overflow) {
			g->rx_overflow = false;
			spin_unlock_irqrestore(&g->rx_lock, flags);
			ret = -ENOBUFS;
			break;
		}

		if (atomic_read(&g->queued_records) > 0) {
			if (kfifo_out(&g->rx_fifo, g->rx_read_buf,
				      GXFP_RX_RECORD_SIZE) !=
			    GXFP_RX_RECORD_SIZE) {
				kfifo_reset(&g->rx_fifo);
				atomic_set(&g->queued_records, 0);
				spin_unlock_irqrestore(&g->rx_lock, flags);
				ret = -EIO;
				break;
			}
			atomic_dec(&g->queued_records);
			spin_unlock_irqrestore(&g->rx_lock, flags);

			if (copy_to_user(buf, g->rx_read_buf,
					 GXFP_RX_RECORD_SIZE))
				ret = -EFAULT;
			else
				ret = GXFP_RX_RECORD_SIZE;
			break;
		}

		spin_unlock_irqrestore(&g->rx_lock, flags);

		if (file->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			break;
		}

		ret = wait_event_interruptible(g->rx_wait,
					       atomic_read(&g->queued_records) > 0 ||
					       !READ_ONCE(g->rx_fifo_ready) ||
					       READ_ONCE(g->rx_overflow) ||
					       READ_ONCE(g->removed) ||
					       READ_ONCE(g->suspended));
		if (ret)
			break;
	}

	mutex_unlock(&g->rx_read_lock);
	return ret;
}

static __poll_t gxfp_poll(struct file *file, poll_table *wait)
{
	struct gxfp *g = file->private_data;
	unsigned long flags;
	__poll_t mask = 0;

	if (!g)
		return EPOLLERR;

	poll_wait(file, &g->rx_wait, wait);

	spin_lock_irqsave(&g->rx_lock, flags);
	if (READ_ONCE(g->removed))
		mask = EPOLLERR | EPOLLHUP;
	else if (READ_ONCE(g->suspended))
		mask = EPOLLERR;
	else if (READ_ONCE(g->rx_overflow))
		mask = EPOLLIN | EPOLLERR;
	else if (g->rx_fifo_ready &&
		 atomic_read(&g->queued_records) > 0)
		mask = EPOLLIN | EPOLLRDNORM;
	spin_unlock_irqrestore(&g->rx_lock, flags);

	return mask;
}

static long gxfp_ioctl(struct file *file, unsigned int cmd,
		       unsigned long arg)
{
	struct gxfp *g = file->private_data;
	unsigned long flags;

	(void)arg;

	if (!g)
		return -ENODEV;

	switch (cmd) {
	case GXFP_IOCTL_FLUSH_RX:
		if (READ_ONCE(g->removed))
			return -ENODEV;
		spin_lock_irqsave(&g->rx_lock, flags);
		if (g->rx_fifo_ready)
			kfifo_reset(&g->rx_fifo);
		atomic_set(&g->queued_records, 0);
		g->rx_overflow = false;
		spin_unlock_irqrestore(&g->rx_lock, flags);
		return 0;
	default:
		return -ENOTTY;
	}
}

static const struct file_operations gxfp_fops = {
	.owner = THIS_MODULE,
	.open = gxfp_open,
	.release = gxfp_release,
	.read = gxfp_read,
	.write = gxfp_write,
	.poll = gxfp_poll,
	.unlocked_ioctl = gxfp_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = compat_ptr_ioctl,
#endif
	.llseek = noop_llseek,
};

/* --------------------------------------------------------------------------
 * probe / remove / PM
 */

static int gxfp_probe(struct platform_device *pdev)
{
	struct gxfp *g;
	struct resource *res;
	unsigned int irq_type;
	int ret;

	g = kzalloc_obj(*g);
	if (!g)
		return -ENOMEM;

	g->dev = &pdev->dev;
	mutex_init(&g->open_lock);
	mutex_init(&g->tx_lock);
	mutex_init(&g->rx_read_lock);
	spin_lock_init(&g->rx_lock);
	init_waitqueue_head(&g->rx_wait);
	atomic_set(&g->queued_records, 0);
	kref_init(&g->refs);
	platform_set_drvdata(pdev, g);

	ret = devm_add_action_or_reset(&pdev->dev, gxfp_put_action, g);
	if (ret)
		return ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		return dev_err_probe(&pdev->dev, -ENODEV,
				     "missing mailbox MMIO resource\n");
	}
	if (resource_size(res) < GXFP_WINDOW_SIZE) {
		return dev_err_probe(&pdev->dev, -ENODEV,
				     "mailbox window too small: %pa\n",
				     &res->start);
	}

	g->window = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(g->window))
		return PTR_ERR(g->window);

	g->tx_buf = kzalloc(GXFP_TX_MAX, GFP_KERNEL);
	if (!g->tx_buf)
		return -ENOMEM;

	g->rx_buf = kzalloc(GXFP_RX_MAX, GFP_KERNEL);
	if (!g->rx_buf)
		return -ENOMEM;

	g->rx_read_buf = kzalloc(GXFP_RX_RECORD_SIZE, GFP_KERNEL);
	if (!g->rx_read_buf)
		return -ENOMEM;

	g->rx_fifo_buf = kvmalloc(GXFP_RX_FIFO_SIZE, GFP_KERNEL);
	if (!g->rx_fifo_buf)
		return -ENOMEM;

	ret = kfifo_init(&g->rx_fifo, g->rx_fifo_buf, GXFP_RX_FIFO_SIZE);
	if (ret)
		return ret;
	g->rx_fifo_ready = true;

	ret = gxfp_init_gpios(pdev, g);
	if (ret)
		return ret;

	g->irq = gpiod_to_irq(g->gpio_irq);
	if (g->irq < 0) {
		return dev_err_probe(&pdev->dev, g->irq,
				     "failed to map IRQ\n");
	}

	irq_type = g->irq_flags ?: irq_get_trigger_type(g->irq);
	if (irq_type == IRQ_TYPE_NONE) {
		return dev_err_probe(&pdev->dev, -EINVAL,
				     "missing IRQ trigger type\n");
	}

	ret = irq_set_irq_type(g->irq, irq_type);
	if (ret) {
		return dev_err_probe(&pdev->dev, ret,
				     "failed to set IRQ type %#x\n", irq_type);
	}
	g->irq_flags = irq_type;

	ret = devm_request_threaded_irq(&pdev->dev, g->irq, NULL,
					gxfp_irq_thread,
					IRQF_ONESHOT | IRQF_NO_AUTOEN,
					GXFP_DRV_NAME, g);
	if (ret) {
		return dev_err_probe(&pdev->dev, ret,
				     "failed to request IRQ %d\n", g->irq);
	}

	g->misc.minor = MISC_DYNAMIC_MINOR;
	g->misc.name = GXFP_DRV_NAME;
	g->misc.fops = &gxfp_fops;
	g->misc.parent = &pdev->dev;
	g->misc.mode = 0600;
	ret = misc_register(&g->misc);
	if (ret) {
		return dev_err_probe(&pdev->dev, ret,
				     "failed to register misc device\n");
	}

	enable_irq(g->irq);
	return 0;
}

static void gxfp_remove(struct platform_device *pdev)
{
	struct gxfp *g = platform_get_drvdata(pdev);
	unsigned long flags;

	if (!g)
		return;

	mutex_lock(&g->open_lock);
	WRITE_ONCE(g->removed, true);
	mutex_unlock(&g->open_lock);

	misc_deregister(&g->misc);

	mutex_lock(&g->tx_lock);
	if (!READ_ONCE(g->suspended))
		disable_irq(g->irq);
	gpiod_set_value_cansleep(g->gpio_write_done, 0);
	gpiod_set_value_cansleep(g->gpio_read_done, 0);

	spin_lock_irqsave(&g->rx_lock, flags);
	if (g->rx_fifo_ready)
		kfifo_reset(&g->rx_fifo);
	atomic_set(&g->queued_records, 0);
	g->rx_fifo_ready = false;
	g->rx_overflow = false;
	spin_unlock_irqrestore(&g->rx_lock, flags);

	wake_up_interruptible_all(&g->rx_wait);
	mutex_unlock(&g->tx_lock);
}

static int gxfp_suspend(struct device *dev)
{
	struct gxfp *g = dev_get_drvdata(dev);
	unsigned long flags;

	if (!g)
		return 0;

	mutex_lock(&g->tx_lock);
	if (READ_ONCE(g->removed)) {
		mutex_unlock(&g->tx_lock);
		return 0;
	}

	WRITE_ONCE(g->suspended, true);
	disable_irq(g->irq);
	gpiod_set_value_cansleep(g->gpio_write_done, 0);
	gpiod_set_value_cansleep(g->gpio_read_done, 0);

	spin_lock_irqsave(&g->rx_lock, flags);
	if (g->rx_fifo_ready)
		kfifo_reset(&g->rx_fifo);
	atomic_set(&g->queued_records, 0);
	g->rx_overflow = false;
	spin_unlock_irqrestore(&g->rx_lock, flags);

	wake_up_interruptible_all(&g->rx_wait);
	mutex_unlock(&g->tx_lock);
	return 0;
}

static int gxfp_resume(struct device *dev)
{
	struct gxfp *g = dev_get_drvdata(dev);

	if (!g)
		return 0;

	mutex_lock(&g->tx_lock);
	if (READ_ONCE(g->removed)) {
		mutex_unlock(&g->tx_lock);
		return 0;
	}

	WRITE_ONCE(g->suspended, false);
	enable_irq(g->irq);
	wake_up_interruptible_all(&g->rx_wait);
	mutex_unlock(&g->tx_lock);
	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(gxfp_pm_ops, gxfp_suspend,
				gxfp_resume);

static const struct acpi_device_id gxfp_acpi_ids[] = {
	{ "GXFP5130", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, gxfp_acpi_ids);

static struct platform_driver gxfp_driver = {
	.driver = {
		.name = GXFP_DRV_NAME,
		.acpi_match_table = gxfp_acpi_ids,
		.pm = pm_sleep_ptr(&gxfp_pm_ops),
	},
	.probe = gxfp_probe,
	.remove = gxfp_remove,
};
module_platform_driver(gxfp_driver);

MODULE_DESCRIPTION("Goodix GXFP5130 EC mailbox transport");
MODULE_LICENSE("GPL");
