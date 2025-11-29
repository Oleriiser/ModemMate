#include <Arduino.h>

#define HOST_SERIAL  Serial1
#define MODEM_SERIAL Serial2
#define DEBUG_SERIAL Serial

#define SERIAL_BAUD 115200
#define DETECT_TIMEOUT 3000
//#define TERMINATOR ';'
// ========== LED Pin Configuration ==========
#define LED_RED   14
#define LED_GREEN 15
#define LED_BLUE  16

void setLEDColor(uint8_t red, uint8_t green, uint8_t blue) {
  analogWrite(LED_RED, 256-red);
  analogWrite(LED_GREEN, 256-green);
  analogWrite(LED_BLUE, 256-blue);
}

void ledRed() {
  setLEDColor(255, 0, 0);
}

void ledGreen() {
  setLEDColor(0, 255, 0);
}

void ledYellow() {
  setLEDColor(255, 255, 0);
}

void ledOff() {
  setLEDColor(0, 0, 0);
}

// ========== State Machine ==========
enum SystemState {
  STATE_WAIT_POWERUP,
  STATE_DETECT_MODEM,
  STATE_READY,
  STATE_MODEM_DEAD
};

SystemState systemState = STATE_WAIT_POWERUP;
unsigned long lastAliveCheck = 0;
const unsigned long ALIVE_CHECK_INTERVAL = 30000; // Check modem every 10 seconds

// ========== Structures ==========
struct ATModification {
  const char* command;
  const char* replacement;
};

struct ATModificationWithWait {
  const char* command;
  const char* replacement;
  bool waitForOK;
};

struct ModemProfile {
  const char* modelID;
  const char* powerUpMessage;
  const ATModification* modifications;
  size_t modCount;
  const ATModificationWithWait* modificationsWithWait;
  size_t modWithWaitCount;
};

// ========== Modem Profiles ==========

const ATModification sim7600_mods[] = {
  
};
const ATModificationWithWait sim7600_mods_wait[] = {
  {"ATZ", "AT+CNMI=2,2,0,0,0\nAT+CMGF=1\n", true}
};

const ATModification ec200_mods[] = {
  {"AT+CREG", "AT+CREG?"}
};

const ATModificationWithWait ec200_mods_wait[] = {
  {"ATZ", "ATE0\rAT+CNMI=2,2,0,0,0\rAT+CMGF=1\r", true}
};

const ATModification bg95_mods[] = {
  {"AT+CSQ", "AT+CSQ?"},
  {"AT+CREG", "AT+CREG?"}
};
const ATModificationWithWait bg95_mods_wait[] = {
  
};

const ModemProfile profiles[] = {
  {"SIM7600", "RDY", sim7600_mods, sizeof(sim7600_mods) / sizeof(sim7600_mods[0]), sim7600_mods_wait, sizeof(sim7600_mods_wait) / sizeof(sim7600_mods_wait[0])},
  {"BG95",    "RDY", bg95_mods,    sizeof(bg95_mods) / sizeof(bg95_mods[0]), bg95_mods_wait, sizeof(bg95_mods_wait) / sizeof(bg95_mods_wait[0])},
  {"EC200A", "RDY", ec200_mods,  sizeof(ec200_mods) / sizeof(ec200_mods[0]), ec200_mods_wait, sizeof(ec200_mods_wait) / sizeof(ec200_mods_wait[0])}
};

const ModemProfile* activeProfile = nullptr;

// ========== Buffers ==========
String hostBuffer = "";
String debugBuffer = "";

// ========== Helper Functions ==========

const char* findModification(const char* cmd) {
  if (!activeProfile) return nullptr;
  for (size_t i = 0; i < activeProfile->modCount; i++) {
    if (strstr(cmd, activeProfile->modifications[i].command) == cmd) {
      return activeProfile->modifications[i].replacement;
    }
  }
  return nullptr;
}

const ATModificationWithWait* findModificationWithWait(const char* cmd) {
  if (!activeProfile) return nullptr;
  for (size_t i = 0; i < activeProfile->modWithWaitCount; i++) {
    if (strstr(cmd, activeProfile->modificationsWithWait[i].command) == cmd) {
      return &activeProfile->modificationsWithWait[i];
    }
  }
  return nullptr;
}

void forwardResponse() {
  while (MODEM_SERIAL.available()) {
    char b = MODEM_SERIAL.read();
    HOST_SERIAL.write(b);
    DEBUG_SERIAL.print(b);
  }
}

// --- Wait for debug command response (blocking, no forward) ---
void waitForDebugResponse(unsigned long timeout = 500) {
  DEBUG_SERIAL.println("[WAIT] Collecting response...");
  unsigned long start = millis();
  while (millis() - start < timeout) {
    while (MODEM_SERIAL.available()) {
      char c = MODEM_SERIAL.read();
      DEBUG_SERIAL.print(c);
    }
  }
}
// --- Detect Modem Model ---
const ModemProfile* detectModemModel() {
  DEBUG_SERIAL.println("Detecting modem model...");
  MODEM_SERIAL.println("AT+GMM");
  unsigned long start = millis();
  String resp = "";

  while (millis() - start < DETECT_TIMEOUT) {
    while (MODEM_SERIAL.available()) {
      char c = MODEM_SERIAL.read();
      resp += c;
    }
    if (resp.indexOf("OK") >= 0) break;
  }

  resp.trim();
  DEBUG_SERIAL.print("AT+GMM response: ");
  DEBUG_SERIAL.println(resp);

  for (size_t i = 0; i < sizeof(profiles) / sizeof(profiles[0]); i++) {
    if (resp.indexOf(profiles[i].modelID) >= 0) {
      DEBUG_SERIAL.print("Matched profile: ");
      DEBUG_SERIAL.println(profiles[i].modelID);
      return &profiles[i];
    }
  }

  DEBUG_SERIAL.println("No known modem detected; using default behavior.");
  return nullptr;
}
// --- Check if modem is alive ---
bool checkModemAlive() {
  DEBUG_SERIAL.println("[ALIVE CHECK] Sending AT...");
  MODEM_SERIAL.println("AT");
  
  unsigned long start = millis();
  String resp = "";
  
  while (millis() - start < DETECT_TIMEOUT) {
    while (MODEM_SERIAL.available()) {
      char c = MODEM_SERIAL.read();
      resp += c;
      DEBUG_SERIAL.print(c);
    }
    if (resp.indexOf("OK") >= 0) {
      DEBUG_SERIAL.println("\n[ALIVE] Modem is responsive.");

      // If we don't have a profile yet, try to detect one now
      if (!activeProfile) {
        DEBUG_SERIAL.println("[ALIVE] No active profile — attempting to detect profile...");
        const ModemProfile* p = detectModemModel();
        if (p) {
          activeProfile = p;
          DEBUG_SERIAL.print("Active profile set to: ");
          DEBUG_SERIAL.println(activeProfile->modelID);
          ledGreen();
        } else {
          DEBUG_SERIAL.println("No profile matched during alive check; continuing without profile.");
          ledYellow();
        }
      }

      return true;
    }
  }
  
  DEBUG_SERIAL.println("\n[ALIVE FAIL] No response from modem!");
  return false;
}

// --- Wait for modem power up ---
void waitForModemPowerUp() {
  DEBUG_SERIAL.println("Waiting for modem to power up...");
  unsigned long start = millis();
  String resp = "";
  bool found = false;

  // First, try to get a response to AT command
  while (millis() - start < DETECT_TIMEOUT && !found) {
    MODEM_SERIAL.println("AT");
    unsigned long cmdStart = millis();
    
    while (millis() - cmdStart < 1000 && !found) {
      while (MODEM_SERIAL.available()) {
        char c = MODEM_SERIAL.read();
        resp += c;
        DEBUG_SERIAL.print(c);
      }
      if (resp.indexOf("OK") >= 0) {
        found = true;
        DEBUG_SERIAL.println("\n[OK] Modem responded to AT command.");
      }
    }
    resp = "";
    delay(500);
  }

  if (!found) {
    DEBUG_SERIAL.println("Modem did not respond to AT; waiting for power-up message...");
    resp = "";
    start = millis();
    
    while (millis() - start < DETECT_TIMEOUT * 2) {
      while (MODEM_SERIAL.available()) {
        char c = MODEM_SERIAL.read();
        resp += c;
        DEBUG_SERIAL.print(c);
      }
      for (size_t i = 0; i < sizeof(profiles) / sizeof(profiles[0]); i++) {
        if (resp.indexOf(profiles[i].powerUpMessage) >= 0) {
          DEBUG_SERIAL.println("\n[OK] Detected power-up message.");
          found = true;
          break;
        }
      }
      if (found) break;
    }
  }

  if (!found) {
    DEBUG_SERIAL.println("[WARN] Modem power-up timeout; proceeding anyway.");
  }
}

static void writeEscaped(HardwareSerial &ser, const char *src, bool appendCRLFIfNoNL = false) {
  bool sawNewline = false;
  bool sawCtrlZ = false;
  for (const char *p = src; *p; ++p) {
    if (*p == '\\') {
      ++p;
      if (!*p) break;
      if (*p == 'n') { ser.write('\n'); sawNewline = true; }
      else if (*p == 'r') { ser.write('\r'); sawNewline = true; }
      else if (*p == 't') { ser.write('\t'); }
      else if (*p == '\\') { ser.write('\\'); }
      else if (*p == 'x' || *p == 'X') {
        char hi = *(++p);
        char lo = *(++p);
        if (!hi || !lo) break;
        auto hexVal = [](char c)->int {
          if (c >= '0' && c <= '9') return c - '0';
          if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
          if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
          return 0;
        };
        uint8_t val = (hexVal(hi) << 4) | hexVal(lo);
        ser.write((char)val);
        if (val == 0x1A) sawCtrlZ = true;
      } else {
        ser.write('\\');
        ser.write(*p);
      }
    } else if (*p == '^') {
      const char *q = p + 1;
      if (*q == 'Z' || *q == 'z') {
        ser.write((char)0x1A);
        sawCtrlZ = true;
        ++p;
      } else {
        ser.write(*p);
        if (*p == '\r' || *p == '\n') sawNewline = true;
      }
    } else {
      ser.write(*p);
      if (*p == '\r' || *p == '\n') sawNewline = true;
      if ((uint8_t)*p == 0x1A) sawCtrlZ = true;
    }
  }

  // don't append CRLF if we saw a newline or a Ctrl-Z terminator
  if (appendCRLFIfNoNL && !sawNewline && !sawCtrlZ) {
    ser.write('\r');
    ser.write('\n');
  }
}

// --- Debug command forwarding (blocking, no forward to host/modem) ---
void processDebugCommand(const String& msg) {
  if (msg.length() < 2) return;

  char prefix = msg.charAt(0);
  String body = msg.substring(1);

  switch (prefix) {
    case 'H':
      DEBUG_SERIAL.print("[DBG→HOST] ");
      DEBUG_SERIAL.println(body);
      HOST_SERIAL.println(body);
      waitForDebugResponse();
      break;

    case 'M':
      DEBUG_SERIAL.print("[DBG→MODEM] ");
      DEBUG_SERIAL.println(body);
      MODEM_SERIAL.println(body);
      waitForDebugResponse();
      break;

    case 'R':
      DEBUG_SERIAL.println("[CMD] Restarting...");
  #ifdef ESP32
      esp_restart();
  #elif defined(ARDUINO_ARCH_SAMD)
      NVIC_SystemReset();
  #elif defined(ARDUINO_ARCH_AVR)
      wdt_enable(WDTO_15MS);
      while (1) {}
  #else
      DEBUG_SERIAL.println("Reboot of this controller is not supported.");
  #endif
      break;

    default:
      DEBUG_SERIAL.print("[WARN] Unknown prefix: ");
      DEBUG_SERIAL.println(prefix);
      break;
  }
}

// --- State Machine Handler ---
void handleSystemState() {
  static unsigned long lastStateTime = 0;
  static const unsigned long STATE_PROCESS_INTERVAL = 100; // Process state every 100ms

  // Only process state machine periodically to avoid blocking
  if (millis() - lastStateTime < STATE_PROCESS_INTERVAL) {
    return;
  }
  lastStateTime = millis();

  switch (systemState) {
    case STATE_WAIT_POWERUP:
      ledRed();
      DEBUG_SERIAL.println("[STATE] Waiting for modem power up...");
      waitForModemPowerUp();
      systemState = STATE_DETECT_MODEM;
      break;

    case STATE_DETECT_MODEM:
      //ledRed();
      activeProfile = detectModemModel();
      if (activeProfile) {
        DEBUG_SERIAL.print("Active profile set to: ");
        DEBUG_SERIAL.println(activeProfile->modelID);
        ledGreen();
      } else {
        DEBUG_SERIAL.println("No active profile — commands will be passed through unchanged.");
        ledYellow();
      }
      systemState = STATE_READY;
      lastAliveCheck = millis();
      break;

    case STATE_READY:
      // Check modem alive periodically
      if (millis() - lastAliveCheck >= ALIVE_CHECK_INTERVAL) {
        if (!checkModemAlive()) {
          DEBUG_SERIAL.println("[CRITICAL] Modem is dead! Returning to WAIT_POWERUP state...");
          ledRed();
          systemState = STATE_WAIT_POWERUP;
        }
        lastAliveCheck = millis();
      }
      break;

    case STATE_MODEM_DEAD:
      ledRed();
      DEBUG_SERIAL.println("[REBOOT] Rebooting microcontroller...");
      delay(1000);
  #ifdef ESP32
      esp_restart();
  #elif defined(ARDUINO_ARCH_SAMD)
      NVIC_SystemReset();
  #elif defined(ARDUINO_ARCH_AVR)
      wdt_enable(WDTO_15MS);
      while (1) {}
  #else
      DEBUG_SERIAL.println("[ERROR] Reboot not supported on this platform!");
  #endif
      break;
  }
}

// ========== Setup ==========
void setup() {
  DEBUG_SERIAL.begin(SERIAL_BAUD);
  HOST_SERIAL.begin(SERIAL_BAUD, SERIAL_8N1, A0, A2);
  MODEM_SERIAL.begin(SERIAL_BAUD, SERIAL_8N1, A1, A3);
  
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  ledRed();
  
  delay(5000);
  DEBUG_SERIAL.println("AT Command Proxy starting...");
}

// ========== Main Loop ==========
void loop() {
  // State machine handler
  handleSystemState();

  // Only process normal commands if system is READY
  if (systemState == STATE_READY) {
    // From Host → Modem
    while (HOST_SERIAL.available()) {
      char c = HOST_SERIAL.read();
      if (c == '\r' || c == '\n') {
        if (hostBuffer.length() > 0) {
          DEBUG_SERIAL.print("RX Host: ");
          DEBUG_SERIAL.println(hostBuffer);

          const ATModificationWithWait* modWait = findModificationWithWait(hostBuffer.c_str());
          if (modWait) {
            DEBUG_SERIAL.print("Sending: ");
            DEBUG_SERIAL.println(modWait->command);
            // send command (append CRLF if none) and wait for OK
            writeEscaped(MODEM_SERIAL, modWait->command, true);
            
            unsigned long start = millis();
            String resp = "";
            bool gotOK = false;
            while (millis() - start < DETECT_TIMEOUT) {
              while (MODEM_SERIAL.available()) {
                char c = MODEM_SERIAL.read();
                resp += c;
                HOST_SERIAL.write(c);
                DEBUG_SERIAL.print(c);
              }
              if (resp.indexOf("OK") >= 0) {
                gotOK = true;
                break;
              }
            }
            
            if (gotOK) {
              DEBUG_SERIAL.print("Got OK, sending replacement: ");
              DEBUG_SERIAL.println(modWait->replacement);
              // send replacement (append CRLF if none)
              writeEscaped(MODEM_SERIAL, modWait->replacement, true);
            } else {
              DEBUG_SERIAL.println("[WARN] No OK received for command");
            }
          } else {
            const char* mod = findModification(hostBuffer.c_str());
            const char* outCmd = mod ? mod : hostBuffer.c_str();

            if (mod) {
              DEBUG_SERIAL.print("Modified to: ");
              DEBUG_SERIAL.println(outCmd);
            }

            // send host command to modem, support escapes and Ctrl sequences, append CRLF if none
            writeEscaped(MODEM_SERIAL, outCmd, true);
          }
          
          hostBuffer = "";
        }
      } else {
        hostBuffer += c;
      }
    }

    // From Modem → Host
    forwardResponse();
  }

  // From Debug Port (always available, even during state transitions)
  while (DEBUG_SERIAL.available()) {
    char c = DEBUG_SERIAL.read();
    DEBUG_SERIAL.print(c);
    if (c == '\r' || c == '\n') {
      if (debugBuffer.length() > 0) {
        processDebugCommand(debugBuffer);
        debugBuffer = "";
      }
    } else {
      debugBuffer += c;
    }
  }
}