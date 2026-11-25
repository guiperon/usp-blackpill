/*!
 * @file      smtc_hal_flash.h
 *
 * @brief     Board specific package FLASH API definition - STM32F411 (Black Pill) port.
 *
 * The STM32F411 flash is organised in *sectors* (16/64/128 KB), not in the 2 KB pages the
 * LoRa Basics Modem HAL expects. This port emulates 2 KB "virtual pages" inside the last
 * 128 KB sector (sector 7, 0x0806_0000), which the linker script reserves (region NVM).
 *
 * Only the first HAL_FLASH_NVM_SHADOW_PAGES virtual pages can be erased individually:
 * erasing one of them copies that region to a RAM shadow, erases the whole sector and
 * writes back the pages that were not meant to be erased. That is plenty for the LBM
 * contexts (modem, keys, LoRaWAN stack, secure element). FUOTA and Store & Forward need
 * many more pages and are NOT supported on this port (keep LBM_FUOTA / LBM_STORE_AND_FORWARD OFF).
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2021. All rights reserved.
 * (F4 adaptation: same license.)
 */

#ifndef __SMTC_HAL_FLASH_H__
#define __SMTC_HAL_FLASH_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/*
 * -----------------------------------------------------------------------------
 * --- PUBLIC CONSTANTS --------------------------------------------------------
 */

#define HAL_FLASH_NVM_SECTOR ( 7U )                         /* FLASH_SECTOR_7 */
#define HAL_FLASH_NVM_START_ADDR ( ( uint32_t ) 0x08060000 ) /* must match the linker script */
#define HAL_FLASH_NVM_SIZE ( ( uint32_t ) 0x00020000 )       /* 128 KB */

#define ADDR_FLASH_PAGE_SIZE ( ( uint32_t ) 0x00000800 ) /* virtual page = 2 KB */
#define HAL_FLASH_NVM_SHADOW_PAGES ( 4U )                /* erasable pages (8 KB RAM shadow) */

#define FLASH_BYTE_EMPTY_CONTENT ( ( uint8_t ) 0xFF )

#define FLASH_NVM_PAGE_ADDR( page ) ( HAL_FLASH_NVM_START_ADDR + ( ( page ) * ADDR_FLASH_PAGE_SIZE ) )

/* Context layout used by smtc_modem_hal.c (STM32F411xE branch) */
#define ADDR_FLASH_F4_SECURE_ELEMENT_CONTEXT FLASH_NVM_PAGE_ADDR( 0 )
#define ADDR_FLASH_F4_MODEM_CONTEXT FLASH_NVM_PAGE_ADDR( 1 )
#define ADDR_FLASH_F4_LORAWAN_CONTEXT FLASH_NVM_PAGE_ADDR( 2 )
#define ADDR_FLASH_F4_MODEM_KEY_CONTEXT FLASH_NVM_PAGE_ADDR( 3 )
/* Not supported (outside the erasable shadow region): defined only so that the code compiles */
#define ADDR_FLASH_F4_FUOTA FLASH_NVM_PAGE_ADDR( 8 )
#define ADDR_FLASH_F4_STORE_AND_FORWARD FLASH_NVM_PAGE_ADDR( 40 )

/*
 * -----------------------------------------------------------------------------
 * --- PUBLIC FUNCTIONS PROTOTYPES ---------------------------------------------
 */

/**
 * @brief Return the (virtual) page size in bytes
 */
uint16_t hal_flash_get_page_size( void );

/**
 * @brief Erase nb_page virtual pages starting at addr (must lie in the shadow region)
 * @return 0 on success, 1 (FAIL) otherwise
 */
uint8_t hal_flash_erase_page( uint32_t addr, uint8_t nb_page );

/**
 * @brief Write buffer to flash (destination must be erased: 0xFF)
 * @return number of bytes written, 0 on failure
 */
uint32_t hal_flash_write_buffer( uint32_t addr, const uint8_t* buffer, uint32_t size );

/**
 * @brief Read size bytes from flash
 */
void hal_flash_read_buffer( uint32_t addr, uint8_t* buffer, uint32_t size );

#if defined( USE_FLASH_READ_MODIFY_WRITE ) || defined( MULTISTACK )
/**
 * @brief Read-modify-write inside the shadow region
 */
void hal_flash_read_modify_write( uint32_t addr, const uint8_t* buffer, uint32_t size );
#endif

#ifdef __cplusplus
}
#endif

#endif  // __SMTC_HAL_FLASH_H__
