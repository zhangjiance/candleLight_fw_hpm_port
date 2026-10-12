/*
 * bl_setting.h - bootloader <-> application hand-shake block.
 *
 * The stock UF2 / MSC bootloader shipped on the device (a CherryDAP "bootuf2"
 * derivative, see CherryDAP projects/HSLink-Pro/common/BL_Setting_Common.h)
 * keeps a small uninitialised block in the last 512 bytes of AHB_SRAM.  It is
 * retentive across a software reset, so the application uses it to:
 *
 *   - clear fail_cnt once it is up and running (otherwise the bootloader
 *     falls back to UF2 mode after it counted too many unsuccessful boots),
 *   - set keep_bootloader = 1 to have the bootloader stay in UF2 mode on the
 *     next boot (the "reboot to bootloader" request).
 *
 * The layout below must stay binary compatible with the bootloader.  It is used
 * for HPM_BUILD_TYPE=flash_uf2 images only; the hpm_dfu_boot path uses the
 * BGPR magic in src/dfu_hpm.c instead.
 */
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

/* Validate the magic, clear fail_cnt and publish the application version.
 * Call once early in the application bring-up (HAL_Init). */
void bl_setting_app_init(void);

/* Ask the bootloader for UF2 mode on the next boot. */
void bl_setting_request_bootloader(void);

#endif /* CANDLE_BL_SETTING_H */
