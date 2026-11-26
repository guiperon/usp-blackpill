# USP on STM32F411 Black Pill + LR2021 (+ FreeRTOS)

Port of Semtech USP v1.2.1 to the WeAct Black Pill (STM32F411CEU6). Read README_BLACKPILL.md first.

## Build (always from the repo root, one build dir per example)
- Board/radio: `-DBOARD=BLACKPILL_F411 -DRAC_RADIO=lr2021`, generator Ninja.
- Porting tests: `cmake -S examples/main_examples/porting_tests_example -B build_pt -G Ninja -DBOARD=BLACKPILL_F411 -DRAC_RADIO=lr2021 && ninja -C build_pt`
- FreeRTOS ping-pong: same with `ping_pong_freertos_example` / `build_rtos` (it forces `USE_FREERTOS=ON`; never mix with bare-metal examples in one build dir).
- Flash: `ninja -C <build> flash_<target>` (OpenOCD + ST-Link). Serial trace: USART2 PA2, 115200 8N1.

## Where things are
- MCU HAL port: `examples/smtc_hal_f4/` (TIM5 = modem timer, RTC = time base, flash sector 7 = NVM, software RNG).
- Pinout: `BLACKPILL_F411` block in `examples/modem_pinout.h` (LabSC board, Waveshare Core2021: IRQ on DIO11 -> PB9, SPI1 on PB3/PB4/PB5, NSS PA15).
- Radio BSP: `examples/radio_hal/ral_lr20xx_bsp.c`, `BLACKPILL_F411` blocks (all IRQs on DIO11, XTAL, no RF switch).
- RTOS glue: `examples/main_examples/ping_pong_freertos_example/rtos_hooks.c`.

## Rules
- Keep upstream USP files (smtc_rac_lib/, protocols/, radio drivers) unmodified; port-specific code goes in smtc_hal_f4/ or behind `BLACKPILL_F411` / `STM32F411xE` guards.
- Radio DIO EXTI and TIM5 must keep the same NVIC priority (HAL_IRQ_PRIO_MODEM = 6, >= configMAX_SYSCALL 5).
- Only the radio task calls `smtc_rac_run_engine()`; no SPI access from ISRs.
- Validate in order: porting_tests (all OK) -> ping_pong bare-metal -> ping_pong_freertos.
