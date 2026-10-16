#ifndef WDT_HPM_H
#define WDT_HPM_H

/* Disable EWDG0 (best effort).  Call early in the bring-up. */
void wdt_hpm_init(void);

#endif /* WDT_HPM_H */
