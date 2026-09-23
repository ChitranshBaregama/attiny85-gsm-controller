/*
 * ================================================================
 * ANANT IONS - GSM SYSTEM CONTROLLER v11
 * ATtiny85 @ 8 MHz  +  SIM900A
 * Bit-banged UART @ 9600, 8N1
 *
 * FUNCTION
 *   Boot              -> all LEDs OFF, SMS "GSM READY" to the user
 *
 *   MISSED CALL from the authorised number:
 *     on the 2nd RING the call is CUT immediately, then toggle
 *       -> ON  : D1 ON 5 s, D2 ON , SMS "SYSTEM ON"
 *       -> OFF : D3 ON 5 s, D2 OFF, SMS "SYSTEM OFF"
 *     Calls from any other number are cut and ignored.
 *
 *   SMS "SYSTEM ON"   -> D1 ON 5 s, D2 ON , reply "SYSTEM ON"
 *   SMS "SYSTEM OFF"  -> D3 ON 5 s, D2 OFF, reply "SYSTEM OFF"
 *
 * v11 makes the hang-up actually work:
 *   - AT+CVHU=0 in init. On much SIM900A firmware the default is
 *     CVHU=1, where ATH is ACCEPTED BUT IGNORED for voice calls.
 *     That is the usual reason a phone keeps ringing.
 *   - AT+CHUP (SIMCom's dedicated disconnect) is issued first,
 *     with ATH as a fallback.
 *   - Any RING arriving after the action re-issues the hang-up,
 *     so a single missed command cannot leave the caller ringing.
 * ================================================================
 */

#include <Arduino.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/eeprom.h>
#include <util/delay.h>

#if F_CPU != 8000000UL
  #error "Set ATtiny85 to 8 MHz."
#endif

/* ================= USER CONFIGURATION ================= */

#define USER_NUMBER   "+919876543210"   /* full, for sending */
#define USER_MATCH    "9876543210"      /* last 10 digits    */

#define CAL_BLINK     0     /* 0 = silent boot, 1 = diagnostics */
#define CAL_SPAN      15
#define FORCE_RECAL   0

#define EE_MAGIC_ADDR  0
#define EE_OSCCAL_ADDR 1
#define EE_MAGIC       0x5A

/* ================= PIN MAP (PORTB) ==================== */

#define UART_TX   PB0     /* -> SIM900A RXD              */
#define UART_RX   PB1     /* <- SIM900A TXD  (PCINT1)    */
#define D1        PB2     /* ON  indicator, 5 s pulse    */
#define D2        PB3     /* system state, latched       */
#define D3        PB4     /* OFF indicator, 5 s pulse    */

/* ================= UART TIMING ======================== */

#define BIT_US        104.17
#define TX_BIT_US     (BIT_US - 1.6)
#define RX_BIT_US     (BIT_US - 2.4)
#define RX_START_US   (BIT_US * 1.5 - 6.0)

/* ================= RX RING BUFFER ===================== */

#define RX_BUF_SIZE   64      /* power of two */
#define RX_BUF_MASK   (RX_BUF_SIZE - 1)

static volatile uint8_t rxBuf[RX_BUF_SIZE];
static volatile uint8_t rxHead = 0;
static volatile uint8_t rxTail = 0;

/* ================= TUNABLES =========================== */

#define PULSE_MS        5000UL
#define RINGS_TO_ACT    2           /* cut on the 2nd ring       */
#define RING_STALE_MS   15000UL
#define PROMPT_TRIES    2
#define AT_TRIES        40
#define BOOT_WAIT_MS    12000UL
#define SETTLE_MS       1200UL
#define HEALTH_MS       30000UL
#define DEDUP_MS        15000UL
#define SMS_BODY_TIMEOUT_MS 2000UL

/* ================= AT COMMANDS (PROGMEM) ============== */

const char C_AT[]      PROGMEM = "AT";
const char C_ECHO[]    PROGMEM = "ATE0";
const char C_TEXT[]    PROGMEM = "AT+CMGF=1";
const char C_CSMS[]    PROGMEM = "AT+CSMS=0";
const char C_CSCS[]    PROGMEM = "AT+CSCS=\"GSM\"";
const char C_CSMP[]    PROGMEM = "AT+CSMP=17,167,0,0";
const char C_CSDH[]    PROGMEM = "AT+CSDH=0";
const char C_CLIP[]    PROGMEM = "AT+CLIP=1";
const char C_CVHU[]    PROGMEM = "AT+CVHU=0";   /* let ATH hang up */
const char C_CNMI[]    PROGMEM = "AT+CNMI=2,2,0,0,0";
const char C_CMGDA[]   PROGMEM = "AT+CMGDA=\"DEL ALL\"";
const char C_CREG[]    PROGMEM = "AT+CREG?";
const char C_CHUP[]    PROGMEM = "AT+CHUP";     /* disconnect call */
const char C_ATH[]     PROGMEM = "ATH";
const char C_CMGS[]    PROGMEM = "AT+CMGS=\"" USER_NUMBER "\"";
const char C_CMGD_P[]  PROGMEM = "AT+CMGD=";

const char T_READY[]   PROGMEM = "GSM READY";
const char T_ON[]      PROGMEM = "SYSTEM ON";
const char T_OFF[]     PROGMEM = "SYSTEM OFF";
const char TOK_OK[]    PROGMEM = "OK";
const char TOK_ERR[]   PROGMEM = "ERROR";
const char TOK_CMGS[]  PROGMEM = "+CMGS";

/* ================= STATE ============================== */

static char     line[64];
static uint8_t  lineLen = 0;

static bool     expectBody = false;
static unsigned long smsHeaderMs = 0;

static uint8_t  pendingAction = 0;     /* 0 none, 1 ON, 2 OFF */
static int16_t  pendingDelete = -1;
static bool     pendingHangup = false;
static bool     pendingToggle = false;

/* call tracking */
static uint8_t  ringCount  = 0;
static bool     callerAuth = false;
static bool     callDone   = false;
static unsigned long lastRingMs = 0;

static uint8_t  lastCmd   = 0;
static unsigned long lastCmdMs = 0;

static bool     systemOn    = false;
static bool     registered  = false;
static bool     needsReinit = false;

static uint8_t  pulsePin = 255;
static unsigned long pulseEnd = 0;
static unsigned long lastHealth = 0;

static void pump(unsigned long ms);
static void gsmInit(void);

/* ================================================================
 * BIT-BANGED UART
 * ============================================================== */

static void uartBegin(void)
{
  DDRB  |=  (1 << UART_TX);
  PORTB |=  (1 << UART_TX);

  DDRB  &= ~(1 << UART_RX);
  PORTB |=  (1 << UART_RX);

  PCMSK  =  (1 << UART_RX);
  GIFR  |=  (1 << PCIF);
  GIMSK |=  (1 << PCIE);

  sei();
}

ISR(PCINT0_vect)
{
  if (PINB & (1 << UART_RX)) return;      /* rising edge - ignore */

  GIMSK &= ~(1 << PCIE);

  _delay_us(RX_START_US);

  uint8_t v = 0;
  for (uint8_t i = 0; i < 7; i++) {
    if (PINB & (1 << UART_RX)) v |= (1 << i);
    _delay_us(RX_BIT_US);
  }
  if (PINB & (1 << UART_RX)) v |= 0x80;   /* bit 7 at 8.5 bits */

  uint8_t next = (rxHead + 1) & RX_BUF_MASK;
  if (next != rxTail) {
    rxBuf[rxHead] = v;
    rxHead = next;
  }

  GIFR  |= (1 << PCIF);
  GIMSK |= (1 << PCIE);
}

static void uartWrite(uint8_t data)
{
  uint8_t sreg = SREG;
  cli();

  PORTB &= ~(1 << UART_TX);
  _delay_us(TX_BIT_US);

  for (uint8_t i = 0; i < 8; i++) {
    if (data & 0x01) PORTB |=  (1 << UART_TX);
    else             PORTB &= ~(1 << UART_TX);
    data >>= 1;
    _delay_us(TX_BIT_US);
  }

  PORTB |= (1 << UART_TX);

  SREG = sreg;
  _delay_us(TX_BIT_US);
}

static bool uartAvailable(void) { return (rxHead != rxTail); }

static uint8_t uartRead(void)
{
  uint8_t c = rxBuf[rxTail];
  rxTail = (rxTail + 1) & RX_BUF_MASK;
  return c;
}

static void uartFlushRx(void)
{
  cli();
  rxTail = rxHead;
  sei();
}

static void uartPrint_P(const char *p)
{
  char c;
  while ((c = pgm_read_byte(p++))) uartWrite((uint8_t)c);
}

static void uartCrLf(void)
{
  uartWrite('\r');
  uartWrite('\n');
}

static void uartPrintln_P(const char *p)
{
  uartPrint_P(p);
  uartCrLf();
}

static void uartPrintUint(uint16_t v)
{
  char b[6];
  uint8_t n = 0;
  if (!v) { uartWrite('0'); return; }
  while (v) { b[n++] = '0' + (v % 10); v /= 10; }
  while (n)  uartWrite((uint8_t)b[--n]);
}

/* ================================================================
 * OUTPUT PULSE  - the only thing that ever drives the LEDs
 * ============================================================== */

static void servicePulse(void)
{
  if (pulsePin != 255 && (long)(millis() - pulseEnd) >= 0) {
    PORTB &= ~(1 << pulsePin);
    pulsePin = 255;
  }
}

static void startPulse(uint8_t pin)
{
  if (pulsePin != 255) PORTB &= ~(1 << pulsePin);
  PORTB |= (1 << pin);
  pulsePin = pin;
  pulseEnd = millis() + PULSE_MS;
}

/* ================================================================
 * HELPERS
 * ============================================================== */

static void toUpperStr(char *s)
{
  for (; *s; s++)
    if (*s >= 'a' && *s <= 'z') *s -= 32;
}

static bool lineIsFromUser(void)
{
  const char *start = strchr(line, '"');
  if (!start) return false;
  ++start;
  const char *end = strchr(start, '"');
  if (!end) return false;
  if (*start == '+') ++start;
  const size_t length = (size_t)(end - start);
  const size_t suffix = sizeof(USER_MATCH) - 1;
  if (length < suffix || length > 15) return false;
  for (const char *p = start; p < end; ++p)
    if (*p < '0' || *p > '9') return false;
  return memcmp(end - suffix, USER_MATCH, suffix) == 0;
}

static int16_t parseTrailingIndex(void)
{
  const char *c = strrchr(line, ',');
  if (!c) return -1;
  c++;
  if (*c < '0' || *c > '9') return -1;
  int16_t v = 0;
  while (*c >= '0' && *c <= '9') v = v * 10 + (*c++ - '0');
  return v;
}

/* ================================================================
 * MODEM I/O
 * ============================================================== */

static void gsmCmd(const char *cmdP, unsigned long waitMs)
{
  uartPrintln_P(cmdP);
  pump(waitMs);
}

static bool waitToken(const char *tokP, unsigned long ms)
{
  uint8_t i = 0;
  unsigned long t0 = millis();
  while (millis() - t0 < ms) {
    servicePulse();
    while (uartAvailable()) {
      char c = (char)uartRead();
      if (c == pgm_read_byte(tokP + i)) {
        i++;
        if (!pgm_read_byte(tokP + i)) return true;
      } else {
        i = (c == pgm_read_byte(tokP)) ? 1 : 0;
      }
    }
  }
  return false;
}

static int8_t waitPrompt(unsigned long ms)
{
  uint8_t ei = 0;
  unsigned long t0 = millis();
  while (millis() - t0 < ms) {
    servicePulse();
    while (uartAvailable()) {
      char c = (char)uartRead();
      if (c == '>') return 1;
      if (c == pgm_read_byte(TOK_ERR + ei)) {
        ei++;
        if (!pgm_read_byte(TOK_ERR + ei)) return -1;
      } else {
        ei = (c == pgm_read_byte(TOK_ERR)) ? 1 : 0;
      }
    }
  }
  return 0;
}

static void waitSendOutcome(unsigned long ms)
{
  uint8_t ci = 0, ei = 0;
  unsigned long t0 = millis();
  while (millis() - t0 < ms) {
    servicePulse();
    while (uartAvailable()) {
      char c = (char)uartRead();

      if (c == pgm_read_byte(TOK_CMGS + ci)) {
        ci++;
        if (!pgm_read_byte(TOK_CMGS + ci)) return;
      } else {
        ci = (c == pgm_read_byte(TOK_CMGS)) ? 1 : 0;
      }

      if (c == pgm_read_byte(TOK_ERR + ei)) {
        ei++;
        if (!pgm_read_byte(TOK_ERR + ei)) return;
      } else {
        ei = (c == pgm_read_byte(TOK_ERR)) ? 1 : 0;
      }
    }
  }
}

static void promptBreak(void)
{
  uartWrite(27);                  /* ESC only, no CRLF */
  pump(300);
  uartFlushRx();
}

static bool probeModem(void)
{
  uartFlushRx();
  uartPrintln_P(C_AT);
  return waitToken(TOK_OK, 800UL);
}

static void modemRecover(void)
{
  promptBreak();
  gsmCmd(C_AT,   500);
  gsmCmd(C_TEXT, 500);
  uartFlushRx();
}

/* CHUP is the reliable disconnect on SIMCom; ATH is the fallback */
static void hangUpCall(void)
{
  gsmCmd(C_CHUP, 600);
  gsmCmd(C_ATH,  400);
}

/* ================================================================
 * OSCCAL AUTO-CALIBRATION  - silent unless CAL_BLINK is 1
 * ============================================================== */

#if CAL_BLINK
static void calBlink(uint8_t pin, uint8_t times)
{
  for (uint8_t i = 0; i < times; i++) {
    PORTB |=  (1 << pin); _delay_ms(60);
    PORTB &= ~(1 << pin); _delay_ms(120);
  }
}
#else
  #define calBlink(pin, times)   do { } while (0)
#endif

static bool tryOsccal(uint8_t target)
{
  while (OSCCAL != target) {
    if (OSCCAL < target) OSCCAL++;
    else                 OSCCAL--;
    _delay_us(50);
  }
  _delay_ms(5);
  return probeModem();
}

static bool calibrateOsccal(void)
{
  uint8_t factory = OSCCAL;

  uint8_t lo = (factory > CAL_SPAN)       ? (factory - CAL_SPAN) : 0;
  uint8_t hi = (factory < 255 - CAL_SPAN) ? (factory + CAL_SPAN) : 255;
  if (factory < 0x80 && hi > 0x7F) hi = 0x7F;
  if (factory > 0x7F && lo < 0x80) lo = 0x80;

  if (tryOsccal(factory)) return true;

  for (uint8_t v = factory; v > lo; ) {
    v--;
    calBlink(D3, 1);
    if (tryOsccal(v)) return true;
  }

  for (uint8_t v = lo; v < hi; ) {
    v++;
    calBlink(D3, 1);
    if (tryOsccal(v)) return true;
  }

  OSCCAL = factory;
  return false;
}

static void setupClock(void)
{
#if !FORCE_RECAL
  if (eeprom_read_byte((uint8_t *)EE_MAGIC_ADDR) == EE_MAGIC) {
    uint8_t saved = eeprom_read_byte((uint8_t *)EE_OSCCAL_ADDR);
    if (tryOsccal(saved)) {
      calBlink(D2, 2);
      return;
    }
  }
#endif

  while (!calibrateOsccal())
    pump(2000);

  eeprom_write_byte((uint8_t *)EE_OSCCAL_ADDR, OSCCAL);
  eeprom_write_byte((uint8_t *)EE_MAGIC_ADDR,  EE_MAGIC);
  calBlink(D2, 3);
}

/* ================================================================
 * SMS SEND - sends the message AT MOST ONCE
 * ============================================================== */

static void sendSMS(const char *textP)
{
  for (uint8_t a = 0; a < PROMPT_TRIES; a++) {

    promptBreak();
    uartPrintln_P(C_CMGS);

    int8_t r = waitPrompt(12000UL);

    if (r != 1) {
      /* no '>' means NOTHING was submitted - safe to try again */
      promptBreak();
      if (a + 1 < PROMPT_TRIES) modemRecover();
      continue;
    }

    pump(200);
    uartPrint_P(textP);
    uartWrite(26);                    /* Ctrl-Z */

    waitSendOutcome(45000UL);
    return;
  }
}

/* ================================================================
 * OUTPUTS
 * ============================================================== */

static void applyOutputs(bool on)
{
  systemOn = on;

  if (on) { startPulse(D1); PORTB |=  (1 << D2); }
  else    { startPulse(D3); PORTB &= ~(1 << D2); }
}

/* ================================================================
 * COMMAND QUEUE  (shared by SMS and missed call)
 * ============================================================== */

static void queueCommand(uint8_t cmd)
{
  if (cmd == lastCmd && (millis() - lastCmdMs) < DEDUP_MS) return;

  lastCmd       = cmd;
  lastCmdMs     = millis();
  pendingAction = cmd;
}

/* ================================================================
 * CALL TRACKING
 * ============================================================== */

static void resetCall(void)
{
  ringCount  = 0;
  callerAuth = false;
  callDone   = false;
}

static void tryCallAction(void)
{
  if (callDone)                 return;
  if (ringCount < RINGS_TO_ACT) return;
  if (!callerAuth)              return;   /* wait for +CLIP match */

  callDone      = true;
  pendingHangup = true;                   /* cut it first         */
  pendingToggle = true;                   /* then flip the state  */
}

/* ================================================================
 * LINE PARSER - sets flags only, never transmits
 * ============================================================== */

static void expireSmsBody(void)
{
  if (expectBody && (unsigned long)(millis() - smsHeaderMs) >= SMS_BODY_TIMEOUT_MS)
    expectBody = false;
}

static void onLine(void)
{
  expireSmsBody();
  if (!line[0]) return;

  if (expectBody) {
    expectBody = false;
    toUpperStr(line);
    if (strstr(line, "SYSTEM OFF"))     queueCommand(2);
    else if (strstr(line, "SYSTEM ON")) queueCommand(1);
    return;
  }

  /* ---- incoming call ---- */

  if (!strncmp(line, "RING", 4)) {
    lastRingMs = millis();
    if (ringCount < 200) ringCount++;
    if (callDone) pendingHangup = true;   /* still ringing: cut again */
    else          tryCallAction();
    return;
  }

  if (!strncmp(line, "+CLIP:", 6)) {
    lastRingMs = millis();
    if (lineIsFromUser()) {
      callerAuth = true;
      if (ringCount == 0) ringCount = 1;  /* CLIP can precede RING */
      tryCallAction();
    } else {
      callerAuth    = false;
      callDone      = true;               /* never act on this call */
      pendingHangup = true;
    }
    return;
  }

  if (!strncmp(line, "NO CARRIER", 10) ||
      !strncmp(line, "BUSY", 4)        ||
      !strncmp(line, "NO ANSWER", 9)) {
    resetCall();
    return;
  }

  /* ---- incoming SMS ---- */

  if (!strncmp(line, "+CMT:", 5)) {
    expectBody = lineIsFromUser();
    smsHeaderMs = millis();
    return;
  }

  if (!strncmp(line, "+CMTI:", 6)) {
    pendingDelete = parseTrailingIndex();
    return;
  }

  /* ---- housekeeping ---- */

  if (!strncmp(line, "+CREG:", 6)) {
    const char *c = strchr(line + 6, ',');
    if (c && (c[1] == '1' || c[1] == '5')) registered = true;
    return;
  }

  if (!strcmp(line, "RDY")       ||
      strstr(line, "Call Ready") ||
      strstr(line, "SMS Ready")) {
    needsReinit = true;
    return;
  }
}

/* ================================================================
 * RX PUMP
 * ============================================================== */

static void pump(unsigned long ms)
{
  unsigned long t0 = millis();
  do {
    servicePulse();
    expireSmsBody();
    while (uartAvailable()) {
      char c = (char)uartRead();
      if (c == '\n') {
        line[lineLen] = '\0';
        lineLen = 0;
        onLine();
      } else if (c != '\r') {
        if (lineLen < sizeof(line) - 1) line[lineLen++] = c;
      }
    }
  } while (millis() - t0 < ms);
}

/* ================================================================
 * INIT
 * ============================================================== */

static bool waitForModem(void)
{
  for (uint8_t i = 0; i < AT_TRIES; i++) {
    if (probeModem()) return true;
    pump(400);
  }
  return false;
}

static void gsmInit(void)
{
  promptBreak();
  gsmCmd(C_ECHO,  600);
  gsmCmd(C_TEXT,  600);
  gsmCmd(C_CSMS,  600);
  gsmCmd(C_CSCS,  600);
  gsmCmd(C_CSMP,  600);
  gsmCmd(C_CSDH,  600);
  gsmCmd(C_CLIP,  600);
  gsmCmd(C_CVHU,  600);           /* without this ATH is ignored */
  gsmCmd(C_CNMI,  600);
  gsmCmd(C_CMGDA, 4000);
}

static void waitForNetwork(unsigned long timeoutMs)
{
  unsigned long t0 = millis();
  registered = false;
  while (!registered && (millis() - t0 < timeoutMs))
    gsmCmd(C_CREG, 2000);
}

/* ================================================================
 * SETUP / LOOP
 * ============================================================== */

void setup(void)
{
  DDRB  |= (1 << D1) | (1 << D2) | (1 << D3);
  PORTB &= ~((1 << D1) | (1 << D2) | (1 << D3));

  uartBegin();

  pump(BOOT_WAIT_MS);            /* SIM900A power-up */

  setupClock();                  /* silent */

  while (!waitForModem())
    pump(1000);

  gsmInit();
  waitForNetwork(60000UL);

  systemOn = false;

  sendSMS(T_READY);

  expectBody    = false;
  pendingAction = 0;
  pendingDelete = -1;
  pendingHangup = false;
  pendingToggle = false;
  lastCmd       = 0;
  lastCmdMs     = 0;
  needsReinit   = false;
  lastRingMs    = millis();
  lastHealth    = millis();
  resetCall();
}

void loop(void)
{
  pump(20);

  /* cutting the call has priority over everything else */
  if (pendingHangup) {
    pendingHangup = false;
    hangUpCall();
  }

  if (needsReinit) {
    needsReinit = false;
    gsmInit();
  }

  if (pendingDelete >= 0) {
    uint16_t idx = (uint16_t)pendingDelete;
    pendingDelete = -1;
    uartPrint_P(C_CMGD_P);
    uartPrintUint(idx);
    uartCrLf();
    pump(2000);
  }

  /* a valid missed call becomes an ON/OFF command */
  if (pendingToggle) {
    pendingToggle = false;
    queueCommand(systemOn ? 2 : 1);
  }

  if (pendingAction) {
    uint8_t a = pendingAction;
    pendingAction = 0;

    applyOutputs(a == 1);         /* LEDs immediately */

    pump(SETTLE_MS);
    sendSMS((a == 1) ? T_ON : T_OFF);
    lastHealth = millis();
  }

  if (ringCount > 0 && (millis() - lastRingMs > RING_STALE_MS))
    resetCall();

  if ((millis() - lastHealth >= HEALTH_MS) &&
      !uartAvailable() && !pendingAction && pendingDelete < 0 &&
      ringCount == 0) {
    lastHealth = millis();
    if (!probeModem()) {
      modemRecover();
      if (!probeModem()) gsmInit();
    }
  }
}

/*
 * ================================================================
 * WIRING
 *   PB0 -> SIM900A RXD
 *   PB1 <- SIM900A TXD
 *   PB2 = D1, PB3 = D2, PB4 = D3  (220R to GND)
 *   PB5 = RESET, do not use
 *   GND common - MANDATORY
 *
 * RING SEQUENCE FROM SIM900A
 *   RING / +CLIP: "+91..." / RING / +CLIP: "+91..."
 *   Ring 1 counts, its +CLIP authorises the caller, ring 2 fires
 *   the action - so the cut lands on the 2nd ring.
 *   Set RINGS_TO_ACT to 1 to cut on the first ring instead.
 * ================================================================
 */
