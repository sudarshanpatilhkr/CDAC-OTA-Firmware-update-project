/*
 * ota_receiver.c - STM32F407 OTA Firmware Receiver Implementation
 *
 * CDAC ACTS PG-Diploma in DESD
 * Secure OTA Firmware Update System
 *
 * Receives firmware data from ESP32 via UART, handles both full
 * firmware and delta patch modes, writes to inactive flash bank,
 * verifies SHA-256 integrity, and triggers bootloader for bank switch.
 */

#include "ota_receiver.h"
#include "sha256.h"
#include <string.h>
#include <stdio.h>

/* ─────────────────────────────────────────────
 * Private Variables
 * ───────────────────────────────────────────── */

static UART_HandleTypeDef *otaUart;
static OTA_Context_t otaCtx;

/* Receive buffer (circular) */
static uint8_t rxBuffer[OTA_RX_BUFFER_SIZE];
static volatile uint16_t rxHead = 0;
static volatile uint16_t rxTail = 0;

/* Temporary storage for incoming data */
static uint8_t chunkBuffer[PAGE_SIZE + 64];  /* Chunk data + header overhead */
static uint32_t chunkBufferLen = 0;

/* Full firmware buffer for delta reconstruction */
static uint8_t pageBuffer[PAGE_SIZE];


/* ─────────────────────────────────────────────
 * OTA Initialization
 * ───────────────────────────────────────────── */

void OTA_Init(UART_HandleTypeDef *huart) {
    otaUart = huart;
    
    /* Reset context */
    memset(&otaCtx, 0, sizeof(OTA_Context_t));
    otaCtx.state = OTA_STATE_IDLE;
    
    /* Determine inactive bank */
    /* Read current active bank from metadata */
    uint32_t metaMagic = *(__IO uint32_t *)METADATA_ADDR;
    if (metaMagic == 0xDEADBEEF) {
        uint8_t activeBank = *(__IO uint8_t *)(METADATA_ADDR + 4);
        otaCtx.inactiveBankAddr = (activeBank == 0) ? APP_BANK_B_START : APP_BANK_A_START;
    } else {
        /* Default: assume running from Bank A, write to Bank B */
        otaCtx.inactiveBankAddr = APP_BANK_B_START;
    }
    
    /* Enable UART receive interrupt */
    HAL_UART_Receive_IT(otaUart, &rxBuffer[rxHead], 1);
}


/* ─────────────────────────────────────────────
 * Main OTA Processing Loop
 * ───────────────────────────────────────────── */

void OTA_Process(void) {
    /* Process received bytes from circular buffer */
    while (rxTail != rxHead) {
        uint8_t byte = rxBuffer[rxTail];
        rxTail = (rxTail + 1) % OTA_RX_BUFFER_SIZE;
        
        switch (otaCtx.state) {
            case OTA_STATE_IDLE:
                /* Waiting for command */
                if (byte == CMD_START_OTA) {
                    /* Read START_OTA packet (6 more bytes: isDelta + size + checksum) */
                    chunkBuffer[0] = byte;
                    chunkBufferLen = 1;
                    /* Continue collecting in next iteration */
                    otaCtx.state = OTA_STATE_RECEIVING;
                    HAL_GPIO_WritePin(LED_PORT, LED_ORANGE, GPIO_PIN_SET);
                }
                else if (byte == CMD_VERSION_REQ) {
                    OTA_SendVersion();
                }
                break;
                
            case OTA_STATE_RECEIVING:
                chunkBuffer[chunkBufferLen++] = byte;
                
                /* Check if we have a complete packet */
                if (chunkBuffer[0] == CMD_START_OTA && chunkBufferLen == 7) {
                    OTA_HandleStartCommand(chunkBuffer, chunkBufferLen);
                    chunkBufferLen = 0;
                }
                else if (chunkBuffer[0] == CMD_CHUNK_DATA && chunkBufferLen >= 5) {
                    /* We have header: cmd(1) + index(2) + length(2) */
                    uint16_t dataLen = chunkBuffer[3] | (chunkBuffer[4] << 8);
                    uint16_t totalPacketLen = 5 + dataLen + 1;  /* header + data + checksum */
                    
                    if (chunkBufferLen == totalPacketLen) {
                        OTA_HandleChunkData(chunkBuffer, chunkBufferLen);
                        chunkBufferLen = 0;
                    }
                }
                else if (byte == CMD_END_OTA && chunkBufferLen <= 2) {
                    OTA_HandleEndCommand();
                    chunkBufferLen = 0;
                }
                else if (byte == CMD_ABORT) {
                    otaCtx.state = OTA_STATE_IDLE;
                    chunkBufferLen = 0;
                    HAL_GPIO_WritePin(LED_PORT, LED_ORANGE, GPIO_PIN_RESET);
                    HAL_GPIO_WritePin(LED_PORT, LED_RED, GPIO_PIN_SET);
                }
                
                /* Reset if buffer overflow */
                if (chunkBufferLen >= sizeof(chunkBuffer)) {
                    chunkBufferLen = 0;
                }
                break;
                
            default:
                break;
        }
    }
}


OTA_State_t OTA_GetState(void) {
    return otaCtx.state;
}


/* ─────────────────────────────────────────────
 * Protocol Command Handlers
 * ───────────────────────────────────────────── */

void OTA_HandleStartCommand(uint8_t *data, uint16_t len) {
    /*
     * START_OTA packet: [0xAA] [isDelta] [size:4] [checksum]
     */
    
    /* Verify checksum */
    uint8_t checksum = 0;
    for (int i = 0; i < len - 1; i++) {
        checksum ^= data[i];
    }
    if (checksum != data[len - 1]) {
        OTA_SendNACK();
        return;
    }
    
    otaCtx.isDelta = data[1];
    otaCtx.totalSize = data[2] | (data[3] << 8) | (data[4] << 16) | (data[5] << 24);
    otaCtx.receivedSize = 0;
    otaCtx.expectedChunk = 0;
    otaCtx.writeAddress = otaCtx.inactiveBankAddr;
    
    /* Erase inactive bank for new firmware */
    if (OTA_EraseInactiveBank() != HAL_OK) {
        otaCtx.state = OTA_STATE_ERROR;
        OTA_SendNACK();
        return;
    }
    
    otaCtx.state = OTA_STATE_RECEIVING;
    OTA_SendACK();
    
    /* Blink orange LED to indicate OTA in progress */
    HAL_GPIO_WritePin(LED_PORT, LED_ORANGE, GPIO_PIN_SET);
}


void OTA_HandleChunkData(uint8_t *data, uint16_t len) {
    /*
     * CHUNK packet: [0xBB] [index:2] [length:2] [data:N] [checksum]
     */
    
    /* Verify checksum */
    uint8_t checksum = 0;
    for (int i = 0; i < len - 1; i++) {
        checksum ^= data[i];
    }
    if (checksum != data[len - 1]) {
        OTA_SendNACK();
        return;
    }
    
    uint16_t chunkIndex = data[1] | (data[2] << 8);
    uint16_t dataLen = data[3] | (data[4] << 8);
    uint8_t *chunkData = &data[5];
    
    /* Verify chunk sequence */
    if (chunkIndex != otaCtx.expectedChunk) {
        OTA_SendNACK();
        return;
    }
    
    if (otaCtx.isDelta) {
        /* For delta: store in temporary buffer for later reconstruction */
        /* Write raw patch data to inactive bank temporarily */
        if (OTA_WriteToFlash(otaCtx.writeAddress, chunkData, dataLen) != HAL_OK) {
            otaCtx.state = OTA_STATE_ERROR;
            OTA_SendNACK();
            return;
        }
    } else {
        /* For full firmware: write directly to inactive bank */
        if (OTA_WriteToFlash(otaCtx.writeAddress, chunkData, dataLen) != HAL_OK) {
            otaCtx.state = OTA_STATE_ERROR;
            OTA_SendNACK();
            return;
        }
    }
    
    otaCtx.writeAddress += dataLen;
    otaCtx.receivedSize += dataLen;
    otaCtx.expectedChunk++;
    
    /* Toggle blue LED on each chunk (visual progress) */
    HAL_GPIO_TogglePin(LED_PORT, LED_BLUE);
    
    OTA_SendACK();
}


void OTA_HandleEndCommand(void) {
    otaCtx.state = OTA_STATE_VERIFYING;
    HAL_GPIO_WritePin(LED_PORT, LED_ORANGE, GPIO_PIN_RESET);
    
    if (otaCtx.isDelta) {
        /* Delta mode: reconstruct full firmware from patch */
        /* Patch data is at inactiveBankAddr, need to process it */
        /* For this implementation, the reconstruction happens
         * by reading the patch header and applying changes */
        
        /* Read patch header from start of inactive bank */
        PatchHeader_t patchHdr;
        memcpy(&patchHdr, (void *)otaCtx.inactiveBankAddr, sizeof(PatchHeader_t));
        
        if (patchHdr.magic != PATCH_MAGIC) {
            otaCtx.state = OTA_STATE_ERROR;
            OTA_SendNACK();
            return;
        }
        
        /* Extract SHA-256 from end of patch data */
        uint8_t expectedHash[32];
        uint32_t hashOffset = otaCtx.inactiveBankAddr + otaCtx.receivedSize - 32;
        memcpy(expectedHash, (void *)hashOffset, 32);
        
        /* 
         * Note: Full delta reconstruction would:
         * 1. Read patch entries (page_index + page_data)
         * 2. For each page in new firmware:
         *    - If page changed: use data from patch
         *    - If page unchanged: copy from active bank
         * 3. Write reconstructed firmware to a temporary location
         * 4. Verify SHA-256
         * 
         * For simplicity in this implementation, we set the update
         * pending flag and let the bootloader handle verification.
         */
        
        OTA_SetUpdatePending(expectedHash, patchHdr.newFirmwareSize, "");
        OTA_SendACK();
        
        otaCtx.state = OTA_STATE_COMPLETE;
        HAL_GPIO_WritePin(LED_PORT, LED_GREEN, GPIO_PIN_SET);
        
        /* Trigger system reset after short delay */
        HAL_Delay(1000);
        OTA_TriggerReboot();
        
    } else {
        /* Full firmware mode: verify what we wrote */
        
        /* Extract SHA-256 from last 32 bytes of received data */
        uint32_t fwSize = otaCtx.receivedSize - 32;  /* Firmware size excluding hash */
        uint8_t expectedHash[32];
        memcpy(expectedHash, 
               (void *)(otaCtx.inactiveBankAddr + fwSize), 
               32);
        
        /* Verify SHA-256 */
        if (OTA_VerifyFirmware(otaCtx.inactiveBankAddr, fwSize, expectedHash)) {
            /* Verification PASSED */
            OTA_SetUpdatePending(expectedHash, fwSize, "");
            OTA_SendACK();
            
            otaCtx.state = OTA_STATE_COMPLETE;
            HAL_GPIO_WritePin(LED_PORT, LED_GREEN, GPIO_PIN_SET);
            
            /* Trigger system reset after short delay */
            HAL_Delay(1000);
            OTA_TriggerReboot();
        } else {
            /* Verification FAILED */
            otaCtx.state = OTA_STATE_ERROR;
            HAL_GPIO_WritePin(LED_PORT, LED_RED, GPIO_PIN_SET);
            OTA_SendNACK();
        }
    }
}


void OTA_HandleVersionRequest(void) {
    OTA_SendVersion();
}


/* ─────────────────────────────────────────────
 * Flash Operations
 * ───────────────────────────────────────────── */

HAL_StatusTypeDef OTA_EraseInactiveBank(void) {
    HAL_StatusTypeDef status;
    FLASH_EraseInitTypeDef eraseInit;
    uint32_t sectorError = 0;
    
    HAL_FLASH_Unlock();
    
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | 
                            FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR | 
                            FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
    
    /* Determine which sectors to erase based on inactive bank */
    if (otaCtx.inactiveBankAddr == APP_BANK_B_START) {
        eraseInit.Sector    = FLASH_SECTOR_8;
        eraseInit.NbSectors = 3;  /* Sectors 8, 9, 10 */
    } else {
        eraseInit.Sector    = FLASH_SECTOR_4;
        eraseInit.NbSectors = 4;  /* Sectors 4, 5, 6, 7 */
    }
    
    eraseInit.TypeErase    = FLASH_TYPEERASE_SECTORS;
    eraseInit.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    
    status = HAL_FLASHEx_Erase(&eraseInit, &sectorError);
    
    HAL_FLASH_Lock();
    
    return status;
}


HAL_StatusTypeDef OTA_WriteToFlash(uint32_t address, uint8_t *data, uint32_t size) {
    HAL_StatusTypeDef status = HAL_OK;
    
    HAL_FLASH_Unlock();
    
    for (uint32_t i = 0; i < size; i += 4) {
        uint32_t word;
        if (i + 4 <= size) {
            word = *(uint32_t *)(data + i);
        } else {
            /* Handle remaining bytes (pad with 0xFF) */
            word = 0xFFFFFFFF;
            memcpy(&word, data + i, size - i);
        }
        
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address + i, word);
        if (status != HAL_OK) break;
    }
    
    HAL_FLASH_Lock();
    
    return status;
}


HAL_StatusTypeDef OTA_CopyPage(uint32_t srcAddr, uint32_t destAddr, uint32_t size) {
    /* Read page from source */
    memcpy(pageBuffer, (void *)srcAddr, size);
    
    /* Write to destination */
    return OTA_WriteToFlash(destAddr, pageBuffer, size);
}


/* ─────────────────────────────────────────────
 * Verification
 * ───────────────────────────────────────────── */

uint8_t OTA_VerifyFirmware(uint32_t startAddr, uint32_t size, uint8_t *expectedHash) {
    SHA256_CTX ctx;
    uint8_t computedHash[32];
    uint8_t buffer[256];
    
    sha256_init(&ctx);
    
    uint32_t remaining = size;
    uint32_t addr = startAddr;
    
    while (remaining > 0) {
        uint32_t chunkSize = (remaining > 256) ? 256 : remaining;
        memcpy(buffer, (void *)addr, chunkSize);
        sha256_update(&ctx, buffer, chunkSize);
        addr += chunkSize;
        remaining -= chunkSize;
    }
    
    sha256_final(&ctx, computedHash);
    
    return (memcmp(computedHash, expectedHash, 32) == 0) ? 1 : 0;
}


void OTA_SetUpdatePending(uint8_t *sha256Hash, uint32_t fwSize, const char *version) {
    /*
     * Update metadata to signal bootloader that new firmware is ready.
     * Bootloader will verify and switch banks on next boot.
     */
    
    /* Read current metadata */
    uint8_t metaBuffer[128];
    memcpy(metaBuffer, (void *)METADATA_ADDR, 128);
    
    /* Modify update flags */
    metaBuffer[5] = 0x01;  /* FLAG_UPDATE_PENDING */
    
    /* Write SHA-256 hash for inactive bank */
    uint32_t metaMagic = *(uint32_t *)metaBuffer;
    if (metaMagic == 0xDEADBEEF) {
        uint8_t activeBank = metaBuffer[4];
        uint32_t hashOffset;
        uint32_t sizeOffset;
        
        if (activeBank == 0) {
            /* Active=A, new firmware in B */
            hashOffset = 40;   /* sha256_b offset in struct */
            sizeOffset = 76;   /* fw_size_b offset */
        } else {
            /* Active=B, new firmware in A */
            hashOffset = 8;    /* sha256_a offset */
            sizeOffset = 72;   /* fw_size_a offset */
        }
        
        memcpy(&metaBuffer[hashOffset], sha256Hash, 32);
        memcpy(&metaBuffer[sizeOffset], &fwSize, 4);
    }
    
    /* Erase metadata sector and write updated metadata */
    FLASH_EraseInitTypeDef eraseInit;
    uint32_t sectorError;
    
    HAL_FLASH_Unlock();
    
    eraseInit.TypeErase    = FLASH_TYPEERASE_SECTORS;
    eraseInit.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    eraseInit.Sector       = FLASH_SECTOR_11;
    eraseInit.NbSectors    = 1;
    
    HAL_FLASHEx_Erase(&eraseInit, &sectorError);
    
    /* Write metadata back */
    for (uint32_t i = 0; i < 128; i += 4) {
        uint32_t word = *(uint32_t *)(&metaBuffer[i]);
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, METADATA_ADDR + i, word);
    }
    
    HAL_FLASH_Lock();
}


void OTA_TriggerReboot(void) {
    /* System software reset */
    HAL_NVIC_SystemReset();
}


/* ─────────────────────────────────────────────
 * UART Communication
 * ───────────────────────────────────────────── */

void OTA_UART_RxCallback(uint8_t data) {
    rxBuffer[rxHead] = data;
    rxHead = (rxHead + 1) % OTA_RX_BUFFER_SIZE;
    
    /* Re-enable receive interrupt */
    HAL_UART_Receive_IT(otaUart, &rxBuffer[rxHead], 1);
}


void OTA_SendACK(void) {
    uint8_t ack = CMD_ACK;
    HAL_UART_Transmit(otaUart, &ack, 1, 100);
}


void OTA_SendNACK(void) {
    uint8_t nack = CMD_NACK;
    HAL_UART_Transmit(otaUart, &nack, 1, 100);
}


void OTA_SendVersion(void) {
    uint8_t resp = CMD_VERSION_RESP;
    HAL_UART_Transmit(otaUart, &resp, 1, 100);
    
    const char *version = FIRMWARE_VERSION;
    HAL_UART_Transmit(otaUart, (uint8_t *)version, strlen(version) + 1, 100);
}
