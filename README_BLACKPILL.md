# USP v1.2.1 → STM32F411 "Black Pill" + LR2021 (+ FreeRTOS)

Porte do [Lora-net/usp](https://github.com/Lora-net/usp) (tag `v1.2.1`) para a WeAct Black Pill
(STM32F411CEU6) com um módulo LR2021, com um exemplo de ping-pong rodando sobre FreeRTOS.

> **Estado:** os 4 exemplos abaixo **compilam sem erros nem warnings** (arm-none-eabi-gcc 13.2,
> CMake 3.28, Ninja). **Nada foi testado em hardware ainda.** A validação começa pela Fase 1.

Todo o trabalho está no branch `blackpill-f411`, em cima da tag `v1.2.1`:

```bash
git log --oneline v1.2.1..blackpill-f411
git diff --stat v1.2.1 -- . ':!examples/mcu_drivers' ':!examples/rtos'   # só o código do porte
```

---

## 1. O que foi feito

### Arquivos novos

| Caminho | Conteúdo |
|---|---|
| `examples/smtc_hal_f4/` | HAL do MCU para o F411 (cópia adaptada de `smtc_hal_l4/`) + toolchain CMake |
| `examples/mcu_drivers/STM32F4xx_HAL_Driver/` | ST HAL F4 (só os módulos usados) |
| `examples/mcu_drivers/cmsis/Device/ST/STM32F4xx/` | headers CMSIS do F411 |
| `examples/mcu_drivers/core/STM32F4xx/` | startup, `system_stm32f4xx.c`, `stm32f4xx_hal_conf.h`, linker script |
| `examples/rtos/FreeRTOS-Kernel/` | FreeRTOS V11.1.0 (kernel + port ARM_CM4F + heap_4) |
| `examples/main_examples/ping_pong_freertos_example/` | ping-pong em FreeRTOS (reaproveita `app_ping_pong.c` sem mudanças) |

### Arquivos do USP modificados (mudanças pequenas)

| Arquivo | Mudança |
|---|---|
| `examples/toolchain.cmake` | nova placa `BOARD=BLACKPILL_F411` |
| `examples/common.cmake` | opção `USE_FREERTOS`; alvo `flash_*` via OpenOCD para a Black Pill |
| `examples/modem_pinout.h` | bloco de pinagem `BLACKPILL_F411` |
| `examples/smtc_modem_hal/smtc_modem_hal.c` | ramo `STM32F411xE` para os endereços de contexto em flash; os 3 ganchos RTOS ficam sob `#if !defined(SMTC_MODEM_HAL_RTOS_HOOKS)` |

### Diferenças L476 → F411 e como foram resolvidas

| Recurso | NUCLEO-L476 | Black Pill F411 | Solução no porte |
|---|---|---|---|
| Clock | HSI → PLL 80 MHz | HSE 25 MHz | PLL 96 MHz (PLLQ = 48 MHz, pronto para USB) |
| Base de tempo | RTC + LSE | RTC + LSE (mesmo IP RTCv2) | `smtc_hal_rtc.c` praticamente igual; se o LSE não partir, cai para LSI e avisa no trace |
| Timer do modem | LPTIM1 | **não tem LPTIM** | `smtc_hal_lp_timer.c` usa **TIM5 32 bits a 10 kHz** (100 µs/tick, one-pulse) |
| Baixo consumo | STOP2 | TIM5 para no STOP | só **Sleep (WFI)**; RTC wake-up continua funcionando |
| RNG | periférico RNG | **não tem RNG** | xoshiro128** semeado pelo UID de 96 bits + jitter (DWT/RTC/SysTick). **Não é criptográfico** |
| Flash (contextos) | páginas de 2 KB | setores de 16/64/128 KB | 2 KB "virtuais" no **setor 7** (0x0806_0000, reservado no linker) com shadow de 8 KB em RAM |
| `hal_mcu_wait_us` | laço de NOP calibrado a 80 MHz | — | contador de ciclos DWT |
| SPI do rádio | SPI1 + DMA | SPI1 | polling (o `lr20xx_hal.c` não usa DMA), 12 MHz |
| GPIO | AHB2 | AHB1 | base de endereço trocada |
| Prioridades NVIC | 0/1 | — | `HAL_IRQ_PRIO_MODEM = 6` para EXTI e TIM5 (mesma prioridade → não aninham; ≥ 5 por causa do FreeRTOS) |
| Watchdog | IWDG | IWDG | igual, mas **congelado durante o debug** (`__HAL_DBGMCU_FREEZE_IWDG`) |
| Botão | PC13 com pull-up externo | KEY em PA0, sem pull-up | pull-up interno |
| LED | ativo alto | PC13 ativo baixo | máscara `SMTC_LED_ACTIVE_LOW` em `smtc_hal_led.c` |

---

## 2. Pinagem

| Black Pill | Função | Módulo LR2021 |
|---|---|---|
| PA5 | SPI1_SCK | SCK |
| PA6 | SPI1_MISO | MISO |
| PA7 | SPI1_MOSI | MOSI |
| PA4 | GPIO (NSS) | NSS |
| PB0 | GPIO (reset) | NRESET |
| PB1 | GPIO (entrada, pull-up interno) | BUSY |
| PA1 | EXTI1, borda de subida | DIO de IRQ principal (**DIO8** na convenção Wio-LR2021/LoRa Plus) |
| 3V3 / GND | alimentação | 3V3 / GND |
| PA2 / PA3 | USART2 TX/RX (trace, 115200 8N1) | — (adaptador USB-serial) |
| PA0 | KEY (vira manager no ping-pong) | — |
| PC13 | LED azul onboard (LED TX) | — |
| PB12 | LED RX externo (opcional) | — |

PA11/PA12 ficam livres para USB CDC, e PA13/PA14 para o SWD. Para mudar a pinagem, edite o bloco
`BLACKPILL_F411` em `examples/modem_pinout.h`.

---

## 3. Compilar e gravar

Requisitos: `arm-none-eabi-gcc` (o USP recomenda ≥ 13.3, mas 13.2 compilou), CMake ≥ 3.28, Ninja, OpenOCD ≥ 0.12.

```bash
# Fase 1: testes de porte (bare-metal)
cmake -S examples/main_examples/porting_tests_example -B build_pt -G Ninja \
      -DBOARD=BLACKPILL_F411 -DRAC_RADIO=lr2021
ninja -C build_pt && ninja -C build_pt flash_porting_tests

# Fase 2: ping-pong bare-metal (sem o uplink LoRaWAN periódico)
cmake -S examples/main_examples/ping_pong_example -B build_pp -G Ninja \
      -DBOARD=BLACKPILL_F411 -DRAC_RADIO=lr2021 -DPERIODIC_UPLINK=OFF
ninja -C build_pp && ninja -C build_pp flash_ping_pong

# Fase 3: ping-pong em FreeRTOS
cmake -S examples/main_examples/ping_pong_freertos_example -B build_rtos -G Ninja \
      -DBOARD=BLACKPILL_F411 -DRAC_RADIO=lr2021
ninja -C build_rtos && ninja -C build_rtos flash_ping_pong_freertos
```

O alvo `flash_*` chama `openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "program ... verify reset exit"`.
O `.elf` serve direto para o Cortex-Debug (VS Code).

Uso de memória (o F411 tem 384 KB de flash para a aplicação e 128 KB de RAM):

| Exemplo | Flash | RAM |
|---|---|---|
| porting_tests | 38 KB | 9 KB |
| ping_pong (bare-metal) | 59 KB | 16 KB |
| ping_pong_freertos | 68 KB | 49 KB (32 KB são do heap do FreeRTOS) |
| periodical_uplink (LoRaWAN) | 116 KB | 27 KB |

---

## 4. Plano de validação

**Fase 1 — `porting_tests` em cada placa.** Saída esperada no terminal: todos OK em
`porting_test_spi`, `radio_irq`, `get_time`, `timer_irq`, `stop_timer`, `disable_enable_irq`,
`random`, `config_rx_radio`, `config_tx_radio`, `sleep_ms` e `timer_irq_low_power`.
Depois, ponha `ENABLE_TEST_FLASH 1` em `main_porting_tests.c` para testar o armazenamento de contexto.
O teste grava, reseta a placa e confere os dados depois do reset.

Diagnóstico rápido:
- `porting_test_spi` falha → fiação SPI/NSS/BUSY/NRESET, ou o BUSY sem pull-up.
- `radio_irq` falha → DIO errado em `RADIO_DIO_MAIN`, ou o BSP do rádio está roteando a IRQ para outro DIO (ver item 5.1).
- `config_rx/tx` falha → configuração de clock do rádio (XTAL × TCXO, ver item 5.1).
- `get_time`/`timer_irq` fora da tolerância → LSE não partiu (procure o aviso "LSE did not start").

**Fase 2 — `ping_pong` bare-metal entre as duas placas.** Aperte KEY em uma delas: ela vira
manager e manda PING, e a outra responde PONG.

**Fase 3 — `ping_pong_freertos`.** Mesmo comportamento. A cada 10 s a tarefa `status` imprime
o heap livre e a folga de pilha das tarefas. Use isso para dimensionar as pilhas.

---

## 5. Pendências e pontos de atenção (TODO)

### 5.1 BSP do rádio (`examples/radio_hal/ral_lr20xx_bsp.c`): confirmar para o "Core2021"
O BSP padrão foi escrito para o Wio-LR2021 na placa LoRa Plus da Semtech (não é o modo `LEGACY_EVK_LR20XX`). Ele assume:
- **DIO8** = IRQ principal e **DIO7** = IRQs de FIFO;
- **DIO5/DIO6** = controle de RF switch;
- regulador **DCDC**;
- **`RAL_XOSC_CFG_XTAL`**, ou seja, sem controle de TCXO pelo rádio.

Se o módulo usado tiver TCXO alimentado pelo pino VTCXO do LR2021, mude para
`RAL_XOSC_CFG_TCXO_RADIO_CTRL` com a tensão e o tempo de partida do datasheet do módulo.
Também confira qual DIO chega no PA1. Se for o DIO9, compile com `-DLEGACY_EVK_LR20XX=ON`
ou ajuste as funções `ral_lr20xx_bsp_get_dio_*`.

### 5.2 Apagamento de flash trava a CPU
Apagar um setor de 128 KB leva de 1 a 2 s. Durante esse tempo a execução a partir da flash fica
parada, inclusive as ISRs. No ping-pong isso não acontece, porque não há escrita de contexto.
No LoRaWAN (`periodical_uplink`), o LBM grava contexto no join e de tempos em tempos, e nessas
horas o radio planner pode perder um deadline. Há duas saídas:
- usar os setores 1–3 (16 KB cada, ~0,3 s), deixando só o vetor no setor 0;
- executar o apagamento a partir da RAM.

### 5.3 RNG sem hardware
O RNG atual serve para backoff, canal e DevNonce em laboratório. Para algo melhor, alimente
`hal_rng_add_entropy()` com amostras de RSSI do rádio.

### 5.4 Trace em UART
O trace sai pela USART2 (PA2), o que exige um adaptador USB-serial. Levar o trace para a USB CDC
nativa (como nas atividades do curso) é um passo posterior. Nesse caso, `trace_uart_tx()` deve
enfileirar os dados e não transmitir direto da ISR.

### 5.5 Baixo consumo
O porte usa apenas Sleep/WFI (no FreeRTOS, a idle hook chama `__WFI()`). Usar STOP exigiria
trocar o TIM5 pelo alarme do RTC (sub-segundos) como timer do modem.

### 5.6 Sem suporte nesta versão
FUOTA, Store & Forward e o exemplo `hw_modem`. Não ative `LBM_FUOTA` nem `LBM_STORE_AND_FORWARD`.

### 5.7 Atualizar o USP
A partir do USP v1.3.0, `ral_lr20xx_bsp_get_rssi_calibration_table()` passa a ser obrigatória.
Faça o rebase deste branch quando atualizar.

---

## 6. Arquitetura FreeRTOS (exemplo `ping_pong_freertos`)

O USP já tem ganchos de RTOS, e com eles a integração fica simples:

```
            ISR (prio 6)                                   radio_task (prio máx.)
 EXTI1 (DIO do rádio) ─┐                                   ┌─────────────────────────────┐
 TIM5 (timer modem)  ──┼─> rp_*_irq_callback()             │ for(;;) {                   │
                       │     └─> smtc_modem_hal_user_lbm_irq()  ──notify──>  run_engine()│
 EXTI0 (KEY) ──────────┴──> on_button_isr() ─────notify──────────> │   (callbacks pre/post)│
                                                           │   xTaskNotifyWait(≤1 s)     │
                                                           └─────────────────────────────┘
 smtc_rac_* API  ──>  protect/unprotect_api_call()  ==  mutex RECURSIVO (outras tarefas podem chamar a API)
 SysTick_Handler ──>  HAL_IncTick() + xPortSysTickHandler()
 trace            ──>  hal_trace_lock()/unlock() == mutex (várias tarefas imprimindo)
```

- Só a `radio_task` chama `smtc_rac_run_engine()`. Os callbacks da aplicação rodam dentro dela.
- O mutex precisa ser **recursivo**: o callback pós-transação chama `smtc_rac_submit_radio_transaction()` de dentro do `run_engine()`.
- **Não** chame `hal_mcu_disable_irq()` antes de `vTaskStartScheduler()`. Com PRIMASK setado, a instrução SVC gera HardFault.
- `hal_mcu_set_sleep_for_ms()` é só para bare-metal, porque suspende o SysTick.
