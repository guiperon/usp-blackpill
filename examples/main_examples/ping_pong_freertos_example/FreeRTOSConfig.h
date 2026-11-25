/*
 * FreeRTOSConfig.h - USP ping-pong on STM32F411 "Black Pill" (Cortex-M4F, 96 MHz)
 *
 * Interrupt priority rules (4 priority bits on STM32F4, 0 = most urgent):
 *   - Kernel (SysTick, PendSV) runs at the lowest priority (15).
 *   - ISRs that call "...FromISR" APIs must be at priority >= 5 (configMAX_SYSCALL_INTERRUPT_PRIORITY).
 *     The radio DIO (EXTI) and the modem timer (TIM5) use HAL_IRQ_PRIO_MODEM = 6 (smtc_hal_mcu.h).
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>
extern uint32_t SystemCoreClock;

/* ---------------------------------------------------------------- scheduler */
#define configUSE_PREEMPTION 1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configUSE_TICKLESS_IDLE 0
#define configCPU_CLOCK_HZ ( SystemCoreClock )
#define configTICK_RATE_HZ ( ( TickType_t ) 1000 )
#define configMAX_PRIORITIES 5
#define configMINIMAL_STACK_SIZE ( ( uint16_t ) 128 )
#define configMAX_TASK_NAME_LEN 12
#define configTICK_TYPE_WIDTH_IN_BITS TICK_TYPE_WIDTH_32_BITS
#define configIDLE_SHOULD_YIELD 1
#define configUSE_TASK_NOTIFICATIONS 1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES 1
#define configUSE_MUTEXES 1
#define configUSE_RECURSIVE_MUTEXES 1
#define configUSE_COUNTING_SEMAPHORES 1
#define configQUEUE_REGISTRY_SIZE 8
#define configUSE_QUEUE_SETS 0
#define configUSE_TIME_SLICING 1
#define configUSE_NEWLIB_REENTRANT 0
#define configENABLE_BACKWARD_COMPATIBILITY 0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 0
#define configUSE_MINI_LIST_ITEM 1
#define configSTACK_DEPTH_TYPE uint16_t
#define configMESSAGE_BUFFER_LENGTH_TYPE size_t

/* ------------------------------------------------------------------ memory */
#define configSUPPORT_STATIC_ALLOCATION 0
#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configTOTAL_HEAP_SIZE ( ( size_t ) ( 32 * 1024 ) )
#define configAPPLICATION_ALLOCATED_HEAP 0

/* ------------------------------------------------------------------- hooks */
#define configUSE_IDLE_HOOK 1  /* WFI in idle */
#define configUSE_TICK_HOOK 0
#define configCHECK_FOR_STACK_OVERFLOW 2
#define configUSE_MALLOC_FAILED_HOOK 1
#define configUSE_DAEMON_TASK_STARTUP_HOOK 0

/* ------------------------------------------------------- stats / debugging */
#define configGENERATE_RUN_TIME_STATS 0
#define configUSE_TRACE_FACILITY 1
#define configUSE_STATS_FORMATTING_FUNCTIONS 0

/* ---------------------------------------------------------- software timers */
#define configUSE_TIMERS 1
#define configTIMER_TASK_PRIORITY ( configMAX_PRIORITIES - 2 )
#define configTIMER_QUEUE_LENGTH 8
#define configTIMER_TASK_STACK_DEPTH ( configMINIMAL_STACK_SIZE * 2 )

/* ---------------------------------------------------- interrupt priorities */
#ifdef __NVIC_PRIO_BITS
#define configPRIO_BITS __NVIC_PRIO_BITS
#else
#define configPRIO_BITS 4
#endif
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5
#define configKERNEL_INTERRUPT_PRIORITY ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << ( 8 - configPRIO_BITS ) )

/* ------------------------------------------------------------------ assert */
void vAssertCalled( const char* file, int line );
#define configASSERT( x )                         \
    if( ( x ) == 0 )                              \
    {                                             \
        vAssertCalled( __FILE__, __LINE__ );      \
    }

/* ---------------------------------------------------------- API inclusion */
#define INCLUDE_vTaskPrioritySet 1
#define INCLUDE_uxTaskPriorityGet 1
#define INCLUDE_vTaskDelete 1
#define INCLUDE_vTaskSuspend 1
#define INCLUDE_vTaskDelayUntil 1
#define INCLUDE_vTaskDelay 1
#define INCLUDE_xTaskGetSchedulerState 1
#define INCLUDE_xTaskGetCurrentTaskHandle 1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define INCLUDE_xTaskGetIdleTaskHandle 0
#define INCLUDE_eTaskGetState 1
#define INCLUDE_xTimerPendFunctionCall 0
#define INCLUDE_xTaskAbortDelay 0
#define INCLUDE_xTaskGetHandle 0
#define INCLUDE_xTaskResumeFromISR 1

/* --------------------------------------------- Cortex-M handler mapping
 * SVC and PendSV go straight to the port. SysTick is NOT mapped: rtos_hooks.c defines
 * SysTick_Handler, which feeds both the STM32 HAL tick (HAL_GetTick) and the kernel.
 */
#define vPortSVCHandler SVC_Handler
#define xPortPendSVHandler PendSV_Handler

#endif /* FREERTOS_CONFIG_H */
