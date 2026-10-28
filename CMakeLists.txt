cmake_minimum_required(VERSION 3.16)

project(gxfpmoc C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(MbedTLS REQUIRED)

add_library(gxfp
		src/io/dev.c
		src/algo/common.c
		src/algo/sensor_cfg.c
		src/algo/image/plain.c
		src/algo/image/decoder.c
		src/algo/image/fpn.c
		src/algo/payload/provision.c
		src/proto/goodix_proto.c
		src/proto/mp_proto.c
		src/tls/tls_backend_mbedtls.c
		src/tls/tls_session.c
		src/flow/device_recovery.c
		src/flow/fdt.c
		src/flow/milang_init.c
		src/cmd/tls_cmd.c
		src/cmd/mode_cmd.c
		src/cmd/goodix_xfer.c
		src/cmd/production_cmd.c
		src/cmd/device_recovery_cmd.c
		src/cmd/fdt_cmd.c
		src/cmd/mcu_state_cmd.c
		src/cmd/sensor_cfg_cmd.c
		src/cmd/register_cmd.c
		src/cmd/nav_cmd.c
		src/cmd/version_cmd.c
)

target_include_directories(gxfp
	PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR}/include
		${CMAKE_CURRENT_SOURCE_DIR}/kernel/include
)

target_compile_options(gxfp PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(gxfp PRIVATE _GNU_SOURCE)
target_link_libraries(gxfp PUBLIC m)

add_executable(gxfp_psk_tool
		demos/psk_tool/main.c
)

target_link_libraries(gxfp_psk_tool PRIVATE gxfp)

target_compile_options(gxfp_psk_tool PRIVATE -Wall -Wextra -Wpedantic)

add_executable(gxfp_capture
		demos/capture/main.c
		demos/common/gxfp_tool.c
)

target_include_directories(gxfp_capture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/demos/common)
target_link_libraries(gxfp_capture PRIVATE gxfp)

target_compile_options(gxfp_capture PRIVATE -Wall -Wextra -Wpedantic)

add_executable(gxfp_info
		demos/recovery/main.c
)

target_link_libraries(gxfp_info PRIVATE gxfp)
target_compile_options(gxfp_info PRIVATE -Wall -Wextra -Wpedantic)

target_link_libraries(gxfp PUBLIC MbedTLS::mbedtls)
target_link_libraries(gxfp_capture PRIVATE MbedTLS::mbedtls)
target_link_libraries(gxfp_info PRIVATE MbedTLS::mbedtls)
