/*!
 * @file      main_ping_pong_freertos.c
 *
 * @brief     USP ping-pong between two STM32F411 "Black Pill" boards + LR2021, on FreeRTOS.
 *
 * Same application as examples/main_examples/ping_pong_example (app_ping_pong.c is reused as is);
 * only the bare-metal super-loop of main_ping_pong.c is replaced by FreeRTOS tasks:
 *
 *   radio_task  (highest priority)
 *       The only task that runs the USP engine. It sleeps on its task notification and is woken by
 *       smtc_modem_hal_user_lbm_irq() (radio DIO / modem timer IRQ, see rtos_hooks.c) or by the
 *       KEY button. All RAC callbacks (pre/post transaction) execute in this task.
 *
 *   status_task (low priority)
 *       Prints uptime, free heap and the stack high-water marks every STATUS_PERIOD_MS.
 *       Shows that the rest of the system keeps running and helps sizing the stacks.
 *
 * Usage: flash both boards, open both serial consoles (USART2 PA2, 115200 8N1), and press KEY (PA0)
 * on one of them: it becomes the manager and starts sending PING; the other answers PONG.
 */

#include <stdint.h>
#include <stdbool.h>

#include "FreeRTOS.h"
#include "task.h"

#include "app_rtos.h"
#include "app_ping_pong.h"

#include "smtc_hal_button.h"
#include "smtc_hal_led.h"
#include "smtc_hal_mcu.h"
#include "smtc_hal_watchdog.h"
#include "smtc_modem_hal.h"
#include "smtc_rac_api.h"

#define RAC_LOG_APP_PREFIX "MAIN-PP-RTOS"
#include "smtc_rac_log.h"

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE CONSTANTS -------------------------------------------------------
 */

#define RADIO_TASK_STACK_WORDS ( 1024 )  // 4 KB: RAC callbacks + traces (vsnprintf) run here
#define RADIO_TASK_PRIORITY ( configMAX_PRIORITIES - 1 )

#define STATUS_TASK_STACK_WORDS ( 384 )
#define STATUS_TASK_PRIORITY ( tskIDLE_PRIORITY + 1 )

#define RADIO_TASK_MAX_SLEEP_MS ( 1000 )  // wake up at least every second to kick the watchdog
#define STATUS_PERIOD_MS ( 10000 )

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE VARIABLES -------------------------------------------------------
 */

TaskHandle_t        radio_task_handle  = NULL;
static TaskHandle_t status_task_handle = NULL;

/*
 * -----------------------------------------------------------------------------
 * --- PRIVATE FUNCTIONS -------------------------------------------------------
 */

/*!
 * Called from the EXTI ISR of the KEY button (smtc_hal_button.c, already debounced)
 */
static void on_button_isr( void* context )
{
    ( void ) context;
    radio_task_notify( RADIO_EVT_BUTTON );
}

static void radio_task( void* arg )
{
    ( void ) arg;

    // USP init must run in the task that will own the engine
    smtc_rac_init( );
    ping_pong_init( );

    uint32_t events = 0;

    for( ;; )
    {
        hal_watchdog_reload( );

        // Progress RAC transactions (launches scheduled radio tasks, runs the user callbacks)
        smtc_rac_run_engine( );

        if( ( events & RADIO_EVT_BUTTON ) != 0 )
        {
            RAC_LOG_INFO( "button pressed\n" );
            ping_pong_on_button_press( );
        }

        // Block only if the engine has nothing pending. An IRQ arriving between the test and
        // xTaskNotifyWait() is not lost: the notification bits stay set until they are read.
        const TickType_t wait = smtc_rac_is_irq_flag_pending( ) ? 0 : pdMS_TO_TICKS( RADIO_TASK_MAX_SLEEP_MS );

        events = 0;
        ( void ) xTaskNotifyWait( 0, UINT32_MAX, &events, wait );
    }
}

static void status_task( void* arg )
{
    ( void ) arg;

    TickType_t last_wake = xTaskGetTickCount( );

    for( ;; )
    {
        vTaskDelayUntil( &last_wake, pdMS_TO_TICKS( STATUS_PERIOD_MS ) );

        SMTC_HAL_TRACE_INFO( "[status] uptime %lu s | heap free %u B (min %u B) | stack free: radio %u W, status %u W\n",
                             ( unsigned long ) smtc_modem_hal_get_time_in_s( ), ( unsigned ) xPortGetFreeHeapSize( ),
                             ( unsigned ) xPortGetMinimumEverFreeHeapSize( ),
                             ( unsigned ) uxTaskGetStackHighWaterMark( radio_task_handle ),
                             ( unsigned ) uxTaskGetStackHighWaterMark( status_task_handle ) );
    }
}

/*
 * -----------------------------------------------------------------------------
 * --- MAIN --------------------------------------------------------------------
 */

int main( void )
{
    // Clocks, GPIO, SPI, RTC, modem timer (TIM5), watchdog, UART traces
    // NOTE: do not call hal_mcu_disable_irq() here: starting the scheduler with PRIMASK set
    // faults. FreeRTOS masks the IRQs it cares about (BASEPRI) from the first xTaskCreate()
    // until the scheduler starts.
    hal_mcu_init( );
    hal_led_init( );
    hal_button_init( on_button_isr, NULL );

    SMTC_HAL_TRACE_MSG( "\n\nUSP ping-pong on FreeRTOS - STM32F411 Black Pill\n" );
    SMTC_HAL_TRACE_MSG( "Press KEY (PA0) on one board to make it the manager\n\n" );

    rtos_hooks_init( );

    BaseType_t ok = xTaskCreate( radio_task, "radio", RADIO_TASK_STACK_WORDS, NULL, RADIO_TASK_PRIORITY,
                                 &radio_task_handle );
    ok &= xTaskCreate( status_task, "status", STATUS_TASK_STACK_WORDS, NULL, STATUS_TASK_PRIORITY,
                       &status_task_handle );
    configASSERT( ok == pdPASS );

    vTaskStartScheduler( );

    // Only reached if there was not enough heap for the idle/timer tasks
    for( ;; )
    {
    }
}
