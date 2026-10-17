
#include <string.h>

#include "board_layout.h" /* board API */
#include "can.h"
#include "can_common.h"
#include "can_drv.h"
#include "hal_include.h"
#include "timer.h"

#include "board.h"          /* hardware board */
#include "status_led.h"     /* status LEDs */
#include "board_contract.h" /* board contract */
#include "hpm_clock_drv.h"
#include "hpm_mcan_drv.h"
#include "hpm_mcan_regs.h"

#define M_CAN_PSR_ACT_SYNC      0U
#define M_CAN_SYNC_BUS_TIMEOUT_MS 100U

/* Bit timing / feature constraints. */
const struct gs_device_bt_const CAN_btconst = {
    .feature =
        GS_CAN_FEATURE_LISTEN_ONLY |
        GS_CAN_FEATURE_LOOP_BACK |
        GS_CAN_FEATURE_HW_TIMESTAMP |
        GS_CAN_FEATURE_IDENTIFY |
        GS_CAN_FEATURE_PAD_PKTS_TO_MAX_PKT_SIZE |
        (IS_ENABLED(CONFIG_CANFD) ?
         GS_CAN_FEATURE_FD | GS_CAN_FEATURE_BT_CONST_EXT : 0) |
        (IS_ENABLED(CONFIG_TERMINATION) ?
         GS_CAN_FEATURE_TERMINATION : 0) |
        GS_CAN_FEATURE_BERR_REPORTING |
        GS_CAN_FEATURE_GET_STATE |
        (IS_ENABLED(CONFIG_CANFD) ?
         GS_CAN_FEATURE_TDC : 0) |
        GS_CAN_FEATURE_BUS_OFF_RECOVERY,
    .fclk_can = CAN_CLOCK_SPEED,
    .btc = {
        .tseg1_min = 1,
        .tseg1_max = 256,
        .tseg2_min = 1,
        .tseg2_max = 128,
        .sjw_max = 128,
        .brp_min = 1,
        .brp_max = 512,
        .brp_inc = 1,
    },
};

const struct gs_device_bt_const_extended CAN_btconst_ext = {
    .feature =
        GS_CAN_FEATURE_LISTEN_ONLY |
        GS_CAN_FEATURE_LOOP_BACK |
        GS_CAN_FEATURE_HW_TIMESTAMP |
        GS_CAN_FEATURE_IDENTIFY |
        GS_CAN_FEATURE_PAD_PKTS_TO_MAX_PKT_SIZE |
        (IS_ENABLED(CONFIG_CANFD) ?
         GS_CAN_FEATURE_FD | GS_CAN_FEATURE_BT_CONST_EXT : 0) |
        (IS_ENABLED(CONFIG_TERMINATION) ?
         GS_CAN_FEATURE_TERMINATION : 0) |
        GS_CAN_FEATURE_BERR_REPORTING |
        GS_CAN_FEATURE_GET_STATE |
        (IS_ENABLED(CONFIG_CANFD) ?
         GS_CAN_FEATURE_TDC : 0) |
        GS_CAN_FEATURE_BUS_OFF_RECOVERY,
    .fclk_can = CAN_CLOCK_SPEED,
    .btc = {
        .tseg1_min = 1,
        .tseg1_max = 256,
        .tseg2_min = 1,
        .tseg2_max = 128,
        .sjw_max = 128,
        .brp_min = 1,
        .brp_max = 512,
        .brp_inc = 1,
    },
    .dbtc = {
        .tseg1_min = 1,
        .tseg1_max = 32,
        .tseg2_min = 1,
        .tseg2_max = 16,
        .sjw_max = 16,
        .brp_min = 1,
        .brp_max = 32,
        .brp_inc = 1,
    },
};

const struct gs_device_tdc_const CAN_tdc_const = {
    .tdco_min = 0,
    .tdco_max = 127,
    .tdcf_min = 0,
    .tdcf_max = 127,
    .mode = GS_CAN_TDC_MODE_OFF | GS_CAN_TDC_MODE_AUTO,
};

static const uint8_t dlc_to_bytes[16] = {
    0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 12U, 16U, 20U, 24U, 32U, 48U, 64U
};

/* Board-owned CAN clock. */
static uint32_t can_clk_freq(FDCAN_GlobalTypeDef *ptr)
{
    return board_init_can_clock(ptr);
}

void can_init(can_data_t *channel, const struct board_channel_config *config)
{
    channel->channel.Instance = config->interface;

    channel->channel.Init.NominalPrescaler = 1U;
    channel->channel.Init.NominalSyncJumpWidth = 16U;
    channel->channel.Init.NominalTimeSeg1 = 127U;
    channel->channel.Init.NominalTimeSeg2 = 32U;
    channel->channel.Init.DataPrescaler = 1U;
    channel->channel.Init.DataSyncJumpWidth = 4U;
    channel->channel.Init.DataTimeSeg1 = 31U;
    channel->channel.Init.DataTimeSeg2 = 8U;
    channel->channel.Init.FrameFormat = FDCAN_FRAME_FD_BRS;
    channel->channel.Init.Mode = FDCAN_MODE_NORMAL;
    channel->channel.Init.AutoRetransmission = 1U;
    channel->channel.State = HAL_FDCAN_STATE_RESET;
    channel->channel.configured = false;

    /* Ensure pins and clock. */
    init_can_pins(config->interface);
    (void)board_init_can_clock(config->interface);
}

static void can_apply_timing(can_data_t *channel, mcan_config_t *cfg)
{
    const struct gs_device_bittiming *bt = &channel->bittiming;
    const struct gs_device_bittiming *dbt = &channel->data_bittiming;

    cfg->use_lowlevel_timing_setting = true;

    cfg->can_timing.prescaler = bt->brp;
    cfg->can_timing.num_seg1 = bt->prop_seg + bt->phase_seg1;
    cfg->can_timing.num_seg2 = bt->phase_seg2;
    cfg->can_timing.num_sjw = bt->sjw;
    cfg->can_timing.enable_tdc = false;

    cfg->canfd_timing.prescaler = dbt->brp;
    cfg->canfd_timing.num_seg1 = dbt->prop_seg + dbt->phase_seg1;
    cfg->canfd_timing.num_seg2 = dbt->phase_seg2;
    cfg->canfd_timing.num_sjw = dbt->sjw;

    if (channel->tdc.mode & GS_CAN_TDC_MODE_AUTO) {
        cfg->enable_tdc = true;
        cfg->tdc_config.ssp_offset = channel->tdc.tdco;
        cfg->tdc_config.filter_window_length = channel->tdc.tdcf;
        cfg->canfd_timing.enable_tdc = true;
    } else {
        cfg->enable_tdc = false;
        cfg->tdc_config.ssp_offset = 0U;
        cfg->tdc_config.filter_window_length = 0U;
        cfg->canfd_timing.enable_tdc = false;
    }
}

void can_drv_enable(struct can_channel *channel)
{
    FDCAN_GlobalTypeDef *base = channel->channel.Instance;
    mcan_config_t cfg;
    mcan_node_mode_t node_mode;
    const uint32_t feature = channel->feature;

    if ((feature & (GS_CAN_FEATURE_LISTEN_ONLY | GS_CAN_FEATURE_LOOP_BACK)) ==
        (GS_CAN_FEATURE_LISTEN_ONLY | GS_CAN_FEATURE_LOOP_BACK)) {
        node_mode = mcan_mode_loopback_internal;
    } else if (feature & GS_CAN_FEATURE_LISTEN_ONLY) {
        node_mode = mcan_mode_listen_only;
    } else if (feature & GS_CAN_FEATURE_LOOP_BACK) {
        node_mode = mcan_mode_loopback_external;
    } else {
        node_mode = mcan_mode_normal;
    }

    channel->channel.Init.Mode = (node_mode == mcan_mode_loopback_internal) ? FDCAN_MODE_INTERNAL_LOOPBACK :
                               (node_mode == mcan_mode_listen_only)     ? FDCAN_MODE_BUS_MONITORING :
                               (node_mode == mcan_mode_loopback_external) ? FDCAN_MODE_EXTERNAL_LOOPBACK :
                                                                           FDCAN_MODE_NORMAL;
    channel->channel.Init.AutoRetransmission = (feature & GS_CAN_FEATURE_ONE_SHOT) ? 0U : 1U;
    channel->channel.Init.FrameFormat = (feature & GS_CAN_FEATURE_FD) ?
                                        FDCAN_FRAME_FD_BRS : FDCAN_FRAME_CLASSIC;

    mcan_get_default_config(base, &cfg);

    cfg.enable_canfd = IS_ENABLED(CONFIG_CANFD);
    cfg.mode = node_mode;
    cfg.disable_auto_retransmission = (channel->channel.Init.AutoRetransmission == 0U);

    can_apply_timing(channel, &cfg);

    /* Accept all frames into RX FIFO0. */
    mcan_get_default_ram_config(base, &cfg.ram_config, cfg.enable_canfd);
    cfg.ram_config.enable_std_filter = false;
    cfg.ram_config.std_filter_elem_count = 0U;
    cfg.ram_config.enable_ext_filter = false;
    cfg.ram_config.ext_filter_elem_count = 0U;

    cfg.all_filters_config.std_id_filter_list.mcan_filter_elem_count = 0U;
    cfg.all_filters_config.ext_id_filter_list.mcan_filter_elem_count = 0U;

    cfg.all_filters_config.global_filter_config.accept_non_matching_std_frame_option =
        MCAN_ACCEPT_NON_MATCHING_FRAME_OPTION_IN_RXFIFO0;
    cfg.all_filters_config.global_filter_config.accept_non_matching_ext_frame_option =
        MCAN_ACCEPT_NON_MATCHING_FRAME_OPTION_IN_RXFIFO0;
    cfg.all_filters_config.global_filter_config.reject_remote_std_frame = false;
    cfg.all_filters_config.global_filter_config.reject_remote_ext_frame = false;

    if (mcan_init(base, &cfg, can_clk_freq(base)) == status_success) {
        channel->channel.State = HAL_FDCAN_STATE_BUSY;
        channel->channel.configured = true;
        /* Channel LED on. */
        status_led_channel((uint8_t)can_channel_get_nr(channel), true);
    } else {
        channel->channel.State = HAL_FDCAN_STATE_READY;
        channel->channel.configured = false;
        status_led_channel((uint8_t)can_channel_get_nr(channel), false);
    }
}

void can_drv_disable(struct can_channel *channel)
{
    FDCAN_GlobalTypeDef *base = channel->channel.Instance;

    if (base != NULL) {
        base->CCCR |= MCAN_CCCR_INIT_MASK;
        while ((base->CCCR & MCAN_CCCR_INIT_MASK) == 0U) {
            ;
        }
    }

    channel->channel.State = HAL_FDCAN_STATE_READY;
    channel->channel.configured = false;

    status_led_channel((uint8_t)can_channel_get_nr(channel), false);
}

bool can_receive(can_data_t *channel, struct gs_host_frame *rx_frame)
{
    FDCAN_GlobalTypeDef *base = channel->channel.Instance;
    mcan_rx_message_t rx;
    uint32_t timestamp_us = timer_get();
    uint32_t len;

    if (mcan_read_rxfifo(base, 0U, &rx) != status_success) {
        return false;
    }

    status_led_activity((uint8_t)can_channel_get_nr(channel), true);

    rx_frame->channel = can_channel_get_nr(channel);
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

    /* Report RX FIFO overflow. */
    if (0U != (mcan_get_interrupt_flags(base) & MCAN_IR_RF0L_MASK)) {
        mcan_clear_interrupt_flags(base, MCAN_IR_RF0L_MASK);
        rx_frame->flags |= GS_CAN_FLAG_OVERFLOW;
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

    if (mcan_transmit_via_txfifo_nonblocking(base, &tx, NULL) != status_success) {
        return false;
    }

    status_led_activity((uint8_t)can_channel_get_nr(channel), false);

    return true;
}

void can_drv_read_reg_status(struct can_channel *channel)
{
    channel->reg_status.ecr = channel->channel.Instance->ECR;
    channel->reg_status.psr = channel->channel.Instance->PSR; /* Clear-on-read. */
}

bool can_drv_bus_error_pending(const struct can_channel *channel)
{
    const uint32_t psr = channel->reg_status.psr;

    return can_is_lec_error((uint8_t)MCAN_PSR_LEC_GET(psr)) ||
           can_is_lec_error((uint8_t)MCAN_PSR_DLEC_GET(psr));
}

bool can_drv_handle_bus_error(const struct can_channel *channel, struct gs_host_frame *frame)
{
    const uint32_t ecr = channel->reg_status.ecr;
    const uint8_t tx_err = (uint8_t)MCAN_ECR_TEC_GET(ecr);
    const uint8_t rx_err = (uint8_t)MCAN_ECR_REC_GET(ecr);

    if (tx_err == 0U && rx_err == 0U) {
        return false;
    }

    frame->classic_can->data[6] = tx_err;
    frame->classic_can->data[7] = rx_err;

    frame->can_id |= CAN_ERR_PROT | CAN_ERR_BUSERROR | CAN_ERR_CNT;

    const uint32_t psr = channel->reg_status.psr;
    can_lec_error_to_frame(frame, (uint8_t)MCAN_PSR_LEC_GET(psr));
    can_lec_error_to_frame(frame, (uint8_t)MCAN_PSR_DLEC_GET(psr));

    return true;
}

enum gs_can_state can_drv_get_state(const struct can_channel *channel)
{
    const uint32_t psr = channel->reg_status.psr;

    if (!(psr & (MCAN_PSR_BO_MASK | MCAN_PSR_EW_MASK | MCAN_PSR_EP_MASK))) {
        return GS_CAN_STATE_ERROR_ACTIVE;
    }
    if (psr & MCAN_PSR_BO_MASK) {
        return GS_CAN_STATE_BUS_OFF;
    }
    if (psr & MCAN_PSR_EP_MASK) {
        return GS_CAN_STATE_ERROR_PASSIVE;
    }

    return GS_CAN_STATE_ERROR_WARNING;
}

void can_drv_get_device_state(const struct can_channel *channel, struct gs_device_state *state)
{
    const uint32_t ecr = channel->reg_status.ecr;

    state->state = can_drv_get_state(channel);
    state->rxerr = (uint8_t)MCAN_ECR_REC_GET(ecr);
    state->txerr = (uint8_t)MCAN_ECR_TEC_GET(ecr);
}

void can_drv_handle_state_change(const struct can_channel *channel, struct gs_host_frame *frame)
{
    const uint32_t ecr = channel->reg_status.ecr;

    const uint8_t tx_err = (uint8_t)MCAN_ECR_TEC_GET(ecr);
    const uint8_t rx_err = (uint8_t)MCAN_ECR_REC_GET(ecr);

    const enum gs_can_state tx_state = can_err_to_state(tx_err);
    const enum gs_can_state rx_state = can_err_to_state(rx_err);

    if (tx_state >= rx_state) {
        frame->classic_can->data[1] |= gs_can_tx_state_to_frame(tx_state);
    }
    if (tx_state <= rx_state) {
        frame->classic_can->data[1] |= gs_can_rx_state_to_frame(rx_state);
    }
}

void can_drv_get_device_tdc(const struct can_channel *channel, struct gs_device_tdc *tdc)
{
    const uint32_t tdcv = MCAN_PSR_TDCV_GET(channel->reg_status.psr);

    /* No FD frame sent yet. */
    if (channel->tdc.tdco >= tdcv) {
        return;
    }

    tdc->tdcv = tdcv - channel->tdc.tdco;
}

static void mcan_synchronize_bus(struct can_channel *channel)
{
    const uint32_t timeout = HAL_GetTick() + M_CAN_SYNC_BUS_TIMEOUT_MS;
    uint32_t now;

    do {
        can_drv_read_reg_status(channel);
        if (MCAN_PSR_ACT_GET(channel->reg_status.psr) != M_CAN_PSR_ACT_SYNC) {
            return;
        }

        now = HAL_GetTick();
    } while (!time_after(now, timeout));
}

void can_drv_handle_bus_off_recovery(struct can_channel *channel)
{
    FDCAN_GlobalTypeDef *base = channel->channel.Instance;

    /* Restart M_CAN. */
    base->TXBCR = MCAN_TXBCR_CR_SET(0x7U);
    base->CCCR |= MCAN_CCCR_INIT_MASK;
    while ((base->CCCR & MCAN_CCCR_INIT_MASK) == 0U) {
        ;
    }
    base->CCCR &= ~MCAN_CCCR_INIT_MASK;
    while ((base->CCCR & MCAN_CCCR_INIT_MASK) != 0U) {
        ;
    }

    mcan_synchronize_bus(channel);
}
