/* =====================================================================
 *  VINHERIA AGNELLO - Sistema de Monitoramento Avancado
 *  ---------------------------------------------------------------------
 *  Placa: Arduino UNO R3 ou Nano (ambos ATmega328P - codigo identico)
 *
 *  Ao ligar, o programa:
 *    1) Imprime no Serial @9600 baud todos os passos
 *    2) Pisca o LED em vermelho, verde e amarelo (1s cada)
 *    3) Bipa o buzzer 2x (500Hz e 1000Hz)
 *    4) Faz scan I2C e lista os enderecos encontrados
 *    5) Inicializa LCD e RTC; falhas viram avisos nao-fatais
 *
 *  Navegacao:
 *    - Joystick (eixo Y) -> rola menus e logs (Up/Down)
 *    - Botao OK          -> confirma/entra
 *    - Botao CANCEL      -> volta/sai
 *
 * ===================================================================== */

// ------------------------- BIBLIOTECAS --------------------------------
#include <Arduino.h>             // API basica do Arduino (pinMode, digitalWrite, etc.)
#include <Wire.h>                // Comunicacao I2C (usada pelo LCD)
#include <LiquidCrystal_I2C.h>   // Driver do LCD I2C (PCF8574 + HD44780)
#include <DHT.h>                 // Driver do sensor de temp/umid DHT11
#include <ThreeWire.h>           // Protocolo serial de 3 fios (dependencia do RtcDS1302)
#include <RtcDS1302.h>           // Driver do RTC DS1302 (lib "Rtc by Makuna")
#include <EEPROM.h>              // Memoria nao-volatil interna do ATmega328P (1KB)

// =================== PINAGEM =========================================
// Mapeamento dos perifericos nos pinos digitais/analogicos do Arduino.
// Mudou a fiacao? Atualize aqui em vez de procurar pelo codigo todo.
#define PIN_DHT       2          // Sensor DHT11 - data pin (precisa de pull-up 10K)
                                 // Pinos 3 e 4 ficaram livres apos a troca dos botoes
                                 // Up/Down pelo joystick - reuse-os a vontade.
#define PIN_BTN_OK    5          // Botao OK - confirma/entra no menu
#define PIN_BTN_CAN   6          // Botao CANCEL - sai/volta
#define PIN_BUZZER    8          // Buzzer ativo/passivo (controlado por tone())
#define PIN_LED_R     9          // LED RGB - canal vermelho (PWM no Timer1)
#define PIN_LED_G     10         // LED RGB - canal verde (PWM no Timer1)
#define PIN_LED_B     7          // digital (sem PWM por causa do tone() no Timer2)
                                 // -> tone() ocupa o Timer2, que controla os PWMs
                                 //    dos pinos 3 e 11. Por isso o azul fica em D7
                                 //    como ON/OFF puro, sem fade.
#define PIN_LDR       A0         // Sensor de luz LDR (pino analogico)

// =================== JOYSTICK (KY-023 ou similar) ====================
// So usamos o eixo Y para navegacao Up/Down. VRx e SW podem ser
// conectados depois se quiser dar mais funcoes (ex: VRx para ajustar
// valores no menu sem precisar de "wrap-around" no OK).
//
// Em repouso o joystick fica em ~512 (analogRead 0-1023). Deslocado:
//   Y < 312  -> uma direcao (UP por convencao neste codigo)
//   Y > 712  -> outra direcao (DOWN)
// Se a direcao ficar invertida na sua montagem, basta inverter os
// sinais em joyDir() abaixo - depende de como o modulo esta orientado.
#define PIN_JOY_Y     A1

// =================== DS1302 (3-wire serial, NAO e I2C) ===============
// O DS1302 NAO usa I2C - ele tem seu proprio protocolo de 3 fios.
// Por isso nao aparece no scan I2C - nao espere ve-lo em 0x68.
#define PIN_RTC_CLK   11         // SCLK - clock serial
#define PIN_RTC_DAT   12         // I/O - linha bidirecional de dados
#define PIN_RTC_RST   13         // CE (chip enable) - tambem e o LED onboard

// =================== ENDERECO LCD ====================================
// LCDs com backpack PCF8574 vem em dois enderecos comuns:
//   0x27 - mais comum (modulos chineses genericos)
//   0x3F - alguns modulos da Adafruit/Sparkfun
// Se o scan I2C mostrar 0x3F, mude esta linha para 0x3F
#define LCD_ADDR 0x27

// =================== LIMITES (conforme spec atualizada) ==============
// Cada sensor tem uma FAIXA verde ideal [GREEN_MIN..GREEN_MAX] e um teto
// amarelo de tolerancia (YELLOW_MAX). Acima do teto amarelo = vermelho.
// Valores abaixo do GREEN_MIN tambem caem em amarelo (deviacao da faixa
// ideal, mas nao critico ainda) - lidado dentro de evalZone().
//
// Spec:
//   Luz:   verde 0-50      | amarelo 51-200    | vermelho > 200
//   Umid:  verde 30-45 %   | amarelo 46-60 %   | vermelho > 60 %
//   Temp:  verde 20-25 C   | amarelo 26-30 C   | vermelho > 30 C
//
// Os limites de temperatura sao SEMPRE em Celsius. O DHT11 mede em C
// e curT armazena em C; a conversao para Fahrenheit acontece apenas
// em tempForDisplay() na hora de mostrar na tela. Isso garante que os
// alertas disparem na mesma temperatura fisica independente da unidade
// escolhida pelo usuario (25 C = 77 F, 30 C = 86 F).
const uint8_t  LDR_GREEN_MIN   = 0;
const uint8_t  LDR_GREEN_MAX   = 50;
const uint16_t LDR_YELLOW_MAX  = 200;

const uint8_t  UMID_GREEN_MIN  = 30;
const uint8_t  UMID_GREEN_MAX  = 45;
const uint8_t  UMID_YELLOW_MAX = 60;

const uint8_t  TEMP_GREEN_MIN  = 20;   // 20 C = 68 F
const uint8_t  TEMP_GREEN_MAX  = 25;   // 25 C = 77 F
const uint8_t  TEMP_YELLOW_MAX = 30;   // 30 C = 86 F

const uint16_t BUZZ_AMARELO  = 500;  // Frequencia em Hz para alerta amarelo
const uint16_t BUZZ_VERMELHO = 1000; // Frequencia mais aguda = mais urgente

// =================== JOYSTICK THRESHOLDS =============================
// Deadzone generosa de proposito: joysticks baratos tem drift de centro
// e ruido, e nao queremos que o menu role sozinho com o joystick parado.
// Se sentir drift, suba a deadzone para 250 ou 300.
const uint16_t JOY_CENTER     = 512;
const uint16_t JOY_DEADZONE   = 200;   // [312..712] = neutro

// =================== EEPROM LAYOUT ===================================
// Mapa de memoria da EEPROM (1024 bytes no ATmega328P):
//   [0..15]   -> Settings (struct cfg)
//   [16..17]  -> contador de logs (uint16)
//   [18..19]  -> head do ring buffer de logs (uint16)
//   [20..31]  -> reserva/padding
//   [32..1023]-> logs propriamente ditos (8 bytes cada, ring buffer)
const uint8_t  EEP_MAGIC      = 0xAD;   // bump para forcar re-sync da hora do RTC
                                        // Quando voce muda este valor, no proximo
                                        // boot a config sera resetada para default.
const uint16_t EEP_ADDR_CFG   = 0;      // Onde mora a struct Settings
const uint16_t EEP_ADDR_LOGN  = 16;     // Quantos logs validos existem
const uint16_t EEP_ADDR_HEAD  = 18;     // Posicao do proximo slot para gravar
const uint16_t EEP_ADDR_LOGS  = 32;     // Inicio dos dados dos logs
const uint8_t  LOG_ENTRY_SIZE = 8;      // Tamanho de cada LogEntry em bytes
const uint16_t LOG_MAX        = (1024 - EEP_ADDR_LOGS) / LOG_ENTRY_SIZE;
                                        // = (1024-32)/8 = 124 logs maximo

// =================== TIPOS ===========================================
// Struct com todas as configuracoes persistentes do usuario.
// IMPORTANTE: o tamanho desta struct precisa caber em 16 bytes (EEP_ADDR_LOGN
// vem logo depois). Mudou a struct? Confira o sizeof.
struct Settings {
  uint8_t  magic;          // Identificador de versao (= EEP_MAGIC se valido)
  int8_t   utcOffset;      // Fuso horario, ex: -3 para Brasil (BRT)
  uint8_t  tempUnit;       // 0 = Celsius, 1 = Fahrenheit
  uint8_t  language;       // 0 = PT, 1 = EN, 2 = ES
  uint8_t  logIntervalS;   // Intervalo entre gravacoes de log (em segundos)
  uint16_t ldrMin;         // Calibracao auto do LDR - minimo lido
  uint16_t ldrMax;         // Calibracao auto do LDR - maximo lido
  uint8_t  buzzerOn;       // 1 = buzzer habilitado, 0 = silencioso
};

// Struct de uma entrada de log na EEPROM. Mantida em 8 bytes para caber
// muitas leituras nos 992 bytes disponiveis para logs.
struct LogEntry {
  uint32_t ts;             // Timestamp (segundos desde 2000-01-01, UTC)
  int8_t   tempC;          // Temperatura em Celsius (signed: pode ser negativo)
  uint8_t  humid;          // Umidade em % (0-100)
  uint8_t  ldr;            // Leitura do LDR ja normalizada (0-255)
  uint8_t  status;         // Zona consolidada: 0=verde, 1=amarelo, 2=vermelho
};

// =================== OBJETOS =========================================
// Instancias dos drivers. Inicializadas aqui mas nao "ligadas" ainda -
// isso acontece no setup() com lcd.init(), dht.begin(), rtc.Begin().
LiquidCrystal_I2C lcd(LCD_ADDR, 20, 4);                 // 20 colunas x 4 linhas
DHT               dht(PIN_DHT, DHT11);                  // Tipo DHT11 (existe DHT22 tb)
ThreeWire         rtcWire(PIN_RTC_DAT, PIN_RTC_CLK, PIN_RTC_RST); // I/O, SCLK, CE
RtcDS1302<ThreeWire> rtc(rtcWire);                      // RTC sobre o barramento 3-wire
Settings          cfg;                                  // Configuracao em RAM

// =================== ESTADO ==========================================
// Maquina de estados do app. So pode estar em um modo por vez.
enum AppMode { MODE_BOOT, MODE_NORMAL, MODE_MENU, MODE_LOGS };
AppMode appMode = MODE_BOOT;

bool lcdOk = false;          // LCD inicializou com sucesso?
bool rtcOk = false;          // RTC esta funcionando?
bool freshConfig = false;    // setado pelo cfgLoad quando magic byte mudou
                             // -> indica que a config foi resetada para default,
                             //    entao tambem precisamos resetar a hora do RTC.

// Acumuladores para a media movel de 10 segundos.
// A cada 1s amostramos os sensores; a cada 10s consolidamos a media.
const uint8_t WINDOW_S = 10;
float    sumT = 0, sumH = 0;     // Soma das temperaturas/umidades na janela
uint32_t sumL = 0;               // Soma do LDR (uint32 evita overflow com 10 amostras)
uint8_t  nSamples = 0;           // Quantas amostras validas tem na janela

// Valores "atuais" exibidos na tela (resultado da ultima janela de 10s).
float   curT = 0, curH = 0;
uint8_t curL = 0;
uint8_t curStatus = 0;           // Pior zona entre os 3 sensores (worst-case)

// Calibracao automatica do LDR: aprende min/max ao longo da operacao
// para mapear 0-255 baseado no range real do ambiente, nao do datasheet.
uint16_t ldrRawMin = 1023;
uint16_t ldrRawMax = 0;

// Timers nao-bloqueantes (estilo "blink without delay").
// Salvamos o millis() da ultima vez que cada coisa rodou.
unsigned long tLastSample = 0;   // Ultima amostragem dos sensores
unsigned long tLastWindow = 0;   // Ultima consolidacao de janela
unsigned long tLastLog    = 0;   // Ultima gravacao na EEPROM
unsigned long tLastBtn    = 0;   // Ultimo botao pressionado (debounce)
unsigned long tLastFrame  = 0;   // Ultimo redraw da tela
unsigned long bootMillis  = 0;   // fallback de timestamp se RTC falhar

// Estado dos menus.
uint8_t menuIdx = 0;             // Item atualmente destacado
const uint8_t MENU_ITEMS = 6;    // Total de itens (0..5)
uint16_t logViewIdx = 0;         // Qual log esta sendo visualizado

// =================== CARACTERES CUSTOMIZADOS =========================
// O HD44780 permite ate 8 caracteres customizados (CGRAM slots 0-7).
// Cada um e uma matriz 5x8 pixels - cada byte e uma linha (5 bits usados).
// Bit 1 = pixel aceso, bit 0 = apagado.
byte chBottle1[8] = { 0b00100,0b01110,0b01010,0b01010,0b11111,0b11111,0b11111,0b11111 }; // garrafa cheia
byte chBottle2[8] = { 0b00100,0b01110,0b01010,0b11111,0b11111,0b11111,0b11111,0b11111 }; // garrafa (frame 2)
byte chGrape  [8] = { 0b00000,0b01110,0b11111,0b11111,0b01110,0b00100,0b00100,0b00000 }; // cacho de uva
byte chThermo [8] = { 0b00100,0b01010,0b01010,0b01110,0b01110,0b11111,0b11111,0b01110 }; // termometro
byte chDrop   [8] = { 0b00100,0b00100,0b01010,0b10001,0b10001,0b10001,0b01110,0b00000 }; // gota d'agua
byte chSun    [8] = { 0b00100,0b10101,0b01110,0b11011,0b01110,0b10101,0b00100,0b00000 }; // sol
byte chOk     [8] = { 0b00000,0b00001,0b00011,0b10110,0b11100,0b01000,0b00000,0b00000 }; // check (zona OK)
byte chAlerta [8] = { 0b00000,0b00100,0b01110,0b01110,0b01010,0b11111,0b00100,0b00000 }; // triangulo (zona ALERTA)
byte chCritico[8] = { 0b01110,0b11111,0b10101,0b11011,0b11111,0b01110,0b01010,0b00000 }; // caveira (zona CRITICO)

// Nomes simbolicos para os indices da CGRAM. lcd.write(ICO_GRAPE) e
// muito mais legivel que lcd.write((uint8_t)2).
//
// O HD44780 so tem 8 slots de CGRAM e precisamos de 9 caracteres (3 da
// animacao de boot + 3 icones de linha + 3 emojis de status). Truque:
// o slot 1 e usado como ICO_BOTTLE2 durante a animacao de boot e depois
// recarregado com chCritico, ja que BOTTLE2 nao e usado em runtime.
#define ICO_BOTTLE   0
#define ICO_BOTTLE2  1   // slot 1, valido apenas durante bootAnimation()
#define ICO_CRITICO  1   // mesmo slot, recarregado pos-boot
#define ICO_GRAPE    2
#define ICO_THERMO   3
#define ICO_DROP     4
#define ICO_SUN      5
#define ICO_OK       6
#define ICO_ALERTA   7

// =====================================================================
// EEPROM
// =====================================================================

// Salva a struct cfg na EEPROM. EEPROM.put usa update interno - so escreve
// se o byte mudou, prolongando a vida util da memoria (~100k ciclos).
void cfgSave() {
  cfg.ldrMin = ldrRawMin;        // Persiste a calibracao atual junto
  cfg.ldrMax = ldrRawMax;
  EEPROM.put(EEP_ADDR_CFG, cfg);
}

// Carrega a config da EEPROM. Se for primeira execucao (magic byte
// diferente), inicializa com defaults e marca freshConfig = true.
void cfgLoad() {
  EEPROM.get(EEP_ADDR_CFG, cfg);
  if (cfg.magic != EEP_MAGIC) {
    // Primeira boot OU EEP_MAGIC foi bumpado no codigo.
    // Em ambos os casos, recriamos a config do zero.
    freshConfig = true;
    cfg.magic        = EEP_MAGIC;
    cfg.utcOffset    = -3;       // Brasil (BRT)
    cfg.tempUnit     = 0;        // Celsius
    cfg.language     = 0;        // Portugues
    cfg.logIntervalS = 60;       // 1 log por minuto
    cfg.ldrMin       = 1023;     // Sera reduzido conforme ler valores menores
    cfg.ldrMax       = 0;        // Sera aumentado conforme ler valores maiores
    cfg.buzzerOn     = 1;        // Buzzer habilitado
    cfgSave();
    // Tambem zeramos o ring buffer de logs: dados antigos podem ser lixo.
    uint16_t z = 0;
    EEPROM.put(EEP_ADDR_LOGN, z);
    EEPROM.put(EEP_ADDR_HEAD, z);
  }
  // Restaura calibracao do LDR (se existir, senao ficam nos defaults).
  if (cfg.ldrMin <= 1023) ldrRawMin = cfg.ldrMin;
  if (cfg.ldrMax <= 1023) ldrRawMax = cfg.ldrMax;
}

// Grava um log no ring buffer circular.
// Quando enche, sobrescreve o mais antigo (FIFO de 124 entradas).
void logWrite(const LogEntry &e) {
  uint16_t head, count;
  EEPROM.get(EEP_ADDR_HEAD, head);
  EEPROM.get(EEP_ADDR_LOGN, count);
  if (head >= LOG_MAX) head = 0;                              // sanidade
  EEPROM.put(EEP_ADDR_LOGS + head * LOG_ENTRY_SIZE, e);       // grava
  head = (head + 1) % LOG_MAX;                                // avanca circular
  if (count < LOG_MAX) count++;                               // ate encher
  EEPROM.put(EEP_ADDR_HEAD, head);
  EEPROM.put(EEP_ADDR_LOGN, count);
}

// Le um log especifico pelo indice fisico (0..LOG_MAX-1).
// Cuidado: indice fisico != ordem cronologica (depende do head).
LogEntry logRead(uint16_t idx) {
  LogEntry e;
  EEPROM.get(EEP_ADDR_LOGS + idx * LOG_ENTRY_SIZE, e);
  return e;
}

// "Apaga" todos os logs zerando os contadores. Os bytes em si continuam
// na EEPROM, mas serao sobrescritos eventualmente pelo ring buffer.
void logClearAll() {
  uint16_t z = 0;
  EEPROM.put(EEP_ADDR_LOGN, z);
  EEPROM.put(EEP_ADDR_HEAD, z);
}

// =====================================================================
// SENSORES
// =====================================================================

// Le o LDR e retorna valor normalizado em 0-255.
// Faz auto-calibracao: enquanto nao tivermos um range razoavel (>=50),
// usa o map padrao 0-1023 -> 0-255. Depois usa o range aprendido.
uint8_t readLDR() {
  uint16_t raw = analogRead(PIN_LDR);
  // Atualiza min/max observados (calibracao continua).
  if (raw < ldrRawMin) ldrRawMin = raw;
  if (raw > ldrRawMax) ldrRawMax = raw;
  if ((ldrRawMax - ldrRawMin) < 50) {
    // Range muito pequeno ainda - usa mapeamento default.
    return constrain(map(raw, 0, 1023, 0, 255), 0, 255);
  }
  // Mapeia o range observado para 0-255 (ganha resolucao no ambiente real).
  return constrain(map(raw, ldrRawMin, ldrRawMax, 0, 255), 0, 255);
}

// Faz uma amostragem dos 3 sensores e acumula nas somas da janela.
// Se DHT11 retornar NaN (acontece ~5% das leituras), descarta a amostra.
void sensorSample() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  uint8_t l = readLDR();
  if (!isnan(t) && !isnan(h)) {
    sumT += t; sumH += h; sumL += l; nSamples++;
  } else {
    Serial.println(F("[WARN] DHT11 retornou NaN"));
  }
}

// Avalia em qual zona um valor esta, dada a faixa verde [gMin..gMax]
// e o teto amarelo yMax:
//   gMin <= v <= gMax           -> 0 (verde / ideal)
//   v < gMin OU gMax < v <= yMax-> 1 (amarelo / atencao)
//   v > yMax                    -> 2 (vermelho / alerta)
// Como sensores fisicos nao retornam negativo (LDR 0-255, umid 0-100,
// temp DHT11 0-50), na pratica "abaixo do gMin" sempre cai como amarelo.
uint8_t evalZone(float v, float gMin, float gMax, float yMax) {
  if (v >= gMin && v <= gMax) return 0;   // dentro da faixa ideal
  if (v <= yMax)              return 1;   // fora do ideal mas toleravel
  return 2;                                // alem do toleravel
}

// Fecha a janela de 10s: calcula medias, classifica em zonas e
// determina o status global como o pior caso (worst-of-3).
void windowFinalize() {
  if (nSamples == 0) {
    Serial.println(F("[WARN] Janela 10s sem amostras validas"));
    return;
  }
  curT = sumT / nSamples;
  curH = sumH / nSamples;
  curL = sumL / nSamples;

  uint8_t zT = evalZone(curT, TEMP_GREEN_MIN, TEMP_GREEN_MAX, TEMP_YELLOW_MAX);
  uint8_t zH = evalZone(curH, UMID_GREEN_MIN, UMID_GREEN_MAX, UMID_YELLOW_MAX);
  uint8_t zL = evalZone(curL, LDR_GREEN_MIN,  LDR_GREEN_MAX,  LDR_YELLOW_MAX);
  curStatus = max(max(zT, zH), zL);     // pior zona = status global

  // Telemetria via Serial - util para debug e gravacao em Tinkercad/PC.
  Serial.print(F("[10s] T=")); Serial.print(curT, 1);
  Serial.print(F("C  U=")); Serial.print(curH, 1);
  Serial.print(F("%  L=")); Serial.print(curL);
  Serial.print(F("/255  zT=")); Serial.print(zT);
  Serial.print(F(" zH="));      Serial.print(zH);
  Serial.print(F(" zL="));      Serial.print(zL);
  Serial.print(F(" status=")); Serial.println(curStatus);

  // Reseta acumuladores para a proxima janela.
  sumT = sumH = 0; sumL = 0; nSamples = 0;
  cfgSave();    // salva a calibracao do LDR caso tenha mudado
}

// =====================================================================
// LED + BUZZER
// =====================================================================

// Define R e G do LED RGB via PWM (0-255 em cada canal).
// Misturando R e G da pra gerar amarelo/laranja sem precisar do azul.
void setRGB(uint8_t r, uint8_t g) {
  analogWrite(PIN_LED_R, r);
  analogWrite(PIN_LED_G, g);
}

// Liga/desliga o canal azul (digital, sem PWM - ver comentario no PIN_LED_B).
void setBlue(bool on) {
  digitalWrite(PIN_LED_B, on ? HIGH : LOW);
}

// Aplica o feedback visual+sonoro conforme o status atual:
//   0 (verde)   -> LED verde, buzzer mudo
//   1 (amarelo) -> LED amarelo/ambar, buzzer 500Hz (se habilitado)
//   2 (vermelho)-> LED vermelho, buzzer 1000Hz (se habilitado)
//
// Tuning do amarelo: LEDs verdes de modulos RGB tipicos sao ~2-3x mais
// brilhantes que os vermelhos com o mesmo PWM, entao para o olho o canal
// G "rouba" o show e o resultado parece esverdeado. Manter G baixo (~80)
// faz o vermelho dominar e a mistura ler como amarelo/ambar de verdade.
// Se ficar muito laranja, suba G; se voltar a parecer verde, abaixe.
void alertApply() {
  switch (curStatus) {
    case 0: setRGB(0, 255); noTone(PIN_BUZZER); break;
    case 1:
      setRGB(255, 80);    // R cheio + G fraco -> amarelo/ambar real
      cfg.buzzerOn ? tone(PIN_BUZZER, BUZZ_AMARELO) : noTone(PIN_BUZZER);
      break;
    case 2:
      setRGB(255, 0);     // vermelho puro
      cfg.buzzerOn ? tone(PIN_BUZZER, BUZZ_VERMELHO) : noTone(PIN_BUZZER);
      break;
  }
}

// =====================================================================
// HELPERS DE STRING
// =====================================================================

// Retorna a string da zona no idioma atual, com padding para 7 caracteres
// (sobrescreve completamente o slot de tela sem precisar limpar).
// Os 7 chars cabem no layout do renderHome (icone + barra + status + emoji = 20).
const char* zoneText(uint8_t z) {
  if (cfg.language == 0) {           // PT
    if (z == 0) return "OK     ";
    if (z == 1) return "ALERTA ";
    return            "CRITICO";
  } else if (cfg.language == 1) {    // EN
    if (z == 0) return "OK     ";
    if (z == 1) return "ALERT  ";
    return            "DANGER ";
  } else {                            // ES
    if (z == 0) return "OK     ";
    if (z == 1) return "ALERTA ";
    return            "CRITICO";
  }
}

// Converte Celsius para a unidade selecionada (C ou F).
// IMPORTANTE: isso e SO para exibicao. A avaliacao de zona em evalZone()
// sempre usa Celsius (curT), entao os alertas disparam na mesma temperatura
// fisica independente do que o usuario escolheu mostrar na tela.
float tempForDisplay(float c) {
  return (cfg.tempUnit == 1) ? (c * 9.0 / 5.0 + 32.0) : c;
}
char tempUnitChar() { return cfg.tempUnit == 1 ? 'F' : 'C'; }

// Retorna o "Unix" do RTC. ATENCAO: a lib RtcByMakuna usa epoch 2000,
// nao o epoch 1970 do Unix tradicional. Mantemos isso internamente -
// timestamps na EEPROM tambem usam epoch 2000.
uint32_t nowUnix() {
  if (rtcOk) {
    RtcDateTime t = rtc.GetDateTime();
    return t.TotalSeconds();   // segundos desde 2000-01-01
  }
  // Fallback: se RTC quebrou, usa millis() desde o boot. Nao e absoluto,
  // mas pelo menos os logs ficam ordenados em sequencia relativa.
  return (millis() - bootMillis) / 1000UL;
}

// Cria um RtcDateTime aplicando o offset de UTC para exibicao.
// O RTC armazena UTC; aqui aplicamos o fuso para mostrar hora local.
RtcDateTime nowLocal() {
  return RtcDateTime(nowUnix() + (int32_t)cfg.utcOffset * 3600L);
}

// =====================================================================
// ANIMACAO DE BOOT
// =====================================================================

// Sequencia bonitinha de abertura: uvas convergindo, titulo aparecendo
// letra-por-letra e garrafa "respirando" entre dois frames.
// Puramente visual - se LCD nao funcionou, simplesmente nao roda.
void bootAnimation() {
  if (!lcdOk) return;
  lcd.clear();

  // Fase 1: uvas vindo das bordas para o centro.
  for (uint8_t i = 0; i < 10; i++) {
    lcd.setCursor(9 - i, 1);  lcd.write(ICO_GRAPE);
    lcd.setCursor(10 + i, 1); lcd.write(ICO_GRAPE);
    delay(40);
  }
  delay(200);

  // Fase 2: titulo aparece letra por letra, centralizado.
  lcd.setCursor(0, 1); lcd.print(F("                    "));
  const char* t1 = "VINHERIA AGNELLO";
  uint8_t col = (20 - 16) / 2;     // centraliza em 20 colunas
  for (uint8_t i = 0; i < 16; i++) {
    lcd.setCursor(col + i, 1);
    lcd.print(t1[i]);
    delay(60);
  }

  // Fase 3: subtitulo + animacao da garrafa piscando.
  // O subtitulo e escrito letra-por-letra (igual fase 2) em vez de um
  // unico lcd.print() longo. Isso evita um glitch visual onde algumas
  // versoes da LiquidCrystal_I2C truncam strings longas devido a timing
  // do latch do PCF8574 - sintoma: "Sistema d" aparece em vez do texto
  // completo.
  const char* t2 = "Sistema de Monit.";
  for (uint8_t i = 0; i < 17; i++) {
    lcd.setCursor(2 + i, 2);
    lcd.print(t2[i]);
    delay(30);
  }
  for (uint8_t f = 0; f < 8; f++) {
    lcd.setCursor(0, 3);  lcd.write(ICO_GRAPE);
    lcd.setCursor(19, 3); lcd.write(ICO_GRAPE);
    lcd.setCursor(9, 3);  lcd.write((f % 2) ? ICO_BOTTLE : ICO_BOTTLE2);
    delay(180);
  }
  delay(400);
  lcd.clear();
}

// =====================================================================
// TELAS
// =====================================================================

// Helper que mapeia o "emoji" certo para cada zona.
// Slot 1 vira CRITICO depois do boot (ver setup() / bootAnimation).
uint8_t zoneEmoji(uint8_t z) {
  if (z == 0) return ICO_OK;
  if (z == 1) return ICO_ALERTA;
  return ICO_CRITICO;
}

// Desenha uma linha de sensor com layout fixo de 20 colunas:
//
//   col:  0  1  2-9            10  11-17     18  19
//   pos:  T  _  ########____   _   ALERTA_   _   ⚠
//
//   - col 0:    icone do sensor (ICO_THERMO/DROP/SUN)
//   - cols 2-9: barra de 8 chars (9 niveis: 0/8 a 8/8 cells preenchidas)
//                   uso 0xFF (full block, built-in do HD44780) e ' '
//   - cols 11-17: texto da zona (7 chars, vindo de zoneText)
//   - col 19:   emoji da zona
//
// O parametro pct e o percentual 0..100 que controla quantos chars da
// barra ficam preenchidos. Maior pct = barra mais cheia.
void drawSensorRow(uint8_t row, uint8_t lineIcon, uint8_t pct, uint8_t zone) {
  lcd.setCursor(0, row);
  lcd.write(lineIcon);                 // col 0
  lcd.print(' ');                      // col 1

  // Barra de 8 cells (cols 2-9). 0xFF = full block; ' ' = vazio.
  uint8_t filled = (uint16_t)pct * 8 / 100;
  if (filled > 8) filled = 8;
  for (uint8_t i = 0; i < 8; i++) {
    lcd.write(i < filled ? (uint8_t)0xFF : (uint8_t)' ');
  }

  lcd.print(' ');                      // col 10
  lcd.print(zoneText(zone));           // cols 11-17 (7 chars padded)
  lcd.print(' ');                      // col 18
  lcd.write(zoneEmoji(zone));          // col 19
}

// Tela principal (modo NORMAL). Layout 20x4:
//   linha 0: data e hora local
//   linha 1: barra da temperatura  + status + emoji
//   linha 2: barra da umidade      + status + emoji
//   linha 3: barra da luminosidade + status + emoji
//
// Os percentuais das barras sao normalizados:
//   Temp:  0-50C (range tipico do DHT11) -> 0-100%
//   Umid:  ja em %, usa direto
//   Luz:   0-255 (LDR ja normalizado)    -> 0-100%
void renderHome() {
  if (!lcdOk) return;

  // Linha 0: data e hora (DD/MM/YYYY HH:MM:SS = 19 chars + 1 espaco = 20)
  RtcDateTime dt = nowLocal();
  char buf[21];
  snprintf(buf, sizeof(buf), "%02d/%02d/%04d %02d:%02d:%02d ",
           dt.Day(), dt.Month(), dt.Year(),
           dt.Hour(), dt.Minute(), dt.Second());
  lcd.setCursor(0, 0); lcd.print(buf);

  // Avalia as zonas de cada sensor com a faixa [min..max] da spec.
  uint8_t zT = evalZone(curT, TEMP_GREEN_MIN, TEMP_GREEN_MAX, TEMP_YELLOW_MAX);
  uint8_t zH = evalZone(curH, UMID_GREEN_MIN, UMID_GREEN_MAX, UMID_YELLOW_MAX);
  uint8_t zL = evalZone(curL, LDR_GREEN_MIN,  LDR_GREEN_MAX,  LDR_YELLOW_MAX);

  // Calcula percentuais 0..100 para as barras (limitados ao range valido).
  uint8_t pctT = constrain((int16_t)(curT * 2.0f), 0, 100);            // 0-50C -> 0-100%
  uint8_t pctH = constrain((int16_t)curH, 0, 100);                     // ja %
  uint8_t pctL = constrain((int16_t)((uint16_t)curL * 100 / 255), 0, 100); // 0-255 -> 0-100%

  drawSensorRow(1, ICO_THERMO, pctT, zT);
  drawSensorRow(2, ICO_DROP,   pctH, zH);
  drawSensorRow(3, ICO_SUN,    pctL, zL);
}

// Retorna o label do item de menu no idioma atual.
const char* menuLabel(uint8_t i) {
  if (cfg.language == 0) {           // PT
    switch (i) {
      case 0: return "UTC offset";
      case 1: return "Unidade temp";
      case 2: return "Idioma";
      case 3: return "Buzzer";
      case 4: return "Ver logs";
      case 5: return "Sair";
    }
  } else if (cfg.language == 1) {    // EN
    switch (i) {
      case 0: return "UTC offset";
      case 1: return "Temp unit";
      case 2: return "Language";
      case 3: return "Buzzer";
      case 4: return "View logs";
      case 5: return "Exit";
    }
  } else {                            // ES
    switch (i) {
      case 0: return "UTC offset";
      case 1: return "Unidad temp";
      case 2: return "Idioma";
      case 3: return "Buzzer";
      case 4: return "Ver logs";
      case 5: return "Salir";
    }
  }
  return "";
}

// Escreve em 'out' o valor atual do item i (ex: "+3", "C", "PT", "ON").
// Usa snprintf para garantir que nao estoure o buffer.
void menuValueAt(uint8_t i, char* out, uint8_t len) {
  switch (i) {
    case 0: snprintf(out, len, "%+d", cfg.utcOffset); break;
    case 1: snprintf(out, len, "%c", cfg.tempUnit == 0 ? 'C' : 'F'); break;
    case 2: {
      const char* langs[] = {"PT", "EN", "ES"};
      snprintf(out, len, "%s", langs[cfg.language]);
      break;
    }
    case 3: snprintf(out, len, "%s", cfg.buzzerOn ? "ON" : "OFF"); break;
    case 4: snprintf(out, len, ">"); break;     // entra em submenu
    case 5: snprintf(out, len, ">"); break;     // sair
  }
}

// Renderiza o menu mostrando 3 itens por vez (anterior, atual, proximo)
// com '>' marcando o item selecionado.
void renderMenu() {
  if (!lcdOk) return;
  lcd.setCursor(0, 0);
  if (cfg.language == 0)      lcd.print(F("=== CONFIGURACAO ==="));
  else if (cfg.language == 1) lcd.print(F("===   SETUP    ====="));
  else                        lcd.print(F("=== CONFIGURACION =="));

  // Loop nas 3 linhas visiveis (1, 2 e 3 do LCD).
  // Mostra menuIdx-1, menuIdx, menuIdx+1; clamp para nao sair do range.
  for (uint8_t row = 0; row < 3; row++) {
    int8_t idx = (int8_t)menuIdx + row - 1;
    lcd.setCursor(0, row + 1);
    if (idx < 0 || idx >= MENU_ITEMS) {
      lcd.print(F("                    ")); continue;     // linha vazia
    }
    lcd.print(idx == menuIdx ? '>' : ' ');
    char val[6];
    menuValueAt(idx, val, sizeof(val));
    char line[21];
    // %-13s = label esquerda alinhado em 13 chars; %5s = valor direita em 5
    snprintf(line, sizeof(line), "%-13s%5s", menuLabel(idx), val);
    lcd.print(line);
  }
}

// Trata acao no item de menu atual. Chamado apenas quando OK e pressionado
// (delta = 0). Up/Down de navegacao sao tratados em handleButtons.
void menuAction(int8_t delta) {
  // delta == 0 = botao OK; UP/DOWN ja sao tratados como navegacao em handleButtons
  if (delta != 0) return;
  switch (menuIdx) {
    case 0:  // UTC offset: incrementa de 1 em 1, wrap de +14 para -12
      cfg.utcOffset++;
      if (cfg.utcOffset > 14) cfg.utcOffset = -12;
      break;
    case 1:  // Unidade temp: toggle C <-> F
      cfg.tempUnit = !cfg.tempUnit;
      break;
    case 2:  // Idioma: cicla PT -> EN -> ES -> PT
      cfg.language = (cfg.language + 1) % 3;
      break;
    case 3:  // Buzzer: toggle ON <-> OFF
      cfg.buzzerOn = !cfg.buzzerOn;
      break;
    case 4:  // Ver logs
      appMode = MODE_LOGS;
      break;
    case 5:  // Sair
      cfgSave();
      appMode = MODE_NORMAL;
      if (lcdOk) lcd.clear();
      break;
  }
  cfgSave();    // qualquer mudanca persiste imediatamente
}

// Tela de visualizacao dos logs gravados na EEPROM.
// O ring buffer e traduzido para indice logico (0 = mais antigo).
void renderLogs() {
  if (!lcdOk) return;
  uint16_t count;
  EEPROM.get(EEP_ADDR_LOGN, count);
  lcd.setCursor(0, 0);
  if (count == 0) {
    lcd.print(F("=== LOGS (vazio) ==="));
    lcd.setCursor(0, 1); lcd.print(F("                    "));
    lcd.setCursor(0, 2); lcd.print(F("CANCEL = voltar     "));
    lcd.setCursor(0, 3); lcd.print(F("                    "));
    return;
  }
  if (logViewIdx >= count) logViewIdx = count - 1;     // clamp

  // Conversao indice logico -> indice fisico no ring buffer:
  //   firstPhys aponta para o log mais antigo;
  //   logViewIdx 0 = mais antigo; count-1 = mais recente.
  uint16_t head;
  EEPROM.get(EEP_ADDR_HEAD, head);
  uint16_t firstPhys = (head + LOG_MAX - count) % LOG_MAX;
  uint16_t phys = (firstPhys + logViewIdx) % LOG_MAX;
  LogEntry e = logRead(phys);

  // Linha 0: contador de logs.
  char buf[21];
  snprintf(buf, sizeof(buf), "LOG %3u/%3u",
           (unsigned)(logViewIdx + 1), (unsigned)count);
  lcd.setCursor(0, 0); lcd.print(buf); lcd.print(F("         "));

  // Linha 1: data/hora do log (em hora local).
  RtcDateTime d(e.ts + (int32_t)cfg.utcOffset * 3600L);
  snprintf(buf, sizeof(buf), "%02d/%02d %02d:%02d:%02d",
           d.Day(), d.Month(), d.Hour(), d.Minute(), d.Second());
  lcd.setCursor(0, 1); lcd.print(buf); lcd.print(F("    "));

  // Linha 2: temperatura, umidade, luminosidade do log.
  // O log armazena temperatura em Celsius (int8_t); convertemos para a
  // unidade escolhida apenas na exibicao.
  float tDisp = tempForDisplay(e.tempC);
  char tb[6]; dtostrf(tDisp, 4, 1, tb);
  snprintf(buf, sizeof(buf), "T:%s%c U:%2u%% L:%3u",
           tb, tempUnitChar(), e.humid, e.ldr);
  lcd.setCursor(0, 2); lcd.print(buf);

  // Linha 3: zona textual do log.
  // "Status: " (8) + zoneText (7) = 15 chars; 5 chars de padding completam a linha.
  lcd.setCursor(0, 3); lcd.print(F("Status: "));
  lcd.print(zoneText(e.status));
  lcd.print(F("     "));
}

// =====================================================================
// BOTOES + JOYSTICK
// =====================================================================

// Como os botoes usam INPUT_PULLUP, "pressionado" significa nivel LOW.
bool btnPressed(uint8_t pin) { return digitalRead(pin) == LOW; }

// Le o eixo Y do joystick e retorna a direcao detectada:
//   -1 = UP   (joystick para cima)
//    0 = neutro (dentro da deadzone)
//   +1 = DOWN (joystick para baixo)
//
// Se na sua montagem fisica o joystick estiver com Up/Down invertidos,
// troque os sinais dos returns aqui (ou gire o modulo 180 graus).
int8_t joyDir() {
  uint16_t v = analogRead(PIN_JOY_Y);
  if (v < JOY_CENTER - JOY_DEADZONE) return -1;
  if (v > JOY_CENTER + JOY_DEADZONE) return +1;
  return 0;
}

// Le os botoes + joystick e despacha a acao conforme o modo do app.
// Implementa debounce simples por tempo (180ms entre eventos), que para o
// joystick tambem funciona como auto-repeat: segurar o eixo rola o menu
// continuamente, ~5 itens por segundo.
//
// Resumo do mapeamento por modo:
//   NORMAL: CANCEL abre o menu;                OK nao tem efeito.
//   MENU:   joystick rola itens; OK confirma;  CANCEL volta pra leitura.
//   LOGS:   joystick rola logs;                CANCEL volta pro menu.
void handleButtons() {
  if (millis() - tLastBtn < 180) return;     // debounce / repeat-rate

  if (btnPressed(PIN_BTN_OK)) {
    tLastBtn = millis();
    Serial.println(F("[BTN] OK"));
    if (appMode == MODE_MENU) {
      // No menu, OK confirma o item selecionado.
      menuAction(0);
    }
    // No NORMAL e LOGS, OK e ignorado de proposito.
  }
  else if (btnPressed(PIN_BTN_CAN)) {
    tLastBtn = millis();
    Serial.println(F("[BTN] CANCEL"));
    if (appMode == MODE_NORMAL) {
      // CANCEL no modo de leitura abre o menu.
      appMode = MODE_MENU; menuIdx = 0;
      if (lcdOk) lcd.clear();
    } else if (appMode == MODE_MENU) {
      // CANCEL no menu volta para a tela principal (modo de leitura).
      cfgSave(); appMode = MODE_NORMAL;
      if (lcdOk) lcd.clear();
    } else if (appMode == MODE_LOGS) {
      // CANCEL nos logs volta para o menu.
      appMode = MODE_MENU;
      if (lcdOk) lcd.clear();
    }
  }
  else {
    // Joystick (eixo Y) substitui os antigos botoes Up/Down.
    int8_t dir = joyDir();
    if (dir == -1) {                         // UP
      tLastBtn = millis();
      Serial.println(F("[JOY] UP"));
      if (appMode == MODE_MENU) {
        // Wrap-around: do primeiro item vai para o ultimo.
        menuIdx = (menuIdx == 0) ? MENU_ITEMS - 1 : menuIdx - 1;
      } else if (appMode == MODE_LOGS) {
        if (logViewIdx > 0) logViewIdx--;
      }
    } else if (dir == +1) {                  // DOWN
      tLastBtn = millis();
      Serial.println(F("[JOY] DOWN"));
      if (appMode == MODE_MENU) {
        menuIdx = (menuIdx + 1) % MENU_ITEMS;     // wrap-around
      } else if (appMode == MODE_LOGS) {
        logViewIdx++;     // clamp e feito em renderLogs
      }
    }
  }
}

// =====================================================================
// LOG PERIODICO
// =====================================================================

// Verifica se ja passou o intervalo configurado e, se sim, grava um
// log com os valores atuais. Chamado todo loop().
void maybeLog() {
  if (millis() - tLastLog < (unsigned long)cfg.logIntervalS * 1000UL) return;
  tLastLog = millis();
  LogEntry e;
  e.ts     = nowUnix();
  e.tempC  = (int8_t)curT;     // trunca para int8 - perde decimais
  e.humid  = (uint8_t)curH;
  e.ldr    = curL;
  e.status = curStatus;
  logWrite(e);
  Serial.print(F("[EEPROM] log gravado @ts="));
  Serial.println(e.ts);
}

// =====================================================================
// SETUP / LOOP
// =====================================================================

// setup() roda uma unica vez no power-on/reset. Inicializa todos os
// perifericos e prepara o estado inicial do sistema.
void setup() {
  // ---- Configura pinos ----
  // Botoes com pull-up interno: pino fica em HIGH ate ser puxado para GND
  // pelo botao. Economiza 2 resistores externos.
  pinMode(PIN_BTN_OK,  INPUT_PULLUP);
  pinMode(PIN_BTN_CAN, INPUT_PULLUP);
  // Joystick: analogRead() ja configura o pino como entrada analogica
  // internamente, entao nao precisa de pinMode aqui.
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  setRGB(0, 0);          // LED apagado
  setBlue(false);

  // ---- Serial ----
  Serial.begin(9600);
  delay(500);            // tempo para o terminal abrir e nao perder linhas

  // ---- I2C (LCD) ----
  Wire.begin();

  Serial.print(F("Inicializando LCD em 0x")); Serial.print(LCD_ADDR, HEX);
  Serial.print(F("... "));
  lcd.init();
  lcd.backlight();
  // Carrega os 8 caracteres customizados na CGRAM do controlador HD44780.
  lcd.createChar(ICO_BOTTLE,  chBottle1);
  lcd.createChar(ICO_BOTTLE2, chBottle2);
  lcd.createChar(ICO_GRAPE,   chGrape);
  lcd.createChar(ICO_THERMO,  chThermo);
  lcd.createChar(ICO_DROP,    chDrop);
  lcd.createChar(ICO_SUN,     chSun);
  lcd.createChar(ICO_OK,      chOk);
  lcd.createChar(ICO_ALERTA,  chAlerta);
  lcdOk = true;     // se travou aqui, e wiring/endereco
  Serial.println(F("OK"));

  // ---- EEPROM (config) ----
  Serial.print(F("Carregando config da EEPROM... "));
  cfgLoad();
  Serial.println(F("OK"));

  // ---- Joystick: leitura inicial (so para debug) ----
  // Util para diagnosticar drift ou fiacao trocada logo no boot.
  Serial.print(F("Joystick Y em repouso: "));
  Serial.println(analogRead(PIN_JOY_Y));

  // ---- DHT11 ----
  Serial.print(F("Inicializando DHT11... "));
  dht.begin();
  Serial.println(F("OK"));

  // ---- RTC DS1302 ----
  // Setup do RTC e a parte mais delicada: precisamos lidar com bateria
  // descarregada, write-protect e sincronizacao com a hora de compilacao.
  Serial.print(F("Inicializando RTC DS1302... "));
  rtc.Begin();

  // __DATE__/__TIME__ vem na hora local do PC (BRT no caso). O RTC deve
  // armazenar UTC para que o offset do menu funcione corretamente.
  // Convertemos local -> UTC subtraindo o offset configurado.
  RtcDateTime compileLocal = RtcDateTime(__DATE__, __TIME__);
  RtcDateTime compileTime(compileLocal.TotalSeconds() - (int32_t)cfg.utcOffset * 3600L);

  if (!rtc.IsDateTimeValid()) {
    // RTC perdeu a hora (sem bateria ou bateria fraca)
    Serial.println(F("hora invalida, ajustando pelo build"));
    rtc.SetDateTime(compileTime);
  } else if (freshConfig) {
    // Config foi renovada (magic byte mudou) - forca re-sync da hora
    Serial.println(F("config renovada, ressincronizando hora pelo build"));
    rtc.SetDateTime(compileTime);
  }

  // Por padrao, o DS1302 vem com write-protect ativo. Precisa desligar
  // para conseguir gravar a hora (e isso ja foi feito acima, mas garantia
  // dupla nao machuca).
  if (rtc.GetIsWriteProtected()) {
    Serial.println(F("  removendo write protect..."));
    rtc.SetIsWriteProtected(false);
  }

  // O CH (Clock Halt) bit pode estar setado se for primeira energizacao;
  // precisa limpar para o oscilador comecar a contar.
  if (!rtc.GetIsRunning()) {
    Serial.println(F("  RTC parado, iniciando..."));
    rtc.SetIsRunning(true);
  }

  // Sanidade extra: se a hora atual e anterior a compilacao, algo esta
  // errado. Reajusta para o build time como fallback.
  RtcDateTime now = rtc.GetDateTime();
  if (now < compileTime) {
    Serial.println(F("  hora antiga, ajustando pelo build"));
    rtc.SetDateTime(compileTime);
  }

  // Verifica final - se conseguiu ler hora valida, RTC esta OK
  if (rtc.IsDateTimeValid()) {
    rtcOk = true;
    char hb[24];
    now = rtc.GetDateTime();
    snprintf(hb, sizeof(hb), "OK [%02d/%02d/%04d %02d:%02d:%02d]",
             now.Day(), now.Month(), now.Year(),
             now.Hour(), now.Minute(), now.Second());
    Serial.println(hb);
  } else {
    rtcOk = false;
    Serial.println(F("FALHA - usando millis() como timestamp"));
  }
  bootMillis = millis();     // ponto zero para fallback de timestamp

  // ---- Animacao de boot ----
  Serial.println(F("\nIniciando animacao de boot...\n"));
  bootAnimation();

  // Pos-boot: o slot 1 (que segurou ICO_BOTTLE2 durante a animacao) e
  // recarregado com chCritico para virar o emoji da zona critica usado
  // no renderHome. BOTTLE2 nao e mais necessario apos esse ponto.
  lcd.createChar(ICO_CRITICO, chCritico);

  // Comeca direto na tela de configuracao (usuario precisa apertar OK ou
  // CANCEL para entrar no modo de monitoramento)
  appMode = MODE_MENU;
  menuIdx = 0;
  if (lcdOk) lcd.clear();

  // Inicializa os timers do loop principal.
  tLastSample = tLastWindow = tLastLog = millis();
  Serial.println(F("Sistema pronto. Loop principal iniciado.\n"));
}

// loop() roda continuamente apos o setup. Nao usa delay() longo - tudo
// e baseado em millis() para nao bloquear leitura de botoes.
void loop() {
  // Detecta transicao de modo para restaurar LED/buzzer ao sair do menu.
  static AppMode lastAppMode = MODE_BOOT;

  // ---- Le botoes e joystick (com debounce interno) ----
  handleButtons();

  // ---- LED RGB e buzzer conforme modo do app ----
  if (appMode == MODE_MENU) {
    setRGB(255, 0);       // vermelho aceso
    setBlue(true);        // + azul = violeta
    noTone(PIN_BUZZER);   // buzzer sempre desligado no menu
  } else {
    setBlue(false);
    if (lastAppMode == MODE_MENU) {
      // Acabou de sair do menu - restaura LED e buzzer conforme alerta atual
      alertApply();
    }
  }
  lastAppMode = appMode;

  unsigned long now = millis();

  // ---- Amostragem de sensores: 1 Hz ----
  if (now - tLastSample >= 1000) {
    tLastSample = now;
    sensorSample();
  }

  // ---- Janela de 10s: consolida medias e atualiza alerta ----
  if (now - tLastWindow >= (unsigned long)WINDOW_S * 1000UL) {
    tLastWindow = now;
    windowFinalize();
    alertApply();
  }

  // ---- Log periodico na EEPROM ----
  maybeLog();

  // ---- Redesenha a tela a cada 300ms (~3 FPS) ----
  // Evita flicker excessivo e reduz trafego I2C.
  if (now - tLastFrame >= 300) {
    tLastFrame = now;
    switch (appMode) {
      case MODE_NORMAL: renderHome(); break;
      case MODE_MENU:   renderMenu(); break;
      case MODE_LOGS:   renderLogs(); break;
      default: break;
    }
  }
}
