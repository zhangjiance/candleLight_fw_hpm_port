/*
 * dfu_hpm.c - candleLight_fw's dfu.h contract on HPMicro.
 *
 * dfu_run_bootloader() stores a magic value in a retention register and
 * re-enters the DFU bootloader through the ROM run_bootloader API.
 * hpm_dfu_boot (the bootloader flashed at 0x80000000) checks that magic on
 * startup and enters its DFU mode instead of jumping to the application.
 *
 * Magic / register usage mirrors hpm_dfu_boot/src/hpm_dfu_trigger.c.
 */

#include <stdint.h>

#include "hpm_soc.h"
#include "board.h"
#include "hpm_common.h"
#include "hpm_l1c_drv.h"
#include "hpm_ppor_drv.h"
#include "hpm_romapi.h"
#ifdef HPM_BCFG_BASE
#include "hpm_bgpr_drv.h"
#endif
#ifdef HPM_PDGO_BASE
#include "hpm_pdgo_drv.h"
#endif

#include "dfu.h"

#define DFU_TRIGGER_MAGIC      (0x55464455UL) /* "UDfU" */
#define DFU_TRIGGER_BGPR_INDEX (0U)

void dfu_run_bootloader(void)
{
#ifdef HPM_BCFG_BASE
    (void)bgpr_write32(BOARD_BGPR, DFU_TRIGGER_BGPR_INDEX, DFU_TRIGGER_MAGIC);
#endif
#ifdef HPM_PDGO_BASE
    if (!pdgo_is_retention_mode_enabled(HPM_PDGO)) {
        pdgo_enable_retention_mode(HPM_PDGO);
    }
    pdgo_write_gpr(HPM_PDGO, DFU_TRIGGER_BGPR_INDEX, DFU_TRIGGER_MAGIC);
#endif

    disable_global_irq(CSR_MSTATUS_MIE_MASK);
    l1c_dc_invalidate_all();
    l1c_dc_disable();
    fencei();

    api_boot_arg_t boot_arg = {
        .index = 0,
        .peripheral = API_BOOT_PERIPH_AUTO,
        .src = API_BOOT_SRC_PRIMARY,
        .tag = API_BOOT_TAG,
    };
    (void)ROM_API_TABLE_ROOT->run_bootloader(&boot_arg);

    /* run_bootloader should not return; fall back to a software reset. */
    ppor_reset_mask_set_source_enable(HPM_PPOR, ppor_reset_software);
    ppor_sw_reset(HPM_PPOR, 24U);

    while (1) {
        ;
    }
}
