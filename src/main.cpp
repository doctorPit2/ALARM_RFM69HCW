#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_now.h>
#include <time.h>

// ============================================================
// RFM12 EMPFÄNGER + ESP-NOW SENDER
// Frequenz: 434,15 MHz | Datenrate: 9600 bps
// Sendet Alarmmeldungen per ESP-NOW an Wetterstation
// ============================================================

// WiFi-Credentials für NTP-Zeitsynchronisation (ANPASSEN!)
const char* ssid = "Glasfaser";
const char* password = "3x3Istneun";

// NTP-Server Konfiguration
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;        // GMT+1 (Mitteleuropäische Zeit)
const int daylightOffset_sec = 3600;    // Sommerzeit (+1 Stunde)

// MAC-Adresse der Wetterstation (ANPASSEN!)
//uint8_t wetterstationMAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t wetterstationMAC[] = {0x14, 0x33, 0x5C, 0x38, 0xD5, 0xD4};

// Pin-Konfiguration
#define RFM12_CS      5   // Chip Select (NSS)
#define RFM12_NIRQ    4   // nIRQ Signal
// Hardware SPI Pins: MOSI=23, MISO=19, SCK=18

// Empfangspuffer
char empfangsString[5];
char collectBuffer[20];  // Buffer zum Sammeln mehrerer Interrupts
int collectCount = 0;
unsigned long lastReceiveTime = 0;

// RFM12 Funktionen
uint16_t rfm12_trans(uint16_t wert);
void rfm12_init();
void rfm12_reset_fifo();

// ESP-NOW Funktionen
void espnow_init();
void sendAlarmViaESPNOW(const char* alarmCode);
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);

// Struktur für ESP-NOW Nachrichten
typedef struct struct_message {
  char alarmType[5];  // "ALVO", "ALHI", "TAVO", "TAHI"
  time_t timestamp;   // Unix-Timestamp (Sekunden seit 1.1.1970)
} struct_message;

struct_message alarmMsg;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("========================================");
  Serial.println("RFM12 Empfänger + ESP-NOW + NTP");
  Serial.println("Frequenz: 434,15 MHz | 9600 bps");
  Serial.println("========================================");
  
  // WiFi verbinden für NTP-Zeitsynchronisation
  Serial.print("Verbinde mit WiFi: ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✓ WiFi verbunden");
    Serial.print("IP-Adresse: ");
    Serial.println(WiFi.localIP());
    
    // NTP-Zeit konfigurieren
    Serial.println("Synchronisiere Zeit mit NTP...");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    
    // Warte auf Zeitsynchronisation
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10000)) {
      Serial.println("✓ Zeit synchronisiert");
      Serial.print("Aktuelle Zeit: ");
      Serial.println(&timeinfo, "%d.%m.%Y %H:%M:%S");
    } else {
      Serial.println("⚠ Zeitsynchronisation fehlgeschlagen!");
    }
  } else {
    Serial.println("\n⚠ WiFi-Verbindung fehlgeschlagen! Zeit nicht synchronisiert.");
  }
  
  // ESP-NOW initialisieren (WiFi bleibt verbunden)
  espnow_init();
  
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
  
  Serial.println("✓ Bereit! Warte auf Daten...");
  Serial.println();
}

void loop() {
  // nIRQ prüfen - LOW bedeutet Interrupt (Daten bereit)
  if (digitalRead(RFM12_NIRQ) == LOW) {
    // Status lesen
    uint16_t status = rfm12_trans(0x0000);
    bool ffit = (status & 0x8000) != 0;  // FIFO hat Daten
    
    if (ffit) {
      // Lese bis zu 10 Bytes (nicht mehr 20)
      int bytesRead = 0;
      while (collectCount < 20 && bytesRead < 10) {
        status = rfm12_trans(0x0000);
        if (status & 0x8000) {  // FIFO hat noch Daten
          uint16_t data = rfm12_trans(0xB000);
          collectBuffer[collectCount++] = (uint8_t)(data & 0xFF);
          bytesRead++;
          delayMicroseconds(200);  // Erhöht auf 200us
        } else {
          break;  // FIFO leer
        }
      }
      
      lastReceiveTime = millis();
      
      // FIFO reset nur wenn wir fertig sind (nach Timeout)
      // NICHT sofort nach jedem Interrupt!
    }
  }
  
  // Wenn 100ms nichts mehr kam, zeige gesammelte Daten  
  if (collectCount > 0 && (millis() - lastReceiveTime) > 100) {
    Serial.print("✓ Empfangen (");
    Serial.print(collectCount);
    Serial.print(" Bytes): ");
    
    for (int i = 0; i < collectCount; i++) {
      Serial.print("0x");
      if ((uint8_t)collectBuffer[i] < 0x10) Serial.print("0");
      Serial.print((uint8_t)collectBuffer[i], HEX);
      Serial.print(" ");
    }
    
    Serial.print("-> \"");
    for (int i = 0; i < collectCount; i++) {
      if (collectBuffer[i] >= 32 && collectBuffer[i] < 127) {
        Serial.print(collectBuffer[i]);
      } else {
        Serial.print(".");
      }
    }
    Serial.println("\"");
    
    // Suche nach bekannten 4-Byte Alarmmeldungen
    bool alarmFound = false;
    char alarmCode[5] = {0};
    
    for (int i = 0; i <= collectCount - 4; i++) {
      // ALVO - Alarm Vorne
      if (collectBuffer[i] == 'A' && collectBuffer[i+1] == 'L' && collectBuffer[i+2] == 'V' && collectBuffer[i+3] == 'O') {
        memcpy(alarmCode, &collectBuffer[i], 4);
        alarmCode[4] = '\0';
        alarmFound = true;
      }
      // ALHI - Alarm Hinten
      else if (collectBuffer[i] == 'A' && collectBuffer[i+1] == 'L' && collectBuffer[i+2] == 'H' && collectBuffer[i+3] == 'I') {
        memcpy(alarmCode, &collectBuffer[i], 4);
        alarmCode[4] = '\0';
        alarmFound = true;
      }
      // TAVO - Tür Alarm Vorne
      else if (collectBuffer[i] == 'T' && collectBuffer[i+1] == 'A' && collectBuffer[i+2] == 'V' && collectBuffer[i+3] == 'O') {
        memcpy(alarmCode, &collectBuffer[i], 4);
        alarmCode[4] = '\0';
        alarmFound = true;
      }
      // TAHI - Tür Alarm Hinten
      else if (collectBuffer[i] == 'T' && collectBuffer[i+1] == 'A' && collectBuffer[i+2] == 'H' && collectBuffer[i+3] == 'I') {
        memcpy(alarmCode, &collectBuffer[i], 4);
        alarmCode[4] = '\0';
        alarmFound = true;
      }
      
      if (alarmFound) {
        Serial.print("  *** ALARM ERKANNT: \"");
        Serial.print(alarmCode);
        Serial.println("\" ***");
        
        // Per ESP-NOW an Wetterstation senden
        sendAlarmViaESPNOW(alarmCode);
        break;  // Nur erstes Muster
      }
    }
    
    // Buffer zurücksetzen
    collectCount = 0;
    
    // Jetzt FIFO reset für nächsten Empfang
    rfm12_reset_fifo();
    rfm12_trans(0x82D9);  // Enable Receiver
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
  Serial.print("ESP-NOW Sendestatus: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Erfolg" : "Fehler");
}

void espnow_init() {
  // WiFi ist bereits im Station Mode und verbunden (für NTP)
  Serial.print("ESP32 MAC-Adresse: ");
  Serial.println(WiFi.macAddress());
  
  // ESP-NOW initialisieren
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Init fehlgeschlagen!");
    return;
  }
  Serial.println("✓ ESP-NOW initialisiert");
  
  // Callback für Sendebestätigung
  esp_now_register_send_cb(OnDataSent);
  
  // Wetterstation als Peer hinzufügen
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, wetterstationMAC, 6);
  peerInfo.channel = 0;  // Auto-Channel
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Fehler beim Hinzufügen der Wetterstation!");
    return;
  }
  
  Serial.print("✓ Wetterstation hinzugefügt: ");
  for (int i = 0; i < 6; i++) {
    Serial.printf("%02X", wetterstationMAC[i]);
    if (i < 5) Serial.print(":");
  }
  Serial.println();
}

void sendAlarmViaESPNOW(const char* alarmCode) {
  // Nachricht vorbereiten
  strncpy(alarmMsg.alarmType, alarmCode, 4);
  alarmMsg.alarmType[4] = '\0';
  alarmMsg.timestamp = time(nullptr);  // Unix-Timestamp
  
  // Per ESP-NOW senden
  esp_err_t result = esp_now_send(wetterstationMAC, (uint8_t *) &alarmMsg, sizeof(alarmMsg));
  
  if (result == ESP_OK) {
    Serial.print("  → ESP-NOW gesendet: ");
    Serial.print(alarmMsg.alarmType);
    
    // Zeit lesbar formatieren
    struct tm* timeinfo = localtime(&alarmMsg.timestamp);
    char timeStr[25];
    strftime(timeStr, sizeof(timeStr), "%d.%m.%Y %H:%M:%S", timeinfo);
    
    Serial.print(" (");
    Serial.print(timeStr);
    Serial.println(")");
  } else {
    Serial.println("  → ESP-NOW Sendefehler!");
  }
}
