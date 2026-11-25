/*!
 * @file      smtc_hal_lp_timer.c
 *
 * @brief     "Low power" timer HAL - STM32F411 (Black Pill) port.
 *
 * The STM32F411 has no LPTIM. The modem timer is implemented with the 32-bit general
 * purpose timer TIM5 (HAL_LP_TIMER_ID_1) and TIM2 (HAL_LP_TIMER_ID_2, only used by SX127x).
 *
 *   APB1 timer clock = 96 MHz  ->  prescaler 9600  ->  10 kHz  ->  1 tick = 100 us
 *
 * Each start programs a one-shot (OPM) of N ticks; the update interrupt calls the callback.
 * Resolution 100 us, range 2^32 x 100 us (~119 h): no clamping needed (the L4 LPTIM clamps at 2 s).
 *
 * Limitation: TIM5 stops in STOP mode, which is why smtc_hal_mcu.c only uses Sleep (WFI).
 *
 * The Clear BSD License
 * Copyright Semtech Corporation 2021. All rights reserved.
 */

#include <stdint.h>
#include <stdbool.h>

#include "smtc_hal_lp_timer.h"
#include "smtc_hal_mcu.h"
#include "stm32f4xx_hal.h"

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE CONSTANTS -------------------------------------------------------
 */

#define HAL_LP_TIMER_NB 2
#define LP_TIMER_TICK_HZ 10000U  // 100 us per tick

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE VARIABLES -------------------------------------------------------
 */

static TIM_TypeDef* const lp_tim_instance[HAL_LP_TIMER_NB] = { TIM5, TIM2 };
static const IRQn_Type    lp_tim_irqn[HAL_LP_TIMER_NB]     = { TIM5_IRQn, TIM2_IRQn };

static hal_lp_timer_irq_t lp_tim_irq[HAL_LP_TIMER_NB] = {
    { .context = NULL, .callback = NULL },
    { .context = NULL, .callback = NULL },
};

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE FUNCTIONS -------------------------------------------------------
 */

static uint32_t lp_timer_input_clock_hz( void )
{
    // On STM32F4, timer clock = PCLK1 if APB1 prescaler is 1, otherwise 2 x PCLK1
    const uint32_t pclk1 = HAL_RCC_GetPCLK1Freq( );
    return ( ( RCC->CFGR & RCC_CFGR_PPRE1 ) == RCC_CFGR_PPRE1_DIV1 ) ? pclk1 : ( 2U * pclk1 );
}

static void lp_timer_irq_handler( hal_lp_timer_id_t id )
{
    TIM_TypeDef* tim = lp_tim_instance[id];

    if( ( tim->SR & TIM_SR_UIF ) != 0 )
    {
        tim->SR = ~TIM_SR_UIF;  // rc_w0: clear only UIF
        tim->CR1 &= ~TIM_CR1_CEN;
        tim->DIER &= ~TIM_DIER_UIE;

        if( lp_tim_irq[id].callback != NULL )
        {
            lp_tim_irq[id].callback( lp_tim_irq[id].context );
        }
    }
}

/*
 * -----------------------------------------------------------------------------
 * --- PUBLIC FUNCTIONS DEFINITION ---------------------------------------------
 */

void hal_lp_timer_init( hal_lp_timer_id_t id )
{
    TIM_TypeDef* tim = lp_tim_instance[id];

    if( id == HAL_LP_TIMER_ID_1 )
    {
        __HAL_RCC_TIM5_CLK_ENABLE( );
    }
    else
    {
        __HAL_RCC_TIM2_CLK_ENABLE( );
    }

    tim->CR1  = TIM_CR1_OPM | TIM_CR1_URS;  // one-pulse, update IRQ only on overflow (not on UG)
    tim->DIER = 0;
    tim->PSC  = ( lp_timer_input_clock_hz( ) / LP_TIMER_TICK_HZ ) - 1U;
    tim->ARR  = 0xFFFFFFFFU;
    tim->EGR  = TIM_EGR_UG;  // load PSC now (URS set: no interrupt)
    tim->SR   = 0;

    HAL_NVIC_SetPriority( lp_tim_irqn[id], HAL_IRQ_PRIO_MODEM, 0 );
    HAL_NVIC_EnableIRQ( lp_tim_irqn[id] );

    lp_tim_irq[id] = ( hal_lp_timer_irq_t ) { .context = NULL, .callback = NULL };
}

void hal_lp_timer_start( hal_lp_timer_id_t id, const uint32_t milliseconds, const hal_lp_timer_irq_t* tmr_irq )
{
    TIM_TypeDef* tim = lp_tim_instance[id];

    uint64_t ticks = ( ( uint64_t ) milliseconds * LP_TIMER_TICK_HZ ) / 1000U;
    if( ticks == 0 )
    {
        ticks = 1;
    }
    if( ticks > 0xFFFFFFFFULL )
    {
        ticks = 0xFFFFFFFFULL;
    }

    tim->CR1 &= ~TIM_CR1_CEN;
    lp_tim_irq[id] = *tmr_irq;

    tim->ARR  = ( uint32_t ) ticks - 1U;  // update event when CNT goes ARR -> 0, i.e. after `ticks` ticks
    tim->CNT  = 0;
    tim->EGR  = TIM_EGR_UG;  // reload shadow registers (no IRQ thanks to URS)
    tim->SR   = ~TIM_SR_UIF;
    tim->DIER = TIM_DIER_UIE;
    tim->CR1 |= TIM_CR1_CEN;
}

void hal_lp_timer_stop( hal_lp_timer_id_t id )
{
    TIM_TypeDef* tim = lp_tim_instance[id];

    tim->CR1 &= ~TIM_CR1_CEN;
    tim->DIER &= ~TIM_DIER_UIE;
    tim->SR = ~TIM_SR_UIF;
}

void hal_lp_timer_irq_enable( hal_lp_timer_id_t id )
{
    HAL_NVIC_EnableIRQ( lp_tim_irqn[id] );
}

void hal_lp_timer_irq_disable( hal_lp_timer_id_t id )
{
    HAL_NVIC_DisableIRQ( lp_tim_irqn[id] );
}

void TIM5_IRQHandler( void )
{
    lp_timer_irq_handler( HAL_LP_TIMER_ID_1 );
}

void TIM2_IRQHandler( void )
{
    lp_timer_irq_handler( HAL_LP_TIMER_ID_2 );
}
