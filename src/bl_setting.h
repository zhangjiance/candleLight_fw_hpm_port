#ifndef CANDLE_BL_SETTING_H
#define CANDLE_BL_SETTING_H

#include <stdint.h>

#define BL_SETTING_MAGIC (0xB7B7B7B7UL)

/* AHB_SRAM (0xF0400000, 32K) top minus the 512 byte reserved window. */
#define BL_SETTING_ADDR (0xF0400000UL + 0x8000UL - 512UL)

typedef struct {
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
    uint8_t reserved;
} bl_version_t;

typedef struct {
    uint32_t magic; /* BL_SETTING_MAGIC once valid */
    union {
        uint8_t data0;
        struct {
            uint8_t keep_bootloader : 1; /* ask the bootloader to stay in UF2 mode */
            uint8_t fail_cnt : 3;        /* cleared by a healthy application boot */
        };
    };
    bl_version_t bl_version;
    bl_version_t app_version;
} bl_setting_t;

/* Pointer to the shared block (never NULL; AHB_SRAM is always mapped). */
bl_setting_t *bl_setting(void);

/* Validate the magic, clear fail_cnt, publish the version; call early in HAL_Init(). */
void bl_setting_app_init(void);

/* Ask the bootloader for UF2 mode on the next boot. */
void bl_setting_request_bootloader(void);

#endif /* CANDLE_BL_SETTING_H */
