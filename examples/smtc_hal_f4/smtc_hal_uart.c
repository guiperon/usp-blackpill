/*!
 * @file      smtc_hal_uart.c
 *
 * @brief     UART Hardware Abstraction Layer implementation - STM32F411 (Black Pill) port.
 *
 * Debug traces on USART2: TX = PA2, RX = PA3 (AF7), 115200 8N1.
 * Connect a USB-serial adapter (3.3 V) to PA2/PA3/GND.
 *
 * The hardware-modem UART (UART4 on the Nucleo) is not provided: the hw_modem example is not
 * supported on this port. Moving the traces to the native USB (CDC) is a later step.
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2021. All rights reserved.
 */

#include <stdint.h>
#include <stdbool.h>

#include "smtc_hal_uart.h"
#include "stm32f4xx_hal.h"
#include "smtc_hal_gpio.h"
#include "modem_pinout.h"
#include "smtc_hal_mcu.h"

#define GPIO_PORT_FROM_PIN( pin ) ( ( GPIO_TypeDef* ) ( AHB1PERIPH_BASE + ( ( ( pin ) & 0xF0 ) << 6 ) ) )
#define GPIO_MASK_FROM_PIN( pin ) ( ( uint16_t ) ( 1U << ( ( pin ) & 0x0F ) ) )

static UART_HandleTypeDef huart2;

void trace_uart_init( void )
{
    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = 115200;
    huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    huart2.Init.StopBits     = UART_STOPBITS_1;
    huart2.Init.Parity       = UART_PARITY_NONE;
    huart2.Init.Mode         = UART_MODE_TX;
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    if( HAL_UART_Init( &huart2 ) != HAL_OK )
    {
        mcu_panic( );
    }
}

void trace_uart_deinit( void )
{
    HAL_UART_DeInit( &huart2 );
}

void trace_uart_tx( uint8_t* buff, uint8_t len )
{
    HAL_UART_Transmit( &huart2, ( uint8_t* ) buff, len, 0xffffff );
}

void HAL_UART_MspInit( UART_HandleTypeDef* huart )
{
    if( huart->Instance != USART2 )
    {
        mcu_panic( );
        return;
    }

    __HAL_RCC_USART2_CLK_ENABLE( );

    GPIO_InitTypeDef gpio = {
        .Mode      = GPIO_MODE_AF_PP,
        .Pull      = GPIO_PULLUP,
        .Speed     = GPIO_SPEED_FREQ_VERY_HIGH,
        .Alternate = GPIO_AF7_USART2,
    };

    hal_gpio_enable_clock( DEBUG_UART_TX );
    gpio.Pin = GPIO_MASK_FROM_PIN( DEBUG_UART_TX );
    HAL_GPIO_Init( GPIO_PORT_FROM_PIN( DEBUG_UART_TX ), &gpio );

    hal_gpio_enable_clock( DEBUG_UART_RX );
    gpio.Pin = GPIO_MASK_FROM_PIN( DEBUG_UART_RX );
    HAL_GPIO_Init( GPIO_PORT_FROM_PIN( DEBUG_UART_RX ), &gpio );
}

void HAL_UART_MspDeInit( UART_HandleTypeDef* huart )
{
    if( huart->Instance == USART2 )
    {
        __HAL_RCC_USART2_CLK_DISABLE( );
        HAL_GPIO_DeInit( GPIO_PORT_FROM_PIN( DEBUG_UART_TX ), GPIO_MASK_FROM_PIN( DEBUG_UART_TX ) );
        HAL_GPIO_DeInit( GPIO_PORT_FROM_PIN( DEBUG_UART_RX ), GPIO_MASK_FROM_PIN( DEBUG_UART_RX ) );
    }
}
