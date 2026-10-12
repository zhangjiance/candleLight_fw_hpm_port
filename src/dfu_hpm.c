/*
 * dfu_hpm.c - candleLight_fw's dfu.h contract on HPMicro.
 *
 * "dfu_run_bootloader()" means "re-enter the second stage bootloader".  Which
 * bootloader is in front of the application depends on HPM_BUILD_TYPE:
 *
 *  - flash_dfu  -> hpm_dfu_boot (DFU/DfuSe).  A magic value is stored in a
 *                  retention register and the ROM run_bootloader API re-enters
 *                  the boot at 0x80000000; hpm_dfu_boot sees the magic and stays
 *                  in DFU mode.
 *
 *  - flash_uf2  -> the stock UF2/MSC bootloader (a CherryDAP "bootuf2"
 *                  derivative).  There is no retention-register hand-shake;
 *                  instead keep_bootloader is set in the shared bl_setting block
 *                  and the ROM boot flow is re-entered, which re-runs the
 *                  bootloader at 0x80000000 (which then exposes its UF2 volume).
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

#if defined(FLASH_UF2) && FLASH_UF2

/* --- UF2 / MSC bootloader ------------------------------------------------ */

#include "bl_setting.h"

void dfu_run_bootloader(void)
{
    /* Ask the bootloader for UF2 mode.  bl_setting lives in AHB_SRAM, which
     * survives the ROM boot flow re-entry below. */
    bl_setting_request_bootloader();

    disable_global_irq(CSR_MSTATUS_MIE_MASK);
    disable_global_irq(CSR_MSTATUS_UIE_MASK);
    l1c_dc_invalidate_all();
    l1c_dc_disable();
    fencei();

    /* Re-enter the ROM boot flow (primary image -> bootloader @0x80000000).
     * NOTE: a plain PPOR software reset does NOT preserve bl_setting for the
     * bootloader, so the bootloader would see keep_bootloader == 0 and jump
     * straight back into the application.  This mirrors CherryDAP's
     * HSP_EnterHSLinkBootloader(). */
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

#else /* flash_dfu (default) */

/* --- DFU bootloader (hpm_dfu_boot) --------------------------------------- */

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

#endif /* FLASH_UF2 */
