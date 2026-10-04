/*
 * usbd_compat.h - STM32 USB Device (USBD) compatibility layer.
 *
 * candleLight_fw's src/usbd_gs_can.c is written against the STM32 USB Device
 * Library.  Instead of re-implementing its frame/linked-list engine we keep
 * that file (and can_common.c) and provide here only the *types, constants and
 * entry points* the STM32 library would have provided.  The functions are
 * implemented in src/usbd_bridge.c on top of CherryUSB.
 *
 * Definitions are taken verbatim from
 *   candleLight_fw/libs/STM32_USB_Device_Library/Core/Inc/usbd_def.h
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "hal_include.h"

/* ------------------------------------------------------------------ */
/* USB protocol constants (STM32 usbd_def.h)                           */
/* ------------------------------------------------------------------ */
#define USB_LEN_DEV_QUALIFIER_DESC 0x0AU
#define USB_LEN_DEV_DESC          0x12U
#define USB_LEN_CFG_DESC          0x09U
#define USB_LEN_IF_DESC           0x09U
#define USB_LEN_EP_DESC           0x07U
#define USB_LEN_LANGID_STR_DESC   0x04U

#define USBD_IDX_LANGID_STR    0x00U
#define USBD_IDX_MFC_STR       0x01U
#define USBD_IDX_PRODUCT_STR   0x02U
#define USBD_IDX_SERIAL_STR    0x03U
#define USBD_IDX_CONFIG_STR    0x04U
#define USBD_IDX_INTERFACE_STR 0x05U

#define USB_REQ_TYPE_STANDARD 0x00U
#define USB_REQ_TYPE_CLASS    0x20U
#define USB_REQ_TYPE_VENDOR   0x40U
#define USB_REQ_TYPE_MASK     0x60U

#define USB_REQ_RECIPIENT_DEVICE    0x00U
#define USB_REQ_RECIPIENT_INTERFACE 0x01U
#define USB_REQ_RECIPIENT_ENDPOINT  0x02U
#define USB_REQ_RECIPIENT_MASK      0x03U

#define USB_REQ_GET_STATUS        0x00U
#define USB_REQ_CLEAR_FEATURE     0x01U
#define USB_REQ_SET_FEATURE       0x03U
#define USB_REQ_SET_ADDRESS       0x05U
#define USB_REQ_GET_DESCRIPTOR    0x06U
#define USB_REQ_SET_DESCRIPTOR    0x07U
#define USB_REQ_GET_CONFIGURATION 0x08U
#define USB_REQ_SET_CONFIGURATION 0x09U
#define USB_REQ_GET_INTERFACE     0x0AU
#define USB_REQ_SET_INTERFACE     0x0BU

#define USB_DESC_TYPE_DEVICE                    0x01U
#define USB_DESC_TYPE_CONFIGURATION             0x02U
#define USB_DESC_TYPE_STRING                    0x03U
#define USB_DESC_TYPE_INTERFACE                 0x04U
#define USB_DESC_TYPE_ENDPOINT                  0x05U
#define USB_DESC_TYPE_DEVICE_QUALIFIER          0x06U
#define USB_DESC_TYPE_OTHER_SPEED_CONFIGURATION 0x07U

#define USB_HS_MAX_PACKET_SIZE     512U
#define USB_FS_MAX_PACKET_SIZE     64U
#define USB_MAX_EP0_SIZE           64U
#define USB_MAX_NUM_CONFIGURATION  1U

#define USBD_STATE_DEFAULT    0x01U
#define USBD_STATE_ADDRESSED  0x02U
#define USBD_STATE_CONFIGURED 0x03U
#define USBD_STATE_SUSPENDED  0x04U

#define USBD_EP_TYPE_CTRL 0x00U
#define USBD_EP_TYPE_ISOC 0x01U
#define USBD_EP_TYPE_BULK 0x02U
#define USBD_EP_TYPE_INTR 0x03U

/* Same values as candleLight_fw/include/usbd_conf.h (guarded so that the
 * unmodified usbd_conf.h can be included alongside this header). */
#ifndef USBD_MAX_NUM_CONFIGURATION
#define USBD_MAX_NUM_CONFIGURATION 1
#endif
#ifndef USBD_DESC_BUF_SIZE
#define USBD_DESC_BUF_SIZE 192
#endif
#ifndef USBD_SUPPORT_USER_STRING_DESC
#define USBD_SUPPORT_USER_STRING_DESC 1
#endif
#ifndef USBD_SELF_POWERED
#define USBD_SELF_POWERED 0
#endif
#ifndef DEVICE_FS
#define DEVICE_FS 0
#endif

/* ------------------------------------------------------------------ */
/* Macros                                                              */
/* ------------------------------------------------------------------ */
#ifndef LOBYTE
#define LOBYTE(x) ((uint8_t)((x) & 0x00FFU))
#endif
#ifndef HIBYTE
#define HIBYTE(x) ((uint8_t)(((x) & 0xFF00U) >> 8U))
#endif
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef __ALIGN_END
#define __ALIGN_END __attribute__((aligned(4U)))
#endif
#ifndef __ALIGN_BEGIN
#define __ALIGN_BEGIN
#endif

/* ------------------------------------------------------------------ */
/* Types (STM32 usbd_def.h)                                            */
/* ------------------------------------------------------------------ */
typedef struct usb_setup_req {
    uint8_t bmRequest;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} USBD_SetupReqTypedef;

struct _USBD_HandleTypeDef;

typedef struct _Device_cb {
    uint8_t (*Init)(struct _USBD_HandleTypeDef *pdev, uint8_t cfgidx);
    uint8_t (*DeInit)(struct _USBD_HandleTypeDef *pdev, uint8_t cfgidx);
    /* Control Endpoints */
    uint8_t (*Setup)(struct _USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
    uint8_t (*EP0_TxSent)(struct _USBD_HandleTypeDef *pdev);
    uint8_t (*EP0_RxReady)(struct _USBD_HandleTypeDef *pdev);
    /* Class Specific Endpoints */
    uint8_t (*DataIn)(struct _USBD_HandleTypeDef *pdev, uint8_t epnum);
    uint8_t (*DataOut)(struct _USBD_HandleTypeDef *pdev, uint8_t epnum);
    uint8_t (*SOF)(struct _USBD_HandleTypeDef *pdev);
    uint8_t (*IsoINIncomplete)(struct _USBD_HandleTypeDef *pdev, uint8_t epnum);
    uint8_t (*IsoOUTIncomplete)(struct _USBD_HandleTypeDef *pdev, uint8_t epnum);

    uint8_t *(*GetHSConfigDescriptor)(uint16_t *length);
    uint8_t *(*GetFSConfigDescriptor)(uint16_t *length);
    uint8_t *(*GetOtherSpeedConfigDescriptor)(uint16_t *length);
    uint8_t *(*GetDeviceQualifierDescriptor)(uint16_t *length);
#if (USBD_SUPPORT_USER_STRING_DESC == 1U)
    uint8_t *(*GetUsrStrDescriptor)(struct _USBD_HandleTypeDef *pdev, uint8_t index, uint16_t *length);
#endif
} USBD_ClassTypeDef;

typedef enum {
    USBD_SPEED_HIGH = 0U,
    USBD_SPEED_FULL = 1U,
    USBD_SPEED_LOW = 2U,
} USBD_SpeedTypeDef;

typedef enum {
    USBD_OK = 0U,
    USBD_BUSY,
    USBD_FAIL,
} USBD_StatusTypeDef;

typedef struct {
    uint32_t status;
    uint32_t is_used;
    uint32_t total_length;
    uint32_t rem_length;
    uint32_t maxpacket;
} USBD_EndpointTypeDef;

typedef struct _USBD_HandleTypeDef {
    uint8_t id;
    uint32_t dev_config;
    uint32_t dev_default_config;
    uint32_t dev_config_status;
    USBD_SpeedTypeDef dev_speed;
    USBD_EndpointTypeDef ep_in[16];
    USBD_EndpointTypeDef ep_out[16];
    uint32_t ep0_state;
    uint32_t ep0_data_len;
    uint8_t dev_state;
    uint8_t dev_old_state;
    uint8_t dev_address;
    uint8_t dev_connection_status;
    uint8_t dev_test_mode;
    uint32_t dev_remote_wakeup;

    USBD_SetupReqTypedef request;
    void *pDesc;
    USBD_ClassTypeDef *pClass;
    void *pClassData;
    void *pUserData;
    void *pData;
} USBD_HandleTypeDef;

/* USB Device descriptors structure (only needed by usbd_desc.c / main.c) */
typedef struct {
    uint8_t *(*GetDeviceDescriptor)(USBD_SpeedTypeDef speed, uint16_t *length);
    uint8_t *(*GetLangIDStrDescriptor)(USBD_SpeedTypeDef speed, uint16_t *length);
    uint8_t *(*GetManufacturerStrDescriptor)(USBD_SpeedTypeDef speed, uint16_t *length);
    uint8_t *(*GetProductStrDescriptor)(USBD_SpeedTypeDef speed, uint16_t *length);
    uint8_t *(*GetSerialStrDescriptor)(USBD_SpeedTypeDef speed, uint16_t *length);
    uint8_t *(*GetConfigurationStrDescriptor)(USBD_SpeedTypeDef speed, uint16_t *length);
    uint8_t *(*GetInterfaceStrDescriptor)(USBD_SpeedTypeDef speed, uint16_t *length);
} USBD_DescriptorsTypeDef;

/* ------------------------------------------------------------------ */
/* Core entry points used by candleLight_fw/src/main.c                 */
/* These are thin wrappers around the CherryUSB device stack.           */
/* ------------------------------------------------------------------ */
USBD_StatusTypeDef USBD_Init(USBD_HandleTypeDef *pdev, USBD_DescriptorsTypeDef *pdesc, uint8_t vbus);
USBD_StatusTypeDef USBD_RegisterClass(USBD_HandleTypeDef *pdev, USBD_ClassTypeDef *pclass);
USBD_StatusTypeDef USBD_Start(USBD_HandleTypeDef *pdev);

/* ------------------------------------------------------------------ */
/* Control helpers implemented in src/usbd_bridge.c                    */
/* ------------------------------------------------------------------ */
extern uint8_t USBD_DescBuf[USBD_DESC_BUF_SIZE] __ALIGN_END;

USBD_StatusTypeDef USBD_CtlSendData(USBD_HandleTypeDef *pdev, uint8_t *pbuf, uint16_t len);
USBD_StatusTypeDef USBD_CtlPrepareRx(USBD_HandleTypeDef *pdev, uint8_t *pbuf, uint16_t len);
void USBD_CtlError(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
void USBD_GetString(uint8_t *desc, uint8_t *unicode, uint16_t *len);

/* ------------------------------------------------------------------ */
/* Low level (LL) driver implemented in src/usbd_bridge.c              */
/* ------------------------------------------------------------------ */
USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr, uint8_t ep_type, uint16_t ep_mps);
USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *pdev, uint8_t ep_addr);
USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *pdev, uint8_t ep_addr, uint8_t *pbuf, uint32_t size);
USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *pdev, uint8_t ep_addr, uint8_t *pbuf, uint32_t size);
uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *pdev, uint8_t ep_addr);
USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *pdev, uint8_t dev_addr);
void USBD_LL_Delay(uint32_t Delay);

/* The single device handle shared between the bridge and the class code. */
USBD_HandleTypeDef *USBD_GetHandle(void);
