/*
 * main.c - STM32F407 Bootloader Entry Point
 *
 * CDAC ACTS PG-Diploma in DESD
 * Secure OTA Firmware Update System
 *
 * This is the first code that runs on power-up.
 * It initializes the system and decides which firmware bank to boot.
 */

#include "bootloader.h"

int main(void) {
    /* Initialize bootloader (HAL, clocks, LEDs) */
    Bootloader_Init();
    
    /* Run boot decision logic and jump to application */
    Bootloader_Run();
    
    /* Should never reach here - Bootloader_Run jumps to app */
    while (1) {
        LED_BlinkError(10);
        HAL_Delay(1000);
    }
}
