# 🍷 Vinheria Agnello — Sistema de Monitoramento Avançado

> Sistema embarcado para monitoramento contínuo das condições ambientais de uma adega: temperatura, umidade e luminosidade, com alertas em tempo real, log persistente e interface multilíngue.

Projeto desenvolvido para o curso de **Edge Computing & Computer Systems** da **FIAP**, evoluído ao longo de várias entregas — partindo de um circuito básico (LDR + LEDs) até o sistema completo descrito aqui.

---

## 📑 Sumário

- [Funcionalidades](#-funcionalidades)
- [Hardware](#-hardware)
- [Pinagem](#-pinagem)
- [Bibliotecas](#-bibliotecas-necessárias)
- [Como compilar](#%EF%B8%8F-como-compilar-e-gravar)
- [Como usar](#-como-usar)
- [Faixas de alerta](#-faixas-de-alerta)
- [Detalhes técnicos](#-detalhes-técnicos)
- [Estrutura do código](#-estrutura-do-código)
- [Troubleshooting](#-troubleshooting)
- [Autor](#-autor)

---

## ✨ Funcionalidades

- **Monitoramento contínuo** de 3 grandezas físicas:
  - 🌡️ Temperatura (DHT11)
  - 💧 Umidade relativa (DHT11)
  - ☀️ Luminosidade (LDR com auto-calibração)
- **Média móvel de 10 segundos** — suaviza ruído dos sensores e dispara alertas só em mudanças sustentadas.
- **Sistema de alerta em 3 zonas** (verde / amarelo / vermelho) com feedback:
  - LED RGB muda de cor conforme severidade
  - Buzzer toca em frequências distintas (500 Hz / 1000 Hz)
- **LCD 20x4 I²C** com:
  - Tela principal com barras de progresso, ícones customizados e relógio em tempo real
  - Animação de boot com personagens da CGRAM (uvas, garrafa, etc.)
  - Menu de configuração e visualizador de logs
- **Menu multilíngue** (Português / Inglês / Espanhol) navegável por joystick.
- **Configuração persistente** na EEPROM: idioma, fuso horário, unidade de temperatura (°C/°F), buzzer on/off.
- **Log circular** na EEPROM (ring buffer de até 124 entradas) com timestamp do RTC.
- **RTC DS1302** com bateria de backup mantém data/hora mesmo sem energia.
- **Telemetria via Serial** (9600 baud) para debug e captura em PC.

---

## 🧰 Hardware

| Componente | Modelo | Quantidade |
|---|---|---|
| Microcontrolador | Arduino UNO R3 / Nano (ATmega328P) | 1 |
| Display LCD | 20x4 com backpack I²C (PCF8574) | 1 |
| Sensor temp/umidade | DHT11 | 1 |
| Sensor de luz | LDR + resistor 10 kΩ (divisor) | 1 |
| RTC | Módulo DS1302 + bateria CR2032 | 1 |
| LED RGB | Cátodo comum (com resistores 220 Ω) | 1 |
| Buzzer | Ativo ou passivo (controlado por `tone()`) | 1 |
| Joystick analógico | KY-023 ou equivalente | 1 |
| Botões | Push-button momentâneo | 2 (OK, Cancel) |
| Resistor pull-up | 10 kΩ para o DHT11 | 1 |
| Protoboard + jumpers | — | — |

---

## 🔌 Pinagem

### Digital

| Pino | Função | Observação |
|---|---|---|
| D2 | DHT11 (data) | Pull-up 10 kΩ para +5 V |
| D3, D4 | **Livres** | (eram os antigos botões Up/Down) |
| D5 | Botão OK | `INPUT_PULLUP` — conecta a GND quando pressionado |
| D6 | Botão Cancel | `INPUT_PULLUP` |
| D7 | LED RGB — canal **B** | Digital (sem PWM por causa do `tone()`) |
| D8 | Buzzer | Controlado por `tone()` (ocupa Timer2) |
| D9 | LED RGB — canal **R** | PWM (Timer1) |
| D10 | LED RGB — canal **G** | PWM (Timer1) |
| D11 | RTC DS1302 — SCLK | |
| D12 | RTC DS1302 — I/O | Linha bidirecional |
| D13 | RTC DS1302 — CE | Compartilhado com LED onboard |

### Analógico

| Pino | Função |
|---|---|
| A0 | LDR (divisor com resistor 10 kΩ) |
| A1 | Joystick — eixo **Y** (VRy) |

> **Nota sobre o joystick:** apenas o eixo Y é usado, para navegação Up/Down. VRx e SW ficam livres — podem ser conectados depois para funcionalidades extras (ex: ajuste fino de valores no menu).

> **Nota sobre o DS1302:** ele **não é I²C**. Usa protocolo serial proprietário de 3 fios, por isso não aparece em scans I²C.

---

## 📚 Bibliotecas necessárias

Instale pela IDE Arduino (Gerenciador de Bibliotecas) ou via PlatformIO:

| Biblioteca | Autor | Uso |
|---|---|---|
| `LiquidCrystal_I2C` | Frank de Brabander | Driver do LCD I²C |
| `DHT sensor library` | Adafruit | Leitura do DHT11 |
| `Adafruit Unified Sensor` | Adafruit | Dependência do DHT |
| `Rtc by Makuna` | Michael C. Miller | Driver do DS1302 |

`Wire.h` e `EEPROM.h` já vêm com o core do Arduino.

### `platformio.ini` de referência

```ini
[env:nanoatmega328]
platform = atmelavr
board = nanoatmega328
framework = arduino
monitor_speed = 9600
lib_deps =
    marcoschwartz/LiquidCrystal_I2C
    adafruit/DHT sensor library
    adafruit/Adafruit Unified Sensor
    Makuna/Rtc
```

---

## ⚙️ Como compilar e gravar

### Arduino IDE

1. Instale as bibliotecas listadas acima.
2. Selecione **Tools → Board → Arduino Nano** (ou UNO).
3. **Tools → Processor → ATmega328P** (ou *Old Bootloader* se o Nano for clone antigo).
4. Selecione a porta correta em **Tools → Port**.
5. **Sketch → Upload** (Ctrl+U).

### PlatformIO

```bash
pio run -t upload
pio device monitor
```

> Após o **primeiro upload**, o RTC é sincronizado com `__DATE__/__TIME__` do build. Se a hora estiver errada, recompile e regrave.

---

## 🕹️ Como usar

### Boot

Ao ligar, o sistema:
1. Imprime no Serial cada etapa de inicialização
2. Inicializa LCD, EEPROM, joystick, DHT11 e RTC
3. Toca a animação de boot (uvas, título, garrafa)
4. Entra direto no **menu de configuração**

### Controles

| Controle | Ação |
|---|---|
| 🕹️ Joystick ↑ | Item anterior (menu) / log anterior |
| 🕹️ Joystick ↓ | Próximo item (menu) / próximo log |
| 🔘 OK | Confirma / entra / altera valor |
| 🔘 Cancel | Volta ao modo anterior |

Segurar o joystick = auto-repeat (~5 itens/segundo).

### Fluxo entre telas

```
   ┌─────────────────┐
   │   MENU (boot)   │ ◄────── Cancel ──────┐
   └────────┬────────┘                       │
            │ Cancel                         │
            ▼                                │
   ┌─────────────────┐         OK em        │
   │  NORMAL (home)  │       "Ver logs"     │
   └────────┬────────┘                       │
            │ Cancel                         │
            └──────► volta ao MENU           │
                          │                  │
                          ▼                  │
                  ┌─────────────────┐        │
                  │      LOGS       │ ───────┘
                  └─────────────────┘
```

### Tela principal (modo NORMAL)

```
┌────────────────────┐
│13/05/2026 14:32:01 │  ← Data e hora local
│🌡 ████████ OK     ✓│  ← Temperatura
│💧 ██████   ALERTA ⚠│  ← Umidade
│☀ ██       OK     ✓│  ← Luminosidade
└────────────────────┘
```

### Menu de configuração

| Item | Valores | Ação no OK |
|---|---|---|
| UTC offset | -12 a +14 | Incrementa em 1 (wrap) |
| Unidade temp | C / F | Alterna |
| Idioma | PT / EN / ES | Cicla |
| Buzzer | ON / OFF | Alterna |
| Ver logs | — | Abre tela de logs |
| Sair | — | Volta ao modo NORMAL |

Qualquer alteração é gravada **imediatamente** na EEPROM.

---

## 🚨 Faixas de alerta

Conforme spec do projeto. As faixas de temperatura são sempre avaliadas em **Celsius** (a conversão para Fahrenheit só afeta a exibição).

| Sensor | 🟢 Verde (ideal) | 🟡 Amarelo (atenção) | 🔴 Vermelho (crítico) |
|---|---|---|---|
| **Temperatura** | 20 – 25 °C | 26 – 30 °C | > 30 °C |
| **Umidade** | 30 – 45 % | 46 – 60 % | > 60 % |
| **Luminosidade** | 0 – 50 | 51 – 200 | > 200 |

> O **status global** é o pior caso entre os 3 sensores (`worst-of-3`). Se temperatura está OK mas umidade está crítica, o LED fica vermelho e o buzzer toca.

### Feedback por zona

| Zona | LED RGB | Buzzer |
|---|---|---|
| 🟢 Verde | Verde | Silencioso |
| 🟡 Amarelo | Âmbar (R=255, G=80) | 500 Hz |
| 🔴 Vermelho | Vermelho puro | 1000 Hz |
| Menu | Violeta (R+B) | Silencioso |

---

## 🔬 Detalhes técnicos

### Janela de média móvel (10 s)

A cada 1 s os sensores são amostrados e os valores acumulados. A cada 10 s, calcula-se a média e dispara o alerta. Isso:
- **Filtra ruído** dos sensores (especialmente do LDR, que oscila bastante)
- **Evita falsos alertas** por leituras pontuais (uma porta abrindo por 2 s não dispara o buzzer)
- **Descarta NaN** do DHT11 (que falha em ~5 % das leituras)

### Ring buffer de logs na EEPROM

Layout da EEPROM (1024 bytes do ATmega328P):

```
[0..15]    → Settings (struct cfg, 16 bytes)
[16..17]   → Contador de logs (uint16)
[18..19]   → Head do ring buffer (uint16)
[20..31]   → Reserva/padding
[32..1023] → Logs (124 entradas × 8 bytes)
```

Cada `LogEntry` (8 bytes):

```c
struct LogEntry {
  uint32_t ts;       // Timestamp (epoch 2000 do RtcByMakuna)
  int8_t   tempC;    // Temperatura em Celsius
  uint8_t  humid;    // Umidade %
  uint8_t  ldr;      // Luminosidade 0-255
  uint8_t  status;   // Zona consolidada
};
```

Quando o buffer enche, o log mais antigo é sobrescrito (FIFO).

### Truque dos 8 slots de CGRAM

O HD44780 só permite **8 caracteres customizados**, mas o projeto usa **9**:
- 3 da animação de boot (garrafa cheia, garrafa frame 2, uva)
- 3 ícones de linha (termômetro, gota, sol)
- 3 emojis de status (OK, alerta, crítico)

**Solução:** o slot 1 segura `BOTTLE2` durante a animação de boot e depois é **recarregado** com `chCritico` (já que `BOTTLE2` não é mais usado em runtime).

### Auto-calibração do LDR

Em vez de mapear `0..1023 → 0..255` direto, o código aprende dinamicamente o **range real** do ambiente:

- Toda leitura atualiza `ldrRawMin` e `ldrRawMax`
- Quando o range observado ≥ 50, o mapeamento usa esses valores como referência
- A calibração é persistida na EEPROM

Isso dá muito mais **resolução prática** — uma adega que varia entre 100 e 400 no raw vai usar toda a faixa 0-255, em vez de só 25-100.

### RTC sempre em UTC

O DS1302 armazena timestamps em **UTC**. O offset de fuso (configurável no menu) é aplicado **apenas na exibição** via `nowLocal()`. Isso significa:
- Mudar o fuso no menu não desloca os logs antigos
- Logs continuam comparáveis mesmo após mudança de fuso

### Mapa de Timers

Conflito de timers do ATmega328P:
- **Timer0** → `millis()` / `delay()` / `micros()`
- **Timer1** → PWM nos pinos 9 e 10 (LED R e G)
- **Timer2** → `tone()` no buzzer (pino 8)

Como o `tone()` ocupa Timer2 (que também controla PWM dos pinos 3 e 11), o canal **B** do LED RGB fica em D7 como digital ON/OFF puro, sem fade. Por isso o "violeta" do menu é só R+B simples.

---

## 📁 Estrutura do código

```
vinheria_agnello.ino
├── Bibliotecas
├── Pinagem (defines)
├── Limites de zona (consts)
├── Layout EEPROM
├── Tipos (Settings, LogEntry)
├── Objetos (lcd, dht, rtc)
├── Estado global
├── Caracteres CGRAM
│
├── EEPROM
│   ├── cfgSave / cfgLoad
│   └── logWrite / logRead / logClearAll
│
├── Sensores
│   ├── readLDR              (com auto-calibração)
│   ├── sensorSample         (amostra a cada 1 s)
│   ├── evalZone             (classifica em verde/amarelo/vermelho)
│   └── windowFinalize       (consolida janela de 10 s)
│
├── LED + Buzzer
│   ├── setRGB / setBlue
│   └── alertApply
│
├── Helpers
│   ├── zoneText             (multilíngue)
│   ├── tempForDisplay       (conversão C/F)
│   └── nowUnix / nowLocal   (timestamp UTC vs local)
│
├── Boot animation
├── Telas
│   ├── renderHome
│   ├── renderMenu
│   └── renderLogs
│
├── Botões + Joystick
│   ├── joyDir               (-1, 0, +1)
│   └── handleButtons        (debounce + dispatch)
│
└── setup() / loop()
```

---

## 🔧 Troubleshooting

| Sintoma | Causa provável | Solução |
|---|---|---|
| LCD não acende / só mostra blocos | Endereço I²C errado | Mude `LCD_ADDR` de `0x27` para `0x3F` |
| LCD acende mas texto truncado | USB com pouca corrente | Use fonte externa 5 V/1 A |
| Hora resetada toda vez que liga | Bateria CR2032 do RTC fraca | Troque a bateria |
| Hora errada após upload | `__DATE__` reflete hora local | Confira o `cfg.utcOffset` (deve ser `-3` no Brasil) |
| Menu rola sozinho | Drift do joystick | Aumente `JOY_DEADZONE` para 250 ou 300 |
| Joystick Up/Down invertido | Orientação física do módulo | Inverta os sinais em `joyDir()` ou gire o módulo 180° |
| DHT11 retorna NaN frequente | Pull-up faltando ou cabo longo demais | Adicione resistor 10 kΩ entre DATA e +5 V |
| Buzzer "afina" no menu | Conflito de timer com PWM | É esperado — `tone()` e PWM compartilham Timer2 |
| LED amarelo parece verde | Canal G do RGB muito brilhante | Ajuste o valor `80` em `setRGB(255, 80)` no `alertApply()` |
| Logs sumiram após reflash | `EEP_MAGIC` foi bumpado | Comportamento intencional — força reset da config |

---

## 👤 Autores

Projeto desenvolvido para a disciplina de **Edge Computing & Computer Systems** — FIAP.

|       nome:               |       RM:      |
|---------------------------|----------------|
|GIANLUCA ANTONICCI         |     570081     |
|MATHEUS MARCONDES ARAÚJO   |     573152     |
|ENZO VIEIRA PROVENZANO     |     569696     |
|JOÃO VITOR RODRIGUES COSTA |     569510     |
---

## 📄 Licença

Uso acadêmico. Sinta-se à vontade para estudar, modificar e adaptar.