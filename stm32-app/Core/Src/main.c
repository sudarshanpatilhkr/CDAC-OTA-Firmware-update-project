/*
 * main.c - STM32F407 Application Firmware
 *
 * CDAC ACTS PG-Diploma in DESD
 * Secure OTA Firmware Update System
 *
 * This is the main application firmware running from Bank A or Bank B.
 * It performs normal application tasks while also handling OTA updates
 * received from ESP32 via UART.
 *
 * Vector Table: Relocated by bootloader via VTOR register.
 * Start Address: 0x08010000 (Bank A) or 0x08080000 (Bank B)
 */

#include "stm32f4xx_hal.h"
#include "ota_receiver.h"
#include <string.h>

/* ─────────────────────────────────────────────
 * Private Variables
 * ───────────────────────────────────────────── */

UART_HandleTypeDef huart2;      /* USART2 for ESP32 communication */
static uint8_t uartRxByte;      /* Single byte receive buffer */
static volatile uint32_t tickCounter = 0;

/* ─────────────────────────────────────────────
 * Function Prototypes
 * ───────────────────────────────────────────── */

void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
void Application_Run(void);
void ClearBootFailCount(void);

/* ─────────────────────────────────────────────
 * Main Entry Point
 * ───────────────────────────────────────────── */

int main(void) {
    /* HAL Initialization */
    HAL_Init();
    
    /* Configure system clock to 168MHz */
    SystemClock_Config();
    
    /* Initialize peripherals */
    MX_GPIO_Init();
    MX_USART2_UART_Init();
    
    /* Signal successful boot: Green LED ON */
    HAL_GPIO_WritePin(LED_PORT, LED_GREEN, GPIO_PIN_SET);
    
    /* Clear boot failure count in metadata (we booted successfully) */
    ClearBootFailCount();
    
    /* Initialize OTA receiver */
    OTA_Init(&huart2);
    
    /* Start UART receive interrupt */
    HAL_UART_Receive_IT(&huart2, &uartRxByte, 1);
    
    /* Main application loop */
    while (1) {
        /* Process OTA commands if any */
        OTA_Process();
        
        /* Normal application tasks */
        Application_Run();
        
        HAL_Delay(10);
    }
}


/* ─────────────────────────────────────────────
 * Application Logic
 * ───────────────────────────────────────────── */

void Application_Run(void) {
    /*
     * Main application behavior.
     * This is where you put your actual application code.
     * 
     * For demonstration: Heartbeat LED blink pattern indicates
     * firmware version and that application is running normally.
     */
    
    static uint32_t lastBlink = 0;
    static uint8_t blinkState = 0;
    
    /* Heartbeat: Green LED blinks every 1 second when idle */
    if (OTA_GetState() == OTA_STATE_IDLE) {
        if (HAL_GetTick() - lastBlink >= 1000) {
            lastBlink = HAL_GetTick();
            blinkState = !blinkState;
            HAL_GPIO_WritePin(LED_PORT, LED_GREEN, 
                            blinkState ? GPIO_PIN_SET : GPIO_PIN_RESET);
        }
    }
    
    /* 
     * Add your application-specific code here:
     * - Sensor readings
     * - Data processing  
     * - Communication tasks
     * - Control logic
     * etc.
     */
}


void ClearBootFailCount(void) {
    /*
     * Clear boot failure counter in metadata to indicate successful boot.
     * The bootloader increments this on each boot attempt.
     * If it reaches MAX_BOOT_FAILURES, bootloader will rollback.
     * We clear it here to signal: "Application booted fine."
     */
    
    uint32_t metaMagic = *(__IO uint32_t *)METADATA_ADDR;
    if (metaMagic == 0xDEADBEEF) {
        uint8_t bootFailCount = *(__IO uint8_t *)(METADATA_ADDR + 6);
        
        if (bootFailCount > 0) {
            /* Read current metadata */
            uint8_t metaBuffer[128];
            memcpy(metaBuffer, (void *)METADATA_ADDR, 128);
            
            /* Clear boot fail count */
            metaBuffer[6] = 0;
            
            /* Clear update flags (mark as stable) */
            metaBuffer[5] = 0x02;  /* FLAG_UPDATE_SUCCESS */
            
            /* Rewrite metadata */
            FLASH_EraseInitTypeDef eraseInit;
            uint32_t sectorError;
            
            HAL_FLASH_Unlock();
            
            eraseInit.TypeErase    = FLASH_TYPEERASE_SECTORS;
            eraseInit.VoltageRange = FLASH_VOLTAGE_RANGE_3;
            eraseInit.Sector       = FLASH_SECTOR_11;
            eraseInit.NbSectors    = 1;
            
            __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | 
                                    FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR | 
                                    FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
            
            HAL_FLASHEx_Erase(&eraseInit, &sectorError);
            
            for (uint32_t i = 0; i < 128; i += 4) {
                uint32_t word = *(uint32_t *)(&metaBuffer[i]);
                HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, METADATA_ADDR + i, word);
            }
            
            HAL_FLASH_Lock();
        }
    }
}


/* ─────────────────────────────────────────────
 * UART Interrupt Callback
 * ───────────────────────────────────────────── */

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART2) {
        /* Forward received byte to OTA receiver */
        OTA_UART_RxCallback(uartRxByte);
        
        /* Re-enable receive interrupt */
        HAL_UART_Receive_IT(&huart2, &uartRxByte, 1);
    }
}


/* ─────────────────────────────────────────────
 * Peripheral Initialization
 * ───────────────────────────────────────────── */

static void MX_GPIO_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    /* Enable GPIO clocks */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    
    /* Configure LED pins (PD12-PD15) */
    GPIO_InitStruct.Pin   = LED_GREEN | LED_ORANGE | LED_RED | LED_BLUE;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_PORT, &GPIO_InitStruct);
    
    /* All LEDs off initially */
    HAL_GPIO_WritePin(LED_PORT, LED_GREEN | LED_ORANGE | LED_RED | LED_BLUE, 
                      GPIO_PIN_RESET);
    
    /* Configure User Button (PA0) */
    GPIO_InitStruct.Pin  = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}


static void MX_USART2_UART_Init(void) {
    /* USART2: PA2 (TX), PA3 (RX) - Connected to ESP32 */
    
    __HAL_RCC_USART2_CLK_ENABLE();
    
    /* Configure USART2 GPIO pins */
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin       = GPIO_PIN_2 | GPIO_PIN_3;   /* PA2=TX, PA3=RX */
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_PULLUP;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    /* USART2 configuration */
    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = OTA_UART_BAUDRATE;
    huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    huart2.Init.StopBits     = UART_STOPBITS_1;
    huart2.Init.Parity       = UART_PARITY_NONE;
    huart2.Init.Mode         = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart2);
    
    /* Enable USART2 interrupt */
    HAL_NVIC_SetPriority(USART2_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
}


void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState       = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState   = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM       = 8;
    RCC_OscInitStruct.PLL.PLLN       = 336;
    RCC_OscInitStruct.PLL.PLLP       = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ       = 7;
    HAL_RCC_OscConfig(&RCC_OscInitStruct);
    
    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                       RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5);
}


/* ─────────────────────────────────────────────
 * Interrupt Handlers
 * ───────────────────────────────────────────── */

void USART2_IRQHandler(void) {
    HAL_UART_IRQHandler(&huart2);
}


void SysTick_Handler(void) {
    HAL_IncTick();
}

void NMI_Handler(void) {}
void HardFault_Handler(void) { while(1) { LED_BlinkError(10); } }
void MemManage_Handler(void) { while(1) {} }
void BusFault_Handler(void)  { while(1) {} }
void UsageFault_Handler(void){ while(1) {} }
void SVC_Handler(void) {}
void PendSV_Handler(void) {}

/* Helper for error LED (defined in ota_receiver.h uses LED_PORT) */
void LED_BlinkError(uint8_t count) {
    for (uint8_t i = 0; i < count; i++) {
        HAL_GPIO_WritePin(LED_PORT, LED_RED, GPIO_PIN_SET);
        HAL_Delay(200);
        HAL_GPIO_WritePin(LED_PORT, LED_RED, GPIO_PIN_RESET);
        HAL_Delay(200);
    }
}
