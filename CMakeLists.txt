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
#                     DFU signature).  Select it with -DBOARD=<name> and add a
#                     matching BOARD_<name> block in candleLight_fw/include/config.h.
#
# Every preset uses HPM_BUILD_TYPE=flash_dfu: the application is linked at
# 0x80020000 behind the .dfu_signature and is started by hpm_dfu_boot (which
# sits at 0x80000000).  Configure/build from this directory, e.g.:
#   cmake --preset hscant-dfu              && cmake --build --preset hscant-dfu
#   cmake --preset hpm5321_usb2can-dfu     && cmake --build --preset hpm5321_usb2can-dfu

cmake_minimum_required(VERSION 3.13)

set(APP_NAME candle_usb2canfd)

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

if(NOT DEFINED BOARD)
    message(FATAL_ERROR "BOARD is not set. Use: cmake --preset <preset>")
endif()

# Custom board search path (boards/<BOARD>)
if(NOT DEFINED BOARD_SEARCH_PATH AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/boards)
    set(BOARD_SEARCH_PATH ${CMAKE_CURRENT_SOURCE_DIR}/boards)
endif()

find_package(hpm-sdk REQUIRED HINTS $ENV{HPM_SDK_BASE})
project(candle_usb2canfd)

sdk_ld_options("--specs=nano.specs")
sdk_linker_global_symbols("_flash_size=1M" "_noncacheable_size=256K")

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
sdk_inc(${CMAKE_CURRENT_SOURCE_DIR}/boards/${BOARD})
sdk_inc(${CANDLE_INC})
# usbd_conf.h (STM32 USB Device Library configuration, provided verbatim)
sdk_inc(${CANDLE_FW}/libs/STM32_USB_Device_Library/config)

# STM32G4 selects candleLight's FDCAN code paths (can.h / device.h) and makes
# m_can.c's "DataLength as is" semantics apply.  We only use it as a switch -
# no STM32 HAL is compiled.
sdk_compile_definitions(-DBOARD_${BOARD} -DSTM32G4)

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

message(STATUS "Board: ${BOARD}  HPM_BUILD_TYPE: ${HPM_BUILD_TYPE}")
