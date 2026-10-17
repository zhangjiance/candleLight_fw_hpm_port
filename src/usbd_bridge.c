
#include <string.h>

#include "usbd_core.h"
#include "usb_config.h"

#include "hpm_soc.h"
#include "board.h"
#include "hpm_interrupt.h"

#include "config.h"
#include "gs_usb.h"
#include "led.h"
#include "timer.h"
#include "util.h"
#include "usbd_bridge.h"
#include "usbd_compat.h"
#include "usbd_gs_can.h"

/* ------------------------------------------------------------------ */
/* Descriptors                                                        */
/* ------------------------------------------------------------------ */
/* The DFU runtime interface exists for flash_dfu only. */
#if defined(FLASH_UF2) && FLASH_UF2
#define GSUSB_HAS_DFU_RUNTIME 0
#else
#define GSUSB_HAS_DFU_RUNTIME 1
#endif

#if GSUSB_HAS_DFU_RUNTIME
#define GSUSB_CFG_LEN        50U
#define GSUSB_NUM_INTERFACES 0x02
/* Interface 1: DFU runtime + DFU functional descriptor (detach + upload + download) */
#define GSUSB_DFU_RUNTIME_DESC                                                                     \
    0x09, 0x04, 0x01, 0x00, 0x00, 0xFE, 0x01, 0x01, DFU_INTERFACE_STR_INDEX,                       \
    0x09, 0x21, 0x0B, 0xFF, 0x00, 0x00, 0x08, 0x1A, 0x01
#else
#define GSUSB_CFG_LEN        32U
#define GSUSB_NUM_INTERFACES 0x01
#define GSUSB_DFU_RUNTIME_DESC
#endif

/* Mirrors candleLight_fw/src/usbd_gs_can.c:USBD_GS_CAN_CfgDesc */
#define GSUSB_CONFIG_BODY(desc_type, mps_lo, mps_hi)                                              \
    0x09, desc_type, GSUSB_CFG_LEN, 0x00, /* configuration, wTotalLength                  */      \
        GSUSB_NUM_INTERFACES,             /* bNumInterfaces                              */      \
        0x01,                             /* bConfigurationValue                         */      \
        USBD_IDX_CONFIG_STR,              /* iConfiguration                              */      \
        0x80,                             /* bmAttributes                                */      \
        0x4B,                             /* MaxPower 150 mA                             */      \
        /* Interface 0: gs_usb (vendor specific) */                                               \
        0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0xFF, 0xFF, 0x00,                                     \
        /* EP 0x81 IN, bulk */                                                                    \
        0x07, 0x05, GSUSB_ENDPOINT_IN, 0x02, mps_lo, mps_hi, 0x00,                                \
        /* EP 0x02 OUT, bulk */                                                                   \
        0x07, 0x05, GSUSB_ENDPOINT_OUT, 0x02, mps_lo, mps_hi, 0x00,                               \
        GSUSB_DFU_RUNTIME_DESC

static const uint8_t config_descriptor_fs[] = { GSUSB_CONFIG_BODY(0x02, 0x40, 0x00) };
static const uint8_t config_descriptor_hs[] = { GSUSB_CONFIG_BODY(0x02, 0x00, 0x02) };
static const uint8_t other_speed_descriptor_fs[] = { GSUSB_CONFIG_BODY(0x07, 0x00, 0x02) };
static const uint8_t other_speed_descriptor_hs[] = { GSUSB_CONFIG_BODY(0x07, 0x40, 0x00) };

/* bcdDevice 0x0002: the serial is the fixed UID, so Windows caches WCID per instance - bump it. */
static const uint8_t device_descriptor[] = {
    USB_DEVICE_DESCRIPTOR_INIT(USB_2_0, 0x00, 0x00, 0x00, USBD_VID, USBD_PID_FS, 0x0002, 0x01),
};

static const uint8_t device_quality_descriptor[] = {
    USB_DEVICE_QUALIFIER_DESCRIPTOR_INIT(USB_2_0, 0x00, 0x00, 0x00, 0x01),
};

static char g_serial_string[25];

/* ------------------------------------------------------------------ */
/* Microsoft OS 1.0 / WCID (private copies of candleLight's blobs)     */
/* ------------------------------------------------------------------ */
#define GSUSB_WINUSB_VENDOR_CODE USBD_GS_CAN_VENDOR_CODE

/* WCID GUIDs, one per function: without one libusb/dfu-util cannot claim the interface. */
#define GSUSB_INTERFACE_GUID       "{c15b4308-04d3-11e6-b3ea-6057189e6443}"
#define DFU_RUNTIME_INTERFACE_GUID "{3f8b2c47-9d15-4a6e-b2c8-5e0f7a4d1b93}"

/* Feature descriptor size: 10 + 4 + 4 + 2 + 42 + 4 + 80 bytes. */
#define MSOS_EXT_PROP_LEN (0x92U)

_Static_assert(sizeof(DFU_RUNTIME_INTERFACE_GUID) == sizeof(GSUSB_INTERFACE_GUID),
               "both WCID GUIDs must have the same length");
_Static_assert((10U + 4U + 4U + 2U + (sizeof("DeviceInterfaceGUIDs") * 2U) + 4U +
                (((sizeof(GSUSB_INTERFACE_GUID) - 1U) + 2U) * 2U)) == MSOS_EXT_PROP_LEN,
               "MS OS extended property size mismatch");

static const uint8_t g_msos_string[] = {
    0x12, 0x03,
    0x4D, 0x00, 0x53, 0x00, 0x46, 0x00, 0x54, 0x00,
    0x31, 0x00, 0x30, 0x00, 0x30, 0x00,
    GSUSB_WINUSB_VENDOR_CODE, 0x00,
};

static const uint8_t g_msos_compat_id[] = {
#if GSUSB_HAS_DFU_RUNTIME
    0x40, 0x00, 0x00, 0x00, /* dwLength */
    0x00, 0x01,             /* bcdVersion 1.0 */
    0x04, 0x00,             /* wIndex 0x0004 */
    0x02,                   /* bCount: gs_usb + DFU interface */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* function 0 */
    0x00, 0x01, 0x57, 0x49, 0x4E, 0x55, 0x53, 0x42, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* function 1 */
    0x01, 0x01, 0x57, 0x49, 0x4E, 0x55, 0x53, 0x42, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
#else
    0x28, 0x00, 0x00, 0x00, /* dwLength: one function */
    0x00, 0x01,             /* bcdVersion 1.0 */
    0x04, 0x00,             /* wIndex 0x0004 */
    0x01,                   /* bCount: gs_usb only */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* function 0 */
    0x00, 0x01, 0x57, 0x49, 0x4E, 0x55, 0x53, 0x42, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
#endif
};

static const uint8_t g_msos_ext_prop[] = {
    0x92, 0x00, 0x00, 0x00, /* dwLength */
    0x00, 0x01,             /* bcdVersion 1.0 */
    0x05, 0x00,             /* wIndex 0x0005 */
    0x01, 0x00,             /* bCount */
    0x88, 0x00, 0x00, 0x00, /* dwPropertySize */
    0x07, 0x00, 0x00, 0x00, /* dwPropertyDataType: REG_MULTI_SZ */
    0x2a, 0x00,             /* wPropertyNameLength */
    0x44, 0x00, 0x65, 0x00, 0x76, 0x00, 0x69, 0x00, /* "DeviceInterfaceGUIDs" */
    0x63, 0x00, 0x65, 0x00, 0x49, 0x00, 0x6e, 0x00,
    0x74, 0x00, 0x65, 0x00, 0x72, 0x00, 0x66, 0x00,
    0x61, 0x00, 0x63, 0x00, 0x65, 0x00, 0x47, 0x00,
    0x55, 0x00, 0x49, 0x00, 0x44, 0x00, 0x73, 0x00,
    0x00, 0x00,
    0x50, 0x00, 0x00, 0x00, /* dwPropertyDataLength */
    0x7b, 0x00, 0x63, 0x00, 0x31, 0x00, 0x35, 0x00, /* "{c15b4308-04d3-11e6-b3ea-6057189e6443}" */
    0x62, 0x00, 0x34, 0x00, 0x33, 0x00, 0x30, 0x00,
    0x38, 0x00, 0x2d, 0x00, 0x30, 0x00, 0x34, 0x00,
    0x64, 0x00, 0x33, 0x00, 0x2d, 0x00, 0x31, 0x00,
    0x31, 0x00, 0x65, 0x00, 0x36, 0x00, 0x2d, 0x00,
    0x62, 0x00, 0x33, 0x00, 0x65, 0x00, 0x61, 0x00,
    0x2d, 0x00, 0x36, 0x00, 0x30, 0x00, 0x35, 0x00,
    0x37, 0x00, 0x31, 0x00, 0x38, 0x00, 0x39, 0x00,
    0x65, 0x00, 0x36, 0x00, 0x34, 0x00, 0x34, 0x00,
    0x33, 0x00, 0x7d, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t g_msos_ext_prop_empty[] = {
    0x0a, 0x00, 0x00, 0x00,
    0x00, 0x01,
    0x05, 0x00,
    0x00, 0x00,
};

#if GSUSB_HAS_DFU_RUNTIME
static uint8_t g_msos_ext_prop_dfu[MSOS_EXT_PROP_LEN];
#endif

/* Indexed with setup->wValue (the function index), plus a guard entry. */
static const uint8_t *g_msos_ext_prop_list[] = {
    g_msos_ext_prop,
#if GSUSB_HAS_DFU_RUNTIME
    g_msos_ext_prop_dfu, /* filled by msos_ext_prop_build_one() before use */
#endif
    g_msos_ext_prop_empty,
};

#if GSUSB_HAS_DFU_RUNTIME
/* One-property descriptor built from the ASCII GUID (no hand-written UTF-16LE). */
static void msos_ext_prop_build_one(uint8_t *dst, const char *guid)
{
    static const char prop_name[] = "DeviceInterfaceGUIDs";
    const uint32_t name_bytes = (uint32_t)sizeof(prop_name) * 2U;
    const uint32_t data_bytes = ((uint32_t)strlen(guid) + 2U) * 2U;
    const uint32_t prop_size = 4U + 4U + 2U + name_bytes + 4U + data_bytes;
    const uint32_t desc_len = 10U + prop_size;
    uint32_t i;
    uint32_t p = 0U;

    dst[p++] = (uint8_t)desc_len;
    dst[p++] = (uint8_t)(desc_len >> 8);
    dst[p++] = 0x00U;
    dst[p++] = 0x00U;
    dst[p++] = 0x00U; /* bcdVersion 1.0 */
    dst[p++] = 0x01U;
    dst[p++] = 0x05U; /* wIndex 0x0005 */
    dst[p++] = 0x00U;
    dst[p++] = 0x01U; /* bCount: one property */
    dst[p++] = 0x00U;

    dst[p++] = (uint8_t)prop_size;
    dst[p++] = (uint8_t)(prop_size >> 8);
    dst[p++] = 0x00U;
    dst[p++] = 0x00U;
    dst[p++] = 0x07U; /* dwPropertyDataType: REG_MULTI_SZ */
    dst[p++] = 0x00U;
    dst[p++] = 0x00U;
    dst[p++] = 0x00U;
    dst[p++] = (uint8_t)name_bytes;
    dst[p++] = (uint8_t)(name_bytes >> 8);

    for (i = 0U; i < (uint32_t)sizeof(prop_name); i++) {
        dst[p++] = (uint8_t)prop_name[i];
        dst[p++] = 0x00U;
    }

    dst[p++] = (uint8_t)data_bytes;
    dst[p++] = (uint8_t)(data_bytes >> 8);
    dst[p++] = 0x00U;
    dst[p++] = 0x00U;

    for (i = 0U; guid[i] != '\0'; i++) {
        dst[p++] = (uint8_t)guid[i];
        dst[p++] = 0x00U;
    }
    dst[p++] = 0x00U; /* MULTI_SZ string terminator */
    dst[p++] = 0x00U;
    dst[p++] = 0x00U; /* MULTI_SZ list terminator   */
    dst[p++] = 0x00U;
}
#endif /* GSUSB_HAS_DFU_RUNTIME */

static const struct usb_msosv1_descriptor g_msosv1_desc = {
    .string = g_msos_string,
    .vendor_code = GSUSB_WINUSB_VENDOR_CODE,
    .compat_id = g_msos_compat_id,
    .comp_id_property = g_msos_ext_prop_list,
};

/* ------------------------------------------------------------------ */
/* Descriptor callbacks                                               */
/* ------------------------------------------------------------------ */
static const uint8_t *device_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return device_descriptor;
}

static const uint8_t *config_descriptor_callback(uint8_t speed)
{
    return (speed == USB_SPEED_HIGH) ? config_descriptor_hs : config_descriptor_fs;
}

static const uint8_t *device_quality_descriptor_callback(uint8_t speed)
{
    (void)speed;
    return device_quality_descriptor;
}

static const uint8_t *other_speed_config_descriptor_callback(uint8_t speed)
{
    return (speed == USB_SPEED_HIGH) ? other_speed_descriptor_hs : other_speed_descriptor_fs;
}

static const char *string_descriptor_callback(uint8_t speed, uint8_t index)
{
    static const char langid[2] = { 0x09, 0x04 }; /* LANGID 0x0409 */

    (void)speed;

    switch (index) {
    case 0x00U:
        return langid;
    case USBD_IDX_MFC_STR:
        return (const char *)USBD_MANUFACTURER_STRING;
    case USBD_IDX_PRODUCT_STR:
        return (const char *)USBD_PRODUCT_STRING_FS;
    case USBD_IDX_SERIAL_STR:
        return g_serial_string;
    case USBD_IDX_CONFIG_STR:
        return (const char *)USBD_CONFIGURATION_STRING_FS;
#if GSUSB_HAS_DFU_RUNTIME
    case DFU_INTERFACE_STR_INDEX:
        return (const char *)DFU_INTERFACE_STRING_FS;
#endif
    default:
        return NULL;
    }
}

static const struct usb_descriptor gs_descriptor = {
    .device_descriptor_callback = device_descriptor_callback,
    .config_descriptor_callback = config_descriptor_callback,
    .device_quality_descriptor_callback = device_quality_descriptor_callback,
    .other_speed_descriptor_callback = other_speed_config_descriptor_callback,
    .string_descriptor_callback = string_descriptor_callback,
    .msosv1_descriptor = &g_msosv1_desc,
};

/* ------------------------------------------------------------------ */
/* State                                                              */
/* ------------------------------------------------------------------ */
/* USBD_DescBuf lives in candleLight_fw/src/usbd_desc.c (compiled verbatim). */

static USBD_HandleTypeDef *g_pdev;

/* Control IN staging: the payload may be a stack buffer, so take a copy. */
static uint8_t g_ctl_in_shadow[USBD_DESC_BUF_SIZE];
static uint16_t g_ctl_in_len;
static uint8_t *g_ctl_out_dst;
static uint16_t g_ctl_out_len;
static volatile bool g_ctl_failed;

/* qHD/qTD DMA needs the buffer until the transfer ends: bounce through this one (80 B). */
#define GSUSB_TX_BOUNCE_SIZE GS_HOST_FRAME_SIZE
static USB_NOCACHE_RAM_SECTION USB_MEM_ALIGNX uint8_t g_tx_bounce[GSUSB_TX_BOUNCE_SIZE];

static volatile uint32_t g_ep_out_last_len;

/* ------------------------------------------------------------------ */
/* Control helpers (STM32 usbd_ctlreq / usbd_ioreq)                   */
/* ------------------------------------------------------------------ */
USBD_StatusTypeDef USBD_CtlSendData(USBD_HandleTypeDef *pdev, uint8_t *pbuf, uint16_t len)
{
    (void)pdev;

    if (len > (uint16_t)sizeof(g_ctl_in_shadow)) {
        len = (uint16_t)sizeof(g_ctl_in_shadow);
    }
    if ((pbuf != NULL) && (len != 0U)) {
        memcpy(g_ctl_in_shadow, pbuf, len);
    }
    g_ctl_in_len = len;

    return USBD_OK;
}

USBD_StatusTypeDef USBD_CtlPrepareRx(USBD_HandleTypeDef *pdev, uint8_t *pbuf, uint16_t len)
{
    (void)pdev;

    g_ctl_out_dst = pbuf;
    g_ctl_out_len = len;

    return USBD_OK;
}

void USBD_CtlError(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
    (void)pdev;
    (void)req;

    g_ctl_failed = true;
}

void USBD_GetString(uint8_t *desc, uint8_t *unicode, uint16_t *len)
{
    uint8_t idx = 0U;

    if (desc == NULL) {
        *len = 0U;
        return;
    }

    *len = (uint16_t)((strlen((const char *)desc) * 2U) + 2U);
    unicode[idx++] = (uint8_t)*len;
    unicode[idx++] = USB_DESC_TYPE_STRING;

    while (*desc != '\0') {
        unicode[idx++] = *desc++;
        unicode[idx++] = 0x00U;
    }
}

/* ------------------------------------------------------------------ */
/* Low level driver (STM32 usbd_core LL)                              */
/* ------------------------------------------------------------------ */
USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr, uint8_t ep_type, uint16_t ep_mps)
{
    /* CherryUSB opens the endpoints while parsing the configuration. */
    (void)pdev;
    (void)ep_addr;
    (void)ep_type;
    (void)ep_mps;

    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    (void)pdev;
    (void)ep_addr;

    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *pdev, uint8_t ep_addr, uint8_t *pbuf, uint32_t size)
{
    (void)pdev;

    if (size > sizeof(g_tx_bounce)) {
        return USBD_FAIL;
    }
    memcpy(g_tx_bounce, pbuf, size);

    if (usbd_ep_start_write(0U, ep_addr, g_tx_bounce, size) != 0) {
        return USBD_FAIL;
    }

    return USBD_OK;
}

USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *pdev, uint8_t ep_addr, uint8_t *pbuf, uint32_t size)
{
    (void)pdev;

    if (usbd_ep_start_read(0U, ep_addr, pbuf, size) != 0) {
        return USBD_FAIL;
    }

    return USBD_OK;
}

uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *pdev, uint8_t ep_addr)
{
    (void)pdev;
    (void)ep_addr;

    return g_ep_out_last_len;
}

USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *pdev, uint8_t dev_addr)
{
    /* CherryUSB performs SET_ADDRESS itself. */
    (void)pdev;
    (void)dev_addr;

    return USBD_OK;
}

void USBD_LL_Delay(uint32_t Delay)
{
    HAL_Delay(Delay);
}

/* ------------------------------------------------------------------ */
/* Class / vendor request routing                                     */
/* ------------------------------------------------------------------ */
static int gs_bridge_setup(uint8_t busid, struct usb_setup_packet *setup, uint8_t **data, uint32_t *len)
{
    USBD_StatusTypeDef st;
    bool out_with_data;

    (void)busid;

    out_with_data = (setup->wLength != 0U) && ((setup->bmRequestType & 0x80U) == 0U);

    g_ctl_in_len = 0U;
    g_ctl_out_dst = NULL;
    g_ctl_out_len = 0U;
    g_ctl_failed = false;

    st = USBD_GS_CAN.Setup(g_pdev, (USBD_SetupReqTypedef *)setup);

    if (g_ctl_failed || (st != USBD_OK)) {
        return -1;
    }

    if (g_ctl_in_len != 0U) {
        *data = g_ctl_in_shadow;
        *len = g_ctl_in_len;
        return 0;
    }

    if (out_with_data) {
        /* CherryUSB calls this once the OUT payload arrived (STM32: setup + EP0_RxReady). */
        if ((g_ctl_out_dst != NULL) && (*data != NULL)) {
            uint32_t n = MIN(*len, (uint32_t)g_ctl_out_len);
            memcpy(g_ctl_out_dst, *data, n);
        }
        (void)USBD_GS_CAN.EP0_RxReady(g_pdev);
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Endpoint callbacks                                                 */
/* ------------------------------------------------------------------ */
static void gs_bridge_ep_in(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid;
    (void)nbytes;

    if ((g_pdev != NULL) && (g_pdev->pClass != NULL) && (g_pdev->pClass->DataIn != NULL)) {
        (void)g_pdev->pClass->DataIn(g_pdev, (uint8_t)(ep & 0x7FU));
    }
}

static void gs_bridge_ep_out(uint8_t busid, uint8_t ep, uint32_t nbytes)
{
    (void)busid;

    g_ep_out_last_len = nbytes;

    if ((g_pdev != NULL) && (g_pdev->pClass != NULL) && (g_pdev->pClass->DataOut != NULL)) {
        (void)g_pdev->pClass->DataOut(g_pdev, (uint8_t)(ep & 0x7FU));
    }
}

/* ------------------------------------------------------------------ */
/* Interface / event glue                                             */
/* ------------------------------------------------------------------ */
static void gs_bridge_notify(uint8_t busid, uint8_t event, void *arg)
{
    (void)busid;
    (void)arg;

    if (g_pdev == NULL) {
        return;
    }

    switch (event) {
    case USBD_EVENT_SUSPEND:
        USBD_GS_CAN_SuspendCallback(g_pdev);
        break;
    case USBD_EVENT_RESUME:
        USBD_GS_CAN_ResumeCallback(g_pdev);
        break;
    default:
        break;
    }
}

static void gs_bridge_event(uint8_t busid, uint8_t event)
{
    (void)busid;

    if (g_pdev == NULL) {
        return;
    }

    switch (event) {
    case USBD_EVENT_CONFIGURED:
        g_pdev->dev_state = USBD_STATE_CONFIGURED;
        if ((g_pdev->pClass != NULL) && (g_pdev->pClass->Init != NULL)) {
            (void)g_pdev->pClass->Init(g_pdev, 0U);
        }
        USBD_GS_CAN_ReceiveFromHost(g_pdev);
        USBD_GS_CAN_SendToHost(g_pdev);
        break;

    case USBD_EVENT_RESET:
        g_pdev->dev_state = USBD_STATE_DEFAULT;
        if ((g_pdev->pClass != NULL) && (g_pdev->pClass->DeInit != NULL)) {
            (void)g_pdev->pClass->DeInit(g_pdev, 0U);
        }
        break;

    default:
        break;
    }
}

static struct usbd_interface g_intf_gs_usb = {
    .class_interface_handler = gs_bridge_setup,
    .vendor_handler = gs_bridge_setup,
    .notify_handler = gs_bridge_notify,
};

#if GSUSB_HAS_DFU_RUNTIME
static struct usbd_interface g_intf_dfu = {
    .class_interface_handler = gs_bridge_setup,
};
#endif

static struct usbd_endpoint g_ep_in = {
    .ep_addr = GSUSB_ENDPOINT_IN,
    .ep_cb = gs_bridge_ep_in,
};

static struct usbd_endpoint g_ep_out = {
    .ep_addr = GSUSB_ENDPOINT_OUT,
    .ep_cb = gs_bridge_ep_out,
};

/* ------------------------------------------------------------------ */
/* STM32 USBD core entry points used by candleLight_fw/src/main.c      */
/* ------------------------------------------------------------------ */
USBD_HandleTypeDef *usbd_bridge_get_handle(void)
{
    return g_pdev;
}

USBD_StatusTypeDef USBD_Init(USBD_HandleTypeDef *pdev, USBD_DescriptorsTypeDef *pdesc, uint8_t vbus)
{
    (void)vbus;

    memset(pdev, 0, sizeof(*pdev));
    pdev->pDesc = pdesc;
    pdev->dev_state = USBD_STATE_DEFAULT;

    g_pdev = pdev;

    return USBD_OK;
}

USBD_StatusTypeDef USBD_RegisterClass(USBD_HandleTypeDef *pdev, USBD_ClassTypeDef *pclass)
{
    uint32_t w0, w1, w2;

    pdev->pClass = pclass;

    /* iSerialNumber: derived from the chip unique ID, like candleLight does. */
    memcpy(&w0, &hpm_uid[0], 4);
    memcpy(&w1, &hpm_uid[4], 4);
    memcpy(&w2, &hpm_uid[8], 4);
    hex32(g_serial_string, w0);
    hex32(g_serial_string + 8, w1);
    hex32(g_serial_string + 16, w2);

#if GSUSB_HAS_DFU_RUNTIME
    /* Function 1 (DFU runtime) needs its own GUID or dfu-util cannot claim it. */
    msos_ext_prop_build_one(g_msos_ext_prop_dfu, DFU_RUNTIME_INTERFACE_GUID);
#endif

    usbd_desc_register(0U, &gs_descriptor);
    usbd_add_interface(0U, &g_intf_gs_usb); /* interface 0 (intf_num = 0) */
#if GSUSB_HAS_DFU_RUNTIME
    usbd_add_interface(0U, &g_intf_dfu);    /* interface 1 (intf_num = 1) */
#endif
    usbd_add_endpoint(0U, &g_ep_in);
    usbd_add_endpoint(0U, &g_ep_out);

    return USBD_OK;
}

USBD_StatusTypeDef USBD_Start(USBD_HandleTypeDef *pdev)
{
    (void)pdev;

    if (usbd_initialize(0U, (uintptr_t)BOARD_USB_BASE, gs_bridge_event) != 0) {
        return USBD_FAIL;
    }

    return USBD_OK;
}
