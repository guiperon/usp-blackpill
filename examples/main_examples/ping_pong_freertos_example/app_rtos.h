/*!
 * @file      app_rtos.h
 *
 * @brief     Shared FreeRTOS objects of the USP ping-pong example (STM32F411 Black Pill)
 */

#ifndef APP_RTOS_H
#define APP_RTOS_H

#include "FreeRTOS.h"
#include "task.h"

/*!
 * Events delivered to the radio task through its task-notification value (bit field)
 */
#define RADIO_EVT_LBM_IRQ ( 1UL << 0 )  // radio DIO or modem timer IRQ: run the RAC engine
#define RADIO_EVT_BUTTON ( 1UL << 1 )   // KEY pressed: become manager (send PING)

/*!
 * The only task allowed to run the USP engine (smtc_rac_run_engine)
 */
extern TaskHandle_t radio_task_handle;

/*!
 * Create the mutexes used by the modem HAL hooks and the trace (call before vTaskStartScheduler)
 */
void rtos_hooks_init( void );

/*!
 * Send events to the radio task, from a task or from an ISR (detected automatically)
 */
void radio_task_notify( uint32_t events );

#endif  // APP_RTOS_H
