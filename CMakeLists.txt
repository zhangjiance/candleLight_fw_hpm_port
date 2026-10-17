# candleLight_fw_hpm_port
#
# candleLight/gs_usb compatible USB2CANFD firmware for HPMicro HPM5321/HPM5361,
# using CherryUSB (device) + hpm_sdk (USB/clock/CAN).
#
# Layering:
#   candleLight_fw (git submodule) = the top level application:
#       src/main.c          - copied to src/candle/ (platform include seam only)
#       src/usbd_gs_can.c   - copied to src/candle/ (platform include seam only)
#       src/can_common.c    - compiled verbatim (frame linked-list engine)
#       src/led.c           - compiled verbatim
#       src/util.c          - compiled verbatim
#       src/usbd_desc.c     - compiled verbatim
#   port/    = platform seam  (HAL, CMSIS, STM32 USBD types, board layout header)
#   src/     = HPMicro implementation (CherryUSB bridge, MCAN, timer, DFU)
#   boards/<BOARD>/ = board package (clock tree, pad muxing, CAN clock, LED,
#                     DFU signature).  Select it with -DBOARD=<name>; the name
#                     must have a matching BOARD_<name> block in
#                     candleLight_fw/include/config.h ('-' in <name> becomes '_'
#                     in that macro, e.g. hscant-lite -> BOARD_hscant_lite).
#
#   hpm_sdk EVKs are used straight from ${HPM_SDK_BASE}/boards: -DBOARD=<evk>
#   (hpm5300evk, hpm6800evk, ...) selects the SDK board package and compiles
#   candleLight as BOARD_hpmicro_evk (single MCAN channel + USB0).  See
#   boards/hpmicro_evk/hpmicro_evk.h.  EVKs are DFU-only in this project.
#
# Two bootloader flavours are supported, selected by HPM_BUILD_TYPE:
#
#   flash_dfu  (.dfu_signature) - APP started by hpm_dfu_boot at 0x80000000.
#               Configure/build from this directory, e.g.:
#                 cmake --preset hscant-dfu && cmake --build --preset hscant-dfu
#
#   flash_uf2  (.uf2_signature) - APP started by the device's stock UF2/MSC
#               bootloader (the original, closed-source boot).
#               Also emits <APP_NAME>.uf2 for drag-and-drop flashing:
#                 cmake --preset hscant-uf2 && cmake --build --preset hscant-uf2
#               Only HSCanT has UF2 presets: that image is kept compatible with
#               HSCanT's closed-source boot.  Other boards could use flash_uf2
#               in principle, but first need their own UF2 boot project (write
#               it from the hpm_sdk USB/MSC/bootloader samples) and a matching
#               shared-memory contract; until then they are DFU-only.
#
# Both link at 0x80020000 behind a 4 byte signature.

cmake_minimum_required(VERSION 3.13)

# RISC-V architecture
set(RV_ARCH "rv32imac_zicsr_zifencei")
set(RV_ABI "ilp32")

# Avoid SDK forcing -u _printf_float/-u _scanf_float
set(HPM_SDK_LD_NO_NANO_SPECS 1)

# ---------------------------------------------------------------------------
# CherryUSB: core + HPM device controller only, the gs_usb class is in src/
# ---------------------------------------------------------------------------
set(CONFIG_CHERRYUSB 1)
set(CONFIG_USB_DEVICE 1)
set(CONFIG_CHERRYUSB_DEVICE 1)
set(CONFIG_HPM_SPI 1)
set(CONFIG_DMA_MGR 1)

if(NOT DEFINED BOARD)
    message(FATAL_ERROR "BOARD is not set. Use: cmake --preset <preset>")
endif()

# BOARD_hpmicro_evk is the candle selector macro, not an hpm_sdk board name.
# The SDK board (hpm5300evk, hpm6800evk, ...) has to be passed as BOARD so that
# hpm-sdk can find its YAML, package and SoC.
if(BOARD STREQUAL "hpmicro_evk")
    message(FATAL_ERROR
        "BOARD_hpmicro_evk is the candle selector, not a board name. Set BOARD to "
        "the hpm_sdk EVK (e.g. -DBOARD=hpm5300evk); the build then defines "
        "BOARD_hpmicro_evk automatically.")
endif()

# Artifact names carry the board so images built in the same tree/release can
# never be confused: candle_usb2canfd-<BOARD>.elf/.bin/.hex/.map (and .uf2).
# APP_NAME feeds the SDK's APP_ELF_NAME etc., so this has to be set before
# find_package(hpm-sdk).
set(APP_NAME "candle_usb2canfd-${BOARD}")

# Custom board search path (boards/<BOARD>)
if(NOT DEFINED BOARD_SEARCH_PATH AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/boards)
    set(BOARD_SEARCH_PATH ${CMAKE_CURRENT_SOURCE_DIR}/boards)
endif()

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE})
project(candle_usb2canfd)

# ---------------------------------------------------------------------------
# Board package resolution
#
#   boards/<BOARD>/  - a board package that lives in this repository
#                      (hscant, hscant-lite): keep using it.
#
#   otherwise BOARD must name an EVK in ${HPM_SDK_BASE}/boards.  The SDK owns
#   that board package; this project only adds the candle bindings through
#   boards/hpmicro_evk/hpmicro_evk.h and compiles candleLight as
#   BOARD_hpmicro_evk.  Every SDK EVK whose CAN controller is an MCAN and
#   which has an on-chip USB device port works this way - switch boards by
#   changing BOARD (or picking another EVK preset), nothing else.
# ---------------------------------------------------------------------------
set(HPMICRO_EVK OFF)
set(HPMICRO_EVK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/boards/hpmicro_evk)
set(CANDLE_BOARD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/boards/${BOARD})

if(EXISTS ${CANDLE_BOARD_DIR}/${BOARD}.yaml)
    message(STATUS "Board package: ${BOARD} (in this repository)")
else()
    set(SDK_EVK_DIR $ENV{HPM_SDK_BASE}/boards/${BOARD})
    if(NOT EXISTS ${SDK_EVK_DIR}/${BOARD}.yaml)
        message(FATAL_ERROR
            "No board named '${BOARD}': neither boards/${BOARD}/ in this repository "
            "nor ${SDK_EVK_DIR} in hpm_sdk")
    endif()

    file(READ ${SDK_EVK_DIR}/board.h SDK_BOARD_H)
    if(NOT SDK_BOARD_H MATCHES "BOARD_APP_CAN_BASE[ \t]+HPM_MCAN")
        message(FATAL_ERROR
            "BOARD=${BOARD} is not supported: the SDK board package does not expose "
            "an MCAN controller (BOARD_APP_CAN_BASE HPM_MCAN...). EVKs with a bxCAN "
            "controller or without CAN cannot be used as candle devices.")
    endif()
    if(NOT SDK_BOARD_H MATCHES "board_init_usb")
        message(FATAL_ERROR
            "BOARD=${BOARD} is not supported: the SDK board package has no "
            "board_init_usb() / on-chip USB device port")
    endif()

    set(HPMICRO_EVK ON)
    set(CANDLE_BOARD_DIR ${HPMICRO_EVK_DIR})
    message(STATUS "Board package: ${BOARD} (hpm_sdk EVK, candle bindings in boards/hpmicro_evk)")
endif()

sdk_ld_options("--specs=nano.specs")
# _flash_size / _noncacheable_size come from the board YAML and the SoC's
# CMakeLists in hpm_sdk.  Forcing them here breaks EVKs whose AXI SRAM is
# smaller than the old HPM5321/HPM5361 value (HPM5E00EVK: 256 KiB total).

set(CANDLE_FW ${CMAKE_CURRENT_SOURCE_DIR}/candleLight_fw)

# ---------------------------------------------------------------------------
# Generated version.h - candleLight's include/config.h uses GIT_HASH
# ---------------------------------------------------------------------------
set(GIT_HASH "hpm_port")
find_package(Git QUIET)
if(GIT_EXECUTABLE)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} -C ${CMAKE_CURRENT_SOURCE_DIR} rev-parse --short=8 HEAD
        OUTPUT_VARIABLE GIT_REV
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(GIT_REV)
        set(GIT_HASH "${GIT_REV}")
    endif()
endif()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/version.h "#define GIT_HASH \"${GIT_HASH}\"\n")

# ---------------------------------------------------------------------------
# Application version, as published to the bootloader through the shared
# bl_setting block (src/bl_setting.c writes app_version.major/minor/patch).
# Pass a release number - a release tag can be handed over as is, a leading "v"
# and a pre-release suffix are accepted:
#
#     -DBL_SETTING_APP_VERSION=1.0.9      -DBL_SETTING_APP_VERSION=v1.0.9
#     -DBL_SETTING_APP_VERSION=1.1.0-rc1
#
# Defaults to 0.0.0, which is what every non-release build reports.
# ---------------------------------------------------------------------------
set(BL_SETTING_APP_VERSION "0.0.0" CACHE STRING "Application version (x.y.z), published through bl_setting")
if(BL_SETTING_APP_VERSION MATCHES "^v?([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    sdk_compile_definitions(
        -DBL_SETTING_APP_VERSION_MAJOR=${CMAKE_MATCH_1}
        -DBL_SETTING_APP_VERSION_MINOR=${CMAKE_MATCH_2}
        -DBL_SETTING_APP_VERSION_PATCH=${CMAKE_MATCH_3})
else()
    message(FATAL_ERROR
        "BL_SETTING_APP_VERSION must look like x.y.z, got '${BL_SETTING_APP_VERSION}'")
endif()

# ---------------------------------------------------------------------------
# candleLight_fw include path, with board.h excluded.
#
# candleLight_fw/include/board.h describes candleLight's board abstraction,
# boards/<BOARD>/board.h describes the concrete hardware - both want the name
# "board.h".  Rather than renaming the board package, the original board.h is
# taken out of the search path: a private directory of symlinks to every other
# header in candleLight_fw/include is generated here.
#
#   "board.h"        -> boards/<BOARD>/board.h  (board package)
#   "board_layout.h" -> port/board_layout.h     (candleLight board abstraction)
# ---------------------------------------------------------------------------
set(CANDLE_INC ${CMAKE_CURRENT_BINARY_DIR}/candle_include)
file(MAKE_DIRECTORY ${CANDLE_INC})
file(GLOB CANDLE_HEADERS CONFIGURE_DEPENDS
    RELATIVE ${CANDLE_FW}/include
    ${CANDLE_FW}/include/*.h)
foreach(_hdr IN LISTS CANDLE_HEADERS)
    file(REMOVE ${CANDLE_INC}/${_hdr})
    if(NOT _hdr STREQUAL "board.h")
        file(CREATE_LINK ${CANDLE_FW}/include/${_hdr} ${CANDLE_INC}/${_hdr} SYMBOLIC)
    endif()
endforeach()

# ---------------------------------------------------------------------------
# Include paths
# ---------------------------------------------------------------------------
sdk_inc(${CMAKE_CURRENT_SOURCE_DIR}/port)
sdk_inc(${CMAKE_CURRENT_SOURCE_DIR}/src)
sdk_inc(${CMAKE_CURRENT_BINARY_DIR})
sdk_inc(${CANDLE_BOARD_DIR})
sdk_inc(${CANDLE_INC})
# usbd_conf.h (STM32 USB Device Library configuration, provided verbatim)
sdk_inc(${CANDLE_FW}/libs/STM32_USB_Device_Library/config)

# STM32G4 selects candleLight's FDCAN code paths (can.h / device.h) and makes
# m_can.c's "DataLength as is" semantics apply.  We only use it as a switch -
# no STM32 HAL is compiled.
#
# The board selector macro is read by candleLight_fw/include/config.h, a
# submodule that owns the board table, and has to be a valid C identifier - so a
# '-' in the board name becomes '_': -DBOARD=hscant-lite selects the
# BOARD_hscant_lite block.
#
# Every hpm_sdk EVK shares one candle board block, BOARD_hpmicro_evk: BOARD is
# the SDK board name (used to pick the board package and its YAML), while the
# application is always built as an HPMicro EVK.
if(HPMICRO_EVK)
    sdk_compile_definitions(-DBOARD_hpmicro_evk -DSTM32G4)
    # board.h (the SDK EVK's) + the candle bindings, before anything else.
    target_compile_options(app PRIVATE
        "-include" "${HPMICRO_EVK_DIR}/hpmicro_evk.h")
else()
    string(REPLACE "-" "_" BOARD_MACRO "${BOARD}")
    sdk_compile_definitions(-DBOARD_${BOARD_MACRO} -DSTM32G4)
endif()

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
# candleLight_fw top level application
sdk_app_src(${CMAKE_CURRENT_SOURCE_DIR}/src/candle/main.c)
sdk_app_src(${CMAKE_CURRENT_SOURCE_DIR}/src/candle/usbd_gs_can.c)
sdk_app_src(${CANDLE_FW}/src/can_common.c)
sdk_app_src(${CANDLE_FW}/src/led.c)
sdk_app_src(${CANDLE_FW}/src/util.c)
sdk_app_src(${CANDLE_FW}/src/usbd_desc.c)

# HPMicro platform implementation
sdk_app_src(${CMAKE_CURRENT_SOURCE_DIR}/src/usbd_bridge.c)
sdk_app_src(${CMAKE_CURRENT_SOURCE_DIR}/src/can_hpm.c)
sdk_app_src(${CMAKE_CURRENT_SOURCE_DIR}/src/timer.c)
sdk_app_src(${CMAKE_CURRENT_SOURCE_DIR}/src/dfu_hpm.c)
sdk_app_src(${CMAKE_CURRENT_SOURCE_DIR}/src/port_device.c)
sdk_app_src(${CMAKE_CURRENT_SOURCE_DIR}/src/port_board.c)
sdk_app_src(${CMAKE_CURRENT_SOURCE_DIR}/src/bl_setting.c)
sdk_app_src(${CMAKE_CURRENT_SOURCE_DIR}/src/wdt_hpm.c)
sdk_app_src(${CMAKE_CURRENT_SOURCE_DIR}/src/ws2812.c)
sdk_app_src(${CMAKE_CURRENT_SOURCE_DIR}/src/status_led.c)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    sdk_compile_definitions(-DNDEBUG)
endif()

# ---------------------------------------------------------------------------
# Artifacts
# ---------------------------------------------------------------------------
set(ELF ${EXECUTABLE_OUTPUT_PATH}/${APP_ELF_NAME})
set(HEX ${EXECUTABLE_OUTPUT_PATH}/${APP_NAME}.hex)
set(BIN ${EXECUTABLE_OUTPUT_PATH}/${APP_NAME}.bin)
add_custom_command(TARGET ${APP_ELF_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex -S ${ELF} ${HEX}
    COMMAND ${CMAKE_OBJCOPY} -O binary -S ${ELF} ${BIN}
    COMMENT "Generating ${APP_NAME}.hex / .bin"
)

# UF2 image for drag-and-drop flashing by a UF2/MSC bootloader.  The family id
# (0x0A4D5048, "HPM") and the application base address (0x80020000) must match
# the bootloader; both default to the stock values and can be overridden with
# -DUF2_FAMILY_ID=... / -DUF2_APP_BASE=...
if(NOT DEFINED UF2_FAMILY_ID)
    set(UF2_FAMILY_ID 0x0A4D5048)
endif()
if(NOT DEFINED UF2_APP_BASE)
    set(UF2_APP_BASE 0x80020000)
endif()
if(HPM_BUILD_TYPE STREQUAL "flash_uf2")
    set(UF2 ${EXECUTABLE_OUTPUT_PATH}/${APP_NAME}.uf2)
    add_custom_command(TARGET ${APP_ELF_NAME} POST_BUILD
        COMMAND ${python_exec} ${CMAKE_CURRENT_SOURCE_DIR}/tools/uf2conv.py
                -f ${UF2_FAMILY_ID} -b ${UF2_APP_BASE} -c ${BIN} -o ${UF2}
        COMMENT "Generating ${APP_NAME}.uf2"
    )
endif()

message(STATUS "Board: ${BOARD}  HPM_BUILD_TYPE: ${HPM_BUILD_TYPE}")
