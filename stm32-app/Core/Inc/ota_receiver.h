/*
 * ota_receiver.h - STM32F407 OTA Firmware Receiver
 *
 * CDAC ACTS PG-Diploma in DESD
 * Secure OTA Firmware Update System
 *
 * The application firmware receives OTA data from ESP32 via UART,
 * reconstructs complete firmware in the inactive bank, verifies
 * SHA-256, and triggers bootloader for bank switch.
 */

#ifndef OTA_RECEIVER_H
#define OTA_RECEIVER_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

/* ─────────────────────────────────────────────
 * Memory Map (same as bootloader)
 * ───────────────────────────────────────────── */

#define BOOTLOADER_START        0x08000000U
#define APP_BANK_A_START        0x08010000U
#define APP_BANK_B_START        0x08080000U
#define APP_BANK_SIZE           0x00070000U     /* 448KB */
#define METADATA_ADDR           0x080E0000U
#define PAGE_SIZE               1024U           /* Must match server PAGE_SIZE */

/* ─────────────────────────────────────────────
 * Protocol Commands (must match ESP32 config.h)
 * ───────────────────────────────────────────── */

#define CMD_START_OTA           0xAA
#define CMD_CHUNK_DATA          0xBB
#define CMD_END_OTA             0xCC
#define CMD_ACK                 0x06
#define CMD_NACK                0x15
#define CMD_VERSION_REQ         0xDD
#define CMD_VERSION_RESP        0xEE
#define CMD_ABORT               0xFF

/* ─────────────────────────────────────────────
 * OTA Transfer State Machine
 * ───────────────────────────────────────────── */

typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_RECEIVING,
    OTA_STATE_VERIFYING,
    OTA_STATE_COMPLETE,
    OTA_STATE_ERROR
} OTA_State_t;

typedef struct {
    OTA_State_t  state;
    uint8_t      isDelta;           /* 1=delta patch, 0=full firmware */
    uint32_t     totalSize;         /* Total bytes expected */
    uint32_t     receivedSize;      /* Bytes received so far */
    uint16_t     expectedChunk;     /* Next expected chunk index */
    uint32_t     writeAddress;      /* Current flash write address */
    uint32_t     inactiveBankAddr;  /* Start of inactive bank */
} OTA_Context_t;

/* ─────────────────────────────────────────────
 * Delta Patch Header (must match server format)
 * ───────────────────────────────────────────── */

#define PATCH_MAGIC             0x4F544150U     /* "OTAP" */

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint16_t numChangedPages;
    uint32_t newFirmwareSize;
    uint16_t totalPages;
    uint16_t pageSize;
} PatchHeader_t;

/* ─────────────────────────────────────────────
 * Firmware Version
 * ───────────────────────────────────────────── */

#define FIRMWARE_VERSION        "1.0.0"
#define FIRMWARE_VERSION_MAJOR  1
#define FIRMWARE_VERSION_MINOR  0
#define FIRMWARE_VERSION_PATCH  0

/* ─────────────────────────────────────────────
 * UART Configuration
 * ───────────────────────────────────────────── */

#define OTA_UART_INSTANCE       USART2
#define OTA_UART_BAUDRATE       115200
#define OTA_RX_BUFFER_SIZE      2048            /* Circular buffer size */
#define OTA_CHUNK_TIMEOUT       5000            /* ms */

/* ─────────────────────────────────────────────
 * LED Pins (STM32F407 Discovery)
 * ───────────────────────────────────────────── */

#define LED_GREEN               GPIO_PIN_12     /* PD12 - Running OK */
#define LED_ORANGE              GPIO_PIN_13     /* PD13 - OTA in progress */
#define LED_RED                 GPIO_PIN_14     /* PD14 - Error */
#define LED_BLUE                GPIO_PIN_15     /* PD15 - Communication */
#define LED_PORT                GPIOD

/* ─────────────────────────────────────────────
 * Function Prototypes
 * ───────────────────────────────────────────── */

/* OTA Receiver Core */
void        OTA_Init(UART_HandleTypeDef *huart);
void        OTA_Process(void);
OTA_State_t OTA_GetState(void);

/* UART Protocol Handling */
void        OTA_UART_RxCallback(uint8_t data);
void        OTA_HandleStartCommand(uint8_t *data, uint16_t len);
void        OTA_HandleChunkData(uint8_t *data, uint16_t len);
void        OTA_HandleEndCommand(void);
void        OTA_HandleVersionRequest(void);

/* Flash Operations */
HAL_StatusTypeDef OTA_EraseInactiveBank(void);
HAL_StatusTypeDef OTA_WriteToFlash(uint32_t address, uint8_t *data, uint32_t size);
HAL_StatusTypeDef OTA_CopyPage(uint32_t srcAddr, uint32_t destAddr, uint32_t size);

/* Delta Patch Reconstruction */
HAL_StatusTypeDef OTA_ReconstructFromDelta(uint8_t *patchData, uint32_t patchSize);

/* Verification */
uint8_t     OTA_VerifyFirmware(uint32_t startAddr, uint32_t size, uint8_t *expectedHash);
void        OTA_SetUpdatePending(uint8_t *sha256Hash, uint32_t fwSize, const char *version);
void        OTA_TriggerReboot(void);

/* Utility */
void        OTA_SendACK(void);
void        OTA_SendNACK(void);
void        OTA_SendVersion(void);

#endif /* OTA_RECEIVER_H */
