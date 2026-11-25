/*!
 * @file      rtos_hooks.c
 *
 * @brief     Glue between USP (RAC + radio planner) and FreeRTOS on the STM32F411 Black Pill.
 *
 * USP already has three RTOS hooks (examples/smtc_modem_hal/smtc_modem_hal.c, bare-metal no-ops):
 *
 *  - smtc_modem_hal_user_lbm_irq()       called by the radio planner on every radio DIO IRQ and
 *                                        modem-timer IRQ (from ISR context), and also from task
 *                                        context when a task is aborted. -> wake the radio task.
 *  - smtc_modem_hal_protect_api_call()   called at the entry/exit of every smtc_rac_* API.
 *  - smtc_modem_hal_unprotect_api_call() -> recursive mutex, so that other tasks may call the
 *                                        RAC API safely. Recursive because user callbacks run
 *                                        inside smtc_rac_run_engine() and usually call
 *                                        smtc_rac_submit_radio_transaction() again.
 *
 * The build defines SMTC_MODEM_HAL_RTOS_HOOKS (CMake option USE_FREERTOS) so that the bare-metal
 * versions are not compiled and the ones below are used.
 *
 * This file also provides SysTick_Handler (HAL tick + kernel tick), the trace lock and the
 * usual FreeRTOS hooks.
 */

#include <stdint.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "stm32f4xx_hal.h"
#include "smtc_modem_hal.h"
#include "smtc_hal_dbg_trace.h"
#include "smtc_hal_mcu.h"

#include "app_rtos.h"

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE VARIABLES -------------------------------------------------------
 */

static SemaphoreHandle_t rac_api_mutex = NULL;
static SemaphoreHandle_t trace_mutex   = NULL;

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE FUNCTIONS -------------------------------------------------------
 */

static inline bool in_isr( void )
{
    return xPortIsInsideInterrupt( ) == pdTRUE;
}

static inline bool scheduler_running( void )
{
    return xTaskGetSchedulerState( ) == taskSCHEDULER_RUNNING;
}

/*
 * -----------------------------------------------------------------------------
 * --- PUBLIC FUNCTIONS --------------------------------------------------------
 */

void rtos_hooks_init( void )
{
    rac_api_mutex = xSemaphoreCreateRecursiveMutex( );
    trace_mutex   = xSemaphoreCreateMutex( );
    configASSERT( rac_api_mutex != NULL );
    configASSERT( trace_mutex != NULL );
}

void radio_task_notify( uint32_t events )
{
    if( radio_task_handle == NULL )
    {
        return;  // not created yet (IRQ before the RTOS is up): nothing to wake
    }

    if( in_isr( ) )
    {
        BaseType_t higher_prio_woken = pdFALSE;
        xTaskNotifyFromISR( radio_task_handle, events, eSetBits, &higher_prio_woken );
        portYIELD_FROM_ISR( higher_prio_woken );
    }
    else
    {
        xTaskNotify( radio_task_handle, events, eSetBits );
    }
}

/* ------------------------------------------------------ USP modem HAL hooks */

void smtc_modem_hal_user_lbm_irq( void )
{
    radio_task_notify( RADIO_EVT_LBM_IRQ );
}

void smtc_modem_hal_protect_api_call( void )
{
    if( scheduler_running( ) && !in_isr( ) )
    {
        xSemaphoreTakeRecursive( rac_api_mutex, portMAX_DELAY );
    }
}

void smtc_modem_hal_unprotect_api_call( void )
{
    if( scheduler_running( ) && !in_isr( ) )
    {
        xSemaphoreGiveRecursive( rac_api_mutex );
    }
}

/* ------------------------------------------------------------- trace lock */

void hal_trace_lock( void )
{
    if( scheduler_running( ) && !in_isr( ) && ( trace_mutex != NULL ) )
    {
        xSemaphoreTake( trace_mutex, portMAX_DELAY );
    }
}

void hal_trace_unlock( void )
{
    if( scheduler_running( ) && !in_isr( ) && ( trace_mutex != NULL ) )
    {
        xSemaphoreGive( trace_mutex );
    }
}

/* --------------------------------------------------------------- SysTick */

extern void xPortSysTickHandler( void );

/*
 * One SysTick at 1 kHz for both worlds: HAL_GetTick()/HAL_Delay() and the FreeRTOS tick.
 * (Overrides the weak handler in smtc_hal_f4/smtc_hal_mcu.c.)
 */
void SysTick_Handler( void )
{
    HAL_IncTick( );
    if( xTaskGetSchedulerState( ) != taskSCHEDULER_NOT_STARTED )
    {
        xPortSysTickHandler( );
    }
}

/* --------------------------------------------------------- FreeRTOS hooks */

void vApplicationIdleHook( void )
{
    // Nothing to do: sleep until the next interrupt (SysTick, radio, timer, button...)
    __WFI( );
}

void vApplicationStackOverflowHook( TaskHandle_t task, char* task_name )
{
    ( void ) task;
    taskDISABLE_INTERRUPTS( );
    SMTC_HAL_TRACE_ERROR( "FreeRTOS: stack overflow in task '%s'\n", task_name );
    for( ;; )
    {
    }
}

void vApplicationMallocFailedHook( void )
{
    taskDISABLE_INTERRUPTS( );
    SMTC_HAL_TRACE_ERROR( "FreeRTOS: malloc failed (increase configTOTAL_HEAP_SIZE)\n" );
    for( ;; )
    {
    }
}

void vAssertCalled( const char* file, int line )
{
    taskDISABLE_INTERRUPTS( );
    SMTC_HAL_TRACE_ERROR( "FreeRTOS assert: %s:%d\n", file, line );
    for( ;; )
    {
    }
}
