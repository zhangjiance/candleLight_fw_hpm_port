/*
 * wdt_hpm.c - see wdt_hpm.h.
 */
#include "wdt_hpm.h"

#if defined(FLASH_UF2) && FLASH_UF2

#include "hpm_common.h"
#include "hpm_soc.h"
#include "hpm_clock_drv.h"
#include "hpm_ewdg_drv.h"

void wdt_hpm_init(void)
{
    /* The EWDG registers are only accessible once the peripheral clock runs. */
    clock_add_to_group(clock_watchdog0, 0);

    /* ewdg_disable() unlocks the control registers first when the watchdog is
     * configured with a configuration lock. */
    (void)ewdg_disable(HPM_EWDG0);
}

#else /* flash_dfu (default) */

void wdt_hpm_init(void)
{
}

#endif /* FLASH_UF2 */
