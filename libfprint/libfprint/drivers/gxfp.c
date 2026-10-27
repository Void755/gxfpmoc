#define FP_COMPONENT "gxfp"

#include "drivers_api.h"

#include "../fpi-image-device.h"

#include <errno.h>
#include <fcntl.h>
#include <gio/gio.h>
#include <stdlib.h>
#include <string.h>

#include "gxfp/algo/common.h"
#include "gxfp/algo/image/decoder.h"
#include "gxfp/algo/image/fpn.h"
#include "gxfp/algo/image/plain.h"
#include "gxfp/algo/sensor_cfg.h"
#include "gxfp/cmd/register_cmd.h"
#include "gxfp/cmd/sensor_cfg_cmd.h"
#include "gxfp/cmd/tls_cmd.h"
#include "gxfp/flow/fdt.h"
#include "gxfp/io/dev.h"
#include "gxfp/proto/goodix_constants.h"
#include "gxfp/proto/mp_proto.h"
#include "gxfp/tls/tls_constants.h"
#include "gxfp/tls/tls_session.h"

#define DEFAULT_ENROLL_SAMPLES 16
#define GXFP_DEFAULT_PSK_PATH "/var/lib/fprintd/gxfp/psk_raw32.bin"
#define GXFP_TASK_TIMEOUT_MS 20000
#define GXFP_RETRY_DELAY_MS 10
#define GXFP_MILANL_CHIP_ID 0x2208

enum gxfp_task_kind {
  GXFP_TASK_NONE = 0,
  GXFP_TASK_ACTIVATE,
  GXFP_TASK_WAIT_DOWN,
  GXFP_TASK_WAIT_UP,
  GXFP_TASK_CAPTURE,
};

enum gxfp_activate_state {
  GXFP_ACTIVATE_TLS = 0,
  GXFP_ACTIVATE_FDT_UP_SETUP,
  GXFP_ACTIVATE_FDT_UP_WAIT,
  GXFP_ACTIVATE_REFERENCE_START,
  GXFP_ACTIVATE_REFERENCE_WAIT,
  GXFP_ACTIVATE_REFERENCE_STORE,
  GXFP_ACTIVATE_DONE,
  GXFP_ACTIVATE_NUM_STATES,
};

enum gxfp_wait_state {
  GXFP_WAIT_FDT_SETUP = 0,
  GXFP_WAIT_FDT_EVENT,
  GXFP_WAIT_FDT_DONE,
  GXFP_WAIT_NUM_STATES,
};

enum gxfp_capture_state {
  GXFP_CAPTURE_START = 0,
  GXFP_CAPTURE_WAIT,
  GXFP_CAPTURE_STORE,
  GXFP_CAPTURE_NUM_STATES,
};

struct _FpiDeviceGxfp {
  FpImageDevice parent;

  struct gxfp_dev dev;
  struct gxfp_tls_session tls;
  struct gxfp_plain_sink plain;
  struct gxfp_fdt_flow fdt;

  gchar *dev_path;
  gchar *psk_path;
  uint8_t *psk;
  size_t psk_len;

  uint8_t *tap_buf;
  size_t tap_cap;

  struct gxfp_fpn_dark dark;
  int dark_loaded;
  FpImage *captured_image;

  FpiSsm *task_ssm;
  enum gxfp_task_kind task_kind;
  int64_t task_deadline_ms;

  int deactivating;
  int deactivate_complete_sent;
  int closing;

  uint16_t otp_tcode;
  uint16_t otp_fdt_delta;
  int otp_has_tcode_delta;
};

G_DECLARE_FINAL_TYPE (FpiDeviceGxfp, fpi_device_gxfp, FPI, DEVICE_GXFP, FpImageDevice)
G_DEFINE_TYPE (FpiDeviceGxfp, fpi_device_gxfp, FP_TYPE_IMAGE_DEVICE)

static void fpi_device_gxfp_dispose (GObject *object);
static int gxfp_rearm_tls (FpiDeviceGxfp *self);
static void gxfp_activate_ssm_handler (FpiSsm *ssm, FpDevice *dev);
static void gxfp_wait_ssm_handler (FpiSsm *ssm, FpDevice *dev);
static void gxfp_capture_ssm_handler (FpiSsm *ssm, FpDevice *dev);

static GError *
gxfp_error_from_errno (int err, const char *what)
{
  int pos = err < 0 ? -err : err;

  if (pos == ECANCELED)
    return g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                "Operation cancelled");
  if (pos == ETIMEDOUT)
    return g_error_new (G_IO_ERROR, G_IO_ERROR_TIMED_OUT,
                        "%s: timed out", what);
  return g_error_new (G_IO_ERROR, g_io_error_from_errno (pos),
                      "%s: %s", what, g_strerror (pos));
}

static gboolean
gxfp_task_expired (FpiDeviceGxfp *self)
{
  return self->task_deadline_ms > 0 &&
         gxfp_monotonic_ms () >= self->task_deadline_ms;
}

static void
gxfp_schedule_retry (FpiSsm *ssm)
{
  fpi_ssm_jump_to_state_delayed (ssm, fpi_ssm_get_cur_state (ssm),
                                 GXFP_RETRY_DELAY_MS);
}

static void
gxfp_free_dark (FpiDeviceGxfp *self)
{
  if (self->dark_loaded)
    {
      gxfp_fpn_free (&self->dark);
      self->dark_loaded = 0;
    }
}

static int
gxfp_set_dark (FpiDeviceGxfp *self, const struct gxfp_decoded_image *img)
{
  uint16_t *pixels;
  size_t count;

  if (!img || !img->pixels || img->rows <= 0 || img->cols <= 0)
    return -EINVAL;

  count = (size_t) img->rows * (size_t) img->cols;
  pixels = g_new (uint16_t, count);
  if (!pixels)
    return -ENOMEM;

  memcpy (pixels, img->pixels, count * sizeof (uint16_t));
  gxfp_free_dark (self);
  self->dark.pixels = pixels;
  self->dark.rows = img->rows;
  self->dark.cols = img->cols;
  self->dark_loaded = 1;
  return 0;
}

static int
gxfp_poll_records (FpiDeviceGxfp *self, uint32_t *out_fdt_events)
{
  if (out_fdt_events)
    *out_fdt_events = GXFP_FDT_EVENT_NONE;

  for (;;)
    {
      struct gxfp_tap_hdr hdr;
      const uint8_t *payload = NULL;
      size_t payload_len = 0;
      uint32_t events = GXFP_FDT_EVENT_NONE;
      ssize_t n;
      int r;

      n = gxfp_dev_read_record (&self->dev, self->tap_buf, self->tap_cap,
                                &hdr, &payload, &payload_len);
      if (n == -EAGAIN)
        return 0;
      if (n < 0)
        return (int) n;
      if (n == 0)
        return 0;

      r = gxfp_fdt_flow_feed_record (&self->fdt, &self->dev, &hdr,
                                     payload, payload_len, &events);
      if (r < 0 && r != -EAGAIN)
        return r;
      if (out_fdt_events)
        *out_fdt_events |= events;

      if (hdr.type == GOODIX_MP_TYPE_TLS)
        {
          r = gxfp_tls_session_feed_cipher (&self->tls, payload, payload_len);
          if (r < 0)
            return r;
        }
    }
}

static int
gxfp_pull_plain (FpiDeviceGxfp *self)
{
  for (;;)
    {
      uint8_t plain[TLS_REC_MAX_PLAIN];
      size_t plain_len = 0;
      int r;

      r = gxfp_tls_session_pull_plain (&self->tls, plain, sizeof (plain),
                                       &plain_len);
      if (r == 1)
        {
          gxfp_plain_scan_chunks (plain, plain_len, &self->plain);
          continue;
        }
      if (r == -EAGAIN || r == 0)
        return 0;
      return r;
    }
}

static int
gxfp_store_reference_image (FpiDeviceGxfp *self)
{
  struct gxfp_decoded_image img;
  uint8_t hdr[5];
  uint8_t *body = NULL;
  size_t body_len = 0;
  int r;

  memset (&img, 0, sizeof (img));
  r = gxfp_plain_sink_extract_normal (&self->plain, hdr, &body, &body_len);
  if (r <= 0)
    return r < 0 ? r : -EIO;

  r = gxfp_decode_image (body, body_len, GXFP_DECODE_AUTO, &img);
  g_free (body);
  if (r < 0)
    return r;

  r = gxfp_set_dark (self, &img);
  gxfp_decoded_image_free (&img);
  return r;
}

static FpImage *
gxfp_decoded_to_fp_image (const struct gxfp_decoded_image *img)
{
  FpImage *fp_img;
  gsize i;
  gsize count;

  fp_img = fp_image_new (img->cols, img->rows);
  count = (gsize) img->rows * (gsize) img->cols;
  for (i = 0; i < count; i++)
    fp_img->data[i] = (guint8) ((img->pixels[i] * 255u) / 4095u);
  return fp_img;
}

static int
gxfp_store_image (FpiDeviceGxfp *self)
{
  struct gxfp_decoded_image img;
  uint8_t hdr[5];
  uint8_t *body = NULL;
  size_t body_len = 0;
  FpImage *fp_img;
  int r;

  memset (&img, 0, sizeof (img));
  r = gxfp_plain_sink_extract_normal (&self->plain, hdr, &body, &body_len);
  if (r <= 0)
    return r < 0 ? r : -EIO;

  r = gxfp_decode_image (body, body_len, GXFP_DECODE_AUTO, &img);
  g_free (body);
  if (r < 0)
    return r;

  if (self->dark_loaded)
    {
      r = gxfp_fpn_correct (img.pixels, img.rows, img.cols, &self->dark);
      if (r < 0)
        {
          gxfp_decoded_image_free (&img);
          return r;
        }
    }

  fp_img = gxfp_decoded_to_fp_image (&img);
  gxfp_decoded_image_free (&img);
  if (!fp_img)
    return -ENOMEM;

  g_clear_pointer (&self->captured_image, g_object_unref);
  self->captured_image = fp_img;
  return 0;
}

static void
gxfp_task_finished (FpiSsm *ssm, FpDevice *dev, GError *error)
{
  FpiDeviceGxfp *self = FPI_DEVICE_GXFP (dev);
  enum gxfp_task_kind kind = self->task_kind;

  if (self->task_ssm == ssm)
    {
      self->task_ssm = NULL;
      self->task_kind = GXFP_TASK_NONE;
      self->task_deadline_ms = 0;
    }

  if (self->closing)
    {
      g_clear_error (&error);
      return;
    }

  if (self->deactivating)
    {
      g_clear_error (&error);
      if (!self->deactivate_complete_sent)
        {
          self->deactivate_complete_sent = 1;
          fpi_image_device_deactivate_complete (FP_IMAGE_DEVICE (self), NULL);
        }
      return;
    }

  if (error)
    {
      if (kind == GXFP_TASK_ACTIVATE)
        fpi_image_device_activate_complete (FP_IMAGE_DEVICE (self), error);
      else
        fpi_image_device_session_error (FP_IMAGE_DEVICE (self), error);
      return;
    }

  switch (kind)
    {
    case GXFP_TASK_ACTIVATE:
      fpi_image_device_activate_complete (FP_IMAGE_DEVICE (self), NULL);
      break;

    case GXFP_TASK_WAIT_DOWN:
      fpi_image_device_report_finger_status (FP_IMAGE_DEVICE (self), TRUE);
      break;

    case GXFP_TASK_WAIT_UP:
      fpi_image_device_report_finger_status (FP_IMAGE_DEVICE (self), FALSE);
      break;

    case GXFP_TASK_CAPTURE:
      if (self->captured_image)
        fpi_image_device_image_captured (FP_IMAGE_DEVICE (self),
                                         g_steal_pointer (&self->captured_image));
      break;

    case GXFP_TASK_NONE:
      break;
    }
}

static void
gxfp_track_task (FpiDeviceGxfp *self, FpiSsm *ssm, enum gxfp_task_kind kind,
                 int64_t deadline_ms)
{
  self->task_ssm = ssm;
  self->task_kind = kind;
  self->task_deadline_ms = deadline_ms;
}

static void
gxfp_cancel_task (FpiDeviceGxfp *self)
{
  if (self->task_ssm)
    {
      fpi_ssm_mark_failed (self->task_ssm,
                           g_error_new_literal (G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                                "Operation cancelled"));
      return;
    }

  if (!self->deactivate_complete_sent)
    {
      self->deactivate_complete_sent = 1;
      fpi_image_device_deactivate_complete (FP_IMAGE_DEVICE (self), NULL);
    }
}

static void
gxfp_activate_ssm_handler (FpiSsm *ssm, FpDevice *dev)
{
  FpiDeviceGxfp *self = FPI_DEVICE_GXFP (dev);
  uint32_t events = GXFP_FDT_EVENT_NONE;
  int r;

  if (gxfp_task_expired (self))
    {
      fpi_ssm_mark_failed (ssm, gxfp_error_from_errno (-ETIMEDOUT, "activate"));
      return;
    }

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case GXFP_ACTIVATE_TLS:
      {
        enum gxfp_tls_hs_state tls_state;

        tls_state = gxfp_tls_session_state (&self->tls);
        if (tls_state == GXFP_TLS_HS_CONNECTED)
          {
            fpi_ssm_next_state (ssm);
            break;
          }

        r = gxfp_poll_records (self, NULL);
        if (r < 0)
          {
            fpi_ssm_mark_failed (ssm, gxfp_error_from_errno (r, "TLS receive"));
            return;
          }

        r = gxfp_tls_session_handshake_step (&self->tls);
        if (r == 1)
          fpi_ssm_next_state (ssm);
        else if (r == -EAGAIN)
          gxfp_schedule_retry (ssm);
        else
          fpi_ssm_mark_failed (ssm, gxfp_error_from_errno (r, "TLS handshake"));
      }
      break;

    case GXFP_ACTIVATE_FDT_UP_SETUP:
      self->fdt.mode = GXFP_FDT_MODE_IDLE;
      r = gxfp_fdt_flow_set_mode (&self->fdt, &self->dev,
                                  GXFP_FDT_MODE_WAIT_UP);
      if (r < 0)
        fpi_ssm_mark_failed (ssm, gxfp_error_from_errno (r, "FDT up setup"));
      else
        fpi_ssm_next_state (ssm);
      break;

    case GXFP_ACTIVATE_FDT_UP_WAIT:
      r = gxfp_poll_records (self, &events);
      if (r < 0)
        {
          fpi_ssm_mark_failed (ssm, gxfp_error_from_errno (r, "FDT up wait"));
          return;
        }

      if (events & (GXFP_FDT_EVENT_UP | GXFP_FDT_EVENT_REVERSE))
        fpi_ssm_next_state (ssm);
      else
        gxfp_schedule_retry (ssm);
      break;

    case GXFP_ACTIVATE_REFERENCE_START:
      gxfp_plain_sink_reset_normal (&self->plain);
      r = gxfp_cmd_get_image (&self->dev);
      if (r < 0)
        fpi_ssm_mark_failed (ssm, gxfp_error_from_errno (r, "reference request"));
      else
        fpi_ssm_next_state (ssm);
      break;

    case GXFP_ACTIVATE_REFERENCE_WAIT:
      r = gxfp_poll_records (self, NULL);
      if (r < 0)
        {
          fpi_ssm_mark_failed (ssm, gxfp_error_from_errno (r, "reference receive"));
          return;
        }

      r = gxfp_pull_plain (self);
      if (r < 0)
        fpi_ssm_mark_failed (ssm, gxfp_error_from_errno (r, "reference decrypt"));
      else if (self->plain.have_normal)
        fpi_ssm_next_state (ssm);
      else
        gxfp_schedule_retry (ssm);
      break;

    case GXFP_ACTIVATE_REFERENCE_STORE:
      r = gxfp_store_reference_image (self);
      if (r < 0)
        fpi_ssm_mark_failed (ssm, gxfp_error_from_errno (r, "reference decode"));
      else
        fpi_ssm_next_state (ssm);
      break;

    case GXFP_ACTIVATE_DONE:
      fpi_ssm_mark_completed (ssm);
      break;

    default:
      g_assert_not_reached ();
    }
}

static void
gxfp_wait_ssm_handler (FpiSsm *ssm, FpDevice *dev)
{
  FpiDeviceGxfp *self = FPI_DEVICE_GXFP (dev);
  uint32_t events = GXFP_FDT_EVENT_NONE;
  uint32_t wanted;
  int r;

  wanted = self->task_kind == GXFP_TASK_WAIT_DOWN ?
           GXFP_FDT_EVENT_DOWN :
           (GXFP_FDT_EVENT_UP | GXFP_FDT_EVENT_REVERSE);

  if (gxfp_task_expired (self))
    {
      fpi_ssm_mark_failed (ssm, gxfp_error_from_errno (-ETIMEDOUT, "FDT wait"));
      return;
    }

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case GXFP_WAIT_FDT_SETUP:
      r = gxfp_fdt_flow_set_mode (&self->fdt, &self->dev,
                                  self->task_kind == GXFP_TASK_WAIT_DOWN ?
                                  GXFP_FDT_MODE_WAIT_DOWN :
                                  GXFP_FDT_MODE_WAIT_UP);
      if (r < 0)
        fpi_ssm_mark_failed (ssm, gxfp_error_from_errno (r, "FDT mode"));
      else
        fpi_ssm_next_state (ssm);
      break;

    case GXFP_WAIT_FDT_EVENT:
      r = gxfp_poll_records (self, &events);
      if (r < 0)
        {
          fpi_ssm_mark_failed (ssm, gxfp_error_from_errno (r, "FDT receive"));
          return;
        }

      if (events & wanted)
        fpi_ssm_next_state (ssm);
      else
        gxfp_schedule_retry (ssm);
      break;

    case GXFP_WAIT_FDT_DONE:
      fpi_ssm_mark_completed (ssm);
      break;

    default:
      g_assert_not_reached ();
    }
}

static void
gxfp_capture_ssm_handler (FpiSsm *ssm, FpDevice *dev)
{
  FpiDeviceGxfp *self = FPI_DEVICE_GXFP (dev);
  int r;

  if (gxfp_task_expired (self))
    {
      fpi_ssm_mark_failed (ssm, gxfp_error_from_errno (-ETIMEDOUT, "capture"));
      return;
    }

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case GXFP_CAPTURE_START:
      if (self->deactivating)
        {
          fpi_ssm_mark_completed (ssm);
          return;
        }

      gxfp_plain_sink_reset_normal (&self->plain);
      r = gxfp_cmd_get_image (&self->dev);
      if (r < 0)
        fpi_ssm_mark_failed (ssm, gxfp_error_from_errno (r, "capture request"));
      else
        fpi_ssm_next_state (ssm);
      break;

    case GXFP_CAPTURE_WAIT:
      r = gxfp_poll_records (self, NULL);
      if (r < 0)
        {
          fpi_ssm_mark_failed (ssm, gxfp_error_from_errno (r, "capture receive"));
          return;
        }

      r = gxfp_pull_plain (self);
      if (r < 0)
        fpi_ssm_mark_failed (ssm, gxfp_error_from_errno (r, "capture decrypt"));
      else if (self->plain.have_normal)
        fpi_ssm_next_state (ssm);
      else
        gxfp_schedule_retry (ssm);
      break;

    case GXFP_CAPTURE_STORE:
      if (self->deactivating)
        {
          fpi_ssm_mark_completed (ssm);
          return;
        }

      r = gxfp_store_image (self);
      if (r < 0)
        fpi_ssm_mark_failed (ssm, gxfp_error_from_errno (r, "capture decode"));
      else
        fpi_ssm_mark_completed (ssm);
      break;

    default:
      g_assert_not_reached ();
    }
}

static void
gxfp_start_wait (FpiDeviceGxfp *self, enum gxfp_task_kind kind)
{
  FpiSsm *ssm;

  if (self->task_ssm || self->deactivating)
    return;

  self->fdt.mode = GXFP_FDT_MODE_IDLE;
  ssm = fpi_ssm_new (FP_DEVICE (self), gxfp_wait_ssm_handler,
                     GXFP_WAIT_NUM_STATES);
  gxfp_track_task (self, ssm, kind, 0);
  fpi_ssm_start (ssm, gxfp_task_finished);
}

static void
gxfp_start_capture (FpiDeviceGxfp *self)
{
  FpiSsm *ssm;

  if (self->task_ssm || self->deactivating)
    return;

  ssm = fpi_ssm_new (FP_DEVICE (self), gxfp_capture_ssm_handler,
                     GXFP_CAPTURE_NUM_STATES);
  gxfp_track_task (self, ssm, GXFP_TASK_CAPTURE,
                   gxfp_monotonic_ms () + GXFP_TASK_TIMEOUT_MS);
  fpi_ssm_start (ssm, gxfp_task_finished);
}

static int
gxfp_apply_milanl_cfg (FpiDeviceGxfp *self)
{
  uint8_t otp[256];
  uint16_t otp_len = 0;
  struct gxfp_milanl_otp_cfg otp_cfg;
  const uint8_t *template_data = NULL;
  size_t template_len = 0;
  uint8_t cfg_blob[GXFP_MILANL_CFG_LEN];
  uint8_t ack_status = 0xff;
  int cfg_blob_len;
  int r;

  r = gxfp_cmd_read_otp (&self->dev, otp, (uint16_t) sizeof (otp), &otp_len);
  if (r < 0)
    return r;

  r = gxfp_milanl_parse_otp (otp, (size_t) otp_len, &otp_cfg);
  if (r < 0)
    return r;

  r = gxfp_milanl_get_default_cfg_template (&template_data, &template_len);
  if (r < 0)
    return r;

  cfg_blob_len = gxfp_milanl_prepare_cfg_blob (template_data, template_len,
                                               cfg_blob, sizeof (cfg_blob));
  if (cfg_blob_len < 0)
    return cfg_blob_len;

  r = gxfp_milanl_apply_otp_patch (cfg_blob, (size_t) cfg_blob_len, &otp_cfg);
  if (r < 0)
    return r;

  r = gxfp_cmd_upload_config_mcu (&self->dev, cfg_blob,
                                  (uint16_t) cfg_blob_len, &ack_status);
  if (r < 0)
    return r;

  self->otp_tcode = otp_cfg.tcode;
  self->otp_fdt_delta = otp_cfg.fdt_delta;
  self->otp_has_tcode_delta = otp_cfg.has_tcode_delta;
  gxfp_cmd_fdt_state_set_runtime (&self->fdt.cmd, 0, 0, 0,
                                  self->otp_has_tcode_delta ?
                                  (int16_t) self->otp_fdt_delta : 0);
  return 0;
}

static void
gxfp_img_open (FpImageDevice *dev)
{
  FpiDeviceGxfp *self = FPI_DEVICE_GXFP (dev);
  g_autoptr(GError) error = NULL;
  uint8_t *psk_raw = NULL;
  size_t psk_raw_len = 0;
  const gchar *chardev_path;
  const gchar *psk_env;
  uint16_t chip_id = 0;
  int r;

  chardev_path = fpi_device_get_udev_data (FP_DEVICE (self),
                                            FPI_DEVICE_UDEV_SUBTYPE_CHARDEV);
  if (!chardev_path || chardev_path[0] == '\0')
    {
      g_set_error (&error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                   "no chardev path from udev enumeration");
      fpi_image_device_open_complete (dev, g_steal_pointer (&error));
      return;
    }

  g_free (self->dev_path);
  self->dev_path = g_strdup (chardev_path);

  psk_env = g_getenv ("FP_GXFP_PSK");
  if (!psk_env || psk_env[0] == '\0')
    psk_env = GXFP_DEFAULT_PSK_PATH;
  g_free (self->psk_path);
  self->psk_path = g_strdup (psk_env);

  r = gxfp_read_file_all (self->psk_path, &psk_raw, &psk_raw_len);
  if (r < 0)
    {
      g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (-r),
                   "read PSK '%s' failed: %s", self->psk_path, g_strerror (-r));
      fpi_image_device_open_complete (dev, g_steal_pointer (&error));
      return;
    }

  r = gxfp_dev_open (&self->dev, self->dev_path, O_RDWR | O_NONBLOCK);
  if (r < 0)
    {
      g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (-r),
                   "open(%s) failed: %s", self->dev_path, g_strerror (-r));
      g_free (psk_raw);
      fpi_image_device_open_complete (dev, g_steal_pointer (&error));
      return;
    }

  self->tap_cap = sizeof (struct gxfp_tap_hdr) + (size_t) GXFP_MP_FRAME_MAX;
  self->tap_buf = g_malloc (self->tap_cap);
  if (!self->tap_buf)
    {
      gxfp_dev_close (&self->dev);
      g_free (psk_raw);
      fpi_image_device_open_complete (dev, g_error_new_literal (G_IO_ERROR,
                                                                G_IO_ERROR_NO_SPACE,
                                                                "out of memory"));
      return;
    }

  gxfp_fdt_flow_init (&self->fdt);
  gxfp_plain_sink_reset (&self->plain);
  (void) gxfp_dev_flush_rxq (&self->dev);

  r = gxfp_cmd_read_chip_id (&self->dev, &chip_id);
  if (r < 0)
    fp_dbg ("read chip id failed: %s", g_strerror (-r));
  else if (chip_id == GXFP_MILANL_CHIP_ID)
    {
      r = gxfp_apply_milanl_cfg (self);
      if (r < 0)
        {
          g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (-r),
                       "OTP/config bootstrap failed: %s", g_strerror (-r));
          goto fail;
        }
    }

  self->psk = psk_raw;
  self->psk_len = psk_raw_len;
  psk_raw = NULL;

  r = gxfp_rearm_tls (self);
  if (r < 0)
    {
      g_set_error (&error, G_IO_ERROR, g_io_error_from_errno (-r),
                   "TLS init failed: %s", g_strerror (-r));
      goto fail;
    }

  fpi_image_device_open_complete (dev, NULL);
  return;

fail:
  gxfp_tls_session_free (&self->tls);
  gxfp_dev_close (&self->dev);
  g_clear_pointer (&self->tap_buf, g_free);
  self->tap_cap = 0;
  g_clear_pointer (&self->psk, g_free);
  self->psk_len = 0;
  g_free (psk_raw);
  fpi_image_device_open_complete (dev, g_steal_pointer (&error));
}

static void
gxfp_img_close (FpImageDevice *dev)
{
  FpiDeviceGxfp *self = FPI_DEVICE_GXFP (dev);

  self->closing = 1;
  if (self->task_ssm)
    {
      FpiSsm *ssm = self->task_ssm;
      self->task_ssm = NULL;
      fpi_ssm_mark_failed (ssm, g_error_new_literal (G_IO_ERROR,
                                                     G_IO_ERROR_CANCELLED,
                                                     "Device closing"));
    }

  self->deactivating = 1;
  gxfp_tls_session_free (&self->tls);
  gxfp_dev_close (&self->dev);
  gxfp_plain_sink_reset (&self->plain);
  gxfp_free_dark (self);
  g_clear_pointer (&self->captured_image, g_object_unref);

  g_clear_pointer (&self->tap_buf, g_free);
  self->tap_cap = 0;
  g_clear_pointer (&self->psk, g_free);
  self->psk_len = 0;
  self->deactivating = 0;
  self->deactivate_complete_sent = 0;
  self->closing = 0;

  fpi_image_device_close_complete (dev, NULL);
}

static int
gxfp_rearm_tls (FpiDeviceGxfp *self)
{
  enum gxfp_tls_hs_state state;
  int r;

  if (!self->psk)
    return -EINVAL;

  state = gxfp_tls_session_state (&self->tls);
  if (state == GXFP_TLS_HS_CONNECTING ||
      state == GXFP_TLS_HS_CONNECTED)
    return 0;

  gxfp_tls_session_free (&self->tls);
  (void) gxfp_dev_flush_rxq (&self->dev);

  r = gxfp_tls_session_init (&self->tls, &self->dev,
                             self->psk, self->psk_len);
  if (r < 0)
    return r;

  return gxfp_cmd_tls_server_init (&self->dev);
}

static void
gxfp_activate (FpImageDevice *dev)
{
  FpiDeviceGxfp *self = FPI_DEVICE_GXFP (dev);
  FpiSsm *ssm;
  int r;

  if (self->task_ssm)
    return;

  self->deactivating = 0;
  self->deactivate_complete_sent = 0;
  g_clear_pointer (&self->captured_image, g_object_unref);

  r = gxfp_rearm_tls (self);
  if (r < 0)
    {
      fpi_image_device_activate_complete (dev,
          gxfp_error_from_errno (r, "TLS rearm"));
      return;
    }

  ssm = fpi_ssm_new (FP_DEVICE (self), gxfp_activate_ssm_handler,
                     GXFP_ACTIVATE_NUM_STATES);
  gxfp_track_task (self, ssm, GXFP_TASK_ACTIVATE,
                   gxfp_monotonic_ms () + GXFP_TASK_TIMEOUT_MS);
  fpi_ssm_start (ssm, gxfp_task_finished);
}

static void
gxfp_deactivate (FpImageDevice *dev)
{
  FpiDeviceGxfp *self = FPI_DEVICE_GXFP (dev);

  self->deactivating = 1;
  (void) gxfp_fdt_flow_set_mode (&self->fdt, &self->dev, GXFP_FDT_MODE_IDLE);
  gxfp_cancel_task (self);
}

static void
gxfp_change_state (FpImageDevice *dev, FpiImageDeviceState state)
{
  FpiDeviceGxfp *self = FPI_DEVICE_GXFP (dev);

  switch (state)
    {
    case FPI_IMAGE_DEVICE_STATE_AWAIT_FINGER_ON:
      gxfp_start_wait (self, GXFP_TASK_WAIT_DOWN);
      break;

    case FPI_IMAGE_DEVICE_STATE_CAPTURE:
      gxfp_start_capture (self);
      break;

    case FPI_IMAGE_DEVICE_STATE_AWAIT_FINGER_OFF:
      gxfp_start_wait (self, GXFP_TASK_WAIT_UP);
      break;

    case FPI_IMAGE_DEVICE_STATE_INACTIVE:
    case FPI_IMAGE_DEVICE_STATE_DEACTIVATING:
      (void) gxfp_fdt_flow_set_mode (&self->fdt, &self->dev,
                                     GXFP_FDT_MODE_IDLE);
      break;

    case FPI_IMAGE_DEVICE_STATE_ACTIVATING:
    case FPI_IMAGE_DEVICE_STATE_IDLE:
      break;
    }
}

static const FpIdEntry driver_ids[] = {
  { .udev_types = FPI_DEVICE_UDEV_SUBTYPE_CHARDEV, .chardev_acpi_hid = "GXFP5130" },
  { .udev_types = 0 },
};

static void
fpi_device_gxfp_class_init (FpiDeviceGxfpClass *klass)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (klass);
  FpDeviceClass *dev_class = FP_DEVICE_CLASS (klass);
  FpImageDeviceClass *img_class = FP_IMAGE_DEVICE_CLASS (klass);

  obj_class->dispose = fpi_device_gxfp_dispose;

  dev_class->id = FP_COMPONENT;
  dev_class->full_name = "Goodix GXFP5130 eSPI Fingerprint Sensor";
  dev_class->type = FP_DEVICE_TYPE_UDEV;
  dev_class->id_table = driver_ids;
  dev_class->scan_type = FP_SCAN_TYPE_PRESS;
  dev_class->nr_enroll_stages = DEFAULT_ENROLL_SAMPLES;
  dev_class->temp_hot_seconds = -1;

  img_class->img_width = 176;
  img_class->img_height = 54;
  img_class->algorithm = FPI_DEVICE_ALGO_SIGFM;

  img_class->img_open = gxfp_img_open;
  img_class->img_close = gxfp_img_close;
  img_class->activate = gxfp_activate;
  img_class->deactivate = gxfp_deactivate;
  img_class->change_state = gxfp_change_state;
}

static void
fpi_device_gxfp_init (FpiDeviceGxfp *self)
{
  self->dev.fd = -1;
  self->task_kind = GXFP_TASK_NONE;
  gxfp_fdt_flow_init (&self->fdt);
}

static void
fpi_device_gxfp_dispose (GObject *object)
{
  FpiDeviceGxfp *self = FPI_DEVICE_GXFP (object);

  g_clear_pointer (&self->dev_path, g_free);
  g_clear_pointer (&self->psk_path, g_free);
  g_clear_pointer (&self->tap_buf, g_free);
  g_clear_pointer (&self->psk, g_free);
  gxfp_free_dark (self);
  g_clear_pointer (&self->captured_image, g_object_unref);

  G_OBJECT_CLASS (fpi_device_gxfp_parent_class)->dispose (object);
}
