#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// ============================================================
// RFM12 EMPFÄNGER + ESP-NOW SENDER
// Frequenz: 434,15 MHz | Datenrate: 9600 bps
// Empfängt Alarmmeldungen über RFM12 und sendet sie per ESP-NOW
// ============================================================

// ESP-NOW Empfänger MAC-Adressen
uint8_t receiver1MAC[] = {0xB4, 0x3A, 0x45, 0x3F, 0xB4, 0x50};  // 2USB ESP - C
uint8_t receiver2MAC[] = {0x14, 0x33, 0x5C, 0x38, 0xD5, 0xD4};

// Pin-Konfiguration
#define RFM12_CS      5   // Chip Select (NSS)
#define RFM12_NIRQ    4   // nIRQ Signal
// Hardware SPI Pins: MOSI=23, MISO=19, SCK=18

// Empfangspuffer
char collectBuffer[20];  // Buffer zum Sammeln mehrerer Interrupts
int collectCount = 0;
unsigned long lastReceiveTime = 0;
unsigned long lastValidMessageTime = 0;  // Zeitpunkt der letzten gültigen Nachricht (Alarm oder "nixx")
bool timeoutAlarmSent = false;  // Flag, ob Timeout-Alarm bereits gesendet wurde
const unsigned long MESSAGE_TIMEOUT = 5000;  // 5 Sekunden Timeout

// RFM12 Funktionen
uint16_t rfm12_trans(uint16_t wert);
void rfm12_init();
void rfm12_reset_fifo();

// ESP-NOW Funktionen
void sendAlarmViaESPNOW(const char* alarmCode, unsigned long duration = 0);
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);

// Vereinfachte Datenstruktur für ESP-NOW (nur Alarmdaten)
typedef struct AlarmMessage {
  char alarmType[5];           // "ALVO", "ALHI", "TAVO", "TAHI", "nixx", "TOUT"
  unsigned long timestamp;     // millis() Zeitstempel
  unsigned long alarmDuration; // Alarmdauer in Sekunden (0 wenn kein Alarm aktiv)
} AlarmMessage;

AlarmMessage espnowMsg;

// Duplikats-Erkennung für Alarme
char lastSentAlarm[5] = "----";  // Letzter gesendeter Alarm-Code

// Timer für Alarmdauer-Messung
unsigned long alarmStartTime = 0;  // Zeitpunkt des Alarmbeginns (millis())
bool alarmActive = false;           // True während ein Alarm läuft

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("========================================");
  Serial.println("RFM12 Empfänger + ESP-NOW Sender");
  Serial.println("Frequenz: 434,15 MHz | 9600 bps");
  Serial.println("========================================");
  
  // WiFi im STA Mode für ESP-NOW (ohne Verbindung zu Router)
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  
  Serial.print("ESP32 MAC-Adresse: ");
  Serial.println(WiFi.macAddress());
  
  // ESP-NOW initialisieren
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Init Fehler!");
    return;
  }
  Serial.println("✓ ESP-NOW initialisiert");
  
  // Callback für Sendebestätigung
  esp_now_register_send_cb(OnDataSent);
  
  // Empfänger 1 hinzufügen
  esp_now_peer_info_t peerInfo1;
  memset(&peerInfo1, 0, sizeof(peerInfo1));
  memcpy(peerInfo1.peer_addr, receiver1MAC, 6);
  peerInfo1.channel = 0;  // 0 = aktueller Kanal
  peerInfo1.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo1) != ESP_OK) {
    Serial.println("Fehler beim Hinzufügen von Empfänger 1!");
    return;
  }
  Serial.print("✓ Empfänger 1 hinzugefügt: ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", receiver1MAC[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.println();
  
  // Empfänger 2 hinzufügen
  esp_now_peer_info_t peerInfo2;
  memset(&peerInfo2, 0, sizeof(peerInfo2));
  memcpy(peerInfo2.peer_addr, receiver2MAC, 6);
  peerInfo2.channel = 0;  // 0 = aktueller Kanal
  peerInfo2.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo2) != ESP_OK) {
    Serial.println("Fehler beim Hinzufügen von Empfänger 2!");
    return;
  }
  Serial.print("✓ Empfänger 2 hinzugefügt: ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", receiver2MAC[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.println();
  
  // SPI initialisieren
  SPI.begin();  // SCK=18, MISO=19, MOSI=23
  SPI.setDataMode(SPI_MODE0);
  SPI.setBitOrder(MSBFIRST);
  SPI.setFrequency(2000000);  // 2 MHz
  
  // CS und nIRQ Pins
  pinMode(RFM12_CS, OUTPUT);
  digitalWrite(RFM12_CS, HIGH);
  pinMode(RFM12_NIRQ, INPUT_PULLUP);
  
  delay(200);
  
  // RFM12 Software Reset
  Serial.println("RFM12 Software Reset...");
  digitalWrite(RFM12_CS, LOW);
  SPI.transfer16(0xFE00);
  digitalWrite(RFM12_CS, HIGH);
  delay(100);
  
  // RFM12 initialisieren
  Serial.println("Initialisiere RFM12...");
  rfm12_init();
  
  // FIFO reset und Receiver aktivieren
  rfm12_reset_fifo();
  rfm12_trans(0x82D9);  // Enable Receiver
  
  // Initialisiere Heartbeat-Timer
  lastValidMessageTime = millis();
  
  Serial.println("✓ Bereit! Warte auf RFM12-Daten...");
  Serial.println("  Heartbeat-Überwachung aktiv (Timeout: 5s)");
  Serial.println();
}

void loop() {
  
  // nIRQ prüfen - LOW bedeutet Interrupt (Daten bereit)
  if (digitalRead(RFM12_NIRQ) == LOW) {
    // Status lesen
    uint16_t status = rfm12_trans(0x0000);
    bool ffit = (status & 0x8000) != 0;  // FIFO hat Daten
    
    if (ffit) {
      // Lese bis zu 10 Bytes
      int bytesRead = 0;
      while (collectCount < 20 && bytesRead < 10) {
        status = rfm12_trans(0x0000);
        if (status & 0x8000) {  // FIFO hat noch Daten
          uint16_t data = rfm12_trans(0xB000);
          collectBuffer[collectCount++] = (uint8_t)(data & 0xFF);
          bytesRead++;
          delayMicroseconds(200);
        } else {
          break;  // FIFO leer
        }
      }
      
      lastReceiveTime = millis();
      
      // Sicherheit: Wenn zu viele Bytes im Buffer (mehr als 16), zurücksetzen
      if (collectCount > 16) {
        Serial.println("⚠ Buffer-Overflow! Zurücksetzen...");
        collectCount = 0;
        rfm12_reset_fifo();
        rfm12_trans(0x82D9);  // Enable Receiver
      }
    }
  }
  
  // Wenn 50ms nichts mehr kam UND mindestens 4 Bytes vorhanden, verarbeite Daten
  if (collectCount >= 4 && (millis() - lastReceiveTime) > 50) {
    
    // Suche nach bekannten 4-Byte Mustern im GESAMTEN Buffer
    bool alarmFound = false;
    bool heartbeatFound = false;
    char alarmCode[5] = {0};
    int patternPosition = -1;  // Position des gefundenen Musters
    
    // Durchsuche ALLE Positionen im Buffer
    for (int i = 0; i <= collectCount - 4; i++) {
      // nixx - Heartbeat/Lebenszeichen
      if (collectBuffer[i] == 'n' && collectBuffer[i+1] == 'i' && collectBuffer[i+2] == 'x' && collectBuffer[i+3] == 'x') {
        heartbeatFound = true;
        patternPosition = i;
        lastValidMessageTime = millis();
        timeoutAlarmSent = false;  // Reset Timeout-Flag
        Serial.println("  ↻ Heartbeat empfangen (nixx)");
        
        // Timer stoppen wenn Alarm aktiv war
        if (alarmActive) {
          unsigned long alarmDuration = (millis() - alarmStartTime) / 1000;  // Dauer in Sekunden
          alarmActive = false;
          
          Serial.print("  ⏱ Alarm beendet. Dauer: ");
          Serial.print(alarmDuration);
          Serial.println(" Sekunden");
          
          // Sende "nixx" mit Alarmdauer
          memcpy(alarmCode, "nixx", 4);
          alarmCode[4] = '\0';
          
          // Per ESP-NOW senden (mit Dauer)
          sendAlarmViaESPNOW(alarmCode, alarmDuration);
        }
        
        break;  // Heartbeat gefunden, fertig
      }
      // ALVO - Alarm Vorne
      else if (collectBuffer[i] == 'A' && collectBuffer[i+1] == 'L' && collectBuffer[i+2] == 'V' && collectBuffer[i+3] == 'O') {
        memcpy(alarmCode, &collectBuffer[i], 4);
        alarmCode[4] = '\0';
        patternPosition = i;
        alarmFound = true;
      }
      // ALHI - Alarm Hinten
      else if (collectBuffer[i] == 'A' && collectBuffer[i+1] == 'L' && collectBuffer[i+2] == 'H' && collectBuffer[i+3] == 'I') {
        memcpy(alarmCode, &collectBuffer[i], 4);
        alarmCode[4] = '\0';
        patternPosition = i;
        alarmFound = true;
      }
      // TAVO - Tür Alarm Vorne
      else if (collectBuffer[i] == 'T' && collectBuffer[i+1] == 'A' && collectBuffer[i+2] == 'V' && collectBuffer[i+3] == 'O') {
        memcpy(alarmCode, &collectBuffer[i], 4);
        alarmCode[4] = '\0';
        patternPosition = i;
        alarmFound = true;
      }
      // TAHI - Tür Alarm Hinten
      else if (collectBuffer[i] == 'T' && collectBuffer[i+1] == 'A' && collectBuffer[i+2] == 'H' && collectBuffer[i+3] == 'I') {
        memcpy(alarmCode, &collectBuffer[i], 4);
        alarmCode[4] = '\0';
        patternPosition = i;
        alarmFound = true;
      }
      
      if (alarmFound) {
        Serial.print("  *** ALARM ERKANNT: \"");
        Serial.print(alarmCode);
        Serial.println("\" ***");
        
        // Update Heartbeat-Timer auch bei Alarmen
        lastValidMessageTime = millis();
        timeoutAlarmSent = false;  // Reset Timeout-Flag
        
        // Timer starten beim ERSTEN Alarm
        if (!alarmActive) {
          alarmStartTime = millis();
          alarmActive = true;
          Serial.println("  ⏱ Alarm-Timer gestartet");
        }
        
        // Per ESP-NOW senden (ohne Dauer, da Alarm läuft)
        sendAlarmViaESPNOW(alarmCode, 0);
        break;  // Nur erstes Muster
      }
    }
    
    // Buffer-Verwaltung: Verwerfe Daten VOR dem Muster, behalte Daten NACH dem Muster
    if (patternPosition >= 0) {
      // Muster gefunden - verwerfe alles davor und das Muster selbst (4 Bytes)
      int bytesToRemove = patternPosition + 4;
      int remainingBytes = collectCount - bytesToRemove;
      
      if (remainingBytes > 0) {
        // Verschiebe übrige Bytes an den Anfang
        for (int i = 0; i < remainingBytes; i++) {
          collectBuffer[i] = collectBuffer[bytesToRemove + i];
        }
        collectCount = remainingBytes;
      } else {
        collectCount = 0;  // Alles verarbeitet
      }
    } else {
      // Kein Muster gefunden - behalte nur die letzten 3 Bytes (könnte Start eines Musters sein)
      if (collectCount > 3) {
        for (int i = 0; i < 3; i++) {
          collectBuffer[i] = collectBuffer[collectCount - 3 + i];
        }
        collectCount = 3;
      }
      // Sonst behalte alles
    }
    
    // Jetzt FIFO reset für nächsten Empfang
    rfm12_reset_fifo();
    rfm12_trans(0x82D9);  // Enable Receiver
  }
  
  // Timeout-Überwachung: Wenn 5 Sekunden nichts empfangen wurde
  if (!timeoutAlarmSent && (millis() - lastValidMessageTime) > MESSAGE_TIMEOUT) {
    Serial.println("\n*** TIMEOUT! Keine Nachricht seit 5 Sekunden! ***");
    Serial.println("  → Sende Timeout-Alarm über ESP-NOW\n");
    
    sendAlarmViaESPNOW("TOUT", 0);  // TOUT = Timeout, keine Dauer
    timeoutAlarmSent = true;  // Nur einmal senden bis wieder eine Nachricht kommt
  }
  
  delay(1);  // Kurze Pause
}

// ============================================================
// RFM12 Funktionen
// ============================================================

uint16_t rfm12_trans(uint16_t wert) {
  digitalWrite(RFM12_CS, LOW);
  delayMicroseconds(1);
  
  uint8_t high = (wert >> 8) & 0xFF;
  uint8_t low = wert & 0xFF;
  
  uint8_t reply_high = SPI.transfer(high);
  uint8_t reply_low = SPI.transfer(low);
  
  uint16_t reply = (reply_high << 8) | reply_low;
  
  delayMicroseconds(1);
  digitalWrite(RFM12_CS, HIGH);
  delayMicroseconds(5);
  
  return reply;
}

void rfm12_init() {
  const uint16_t initData[] = {
    0x80D7,  // Config
    0x82D9,  // Enable RX
    0xA67C,  // Frequenz 434,15 MHz
    0xC623,  // Datenrate 9600 bps
    0x90C0,  // LNA Gain + Bandwidth
    0xC2AC,  // Filter
    0xCAC3,  // FIFO: al=1 Fill Always, Level=3 (4 Bytes=32 Bits), Sync OFF!
    0xC483,  // AFC
    0x9820,  // Deviation
    0xE000,  // WakeUp
    0xC800,  // Duty
    0xC000,  // Battery
    0xCED4,  // Sync Pattern (wird ignoriert wenn FIFO Sync=OFF)
    0xCC57,  // PLL
    0x0000   // Status
  };
  
  for (int i = 0; i < 15; i++) {
    rfm12_trans(initData[i]);
    delay(10);
  }
  
  Serial.println("  Init abgeschlossen (FIFO Level=4 Bytes)");
}

void rfm12_reset_fifo() {
  rfm12_trans(0xCA80);  // FIFO disable
  delayMicroseconds(10);
  rfm12_trans(0xCA83);  // FIFO enable
  delay(2);
}

// ============================================================
// ESP-NOW Funktionen
// ============================================================

void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("  -> Gesendet an ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", mac_addr[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.print(" - Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Erfolg" : "Fehler");
}

void sendAlarmViaESPNOW(const char* alarmCode, unsigned long duration) {
  
  // ** DUPLIKATS-FILTER **
  // Nur senden wenn:
  // 1. Alarm sich geändert hat ODER
  // 2. Es eine Endmeldung mit Dauer ist (duration > 0)
  if (strcmp(alarmCode, lastSentAlarm) == 0 && duration == 0) {
    Serial.print("  ⊗ Duplikat ignoriert: ");
    Serial.println(alarmCode);
    return;  // Gleicher Alarm wie vorher, nicht nochmal senden
  }
  
  // Nachricht vorbereiten
  strncpy(espnowMsg.alarmType, alarmCode, 4);
  espnowMsg.alarmType[4] = '\0';
  espnowMsg.timestamp = millis();
  espnowMsg.alarmDuration = duration;
  
  // Speichere als "letzter gesendeter Alarm"
  strncpy(lastSentAlarm, alarmCode, 5);
  
  // Serial Monitor Ausgabe
  Serial.print("\n=== ALARM SENDEN: ");
  Serial.print(espnowMsg.alarmType);
  Serial.println(" ===");
  Serial.print("Zeitstempel: ");
  Serial.print(espnowMsg.timestamp);
  Serial.println(" ms");
  
  if (duration > 0) {
    Serial.print("Alarmdauer: ");
    Serial.print(duration);
    Serial.println(" Sekunden");
  }
  
  // An beide Empfänger senden
  Serial.println("\nSende per ESP-NOW...");
  
  esp_err_t result1 = esp_now_send(receiver1MAC, (uint8_t *)&espnowMsg, sizeof(espnowMsg));
  if (result1 != ESP_OK) {
    Serial.println("  ✗ Sendefehler an Empfänger 1!");
  }
  
  delay(10);  // Kurze Pause zwischen Sendungen
  
  esp_err_t result2 = esp_now_send(receiver2MAC, (uint8_t *)&espnowMsg, sizeof(espnowMsg));
  if (result2 != ESP_OK) {
    Serial.println("  ✗ Sendefehler an Empfänger 2!");
  }
  
  Serial.println("========================================\n");
}
