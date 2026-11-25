/*!
 * @file      smtc_hal_spi.c
 *
 * @brief     SPI Hardware Abstraction Layer implementation - STM32F411 (Black Pill) port.
 *
 * SPI1 on PA5 (SCK) / PA6 (MISO) / PA7 (MOSI), AF5. NSS is a plain GPIO driven by lr20xx_hal.c.
 * SPI1 sits on APB2 (96 MHz): prescaler 8 -> 12 MHz SCK.
 *
 * The L4 version used DMA1 channels 2/3 for hal_spi_in_dma()/hal_spi_out_dma(). The radio HAL
 * (lr20xx_hal.c) does not use them, so on the F4 they are plain blocking transfers.
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2021. All rights reserved.
 */

#include <stdint.h>
#include <stdbool.h>

#include "smtc_hal_spi.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx_ll_spi.h"
#include "smtc_hal_gpio.h"
#include "modem_pinout.h"
#include "smtc_hal_mcu.h"

#define GPIO_PORT_FROM_PIN( pin ) ( ( GPIO_TypeDef* ) ( AHB1PERIPH_BASE + ( ( ( pin ) & 0xF0 ) << 6 ) ) )
#define GPIO_MASK_FROM_PIN( pin ) ( ( uint16_t ) ( 1U << ( ( pin ) & 0x0F ) ) )

typedef struct spi_s
{
    SPI_TypeDef*      interface;
    SPI_HandleTypeDef handle;
    struct
    {
        hal_gpio_pin_names_t mosi;
        hal_gpio_pin_names_t miso;
        hal_gpio_pin_names_t sclk;
    } pins;
} spi_t;

static spi_t spi_periph[] = {
    [0] =
        {
            .interface = SPI1,
            .handle    = { 0 },
            .pins =
                {
                    .mosi = NC,
                    .miso = NC,
                    .sclk = NC,
                },
        },
};

#define SPI_PERIPH_NB ( sizeof( spi_periph ) / sizeof( spi_periph[0] ) )

void hal_spi_init( const uint32_t id, const hal_gpio_pin_names_t mosi, const hal_gpio_pin_names_t miso,
                   const hal_gpio_pin_names_t sclk )
{
    assert_param( ( id > 0 ) && ( ( id - 1 ) < SPI_PERIPH_NB ) );
    uint32_t local_id = id - 1;

    spi_periph[local_id].handle.Instance               = spi_periph[local_id].interface;
    spi_periph[local_id].handle.Init.Mode              = SPI_MODE_MASTER;
    spi_periph[local_id].handle.Init.Direction         = SPI_DIRECTION_2LINES;
    spi_periph[local_id].handle.Init.DataSize          = SPI_DATASIZE_8BIT;
    spi_periph[local_id].handle.Init.CLKPolarity       = SPI_POLARITY_LOW;
    spi_periph[local_id].handle.Init.CLKPhase          = SPI_PHASE_1EDGE;
    spi_periph[local_id].handle.Init.NSS               = SPI_NSS_SOFT;
    spi_periph[local_id].handle.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8;  // 96 MHz / 8 = 12 MHz
    spi_periph[local_id].handle.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    spi_periph[local_id].handle.Init.TIMode            = SPI_TIMODE_DISABLE;
    spi_periph[local_id].handle.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    spi_periph[local_id].handle.Init.CRCPolynomial     = 7;

    spi_periph[local_id].pins.mosi = mosi;
    spi_periph[local_id].pins.miso = miso;
    spi_periph[local_id].pins.sclk = sclk;

    if( HAL_SPI_Init( &spi_periph[local_id].handle ) != HAL_OK )
    {
        mcu_panic( );
    }
    __HAL_SPI_ENABLE( &spi_periph[local_id].handle );
}

void hal_spi_de_init( const uint32_t id )
{
    assert_param( ( id > 0 ) && ( ( id - 1 ) < SPI_PERIPH_NB ) );
    HAL_SPI_DeInit( &spi_periph[id - 1].handle );
}

uint16_t hal_spi_in_out( const uint32_t id, const uint16_t out_data )
{
    assert_param( ( id > 0 ) && ( ( id - 1 ) < SPI_PERIPH_NB ) );
    SPI_TypeDef* spi = spi_periph[id - 1].interface;

    while( LL_SPI_IsActiveFlag_TXE( spi ) == 0 )
    {
    }
    LL_SPI_TransmitData8( spi, ( uint8_t ) ( out_data & 0xFF ) );
    while( LL_SPI_IsActiveFlag_RXNE( spi ) == 0 )
    {
    }
    return LL_SPI_ReceiveData8( spi );
}

void hal_spi_in_out_v2( const uint32_t id, uint8_t* out_data, uint8_t* in_data, const uint16_t length )
{
    assert_param( ( id > 0 ) && ( ( id - 1 ) < SPI_PERIPH_NB ) );
    SPI_HandleTypeDef* handle = &spi_periph[id - 1].handle;

    if( ( out_data != NULL ) && ( in_data != NULL ) )
    {
        if( HAL_SPI_TransmitReceive( handle, out_data, in_data, length, 0xFFFFFF ) != HAL_OK )
        {
            mcu_panic( "SPI TXRX error" );
        }
    }
    else if( in_data != NULL )
    {
        if( HAL_SPI_Receive( handle, in_data, length, 0xFFFFFF ) != HAL_OK )
        {
            mcu_panic( "SPI RX error" );
        }
    }
    else if( out_data != NULL )
    {
        if( HAL_SPI_Transmit( handle, out_data, length, 0xFFFFFF ) != HAL_OK )
        {
            mcu_panic( "SPI TX error" );
        }
    }
}

void hal_spi_in_dma( const uint32_t id, uint8_t* in_data, const uint16_t length )
{
    // No DMA on this port (not used by the LR20xx HAL): blocking transfer
    hal_spi_in_out_v2( id, NULL, in_data, length );
}

void hal_spi_out_dma( const uint32_t id, uint8_t* out_data, const uint16_t length )
{
    hal_spi_in_out_v2( id, out_data, NULL, length );
}

void HAL_SPI_MspInit( SPI_HandleTypeDef* spiHandle )
{
    if( spiHandle->Instance != spi_periph[0].interface )
    {
        mcu_panic( );
        return;
    }

    __HAL_RCC_SPI1_CLK_ENABLE( );

    GPIO_InitTypeDef gpio = {
        .Mode      = GPIO_MODE_AF_PP,
        .Pull      = GPIO_NOPULL,
        .Speed     = GPIO_SPEED_FREQ_VERY_HIGH,
        .Alternate = GPIO_AF5_SPI1,
    };

    const hal_gpio_pin_names_t pins[] = { spi_periph[0].pins.miso, spi_periph[0].pins.mosi,
                                          spi_periph[0].pins.sclk };
    for( uint32_t i = 0; i < sizeof( pins ) / sizeof( pins[0] ); i++ )
    {
        hal_gpio_enable_clock( pins[i] );
        gpio.Pin = GPIO_MASK_FROM_PIN( pins[i] );
        HAL_GPIO_Init( GPIO_PORT_FROM_PIN( pins[i] ), &gpio );
    }
}

void HAL_SPI_MspDeInit( SPI_HandleTypeDef* spiHandle )
{
    if( spiHandle->Instance != spi_periph[0].interface )
    {
        mcu_panic( );
        return;
    }

    __HAL_RCC_SPI1_CLK_DISABLE( );

    GPIO_InitTypeDef gpio = { .Speed = GPIO_SPEED_FREQ_LOW };

    gpio.Mode = GPIO_MODE_ANALOG;
    gpio.Pull = GPIO_NOPULL;
    gpio.Pin  = GPIO_MASK_FROM_PIN( spi_periph[0].pins.miso );
    HAL_GPIO_Init( GPIO_PORT_FROM_PIN( spi_periph[0].pins.miso ), &gpio );

    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Pin  = GPIO_MASK_FROM_PIN( spi_periph[0].pins.mosi );
    HAL_GPIO_Init( GPIO_PORT_FROM_PIN( spi_periph[0].pins.mosi ), &gpio );
    gpio.Pin = GPIO_MASK_FROM_PIN( spi_periph[0].pins.sclk );
    HAL_GPIO_Init( GPIO_PORT_FROM_PIN( spi_periph[0].pins.sclk ), &gpio );
}
