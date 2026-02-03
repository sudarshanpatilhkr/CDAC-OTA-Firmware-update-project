/*
 * bootloader.h - STM32F407 OTA Bootloader Definitions
 *
 * CDAC ACTS PG-Diploma in DESD
 * Secure OTA Firmware Update System
 *
 * Memory Layout:
 *   0x08000000 - 0x0800FFFF : Bootloader (64KB, Sectors 0-3)
 *   0x08010000 - 0x0807FFFF : Application Bank A (448KB, Sectors 4-7)
 *   0x08080000 - 0x080DFFFF : Application Bank B (384KB, Sectors 8-10)
 *   0x080E0000 - 0x080FFFFF : Metadata/Flags (128KB, Sector 11)
 */

#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

/* ─────────────────────────────────────────────
 * Flash Memory Map
 * ───────────────────────────────────────────── */

#define BOOTLOADER_START_ADDR       0x08000000U
#define BOOTLOADER_SIZE             0x00010000U     /* 64KB */

#define APP_BANK_A_START_ADDR       0x08010000U     /* Application Bank A start */
#define APP_BANK_B_START_ADDR       0x08080000U     /* Application Bank B start */
#define APP_BANK_SIZE               0x00070000U     /* 448KB per bank */

#define METADATA_START_ADDR         0x080E0000U     /* Metadata sector start */
#define METADATA_SIZE               0x00020000U     /* 128KB */

/* ─────────────────────────────────────────────
 * Flash Sectors (STM32F407)
 * ───────────────────────────────────────────── */

/* Bootloader: Sectors 0-3 (4 x 16KB = 64KB) */
#define BOOTLOADER_SECTOR_START     FLASH_SECTOR_0
#define BOOTLOADER_SECTOR_END       FLASH_SECTOR_3

/* Bank A: Sector 4 (64KB) + Sectors 5-7 (3 x 128KB) = 448KB */
#define BANK_A_SECTOR_START         FLASH_SECTOR_4
#define BANK_A_SECTOR_END           FLASH_SECTOR_7

/* Bank B: Sectors 8-10 (3 x 128KB) = 384KB */
#define BANK_B_SECTOR_START         FLASH_SECTOR_8
#define BANK_B_SECTOR_END           FLASH_SECTOR_10

/* Metadata: Sector 11 (128KB) */
#define METADATA_SECTOR             FLASH_SECTOR_11

/* ─────────────────────────────────────────────
 * Firmware Metadata Structure
 * ───────────────────────────────────────────── */

#define METADATA_MAGIC              0xDEADBEEFU
#define FIRMWARE_VERSION_LEN        16
#define SHA256_HASH_LEN             32

typedef enum {
    BANK_A = 0,
    BANK_B = 1
} ActiveBank_t;

typedef enum {
    FLAG_NONE           = 0x00,
    FLAG_UPDATE_PENDING = 0x01,     /* New firmware written, needs verification */
    FLAG_UPDATE_SUCCESS = 0x02,     /* Update verified and running */
    FLAG_ROLLBACK       = 0x04,     /* Rollback requested */
    FLAG_BOOT_FAILED    = 0x08      /* Boot attempt failed */
} UpdateFlag_t;

/* Stored at METADATA_START_ADDR in flash */
typedef struct __attribute__((packed)) {
    uint32_t    magic;                              /* 0xDEADBEEF = valid metadata */
    ActiveBank_t active_bank;                       /* Currently active bank (A or B) */
    uint8_t     update_flags;                       /* UpdateFlag_t combination */
    uint8_t     boot_fail_count;                    /* Consecutive boot failures */
    uint8_t     reserved;                           /* Alignment padding */
    char        fw_version_a[FIRMWARE_VERSION_LEN]; /* Bank A firmware version */
    char        fw_version_b[FIRMWARE_VERSION_LEN]; /* Bank B firmware version */
    uint8_t     sha256_a[SHA256_HASH_LEN];          /* Bank A firmware SHA-256 */
    uint8_t     sha256_b[SHA256_HASH_LEN];          /* Bank B firmware SHA-256 */
    uint32_t    fw_size_a;                          /* Bank A firmware size */
    uint32_t    fw_size_b;                          /* Bank B firmware size */
    uint32_t    update_timestamp;                   /* Last update timestamp */
    uint32_t    crc32;                              /* CRC of this metadata struct */
} FirmwareMetadata_t;

/* ─────────────────────────────────────────────
 * Boot Configuration
 * ───────────────────────────────────────────── */

#define MAX_BOOT_FAILURES           3               /* Max failures before rollback */
#define WATCHDOG_TIMEOUT_MS         10000            /* 10 second watchdog */

/* ─────────────────────────────────────────────
 * LED Status Indicators
 * ───────────────────────────────────────────── */

/* STM32F407 Discovery Board LEDs */
#define LED_GREEN_PIN               GPIO_PIN_12     /* PD12 - Boot OK */
#define LED_ORANGE_PIN              GPIO_PIN_13     /* PD13 - Update in progress */
#define LED_RED_PIN                 GPIO_PIN_14     /* PD14 - Error / Rollback */
#define LED_BLUE_PIN                GPIO_PIN_15     /* PD15 - Bootloader active */
#define LED_GPIO_PORT               GPIOD

/* ─────────────────────────────────────────────
 * Function Prototypes
 * ───────────────────────────────────────────── */

/* Bootloader core */
void        Bootloader_Init(void);
void        Bootloader_Run(void);
void        Bootloader_JumpToApp(uint32_t appAddress);

/* Metadata management */
HAL_StatusTypeDef Metadata_Read(FirmwareMetadata_t *meta);
HAL_StatusTypeDef Metadata_Write(FirmwareMetadata_t *meta);
HAL_StatusTypeDef Metadata_Init_Default(void);

/* Firmware verification */
uint8_t     Verify_Firmware_SHA256(uint32_t startAddr, uint32_t size, 
                                    uint8_t *expectedHash);
uint32_t    Compute_CRC32_Metadata(FirmwareMetadata_t *meta);

/* Flash operations */
HAL_StatusTypeDef Flash_EraseSectors(uint32_t startSector, uint32_t endSector);
HAL_StatusTypeDef Flash_WriteData(uint32_t address, uint8_t *data, uint32_t size);

/* Bank management */
uint32_t    GetActiveAppAddress(FirmwareMetadata_t *meta);
uint32_t    GetInactiveAppAddress(FirmwareMetadata_t *meta);
void        SwitchActiveBank(FirmwareMetadata_t *meta);

/* LED indicators */
void        LED_Init(void);
void        LED_SetStatus(uint16_t pin, uint8_t state);
void        LED_BlinkError(uint8_t count);

#endif /* BOOTLOADER_H */
