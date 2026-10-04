/*
 * can_hpm.c - implementation of candleLight_fw's can.h contract on HPMicro.
 *
 * This is a port of candleLight_fw/src/can/m_can.c: the same bit-timing
 * constraints, the same frame <-> gs_host_frame mapping and the same
 * PSR/ECR/CCCR error-status handling.  The HPM MCAN is a Bosch M_CAN, so the
 * register level code maps 1:1; only the STM32 HAL_FDCAN_* calls are replaced:
 *
 *   HAL_FDCAN_Init                 -> mcan_init()            (message RAM + CCCR)
 *   HAL_FDCAN_Start / _Stop        -> CCCR.INIT bit
 *   HAL_FDCAN_AddMessageToTxFifoQ  -> mcan_write_txbuf() + TXBAR
 *   HAL_FDCAN_GetRxMessage         -> mcan_read_rxfifo()
 *   HAL_FDCAN_GetRxFifoFillLevel   -> RXF0S.F0FL
 *
 * Frame queueing/linked-list logic lives in the unmodified
 * candleLight_fw sources (can_common.c + usbd_gs_can.c).
 */

#include <string.h>

#include "board_layout.h" /* candleLight's board abstraction (config, ...) */
#include "can.h"
#include "hal_include.h"
#include "timer.h"

#include "board.h"          /* the board package (init_can_pins, ...) */
#include "board_contract.h" /* declares board_init_can_clock() for every board */
#include "hpm_clock_drv.h"
#include "hpm_mcan_drv.h"
#include "hpm_mcan_regs.h"

/* ------------------------------------------------------------------ */
/* Bit timing constraints (verbatim from candleLight's m_can.c)        */
/* ------------------------------------------------------------------ */
const struct gs_device_bt_const CAN_btconst = {
    .feature =
        GS_CAN_FEATURE_LISTEN_ONLY |
        GS_CAN_FEATURE_LOOP_BACK |
        GS_CAN_FEATURE_HW_TIMESTAMP |
        GS_CAN_FEATURE_IDENTIFY |
        GS_CAN_FEATURE_PAD_PKTS_TO_MAX_PKT_SIZE |
        (IS_ENABLED(CONFIG_CANFD) ?
         GS_CAN_FEATURE_FD | GS_CAN_FEATURE_BT_CONST_EXT : 0)
#ifdef TERM_Pin
        | GS_CAN_FEATURE_TERMINATION
#endif
    ,
    .fclk_can = CAN_CLOCK_SPEED,
    .tseg1_min = 1,
    .tseg1_max = 256,
    .tseg2_min = 1,
    .tseg2_max = 128,
    .sjw_max = 128,
    .brp_min = 1,
    .brp_max = 512,
    .brp_inc = 1,
};

const struct gs_device_bt_const_extended CAN_btconst_ext = {
    .device_bt_const = CAN_btconst,

    .dtseg1_min = 1,
    .dtseg1_max = 32,
    .dtseg2_min = 1,
    .dtseg2_max = 16,
    .dsjw_max = 16,
    .dbrp_min = 1,
    .dbrp_max = 32,
    .dbrp_inc = 1,
};

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */
static const uint8_t dlc_to_bytes[16] = {
    0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 12U, 16U, 20U, 24U, 32U, 48U, 64U
};

/* The CAN clock source/divider and the pad muxing are board specific; the
 * board package owns both and reports the resulting frequency. */
static uint32_t can_clk_freq(FDCAN_GlobalTypeDef *ptr)
{
    return board_init_can_clock(ptr);
}

/* ------------------------------------------------------------------ */
/* init / configuration                                               */
/* ------------------------------------------------------------------ */
void can_init(can_data_t *channel, FDCAN_GlobalTypeDef *instance)
{
    memset(&channel->channel, 0, sizeof(channel->channel));

    channel->channel.Instance = instance;
    channel->channel.Init.NominalPrescaler = 8U;
    channel->channel.Init.NominalSyncJumpWidth = 1U;
    channel->channel.Init.NominalTimeSeg1 = 13U;
    channel->channel.Init.NominalTimeSeg2 = 2U;
    channel->channel.Init.DataPrescaler = 2U;
    channel->channel.Init.DataSyncJumpWidth = 4U;
    channel->channel.Init.DataTimeSeg1 = 15U;
    channel->channel.Init.DataTimeSeg2 = 4U;
    channel->channel.Init.FrameFormat = FDCAN_FRAME_FD_BRS;
    channel->channel.Init.Mode = FDCAN_MODE_NORMAL;
    channel->channel.Init.AutoRetransmission = 1U;
    channel->channel.State = HAL_FDCAN_STATE_RESET;
    channel->channel.configured = false;

    channel->last_err = 0U;

    /* Make sure the pad muxing and the CAN clock are ready (board provided). */
    init_can_pins(instance);
    (void)board_init_can_clock(instance);
}

void can_set_bittiming(can_data_t *channel, const struct gs_device_bittiming *timing)
{
    const uint8_t tseg1 = (uint8_t)(timing->prop_seg + timing->phase_seg1);

    channel->channel.Init.NominalSyncJumpWidth = (uint8_t)timing->sjw;
    channel->channel.Init.NominalTimeSeg1 = tseg1;
    channel->channel.Init.NominalTimeSeg2 = (uint8_t)timing->phase_seg2;
    channel->channel.Init.NominalPrescaler = (uint16_t)timing->brp;
}

void can_set_data_bittiming(can_data_t *channel, const struct gs_device_bittiming *timing)
{
    const uint8_t tseg1 = (uint8_t)(timing->prop_seg + timing->phase_seg1);

    channel->channel.Init.DataSyncJumpWidth = (uint8_t)timing->sjw;
    channel->channel.Init.DataTimeSeg1 = tseg1;
    channel->channel.Init.DataTimeSeg2 = (uint8_t)timing->phase_seg2;
    channel->channel.Init.DataPrescaler = (uint16_t)timing->brp;
}

void can_enable(can_data_t *channel, uint32_t mode)
{
    FDCAN_GlobalTypeDef *base = channel->channel.Instance;
    mcan_config_t cfg;
    mcan_node_mode_t node_mode;

    if ((mode & (GS_CAN_MODE_LISTEN_ONLY | GS_CAN_MODE_LOOP_BACK)) ==
        (GS_CAN_MODE_LISTEN_ONLY | GS_CAN_MODE_LOOP_BACK)) {
        node_mode = mcan_mode_loopback_internal;
    } else if (mode & GS_CAN_MODE_LISTEN_ONLY) {
        node_mode = mcan_mode_listen_only;
    } else if (mode & GS_CAN_MODE_LOOP_BACK) {
        node_mode = mcan_mode_loopback_external;
    } else {
        node_mode = mcan_mode_normal;
    }

    channel->channel.Init.Mode = (node_mode == mcan_mode_loopback_internal) ? FDCAN_MODE_INTERNAL_LOOPBACK :
                               (node_mode == mcan_mode_listen_only)     ? FDCAN_MODE_BUS_MONITORING :
                               (node_mode == mcan_mode_loopback_external) ? FDCAN_MODE_EXTERNAL_LOOPBACK :
                                                                           FDCAN_MODE_NORMAL;

    channel->channel.Init.AutoRetransmission = (mode & GS_CAN_MODE_ONE_SHOT) ? 0U : 1U;

    if (mode & GS_CAN_MODE_FD) {
        channel->channel.Init.FrameFormat = FDCAN_FRAME_FD_BRS;
    } else {
        channel->channel.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
    }

    mcan_get_default_config(base, &cfg);

    cfg.enable_canfd = IS_ENABLED(CONFIG_CANFD);
    cfg.mode = node_mode;
    cfg.disable_auto_retransmission = (channel->channel.Init.AutoRetransmission == 0U);
    cfg.enable_tdc = false;

    /* Hand candleLight's bit timing straight to the driver so the exact
     * prescaler / segment values requested by the host are programmed. */
    cfg.use_lowlevel_timing_setting = true;
    cfg.can_timing.prescaler = channel->channel.Init.NominalPrescaler;
    cfg.can_timing.num_seg1 = channel->channel.Init.NominalTimeSeg1;
    cfg.can_timing.num_seg2 = channel->channel.Init.NominalTimeSeg2;
    cfg.can_timing.num_sjw = channel->channel.Init.NominalSyncJumpWidth;
    cfg.can_timing.enable_tdc = false;

    cfg.canfd_timing.prescaler = channel->channel.Init.DataPrescaler;
    cfg.canfd_timing.num_seg1 = channel->channel.Init.DataTimeSeg1;
    cfg.canfd_timing.num_seg2 = channel->channel.Init.DataTimeSeg2;
    cfg.canfd_timing.num_sjw = channel->channel.Init.DataSyncJumpWidth;
    cfg.canfd_timing.enable_tdc = false;

    /* No identifier filter list: every frame goes to RX FIFO0. */
    mcan_get_default_ram_config(base, &cfg.ram_config, cfg.enable_canfd);
    cfg.ram_config.enable_std_filter = false;
    cfg.ram_config.std_filter_elem_count = 0U;
    cfg.ram_config.enable_ext_filter = false;
    cfg.ram_config.ext_filter_elem_count = 0U;
    cfg.all_filters_config.std_id_filter_list.mcan_filter_elem_count = 0U;
    cfg.all_filters_config.ext_id_filter_list.mcan_filter_elem_count = 0U;

    /* Accept everything into RX FIFO0, reject remote frames. */
    cfg.all_filters_config.global_filter_config.accept_non_matching_std_frame_option =
        MCAN_ACCEPT_NON_MATCHING_FRAME_OPTION_IN_RXFIFO0;
    cfg.all_filters_config.global_filter_config.accept_non_matching_ext_frame_option =
        MCAN_ACCEPT_NON_MATCHING_FRAME_OPTION_IN_RXFIFO0;
    cfg.all_filters_config.global_filter_config.reject_remote_std_frame = false;
    cfg.all_filters_config.global_filter_config.reject_remote_ext_frame = false;

    if (mcan_init(base, &cfg, can_clk_freq(base)) == status_success) {
        channel->channel.State = HAL_FDCAN_STATE_BUSY;
        channel->channel.configured = true;
        if (config.phy_power_set) {
            config.phy_power_set(channel, true);
        }
    } else {
        channel->channel.State = HAL_FDCAN_STATE_READY;
    }
}

void can_disable(can_data_t *channel)
{
    FDCAN_GlobalTypeDef *base = channel->channel.Instance;

    if (base != NULL) {
        base->CCCR |= MCAN_CCCR_INIT_MASK;
        while ((base->CCCR & MCAN_CCCR_INIT_MASK) == 0U) {
            ;
        }
    }

    channel->channel.State = HAL_FDCAN_STATE_READY;

    if (config.phy_power_set) {
        config.phy_power_set(channel, false);
    }
}

bool can_is_enabled(can_data_t *channel)
{
    return channel->channel.State == HAL_FDCAN_STATE_BUSY;
}

/* ------------------------------------------------------------------ */
/* RX / TX                                                            */
/* ------------------------------------------------------------------ */
bool can_receive(can_data_t *channel, struct gs_host_frame *rx_frame)
{
    FDCAN_GlobalTypeDef *base = channel->channel.Instance;
    mcan_rx_message_t rx;
    uint32_t timestamp_us = timer_get();
    uint32_t len;

    if (mcan_read_rxfifo(base, 0U, &rx) != status_success) {
        return false;
    }

    rx_frame->channel = channel->nr;
    rx_frame->flags = 0U;
    rx_frame->can_id = rx.use_ext_id ? rx.ext_id : rx.std_id;

    if (rx.use_ext_id) {
        rx_frame->can_id |= CAN_EFF_FLAG;
    }

    if (rx.rtr) {
        rx_frame->can_id |= CAN_RTR_FLAG;
    }

    rx_frame->can_dlc = (uint8_t)(rx.dlc & 0x0FU);

    len = dlc_to_bytes[rx.dlc & 0x0FU];
    if (len != 0U) {
        memcpy(rx_frame->canfd->data, rx.data_8, len);
    }

    if (rx.canfd_frame) {
        rx_frame->canfd_ts->timestamp_us = timestamp_us;

        rx_frame->flags = GS_CAN_FLAG_FD;
        if (rx.bitrate_switch) {
            rx_frame->flags |= GS_CAN_FLAG_BRS;
        }
        if (rx.error_state_indicator) {
            rx_frame->flags |= GS_CAN_FLAG_ESI;
        }
    } else {
        rx_frame->classic_can_ts->timestamp_us = timestamp_us;
    }

    return true;
}

bool can_is_rx_pending(can_data_t *channel)
{
    return MCAN_RXF0S_F0FL_GET(channel->channel.Instance->RXF0S) >= 1U;
}

bool can_send(can_data_t *channel, struct gs_host_frame *frame)
{
    FDCAN_GlobalTypeDef *base = channel->channel.Instance;
    mcan_tx_frame_t tx;
    uint32_t txfqs;
    uint32_t index;
    uint32_t len;

    memset(&tx, 0, sizeof(tx));

    tx.rtr = (frame->can_id & CAN_RTR_FLAG) ? 1U : 0U;

    if (frame->can_id & CAN_EFF_FLAG) {
        tx.use_ext_id = 1U;
        tx.ext_id = frame->can_id & 0x1FFFFFFFU;
    } else {
        tx.use_ext_id = 0U;
        tx.std_id = frame->can_id & 0x7FFU;
    }

    tx.dlc = frame->can_dlc & 0x0FU;

    if (frame->flags & GS_CAN_FLAG_FD) {
        tx.canfd_frame = 1U;
        tx.bitrate_switch = (frame->flags & GS_CAN_FLAG_BRS) ? 1U : 0U;
        tx.error_state_indicator = (frame->flags & GS_CAN_FLAG_ESI) ? 1U : 0U;
    }

    len = dlc_to_bytes[tx.dlc];
    if (len != 0U) {
        memcpy(tx.data_8, frame->canfd->data, len);
    }

    /* Pick a free TX buffer from the TX FIFO/queue. */
    txfqs = base->TXFQS;
    if ((txfqs & MCAN_TXFQS_TFQF_MASK) != 0U) {
        return false; /* TX buffer full, caller retries later */
    }
    index = MCAN_TXFQS_TFQPI_GET(txfqs);

    if (mcan_write_txbuf(base, index, &tx) != status_success) {
        return false;
    }

    base->TXBAR = (1UL << index);

    return true;
}

/* ------------------------------------------------------------------ */
/* Error status (port of m_can.c)                                     */
/* ------------------------------------------------------------------ */
uint32_t can_get_error_status(can_data_t *channel)
{
    /* Nothing to reset: the hardware clears LEC on read. */
    return channel->channel.Instance->PSR;
}

void can_manage_bus_off_recovery(can_data_t *channel, uint32_t err)
{
    /*
     * bxcan recovers from bus-off automatically (ABOM), the M_CAN does not.
     * On bus-off the hardware sets CCCR.INIT by itself, putting the module
     * back into configuration mode.  Clear INIT to start the recovery
     * sequence - but only if the channel was enabled by the host.
     */
    if ((0U != (err & MCAN_PSR_BO_MASK)) &&
        can_is_enabled(channel) &&
        (0U != (channel->channel.Instance->CCCR & MCAN_CCCR_INIT_MASK))) {
        channel->channel.Instance->CCCR &= ~MCAN_CCCR_INIT_MASK;

        for (uint32_t i = 0U; i < 100U; i++) {
            if (0U == (channel->channel.Instance->CCCR & MCAN_CCCR_INIT_MASK)) {
                break;
            }
        }
    }
}

bool can_has_error_status_changed(uint32_t last_err, uint32_t curr_err)
{
    uint8_t curr_lec = (uint8_t)MCAN_PSR_LEC_GET(curr_err);

    if ((0x0U != curr_lec) && (0x7U != curr_lec)) {
        /* An error is being reported in the last error code field. */
        return true;
    }

    /* Error status reported by any other field has changed. */
    return (last_err & ~MCAN_PSR_LEC_MASK) != (curr_err & ~MCAN_PSR_LEC_MASK);
}

static bool status_is_active(uint32_t err)
{
    return !(err & (MCAN_PSR_BO_MASK | MCAN_PSR_EP_MASK));
}

bool can_parse_error_status(can_data_t *channel, struct gs_host_frame *frame, uint32_t last_err, uint32_t curr_err)
{
    bool should_send = false;

    frame->echo_id = 0xFFFFFFFFU;
    frame->can_id = CAN_ERR_FLAG;
    frame->can_dlc = CAN_ERR_DLC;
    frame->classic_can->data[0] = CAN_ERR_LOSTARB_UNSPEC;
    frame->classic_can->data[1] = CAN_ERR_CRTL_UNSPEC;
    frame->classic_can->data[2] = CAN_ERR_PROT_UNSPEC;
    frame->classic_can->data[3] = CAN_ERR_PROT_LOC_UNSPEC;
    frame->classic_can->data[4] = CAN_ERR_TRX_UNSPEC;
    frame->classic_can->data[5] = 0U;
    frame->classic_can->data[6] = 0U;
    frame->classic_can->data[7] = 0U;

    /* We transitioned from passive/bus-off to active, so report the edge. */
    if (!status_is_active(last_err) && status_is_active(curr_err)) {
        frame->can_id |= CAN_ERR_CRTL;
        frame->classic_can->data[1] |= CAN_ERR_CRTL_ACTIVE;
        should_send = true;
    }

    if (curr_err & MCAN_PSR_BO_MASK) {
        if (!(last_err & MCAN_PSR_BO_MASK)) {
            frame->can_id |= CAN_ERR_BUSOFF;
            should_send = true;
        }
    }

    /* The Linux sja1000 driver puts the counters here. */
    frame->classic_can->data[6] = (uint8_t)MCAN_ECR_TEC_GET(channel->channel.Instance->ECR);
    frame->classic_can->data[7] = (uint8_t)MCAN_ECR_REC_GET(channel->channel.Instance->ECR);

    if (curr_err & MCAN_PSR_EP_MASK) {
        if (!(last_err & MCAN_PSR_EP_MASK)) {
            frame->can_id |= CAN_ERR_CRTL;
            frame->classic_can->data[1] |= CAN_ERR_CRTL_RX_PASSIVE | CAN_ERR_CRTL_TX_PASSIVE;
            should_send = true;
        }
    } else if (curr_err & MCAN_PSR_EW_MASK) {
        if (!(last_err & MCAN_PSR_EW_MASK)) {
            frame->can_id |= CAN_ERR_CRTL;
            frame->classic_can->data[1] |= CAN_ERR_CRTL_RX_WARNING | CAN_ERR_CRTL_TX_WARNING;
            should_send = true;
        }
    }

    switch ((uint8_t)MCAN_PSR_LEC_GET(curr_err)) {
    case 0x01U: /* stuff error */
        frame->can_id |= CAN_ERR_PROT;
        frame->classic_can->data[2] |= CAN_ERR_PROT_STUFF;
        should_send = true;
        break;
    case 0x02U: /* form error */
        frame->can_id |= CAN_ERR_PROT;
        frame->classic_can->data[2] |= CAN_ERR_PROT_FORM;
        should_send = true;
        break;
    case 0x03U: /* ack error */
        frame->can_id |= CAN_ERR_ACK;
        should_send = true;
        break;
    case 0x04U: /* bit recessive error */
        frame->can_id |= CAN_ERR_PROT;
        frame->classic_can->data[2] |= CAN_ERR_PROT_BIT1;
        should_send = true;
        break;
    case 0x05U: /* bit dominant error */
        frame->can_id |= CAN_ERR_PROT;
        frame->classic_can->data[2] |= CAN_ERR_PROT_BIT0;
        should_send = true;
        break;
    case 0x06U: /* CRC error */
        frame->can_id |= CAN_ERR_PROT;
        frame->classic_can->data[3] |= CAN_ERR_PROT_LOC_CRC_SEQ;
        should_send = true;
        break;
    default: /* 0 = no error, 7 = no change */
        break;
    }

    return should_send;
}
