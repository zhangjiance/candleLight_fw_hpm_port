/*
 * Copyright (c) 2023-2024 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#ifndef _HPM_BOARD_H
#define _HPM_BOARD_H
#include "hpm_clock_drv.h"
#include "hpm_common.h"
#include "hpm_gpio_drv.h"
#include "hpm_gpiom_drv.h"
#include "hpm_soc.h"
#include "hpm_soc_feature.h"
#include "pinmux.h"
#include <stdarg.h>
#include <stdio.h>
#if !defined(CONFIG_NDEBUG_CONSOLE) || !CONFIG_NDEBUG_CONSOLE
#include "hpm_debug_console.h"
#endif

#define BOARD_NAME "hscant"
#define BOARD_UF2_SIGNATURE (0x0A4D5048UL)
#define BOARD_DFU_SIGNATURE (0x48504D21UL)  /* "HPM!" */
#define BOARD_BGPR          HPM_BGPR0

/* ------------------------------------------------------------------
 * Interface required by the candleLight port layer (port/board_contract.h)
 * ------------------------------------------------------------------ */
#define BOARD_CAN_COUNT     4
#define BOARD_CAN_INSTANCES HPM_MCAN0, HPM_MCAN1, HPM_MCAN2, HPM_MCAN3
#define BOARD_CAN_CLOCKS    clock_can0, clock_can1, clock_can2, clock_can3
#define BOARD_USB_BASE      HPM_USB0
#define BOARD_USB_IRQn      IRQn_USB0
#define BOARD_HAS_SYS_LED   1 /* GPIOB[10], active high */

/* clock.c already sets clk_can0..3 to clk_src_pll1_clk0 / 10 and adds them to
 * group 0, so the port layer's generic board_init_can_clock() default applies
 * (no board specific clock code needed). */

/* dma section */
#define BOARD_APP_HDMA HPM_HDMA
#define BOARD_APP_HDMA_IRQ IRQn_HDMA
#define BOARD_APP_DMAMUX HPM_DMAMUX
#define TEST_DMA_CONTROLLER HPM_HDMA
#define TEST_DMA_IRQ IRQn_HDMA

#ifndef BOARD_RUNNING_CORE
#define BOARD_RUNNING_CORE HPM_CORE0
#endif

/* uart section */
#ifndef BOARD_APP_UART_BASE
#define BOARD_APP_UART_BASE HPM_UART2
#define BOARD_APP_UART_IRQ IRQn_UART2
#define BOARD_APP_UART_BAUDRATE (115200UL)
#define BOARD_APP_UART_CLK_NAME clock_uart2
#define BOARD_APP_UART_RX_DMA_REQ HPM_DMA_SRC_UART2_RX
#define BOARD_APP_UART_TX_DMA_REQ HPM_DMA_SRC_UART2_TX
#endif

#if !defined(CONFIG_NDEBUG_CONSOLE) || !CONFIG_NDEBUG_CONSOLE
#ifndef BOARD_CONSOLE_TYPE
#define BOARD_CONSOLE_TYPE CONSOLE_TYPE_UART
#endif

#if BOARD_CONSOLE_TYPE == CONSOLE_TYPE_UART
#ifndef BOARD_CONSOLE_UART_BASE
#define BOARD_CONSOLE_UART_BASE HPM_UART0
#define BOARD_CONSOLE_UART_CLK_NAME clock_uart0
#define BOARD_CONSOLE_UART_IRQ IRQn_UART0
#define BOARD_CONSOLE_UART_TX_DMA_REQ HPM_DMA_SRC_UART0_TX
#define BOARD_CONSOLE_UART_RX_DMA_REQ HPM_DMA_SRC_UART0_RX
#endif
#define BOARD_CONSOLE_UART_BAUDRATE (115200UL)
#endif
#endif

/* usb cdc acm uart section */
#define BOARD_USB_CDC_ACM_UART BOARD_APP_UART_BASE
#define BOARD_USB_CDC_ACM_UART_CLK_NAME BOARD_APP_UART_CLK_NAME
#define BOARD_USB_CDC_ACM_UART_TX_DMA_SRC BOARD_APP_UART_TX_DMA_REQ
#define BOARD_USB_CDC_ACM_UART_RX_DMA_SRC BOARD_APP_UART_RX_DMA_REQ

/* nor flash section */
#define BOARD_FLASH_BASE_ADDRESS (0x80000000UL) /* Check */
#define BOARD_FLASH_SIZE (SIZE_1MB)

/* gptmr section */
#define BOARD_GPTMR HPM_GPTMR3
#define BOARD_GPTMR_IRQ IRQn_GPTMR3
#define BOARD_GPTMR_CHANNEL 3
#define BOARD_GPTMR_DMA_SRC HPM_DMA_SRC_GPTMR3_3
#define BOARD_GPTMR_CLK_NAME clock_gptmr3

/* User LED */
#define BOARD_LED_GPIO_CTRL HPM_FGPIO
#define BOARD_LED_GPIO_INDEX GPIO_DO_GPIOB
#define BOARD_LED_GPIO_PIN 10

#define BOARD_LED_OFF_LEVEL 0
#define BOARD_LED_ON_LEVEL 1

/* gpiom section */
#define BOARD_APP_GPIOM_BASE HPM_GPIOM
#define BOARD_APP_GPIOM_USING_CTRL HPM_FGPIO
#define BOARD_APP_GPIOM_USING_CTRL_NAME gpiom_core0_fast

/* User button */
#define BOARD_APP_GPIO_CTRL HPM_FGPIO
#define BOARD_APP_GPIO_INDEX GPIO_DI_GPIOA
#define BOARD_APP_GPIO_PIN 3
#define BOARD_APP_GPIO_IRQ IRQn_GPIO0_A
#define BOARD_BUTTON_PRESSED_VALUE 1

/* spi section */
#define BOARD_APP_SPI_BASE HPM_SPI1
#define BOARD_APP_SPI_CLK_NAME clock_spi1
#define BOARD_APP_SPI_IRQ IRQn_SPI1
#define BOARD_APP_SPI_SCLK_FREQ (20000000UL)
#define BOARD_APP_SPI_ADDR_LEN_IN_BYTES (1U)
#define BOARD_APP_SPI_DATA_LEN_IN_BITS (8U)
#define BOARD_APP_SPI_RX_DMA HPM_DMA_SRC_SPI1_RX
#define BOARD_APP_SPI_TX_DMA HPM_DMA_SRC_SPI1_TX
#define BOARD_SPI_CS_GPIO_CTRL HPM_GPIO0
#define BOARD_SPI_CS_PIN IOC_PAD_PA26
#define BOARD_SPI_CS_ACTIVE_LEVEL (0U)


/* Flash section */
#define BOARD_APP_XPI_NOR_XPI_BASE (HPM_XPI0)
#define BOARD_APP_XPI_NOR_CFG_OPT_HDR (0xfcf90002U)
#define BOARD_APP_XPI_NOR_CFG_OPT_OPT0 (0x00000006U)
#define BOARD_APP_XPI_NOR_CFG_OPT_OPT1 (0x00001000U)

/* CALLBACK TIMER section */
#define BOARD_CALLBACK_TIMER (HPM_GPTMR3)
#define BOARD_CALLBACK_TIMER_CH 3
#define BOARD_CALLBACK_TIMER_IRQ IRQn_GPTMR3
#define BOARD_CALLBACK_TIMER_CLK_NAME (clock_gptmr3)

#ifndef BOARD_SHOW_CLOCK
#define BOARD_SHOW_CLOCK 1
#endif
#ifndef BOARD_SHOW_BANNER
#define BOARD_SHOW_BANNER 1
#endif

/* FreeRTOS Definitions */
#define BOARD_FREERTOS_TIMER HPM_GPTMR0
#define BOARD_FREERTOS_TIMER_CHANNEL 1
#define BOARD_FREERTOS_TIMER_IRQ IRQn_GPTMR0
#define BOARD_FREERTOS_TIMER_CLK_NAME clock_gptmr0

#define BOARD_FREERTOS_LOWPOWER_TIMER HPM_PTMR
#define BOARD_FREERTOS_LOWPOWER_TIMER_CHANNEL 1
#define BOARD_FREERTOS_LOWPOWER_TIMER_IRQ IRQn_PTMR
#define BOARD_FREERTOS_LOWPOWER_TIMER_CLK_NAME clock_ptmr

#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */

extern uint32_t running_status;

typedef void (*board_timer_cb)(void);

void board_timer_process(void);
void board_init_gpio_pins(void);
void board_init_led_pins(void);
void board_init_usb(USB_Type *ptr);
void board_led_write(uint8_t state);
void board_led_toggle(void);
void board_can_set_termination(uint8_t channel, uint8_t on);
void board_init_console(void);
void board_init_uart(UART_Type *ptr);
uint32_t board_init_spi_clock(SPI_Type *ptr);
void board_init_spi_pins(SPI_Type *ptr);

void board_init(void);
void board_init_usb_dp_dm_pins(void);
void board_init_clock(void);
void board_delay_us(uint32_t us);
void board_delay_ms(uint32_t ms);
void board_timer_create(uint32_t ms, board_timer_cb cb);
void board_ungate_mchtmr_at_lp_mode(void);

uint8_t board_get_led_gpio_off_level(void);

void board_init_pmp(void);

uint32_t board_init_uart_clock(UART_Type *ptr);

void board_init_gptmr_channel_pin(GPTMR_Type *ptr, uint32_t channel,
                                  bool as_comp);

void init_uart_pins(UART_Type *ptr);
void init_spi_pins(SPI_Type *ptr);
void init_usb_pins(USB_Type *ptr);
void init_can_pins(MCAN_Type *ptr);
void init_led_pins(void);


#if defined(__cplusplus)
}
#endif /* __cplusplus */
#endif /* _HPM_BOARD_H */
