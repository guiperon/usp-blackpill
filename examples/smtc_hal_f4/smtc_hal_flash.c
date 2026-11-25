/*!
 * @file      smtc_hal_flash.c
 *
 * @brief     Board specific package FLASH API implementation - STM32F411 (Black Pill) port.
 *
 * See smtc_hal_flash.h for the "virtual page in sector 7" scheme.
 *
 * Differences from the STM32L4 implementation:
 *  - F4 has no ECC on flash words: any byte can be programmed once after an erase
 *    (FLASH_TYPEPROGRAM_BYTE), so writes need no 64-bit alignment.
 *  - The erase unit is a whole sector (128 KB here), hence the RAM shadow.
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2021. All rights reserved.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "smtc_hal_flash.h"
#include "smtc_hal_mcu.h"
#include "stm32f4xx_hal.h"

#define FLASH_OP_SUCCESS ( 0 )
#define FLASH_OP_FAIL ( 1 )

#define SHADOW_SIZE ( HAL_FLASH_NVM_SHADOW_PAGES * ADDR_FLASH_PAGE_SIZE )

static uint8_t flash_shadow[SHADOW_SIZE];

static bool addr_in_nvm( uint32_t addr, uint32_t size )
{
    return ( addr >= HAL_FLASH_NVM_START_ADDR ) &&
           ( ( addr + size ) <= ( HAL_FLASH_NVM_START_ADDR + HAL_FLASH_NVM_SIZE ) );
}

static bool addr_in_shadow( uint32_t addr, uint32_t size )
{
    return ( addr >= HAL_FLASH_NVM_START_ADDR ) && ( ( addr + size ) <= ( HAL_FLASH_NVM_START_ADDR + SHADOW_SIZE ) );
}

static bool sector_erase( void )
{
    FLASH_EraseInitTypeDef erase = {
        .TypeErase    = FLASH_TYPEERASE_SECTORS,
        .Banks        = FLASH_BANK_1,
        .Sector       = HAL_FLASH_NVM_SECTOR,
        .NbSectors    = 1,
        .VoltageRange = FLASH_VOLTAGE_RANGE_3,  // 2.7 V - 3.6 V: byte/half-word/word parallelism
    };
    uint32_t sector_error = 0;

    HAL_FLASH_Unlock( );
    __HAL_FLASH_CLEAR_FLAG( FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR |
                            FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR );
    HAL_StatusTypeDef status = HAL_FLASHEx_Erase( &erase, &sector_error );
    HAL_FLASH_Lock( );
    return ( status == HAL_OK ) && ( sector_error == 0xFFFFFFFFU );
}

static uint32_t program_bytes( uint32_t addr, const uint8_t* buffer, uint32_t size )
{
    uint32_t written = 0;

    HAL_FLASH_Unlock( );
    for( uint32_t i = 0; i < size; i++ )
    {
        // Skip 0xFF bytes: they are already in the erased state
        if( buffer[i] != FLASH_BYTE_EMPTY_CONTENT )
        {
            if( HAL_FLASH_Program( FLASH_TYPEPROGRAM_BYTE, addr + i, buffer[i] ) != HAL_OK )
            {
                break;
            }
        }
        written++;
    }
    HAL_FLASH_Lock( );
    return written;
}

uint16_t hal_flash_get_page_size( void )
{
    return ( uint16_t ) ADDR_FLASH_PAGE_SIZE;
}

uint8_t hal_flash_erase_page( uint32_t addr, uint8_t nb_page )
{
    const uint32_t erase_size = ( uint32_t ) nb_page * ADDR_FLASH_PAGE_SIZE;

    if( !addr_in_shadow( addr, erase_size ) )
    {
        mcu_panic( "flash: erase outside shadow region 0x%08lx (%u pages)\n", addr, nb_page );
        return FLASH_OP_FAIL;
    }

    // 1. Save the erasable region, 2. blank the pages to erase, 3. erase sector, 4. restore
    memcpy( flash_shadow, ( const void* ) HAL_FLASH_NVM_START_ADDR, SHADOW_SIZE );
    memset( &flash_shadow[addr - HAL_FLASH_NVM_START_ADDR], FLASH_BYTE_EMPTY_CONTENT, erase_size );

    if( !sector_erase( ) )
    {
        return FLASH_OP_FAIL;
    }
    // NOTE: anything stored in sector 7 beyond the shadow region is lost here (by design).
    return ( program_bytes( HAL_FLASH_NVM_START_ADDR, flash_shadow, SHADOW_SIZE ) == SHADOW_SIZE ) ? FLASH_OP_SUCCESS
                                                                                                   : FLASH_OP_FAIL;
}

uint32_t hal_flash_write_buffer( uint32_t addr, const uint8_t* buffer, uint32_t size )
{
    if( !addr_in_nvm( addr, size ) )
    {
        mcu_panic( "flash: write outside NVM 0x%08lx\n", addr );
        return 0;
    }
    return program_bytes( addr, buffer, size );
}

void hal_flash_read_buffer( uint32_t addr, uint8_t* buffer, uint32_t size )
{
    memcpy( buffer, ( const void* ) addr, size );
}

#if defined( USE_FLASH_READ_MODIFY_WRITE ) || defined( MULTISTACK )
void hal_flash_read_modify_write( uint32_t addr, const uint8_t* buffer, uint32_t size )
{
    if( !addr_in_shadow( addr, size ) )
    {
        mcu_panic( "flash: RMW outside shadow region 0x%08lx\n", addr );
        return;
    }
    memcpy( flash_shadow, ( const void* ) HAL_FLASH_NVM_START_ADDR, SHADOW_SIZE );
    memcpy( &flash_shadow[addr - HAL_FLASH_NVM_START_ADDR], buffer, size );
    if( sector_erase( ) )
    {
        ( void ) program_bytes( HAL_FLASH_NVM_START_ADDR, flash_shadow, SHADOW_SIZE );
    }
}
#endif
