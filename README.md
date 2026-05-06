# 🍷 Vinheria Agnello — Sistema de Monitoramento Avançado

Sistema embarcado para monitoramento ambiental de uma vinheria, projetado para manter as condições ideais de armazenamento das garrafas: **luminosidade baixa**, **umidade controlada** e **temperatura amena**. Quando algum dos parâmetros sai da faixa ideal, o sistema sinaliza visual e sonoramente o nível de alerta.

> Projeto desenvolvido para a disciplina de **Edge Computing & Computer Systems** da **FIAP**.

---

## 🛠️ Ambiente de Desenvolvimento

Este projeto foi inteiramente desenvolvido utilizando a extensão **[PlatformIO IDE](https://platformio.org/install/ide?install=vscode)** do **Visual Studio Code**. Toda a estrutura de pastas (`src/`, `lib/`, `include/`, `test/`, `.pio/`) e o arquivo `platformio.ini` seguem o padrão dessa extensão.

A configuração do projeto inclusive recomenda automaticamente a extensão ao abrir a pasta no VS Code (veja `.vscode/extensions.json`).

### Por que PlatformIO em vez da Arduino IDE?

- Gerenciamento automático de dependências via `platformio.ini`
- Build mais rápido e com mensagens de erro melhores
- IntelliSense completo no C++ (auto-complete, navegação, refactoring)
- Mesmo projeto compila para UNO ou Nano sem alterações
- Integração nativa com Git e VS Code

---

## ✨ Funcionalidades

- 📊 **Monitoramento contínuo** de temperatura, umidade e luminosidade
- 🚦 **Sistema de alertas em três zonas** (Verde / Amarelo / Vermelho) com lógica *worst-of-3* (o pior dos três sensores define o estado global)
- 📈 **Média móvel de 10 segundos** para suavizar leituras ruidosas
- 💡 **Feedback visual** via LED RGB (verde / amarelo / vermelho)
- 🔊 **Feedback sonoro** via buzzer com frequências distintas para cada nível de alerta
- 🖥️ **Interface em LCD 20x4 I2C** com ícones customizados (garrafa, uva, termômetro, gota, sol, check, alerta)
- 🎬 **Animação de boot** com uvas convergindo e título letra-por-letra
- 🕐 **RTC DS1302** com fuso horário configurável e fallback via `millis()` se o módulo falhar
- 💾 **Logs persistentes em EEPROM** (ring buffer circular com até **124 registros**)
- ⚙️ **Menu de configuração** navegável com 6 itens
- 🌍 **Suporte a 3 idiomas**: Português, Inglês e Espanhol
- 🌡️ **Unidade de temperatura configurável**: Celsius ou Fahrenheit
- 🔇 **Buzzer silenciável** pelo menu
- 🎯 **Auto-calibração do LDR** baseada nas condições reais do ambiente

---

## 🧰 Componentes Utilizados

| Componente | Quantidade | Função |
|---|---|---|
| Arduino UNO R3 *ou* Nano (ATmega328P) | 1 | Microcontrolador |
| Sensor DHT11 | 1 | Temperatura e umidade |
| Sensor LDR | 1 | Luminosidade |
| LCD 20x4 com módulo I2C (PCF8574) | 1 | Display |
| RTC DS1302 | 1 | Relógio em tempo real |
| LED RGB (cátodo comum) | 1 | Indicador visual |
| Buzzer (ativo ou passivo) | 1 | Indicador sonoro |
| Botões (push button) | 4 | Navegação no menu |
| Resistor 10 kΩ | 1 | Pull-up do DHT11 |
| Resistores 220 Ω | 3 | Limitadores do LED RGB |
| Resistor 10 kΩ | 1 | Divisor de tensão do LDR |
| Protoboard + jumpers | — | Montagem do circuito |

---

## 📍 Pinagem

### Periféricos digitais e analógicos

| Pino | Componente | Observação |
|---|---|---|
| `D2` | DHT11 (data) | Precisa de pull-up de 10 kΩ |
| `D3` | Botão UP | `INPUT_PULLUP` |
| `D4` | Botão DOWN | `INPUT_PULLUP` |
| `D5` | Botão OK | `INPUT_PULLUP` |
| `D6` | Botão CANCEL | `INPUT_PULLUP` |
| `D7` | LED RGB — canal **azul** | Digital ON/OFF (sem PWM) |
| `D8` | Buzzer | Controlado por `tone()` |
| `D9` | LED RGB — canal **vermelho** | PWM |
| `D10` | LED RGB — canal **verde** | PWM |
| `A0` | LDR | Leitura analógica |

### RTC DS1302 (protocolo de 3 fios — **NÃO é I2C**)

| Pino | Função |
|---|---|
| `D11` | SCLK (clock) |
| `D12` | I/O (dados bidirecionais) |
| `D13` | CE (chip enable — também é o LED onboard) |

### Barramento I2C (LCD)

| Pino | Função |
|---|---|
| `A4` | SDA |
| `A5` | SCL |

> 💡 O LCD tipicamente fica no endereço **`0x27`**. Se o scan I2C de boot mostrar **`0x3F`**, altere a constante `LCD_ADDR` no `main.cpp`.

> ⚠️ O **DS1302 NÃO aparece no scan I2C** porque usa um protocolo serial próprio de 3 fios — isso é normal e esperado.

---

## 🚦 Limites de Alerta

| Sensor | 🟢 Verde (Ideal) | 🟡 Amarelo (Atenção) | 🔴 Vermelho (Alerta) |
|---|---|---|---|
| **Luminosidade** (0–255) | ≤ 10 | ≤ 50 | > 50 |
| **Umidade** (%) | ≤ 30 | ≤ 45 | > 45 |
| **Temperatura** (°C) | ≤ 20 | ≤ 25 | > 25 |

| Status | LED | Buzzer |
|---|---|---|
| 🟢 Verde | Verde | Mudo |
| 🟡 Amarelo | Amarelo (R+G) | 500 Hz |
| 🔴 Vermelho | Vermelho | 1000 Hz |
| ⚙️ Menu | Violeta (R+B) | Mudo |

A regra é **worst-of-3**: o sistema sempre exibe o status do pior dos três sensores. Por exemplo, se a temperatura está verde mas a umidade está vermelha, o status global é vermelho.

---

## 🎮 Controles e Navegação

| Botão | Tela Principal | Menu | Visualizar Logs |
|---|---|---|---|
| **OK** | Abre o menu | Confirma / altera o valor | — |
| **CANCEL** | — | Volta para a tela principal | Volta ao menu |
| **UP** | — | Item anterior (com wrap-around) | Log mais antigo |
| **DOWN** | — | Próximo item (com wrap-around) | Log mais recente |

### Itens do menu

1. **UTC offset** — fuso horário (-12 a +14, padrão `-3` para Brasil)
2. **Unidade de temperatura** — Celsius / Fahrenheit
3. **Idioma** — Português / English / Español
4. **Buzzer** — ON / OFF
5. **Ver logs** — abre a tela de visualização da EEPROM
6. **Sair** — retorna à tela principal

Todas as alterações são **persistidas imediatamente na EEPROM**.

---

## 📁 Estrutura do Projeto

```
Vinheria_Agnello/
├── .pio/                    # Build outputs e libs baixadas (auto-gerado)
│   └── libdeps/
│       └── nanoatmega328/   # Bibliotecas instaladas pelo PlatformIO
├── .vscode/                 # Config do VS Code (PlatformIO recomendado)
├── include/                 # Headers próprios (vazio neste projeto)
├── lib/                     # Bibliotecas locais (vazio neste projeto)
├── src/
│   └── main.cpp             # Código principal
├── test/                    # Testes unitários (não utilizado)
├── platformio.ini           # Configuração do PlatformIO
└── README.md                # Este arquivo
```

---

## ⚙️ Configuração do `platformio.ini`

```ini
[env:nanoatmega328]
platform = atmelavr
board = nanoatmega328
framework = arduino
monitor_speed = 9600
lib_deps = 
    marcoschwartz/LiquidCrystal_I2C@^1.1.4
    adafruit/DHT sensor library@^1.4.7
    makuna/RTC@^2.5.0
```

> 🔁 Para usar **Arduino UNO** em vez do Nano, basta trocar `board = nanoatmega328` por `board = uno`. O código é idêntico para ambos (mesmo ATmega328P).

---

## 🚀 Como Rodar o Projeto

### Pré-requisitos

1. Instalar o **[Visual Studio Code](https://code.visualstudio.com/)**
2. Instalar a extensão **[PlatformIO IDE](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide)** dentro do VS Code

### Passos

1. **Clonar / abrir** o projeto:
   ```bash
   git clone <url-do-repo>
   cd Vinheria_Agnello
   code .
   ```

2. Aguardar o PlatformIO **baixar automaticamente** as dependências listadas em `platformio.ini` (`LiquidCrystal_I2C`, `DHT sensor library`, `RTC`).

3. **Conectar** o Arduino via USB.

4. Na barra inferior do VS Code, clicar em:
   - `✓` (**Build**) — compila o código
   - `→` (**Upload**) — envia para a placa
   - 🔌 (**Serial Monitor**) — abre o terminal serial em `9600 baud`

### Ou pela linha de comando

```bash
pio run                  # Build
pio run --target upload  # Upload
pio device monitor       # Serial monitor
```

---

## 🔄 Fluxo de Execução

1. **Boot** — pisca o LED em vermelho/verde/amarelo, bipa o buzzer 2x, faz scan I2C, inicializa LCD, EEPROM, DHT11 e RTC. Falhas viram avisos não-fatais.
2. **Animação de abertura** — uvas convergindo, título letra-por-letra, garrafa "respirando".
3. **Modo Menu** *(estado inicial)* — usuário precisa apertar OK ou CANCEL para começar a monitorar.
4. **Modo Normal** — loop principal:
   - A cada **1s**: amostra os 3 sensores
   - A cada **10s**: consolida média e atualiza o alerta
   - A cada **60s** *(configurável)*: grava log na EEPROM
   - A cada **300ms**: redesenha a tela (~3 FPS)

---

## 💾 Layout da EEPROM (1024 bytes do ATmega328P)

| Endereço | Tamanho | Conteúdo |
|---|---|---|
| `0..15` | 16 B | Struct `Settings` (configurações) |
| `16..17` | 2 B | Contador de logs válidos |
| `18..19` | 2 B | Head do ring buffer |
| `20..31` | 12 B | Reserva / padding |
| `32..1023` | 992 B | **124 logs** de 8 bytes (ring buffer circular) |

Cada `LogEntry` tem 8 bytes: timestamp (4 B), temperatura (1 B), umidade (1 B), LDR (1 B), status (1 B).

> Quando o ring buffer enche, os logs mais antigos são sobrescritos automaticamente (FIFO).

---

## 🐛 Debug e Troubleshooting

- O **Serial Monitor** (9600 baud) imprime todos os passos de inicialização e os valores consolidados a cada janela de 10s.
- Se o **LCD não acender**: rode o scan I2C que aparece no boot e ajuste `LCD_ADDR` se necessário (`0x27` ou `0x3F`).
- Se o **RTC voltar com hora errada** após desligar: verifique a bateria CR2032 do módulo DS1302.
- Se o **DHT11 retornar `NaN`** com frequência: confira o resistor de pull-up de 10 kΩ entre `VCC` e o pino de dados.
- Para **forçar um reset da configuração** (ex.: após mudar a struct `Settings`): incremente o valor de `EEP_MAGIC` no código — no próximo boot, a config será recriada com os defaults.

---

## 📚 Bibliotecas Utilizadas

- **[LiquidCrystal_I2C](https://github.com/marcoschwartz/LiquidCrystal_I2C)** — Driver do LCD via PCF8574
- **[DHT sensor library](https://github.com/adafruit/DHT-sensor-library)** — Driver do DHT11/DHT22 (Adafruit)
- **[Rtc by Makuna](https://github.com/Makuna/Rtc)** — Driver do DS1302 (e outros RTCs)
- `Wire.h`, `EEPROM.h`, `Arduino.h` — Bibliotecas core do framework Arduino

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

