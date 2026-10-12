/*
 * wdt_hpm.h - EWDG (watchdog) handling for UF2 images.
 *
 * The application can run behind the stock UF2/MSC bootloader.  Instead of
 * feeding the watchdog forever (the original application does that), this port
 * disables EWDG0 once during bring-up: with the watchdog off, no blocking code
 * path can reset the application.
 *
 * No-op for flash_dfu images (hpm_dfu_boot does not touch the watchdog).
 */
#ifndef WDT_HPM_H
#define WDT_HPM_H

/* Disable EWDG0 (best effort).  Call early in the bring-up. */
void wdt_hpm_init(void);

#endif /* WDT_HPM_H */
