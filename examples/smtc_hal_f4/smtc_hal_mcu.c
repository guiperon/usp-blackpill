/*!
 * @file      smtc_hal_mcu.c
 *
 * @brief     MCU Hardware Abstraction Layer implementation - STM32F411CEU6 "Black Pill" port.
 *
 * Main differences from smtc_hal_l4/smtc_hal_mcu.c:
 *  - Clock tree: HSE 25 MHz -> PLL -> SYSCLK 96 MHz (PLLQ = 48 MHz, ready for USB CDC later).
 *  - hal_mcu_wait_us() uses the DWT cycle counter instead of a NOP loop calibrated for 80 MHz.
 *  - Low power: STOP mode is NOT used (TIM5, the modem timer, does not run in STOP on the F4).
 *    hal_mcu_set_sleep_for_ms() uses the RTC wake-up timer + WFI (Sleep mode).
 *  - SysTick_Handler is weak: a FreeRTOS application overrides it to also call
 *    xPortSysTickHandler() (see examples/main_examples/ping_pong_freertos_example).
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2021. All rights reserved.
 */

#include <stdint.h>
#include <stdbool.h>

#include "smtc_hal_mcu.h"
#include "modem_pinout.h"
#include "stm32f4xx_hal.h"

#include "smtc_hal_uart.h"
#include "smtc_hal_rtc.h"
#include "smtc_hal_spi.h"
#include "smtc_hal_lp_timer.h"
#include "smtc_hal_watchdog.h"
#include "smtc_hal_gpio.h"

// 1 to keep the SWD link alive during WFI (lab default: we debug with OpenOCD/Cortex-Debug)
#ifndef HW_DEBUG_PROBE
#define HW_DEBUG_PROBE 1
#endif

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE FUNCTIONS DECLARATION -------------------------------------------
 */

static void system_clock_config( void );

static bool lse_is_running = false;
static void mcu_gpio_init( void );
static void dwt_init( void );

/*
 * -----------------------------------------------------------------------------
 * --- PUBLIC FUNCTIONS DEFINITION ---------------------------------------------
 */

void hal_mcu_critical_section_begin( uint32_t* mask )
{
    *mask = __get_PRIMASK( );
    __disable_irq( );
}

void hal_mcu_critical_section_end( uint32_t* mask )
{
    __set_PRIMASK( *mask );
}

void hal_mcu_disable_irq( void )
{
    __disable_irq( );
}

void hal_mcu_enable_irq( void )
{
    __enable_irq( );
}

void hal_mcu_init( void )
{
    // Initialize MCU HAL library (also sets NVIC priority grouping 4 = 4 preemption bits)
    HAL_Init( );

    // Initialize clocks
    system_clock_config( );

    dwt_init( );

    // Initialize watchdog
    hal_watchdog_init( );

#if HAL_DBG_TRACE == HAL_FEATURE_ON
    // Initialize Uart for debug traces
    trace_uart_init( );
#endif
    if( !lse_is_running )
    {
        SMTC_HAL_TRACE_WARNING( "LSE did not start: RTC running on LSI (time base off by a few %%)\n" );
    }

    // Initialize GPIOs
    mcu_gpio_init( );

    // Initialize the modem timer (TIM5 replaces the L4 LPTIM1)
    hal_lp_timer_init( HAL_LP_TIMER_ID_1 );

    // Initialize SPI for radio
    hal_spi_init( RADIO_SPI_ID, RADIO_SPI_MOSI, RADIO_SPI_MISO, RADIO_SPI_SCLK );

    // Initialize RTC (for real time and wake-up timer)
    hal_rtc_init( );
}

void hal_mcu_reset( void )
{
    __disable_irq( );
    NVIC_SystemReset( );
}

void hal_mcu_wait_us( const int32_t microseconds )
{
    if( microseconds <= 0 )
    {
        return;
    }
    const uint32_t start  = DWT->CYCCNT;
    const uint32_t cycles = ( uint32_t ) microseconds * ( SystemCoreClock / 1000000U );
    while( ( DWT->CYCCNT - start ) < cycles )
    {
    }
}

void hal_mcu_set_sleep_for_ms( const int32_t milliseconds )
{
    if( milliseconds <= 0 )
    {
        return;
    }

    hal_rtc_wakeup_timer_set_ms( milliseconds );

    // Sleep mode (not STOP): every peripheral keeps running, any enabled IRQ wakes the core.
    // Called with IRQs masked (PRIMASK) by the bare-metal main loops: WFI still wakes up on a
    // pending interrupt, which is then served when the caller re-enables IRQs.
    HAL_SuspendTick( );
    __DSB( );
    __WFI( );
    HAL_ResumeTick( );

    hal_rtc_wakeup_timer_stop( );
}

#ifdef USE_FULL_ASSERT
void assert_failed( uint8_t* file, uint32_t line )
{
    SMTC_HAL_TRACE_PRINTF( "Wrong parameters value: file %s on line %lu\n", ( const char* ) file, line );
    while( 1 )
    {
    }
}
#endif

/*
 * Weak: the FreeRTOS example provides its own SysTick_Handler that also feeds the kernel.
 */
__attribute__( ( weak ) ) void SysTick_Handler( void )
{
    HAL_IncTick( );
}

void HAL_MspInit( void )
{
    __HAL_RCC_SYSCFG_CLK_ENABLE( );
    __HAL_RCC_PWR_CLK_ENABLE( );
}

void HardFault_Handler( void )
{
    SMTC_HAL_TRACE_ERROR( "\x1B[0;31m" );  // red color
    SMTC_HAL_TRACE_ERROR( "HARDFAULT_Handler\n" );
    SMTC_HAL_TRACE_ERROR( "\x1B[0m" );  // default color
    while( 1 )
    {
    }
}

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE FUNCTIONS DEFINITION --------------------------------------------
 */

static void system_clock_config( void )
{
    RCC_OscInitTypeDef       osc  = { 0 };
    RCC_ClkInitTypeDef       clk  = { 0 };
    RCC_PeriphCLKInitTypeDef pclk = { 0 };

    __HAL_RCC_PWR_CLK_ENABLE( );
    __HAL_PWR_VOLTAGESCALING_CONFIG( PWR_REGULATOR_VOLTAGE_SCALE1 );

    // LSE (32.768 kHz crystal on PC14/PC15) is in the backup domain
    HAL_PWR_EnableBkUpAccess( );

    // HSE 25 MHz -> /25 -> x192 -> /2 = 96 MHz SYSCLK, /4 = 48 MHz (USB OTG FS)
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_LSI;
    osc.HSEState       = RCC_HSE_ON;
    osc.LSIState       = RCC_LSI_ON;  // IWDG clock (and RTC fallback)
    osc.PLL.PLLState   = RCC_PLL_ON;
    osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
    osc.PLL.PLLM       = 25;
    osc.PLL.PLLN       = 192;
    osc.PLL.PLLP       = RCC_PLLP_DIV2;
    osc.PLL.PLLQ       = 4;
    if( HAL_RCC_OscConfig( &osc ) != HAL_OK )
    {
        mcu_panic( );
    }

    // LSE (32.768 kHz crystal on PC14/PC15) started separately: some Black Pill clones have a
    // slow/weak LSE. If it does not start, fall back to LSI (~32 kHz, a few % error) and warn.
    RCC_OscInitTypeDef lse = { 0 };
    lse.OscillatorType     = RCC_OSCILLATORTYPE_LSE;
    lse.LSEState           = RCC_LSE_ON;
    lse.PLL.PLLState       = RCC_PLL_NONE;
    lse_is_running         = ( HAL_RCC_OscConfig( &lse ) == HAL_OK );

    // AHB 96 MHz, APB1 48 MHz (max 50 MHz, timers x2 = 96 MHz), APB2 96 MHz
    clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV2;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if( HAL_RCC_ClockConfig( &clk, FLASH_LATENCY_3 ) != HAL_OK )
    {
        mcu_panic( );
    }

    // RTC clocked by LSE
    pclk.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    pclk.RTCClockSelection    = lse_is_running ? RCC_RTCCLKSOURCE_LSE : RCC_RTCCLKSOURCE_LSI;
    if( HAL_RCCEx_PeriphCLKConfig( &pclk ) != HAL_OK )
    {
        mcu_panic( );
    }
}

static void dwt_init( void )
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void mcu_gpio_init( void )
{
#if( HW_DEBUG_PROBE == 1 )
    HAL_DBGMCU_EnableDBGSleepMode( );
    HAL_DBGMCU_EnableDBGStopMode( );
    HAL_DBGMCU_EnableDBGStandbyMode( );
#endif

    hal_gpio_init_out( RADIO_NSS, 1 );
#if defined( SPI_SHARED_CS_1 )
    // Other devices on the radio SPI bus must stay deselected, or they fight the radio on MISO
    hal_gpio_init_out( SPI_SHARED_CS_1, 1 );
#endif
#if defined( SPI_SHARED_CS_2 )
    hal_gpio_init_out( SPI_SHARED_CS_2, 1 );
#endif
    // BUSY: push-pull on the Waveshare Core2021, open-drain on some other modules (e.g.
    // Wio-LR2021). The internal pull-up (~40 kOhm) is harmless in the first case and needed in the second.
    hal_gpio_init_in( RADIO_BUSY_PIN, BSP_GPIO_PULL_MODE_UP, BSP_GPIO_IRQ_MODE_OFF, NULL );
    // Radio IRQ: EXTI rising edge, the callback is attached later by smtc_modem_hal
    hal_gpio_init_in( RADIO_DIO_MAIN, BSP_GPIO_PULL_MODE_DOWN, BSP_GPIO_IRQ_MODE_RISING, NULL );
    hal_gpio_init_out( RADIO_NRST, 1 );
}
