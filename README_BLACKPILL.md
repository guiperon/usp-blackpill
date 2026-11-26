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
| `examples/radio_hal/ral_lr20xx_bsp.c` | blocos `BLACKPILL_F411`: IRQ do rádio no DIO11 (Core2021 na placa LabSC) |
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

## 2. Pinagem (placa LabSC "Blackpill/ESP32C6 + Radioenge AT interface + LR1121", rev 0)

O módulo de rádio é o **Waveshare Core2021** (LR2021), no footprint do Core1121. Os dois são
compatíveis pino a pino, **exceto o pino 3**: no Core1121 ele é o DIO9, no Core2021 é o **DIO11**.
Por isso os nomes de rede da placa continuam `LR1121_*`.

| Black Pill | Função | Rede na placa | Core2021 (pino) |
|---|---|---|---|
| PB3 | SPI1_SCK (AF5) | SCLK | SCK (13) |
| PB4 | SPI1_MISO (AF5) | MISO | MISO (11) |
| PB5 | SPI1_MOSI (AF5) | MOSI | MOSI (12) |
| PA15 | GPIO (NSS) | LR1121_SSEL | NSS (14) |
| PB6 | GPIO (reset) | LR1121_RESET | RESET (10) |
| PB8 | GPIO (entrada) | LR1121_BUSY | BUSY (9) |
| PB9 | EXTI9, borda de subida | LR1121_IRQ | **DIO11** (3) |
| PA8 / PB0 | GPIO, mantidos em nível alto | TEMP1_SSEL / TEMP2_SSEL | (termopares no mesmo SPI) |
| PA2 / PA3 | USART2 TX/RX (trace, 115200 8N1) | UART_AT_TX / UART_AT_RX | — (FTDI via diodos) |
| PB12 / PB13 / PB14 | LED TX / RX / SCAN (ativos em alto) | LED1 / LED2 / LED3 | — |
| PA0 | KEY da Black Pill (vira manager) | OUT1_3V3 | — |

Observações:
- PB3, PB4 e PA15 são pinos de JTAG depois do reset, e o firmware os reconfigura. O SWD (PA13/PA14) continua funcionando; o SWO (PB3) é perdido.
- O BSP (`ral_lr20xx_bsp.c`, bloco `BLACKPILL_F411`) põe **todas** as IRQs do rádio no DIO11. O DIO7 e o DIO8 não estão ligados na placa.
- O Core2021 usa cristal passivo de 32 MHz (`RAL_XOSC_CFG_XTAL`, o padrão do BSP) e não tem RF switch: as antenas Sub-GHz e 2,4 GHz têm conectores separados. **Use o conector LoRa_ANT (Sub-GHz).**

Para mudar a pinagem, edite o bloco `BLACKPILL_F411` em `examples/modem_pinout.h`.

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

**Fase 1 — `porting_tests` em cada placa.** O trace sai pelo FTDI da própria placa (U2). Saída esperada no terminal: todos OK em
`porting_test_spi`, `radio_irq`, `get_time`, `timer_irq`, `stop_timer`, `disable_enable_irq`,
`random`, `config_rx_radio`, `config_tx_radio`, `sleep_ms` e `timer_irq_low_power`.
Depois, ponha `ENABLE_TEST_FLASH 1` em `main_porting_tests.c` para testar o armazenamento de contexto.
O teste grava, reseta a placa e confere os dados depois do reset.

Diagnóstico rápido:
- `porting_test_spi` falha → fiação SPI/NSS/BUSY/NRESET, ou o BUSY sem pull-up.
- `radio_irq` falha → confira a ligação do pino 3 (DIO11) do Core2021 ao PB9 e o bloco `BLACKPILL_F411` em `ral_lr20xx_bsp.c`.
- `config_rx/tx` falha → configuração do rádio no BSP (regulador, clock) ou alimentação de 3V3 fraca na transmissão.
- `get_time`/`timer_irq` fora da tolerância → LSE não partiu (procure o aviso "LSE did not start").

**Fase 2 — `ping_pong` bare-metal entre as duas placas.** Aperte KEY em uma delas: ela vira
manager e manda PING, e a outra responde PONG.

**Fase 3 — `ping_pong_freertos`.** Mesmo comportamento. A cada 10 s a tarefa `status` imprime
o heap livre e a folga de pilha das tarefas. Use isso para dimensionar as pilhas.

---

## 5. Pendências e pontos de atenção (TODO)

### 5.1 Barramento SPI compartilhado
- **Termopares (J1/J2):** compartilham SCLK e MISO com o rádio. Os CS deles (PA8, PB0) ficam em nível alto desde o boot. Se os módulos de termopar forem MAX6675 (máx. ~4 MHz), a leitura vai exigir baixar a velocidade do SPI (hoje 12 MHz) e usar um mutex de barramento compartilhado com o rádio.
- **ESP32-C6 (M1):** está ligado às mesmas redes (SPI, reset, BUSY, IRQ, UART, saídas). **Não monte a Black Pill e o ESP32-C6 ao mesmo tempo**: seriam dois mestres no mesmo barramento.
- **PA0 (KEY) também é a rede OUT1_3V3.** Hoje é só entrada com pull-up, o que é inofensivo. Se um laboratório futuro usar o PA0 como saída, apertar o KEY vai curto-circuitar a saída com o GND.

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
