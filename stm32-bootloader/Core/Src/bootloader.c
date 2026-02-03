/*
 * bootloader.c - STM32F407 OTA Bootloader Implementation
 *
 * CDAC ACTS PG-Diploma in DESD
 * Secure OTA Firmware Update System
 *
 * Boot Decision Logic:
 *   1. Read metadata from flash
 *   2. If UPDATE_PENDING: verify new firmware SHA-256
 *      - Valid: switch banks, clear flag, boot new firmware
 *      - Invalid: clear flag, boot old firmware (rollback)
 *   3. If BOOT_FAILED count > MAX: rollback to other bank
 *   4. Otherwise: boot active bank
 */

#include "bootloader.h"
#include "sha256.h"
#include <string.h>

/* ─────────────────────────────────────────────
 * Private Variables
 * ───────────────────────────────────────────── */

static FirmwareMetadata_t currentMetadata;


/* ─────────────────────────────────────────────
 * Bootloader Initialization
 * ───────────────────────────────────────────── */

void Bootloader_Init(void) {
    /* Initialize HAL */
    HAL_Init();
    
    /* Configure system clock */
    SystemClock_Config();
    
    /* Initialize LEDs */
    LED_Init();
    
    /* Blue LED = bootloader active */
    LED_SetStatus(LED_BLUE_PIN, 1);
}


void Bootloader_Run(void) {
    uint32_t appAddress;
    
    /* Step 1: Read metadata */
    if (Metadata_Read(&currentMetadata) != HAL_OK || 
        currentMetadata.magic != METADATA_MAGIC) {
        /* No valid metadata - first boot or corrupted */
        /* Initialize default metadata and boot Bank A */
        Metadata_Init_Default();
        Metadata_Read(&currentMetadata);
    }
    
    /* Step 2: Check for pending update */
    if (currentMetadata.update_flags & FLAG_UPDATE_PENDING) {
        LED_SetStatus(LED_ORANGE_PIN, 1);  /* Orange = verifying update */
        
        /* Get the NEW firmware bank address and info */
        uint32_t newBankAddr;
        uint8_t *expectedHash;
        uint32_t fwSize;
        
        if (currentMetadata.active_bank == BANK_A) {
            /* New firmware is in Bank B */
            newBankAddr = APP_BANK_B_START_ADDR;
            expectedHash = currentMetadata.sha256_b;
            fwSize = currentMetadata.fw_size_b;
        } else {
            /* New firmware is in Bank A */
            newBankAddr = APP_BANK_A_START_ADDR;
            expectedHash = currentMetadata.sha256_a;
            fwSize = currentMetadata.fw_size_a;
        }
        
        /* Verify SHA-256 of new firmware */
        if (Verify_Firmware_SHA256(newBankAddr, fwSize, expectedHash)) {
            /* Verification PASSED - switch to new firmware */
            SwitchActiveBank(&currentMetadata);
            currentMetadata.update_flags = FLAG_UPDATE_SUCCESS;
            currentMetadata.boot_fail_count = 0;
            Metadata_Write(&currentMetadata);
            
            LED_SetStatus(LED_GREEN_PIN, 1);   /* Green = update success */
            LED_SetStatus(LED_ORANGE_PIN, 0);
            HAL_Delay(500);
        } else {
            /* Verification FAILED - rollback, keep current bank */
            currentMetadata.update_flags = FLAG_ROLLBACK;
            currentMetadata.boot_fail_count = 0;
            Metadata_Write(&currentMetadata);
            
            LED_BlinkError(5);  /* Red blink = verification failed */
        }
    }
    
    /* Step 3: Check boot failure count */
    if (currentMetadata.boot_fail_count >= MAX_BOOT_FAILURES) {
        /* Too many failures - switch to other bank */
        SwitchActiveBank(&currentMetadata);
        currentMetadata.boot_fail_count = 0;
        currentMetadata.update_flags = FLAG_ROLLBACK;
        Metadata_Write(&currentMetadata);
        
        LED_BlinkError(3);  /* Red blink = rollback */
    }
    
    /* Step 4: Increment boot count (will be cleared by app on successful boot) */
    currentMetadata.boot_fail_count++;
    Metadata_Write(&currentMetadata);
    
    /* Step 5: Get active application address and jump */
    appAddress = GetActiveAppAddress(&currentMetadata);
    
    /* Validate application: check if valid stack pointer at app address */
    uint32_t stackPtr = *(__IO uint32_t *)appAddress;
    if ((stackPtr & 0x2FFE0000) == 0x20000000) {
        /* Valid stack pointer in SRAM range - jump to application */
        LED_SetStatus(LED_BLUE_PIN, 0);   /* Turn off bootloader LED */
        LED_SetStatus(LED_GREEN_PIN, 1);  /* Green = booting */
        
        Bootloader_JumpToApp(appAddress);
    } else {
        /* Invalid application - try other bank */
        SwitchActiveBank(&currentMetadata);
        Metadata_Write(&currentMetadata);
        
        appAddress = GetActiveAppAddress(&currentMetadata);
        stackPtr = *(__IO uint32_t *)appAddress;
        
        if ((stackPtr & 0x2FFE0000) == 0x20000000) {
            Bootloader_JumpToApp(appAddress);
        } else {
            /* BOTH banks invalid - stay in bootloader, blink error */
            while (1) {
                LED_BlinkError(10);
                HAL_Delay(2000);
            }
        }
    }
}


/* ─────────────────────────────────────────────
 * Jump to Application
 * ───────────────────────────────────────────── */

void Bootloader_JumpToApp(uint32_t appAddress) {
    /* Disable all interrupts */
    __disable_irq();
    
    /* Deinitialize HAL and peripherals */
    HAL_DeInit();
    
    /* Reset SysTick */
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;
    
    /* Clear all pending interrupts */
    for (uint8_t i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFF;  /* Disable all NVIC interrupts */
        NVIC->ICPR[i] = 0xFFFFFFFF;  /* Clear all pending interrupts */
    }
    
    /* Set Vector Table Offset Register (VTOR) to application's vector table */
    SCB->VTOR = appAddress;
    
    /* Get application's initial stack pointer and reset handler */
    uint32_t appStackPtr   = *(__IO uint32_t *)(appAddress);        /* First word = SP */
    uint32_t appResetHandler = *(__IO uint32_t *)(appAddress + 4);  /* Second word = Reset_Handler */
    
    /* Set Main Stack Pointer to application's stack pointer */
    __set_MSP(appStackPtr);
    
    /* Re-enable interrupts */
    __enable_irq();
    
    /* Jump to application's Reset_Handler */
    void (*jumpToApp)(void) = (void (*)(void))appResetHandler;
    jumpToApp();
    
    /* Should never reach here */
    while (1) {}
}


/* ─────────────────────────────────────────────
 * Metadata Management
 * ───────────────────────────────────────────── */

HAL_StatusTypeDef Metadata_Read(FirmwareMetadata_t *meta) {
    /* Read metadata directly from flash (memory-mapped) */
    memcpy(meta, (void *)METADATA_START_ADDR, sizeof(FirmwareMetadata_t));
    
    /* Verify CRC */
    uint32_t expectedCRC = meta->crc32;
    meta->crc32 = 0;
    uint32_t computedCRC = Compute_CRC32_Metadata(meta);
    meta->crc32 = expectedCRC;
    
    if (computedCRC != expectedCRC) {
        return HAL_ERROR;  /* Metadata corrupted */
    }
    
    return HAL_OK;
}


HAL_StatusTypeDef Metadata_Write(FirmwareMetadata_t *meta) {
    HAL_StatusTypeDef status;
    
    /* Compute CRC before writing */
    meta->crc32 = 0;
    meta->crc32 = Compute_CRC32_Metadata(meta);
    
    /* Erase metadata sector */
    status = Flash_EraseSectors(METADATA_SECTOR, METADATA_SECTOR);
    if (status != HAL_OK) return status;
    
    /* Write metadata */
    status = Flash_WriteData(METADATA_START_ADDR, (uint8_t *)meta, sizeof(FirmwareMetadata_t));
    
    return status;
}


HAL_StatusTypeDef Metadata_Init_Default(void) {
    FirmwareMetadata_t defaultMeta;
    
    memset(&defaultMeta, 0, sizeof(FirmwareMetadata_t));
    defaultMeta.magic = METADATA_MAGIC;
    defaultMeta.active_bank = BANK_A;
    defaultMeta.update_flags = FLAG_NONE;
    defaultMeta.boot_fail_count = 0;
    strncpy(defaultMeta.fw_version_a, "1.0.0", FIRMWARE_VERSION_LEN);
    strncpy(defaultMeta.fw_version_b, "0.0.0", FIRMWARE_VERSION_LEN);
    defaultMeta.fw_size_a = 0;
    defaultMeta.fw_size_b = 0;
    
    return Metadata_Write(&defaultMeta);
}


/* ─────────────────────────────────────────────
 * Firmware Verification
 * ───────────────────────────────────────────── */

uint8_t Verify_Firmware_SHA256(uint32_t startAddr, uint32_t size, uint8_t *expectedHash) {
    SHA256_CTX ctx;
    uint8_t computedHash[SHA256_HASH_LEN];
    
    sha256_init(&ctx);
    
    /* Hash firmware in chunks to save RAM */
    uint32_t remaining = size;
    uint32_t addr = startAddr;
    uint8_t buffer[256];
    
    while (remaining > 0) {
        uint32_t chunkSize = (remaining > 256) ? 256 : remaining;
        memcpy(buffer, (void *)addr, chunkSize);
        sha256_update(&ctx, buffer, chunkSize);
        addr += chunkSize;
        remaining -= chunkSize;
    }
    
    sha256_final(&ctx, computedHash);
    
    /* Compare hashes */
    return (memcmp(computedHash, expectedHash, SHA256_HASH_LEN) == 0) ? 1 : 0;
}


uint32_t Compute_CRC32_Metadata(FirmwareMetadata_t *meta) {
    /* Simple CRC32 for metadata integrity (not security - just corruption check) */
    uint32_t crc = 0xFFFFFFFF;
    uint8_t *data = (uint8_t *)meta;
    uint32_t len = sizeof(FirmwareMetadata_t) - sizeof(uint32_t); /* Exclude CRC field */
    
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }
    
    return crc ^ 0xFFFFFFFF;
}


/* ─────────────────────────────────────────────
 * Flash Operations
 * ───────────────────────────────────────────── */

HAL_StatusTypeDef Flash_EraseSectors(uint32_t startSector, uint32_t endSector) {
    HAL_StatusTypeDef status;
    FLASH_EraseInitTypeDef eraseInit;
    uint32_t sectorError = 0;
    
    /* Unlock flash */
    HAL_FLASH_Unlock();
    
    /* Clear pending flags */
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | 
                            FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR | 
                            FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
    
    eraseInit.TypeErase    = FLASH_TYPEERASE_SECTORS;
    eraseInit.VoltageRange = FLASH_VOLTAGE_RANGE_3;  /* 2.7V - 3.6V */
    eraseInit.Sector       = startSector;
    eraseInit.NbSectors    = endSector - startSector + 1;
    
    status = HAL_FLASHEx_Erase(&eraseInit, &sectorError);
    
    HAL_FLASH_Lock();
    
    return status;
}


HAL_StatusTypeDef Flash_WriteData(uint32_t address, uint8_t *data, uint32_t size) {
    HAL_StatusTypeDef status = HAL_OK;
    
    HAL_FLASH_Unlock();
    
    /* Write word by word (32-bit) for efficiency */
    uint32_t i = 0;
    while (i < size) {
        if (size - i >= 4) {
            /* Write 32-bit word */
            uint32_t word = *(uint32_t *)(data + i);
            status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address + i, word);
            i += 4;
        } else {
            /* Write remaining bytes one at a time */
            status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_BYTE, address + i, data[i]);
            i += 1;
        }
        
        if (status != HAL_OK) break;
    }
    
    HAL_FLASH_Lock();
    
    return status;
}


/* ─────────────────────────────────────────────
 * Bank Management
 * ───────────────────────────────────────────── */

uint32_t GetActiveAppAddress(FirmwareMetadata_t *meta) {
    return (meta->active_bank == BANK_A) ? APP_BANK_A_START_ADDR : APP_BANK_B_START_ADDR;
}


uint32_t GetInactiveAppAddress(FirmwareMetadata_t *meta) {
    return (meta->active_bank == BANK_A) ? APP_BANK_B_START_ADDR : APP_BANK_A_START_ADDR;
}


void SwitchActiveBank(FirmwareMetadata_t *meta) {
    meta->active_bank = (meta->active_bank == BANK_A) ? BANK_B : BANK_A;
}


/* ─────────────────────────────────────────────
 * LED Indicators
 * ───────────────────────────────────────────── */

void LED_Init(void) {
    __HAL_RCC_GPIOD_CLK_ENABLE();
    
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin   = LED_GREEN_PIN | LED_ORANGE_PIN | LED_RED_PIN | LED_BLUE_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    
    HAL_GPIO_Init(LED_GPIO_PORT, &GPIO_InitStruct);
    
    /* All LEDs off */
    HAL_GPIO_WritePin(LED_GPIO_PORT, 
                      LED_GREEN_PIN | LED_ORANGE_PIN | LED_RED_PIN | LED_BLUE_PIN, 
                      GPIO_PIN_RESET);
}


void LED_SetStatus(uint16_t pin, uint8_t state) {
    HAL_GPIO_WritePin(LED_GPIO_PORT, pin, state ? GPIO_PIN_SET : GPIO_PIN_RESET);
}


void LED_BlinkError(uint8_t count) {
    for (uint8_t i = 0; i < count; i++) {
        LED_SetStatus(LED_RED_PIN, 1);
        HAL_Delay(200);
        LED_SetStatus(LED_RED_PIN, 0);
        HAL_Delay(200);
    }
}


/* ─────────────────────────────────────────────
 * System Clock Configuration (168MHz)
 * ───────────────────────────────────────────── */

void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    
    /* Configure power supply */
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    
    /* HSE oscillator -> PLL -> 168MHz */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM       = 8;
    RCC_OscInitStruct.PLL.PLLN       = 336;
    RCC_OscInitStruct.PLL.PLLP       = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ       = 7;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);
    
    /* HCLK=168MHz, APB1=42MHz, APB2=84MHz */
    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                       RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5);
}
