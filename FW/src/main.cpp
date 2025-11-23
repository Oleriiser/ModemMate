#include <Arduino.h>

#define HOST_SERIAL  Serial1
#define MODEM_SERIAL Serial2
#define DEBUG_SERIAL Serial

#define SERIAL_BAUD 115200
#define DETECT_TIMEOUT 3000
#define TERMINATOR ';'

// ========== Structures ==========
struct ATModification {
  const char* command;
  const char* replacement;
};

struct ModemProfile {
  const char* modelID;
  const ATModification* modifications;
  size_t modCount;
};

// ========== Modem Profiles ==========

const ATModification sim7600_mods[] = {
  {"ATZ", "ATZ\nAT+CNMI=2,2,0,0,0\nAT+CMGF=1"}
};

const ATModification bg95_mods[] = {
  {"AT+CSQ", "AT+CSQ?"},
  {"AT+CREG", "AT+CREG?"}
};

const ModemProfile profiles[] = {
  {"SIM7600", sim7600_mods, sizeof(sim7600_mods) / sizeof(sim7600_mods[0])},
  {"BG95",    bg95_mods,    sizeof(bg95_mods) / sizeof(bg95_mods[0])}
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

void forwardResponse() {
  while (MODEM_SERIAL.available()) {
    HOST_SERIAL.write(MODEM_SERIAL.read());
  }
}

// --- Detect Modem Model by "ATI" ---
const ModemProfile* detectModemModel() {
  DEBUG_SERIAL.println("Detecting modem model...");
  MODEM_SERIAL.println("ATI");
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
  DEBUG_SERIAL.print("ATI response: ");
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

// --- Debug command forwarding ---
void processDebugCommand(const String& msg) {
  if (msg.length() < 2) return;

  char prefix = msg.charAt(0);
  String body = msg.substring(1);

  switch (prefix) {
    case 'H':
      DEBUG_SERIAL.print("[DBG→HOST] ");
      DEBUG_SERIAL.println(body);
      HOST_SERIAL.println(body);
      break;

    case 'M':
      DEBUG_SERIAL.print("[DBG→MODEM] ");
      DEBUG_SERIAL.println(body);
      MODEM_SERIAL.println(body);
      break;

    default:
      DEBUG_SERIAL.print("[WARN] Unknown prefix: ");
      DEBUG_SERIAL.println(prefix);
      break;
  }
}

// ========== Setup ==========
void setup() {
  DEBUG_SERIAL.begin(SERIAL_BAUD);
  HOST_SERIAL.begin(SERIAL_BAUD,SERIAL_8N1, D12, D13);
  MODEM_SERIAL.begin(SERIAL_BAUD,SERIAL_8N1, D11, D10);

  delay(1000);
  DEBUG_SERIAL.println("AT Command Proxy starting...");

  activeProfile = detectModemModel();

  if (activeProfile) {
    DEBUG_SERIAL.print("Active profile set to: ");
    DEBUG_SERIAL.println(activeProfile->modelID);
  } else {
    DEBUG_SERIAL.println("No active profile — commands will be passed through unchanged.");
  }
}

// ========== Main Loop ==========
void loop() {
  // From Host → Modem
  while (HOST_SERIAL.available()) {
    char c = HOST_SERIAL.read();
    if (c == '\r' || c == '\n') {
      if (hostBuffer.length() > 0) {
        DEBUG_SERIAL.print("RX Host: ");
        DEBUG_SERIAL.println(hostBuffer);

        const char* mod = findModification(hostBuffer.c_str());
        const char* outCmd = mod ? mod : hostBuffer.c_str();

        if (mod) {
          DEBUG_SERIAL.print("Modified to: ");
          DEBUG_SERIAL.println(outCmd);
        }

        MODEM_SERIAL.println(outCmd);
        hostBuffer = "";
      }
    } else {
      hostBuffer += c;
    }
  }

  // From Modem → Host
  forwardResponse();

  // From Debug Port (Manual)
  while (DEBUG_SERIAL.available()) {
    char c = DEBUG_SERIAL.read();
    if (c == TERMINATOR) {
      if (debugBuffer.length() > 0) {
        processDebugCommand(debugBuffer);
        debugBuffer = "";
      }
    } else {
      debugBuffer += c;
    }
  }
}
