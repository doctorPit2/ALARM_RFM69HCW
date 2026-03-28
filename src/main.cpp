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

// Serielle Schnittstelle für WetterDach-Empfang
#define RXD0 27   // GPIO für RX (an deinen Pins anpassen!)
#define TXD0 26  // GPIO für TX

#define HC08_SET 16  // Zur Kanal-Umschaltung (falls benötigt)

#define GPS_BAUD 9600

uint8_t receiverMAC[] = {0x14, 0x33, 0x5C, 0x38, 0xD5, 0xD4};

HardwareSerial DachSerial(2);  // UART2 verwenden

// Empfangspuffer
char empfangsString[5];
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
void espnow_init();
void sendAlarmViaESPNOW(const char* alarmCode, unsigned long duration = 0);
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);

// Struktur für ESP-NOW Nachrichten
typedef struct struct_message {
  char alarmType[5];     // "ALVO", "ALHI", "TAVO", "TAHI", "nixx"
  time_t timestamp;      // Unix-Timestamp (Sekunden seit 1.1.1970)
  unsigned long alarmDuration;  // Alarmdauer in Sekunden (0 wenn kein Alarm aktiv war)
} struct_message;

struct_message alarmMsg;


// Datenstruktur für WetterDach (muss identisch mit Empfänger sein!)
struct WetterDach {
  float STX = 0;
  float Speed = 0;
  float Dir = 0;
  float Hum = 0;
  float Taupunkt = 0;
  float Temp = 0;
  float Press = 0;
  float sectic = 0;
  float Regentic = 0;
  float bmp085 = 0;
  float speedinv = 0;
  float calcCheck = 0;
  unsigned long timestamp = 0;
} WetterDach;

// Timer für Alarmdauer-Messung
unsigned long alarmStartTime = 0;  // Zeitpunkt des Alarmbeginns (millis())
bool alarmActive = false;           // True während ein Alarm läuft

void setup() {
  Serial.begin(115200);
  delay(1000);
  
pinMode(HC08_SET, OUTPUT);
  digitalWrite(HC08_SET, LOW);
  delay(200);
  
  // AT-Befehl senden (Kanal 15)
  Serial.println("Sende AT+C015 an HC-08...");
  DachSerial.print("AT+C015\r\n");
  delay(200);
  
  digitalWrite(HC08_SET, HIGH);

  Serial.println("========================================");
  Serial.println("RFM12 Empfänger + ESP-NOW + NTP");
  Serial.println("Frequenz: 434,15 MHz | 9600 bps");
  Serial.println("========================================");
  
  // Peer (Empfänger) registrieren
  esp_now_peer_info_t peerInfo;
  memcpy(peerInfo.peer_addr, receiverMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Fehler beim Hinzufügen des Peers!");
    return;
  }
  
  Serial.println("ESP-NOW Sender bereit!");
 
 
 
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
   if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Init Fehler!");
    return;
  }
  
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
  
  Serial.println("✓ Bereit! Warte auf Daten...");
  Serial.println("  Heartbeat-Überwachung aktiv (Timeout: 5s)");
  Serial.println();
}

void loop() {
  
  // WetterDach-Daten empfangen und weiterleiten
  if (DachSerial.available()) {
    String Wetterdaten = DachSerial.readStringUntil('\n');
    Serial.print("Empfangen: ");
    Serial.print(Wetterdaten);
    
    int length = Wetterdaten.length();
    Serial.print(" (Länge: ");
    Serial.print(length);
    Serial.println(")");
    
    // Parse Wetterdaten in Float-Array
    float wetterArray[20];
    int arrayIndex = 0;
    int startPos = 0;
    
    // Durchlaufe den String und trenne an Semikolons
    for (int i = 0; i <= Wetterdaten.length() && arrayIndex < 20; i++) {
      if (i == Wetterdaten.length() || Wetterdaten.charAt(i) == ';') {
        String valueStr = Wetterdaten.substring(startPos, i);
        wetterArray[arrayIndex] = valueStr.toFloat();
        arrayIndex++;
        startPos = i + 1;
      }
    }
    
    Serial.print("Anzahl Werte: ");
    Serial.println(arrayIndex);
    
    // Aktualisiere globale Wetterdaten-Struktur
    if(arrayIndex >= 12) {
      WetterDach.STX = wetterArray[0];
      WetterDach.Speed = wetterArray[1];
      WetterDach.Dir = wetterArray[2];
      WetterDach.Hum = wetterArray[3];
      WetterDach.Taupunkt = wetterArray[4];
      WetterDach.Temp = wetterArray[5];
      WetterDach.Press = wetterArray[6];
      WetterDach.sectic = wetterArray[7];
      WetterDach.Regentic = wetterArray[8];
      WetterDach.bmp085 = wetterArray[9];
      WetterDach.speedinv = wetterArray[10];
      WetterDach.calcCheck = wetterArray[11];
      WetterDach.timestamp = millis();
      
      Serial.println("✓ WetterDach-Daten aktualisiert!");
      
      // *** Weiterleitung via ESP-NOW ***
      esp_err_t result = esp_now_send(receiverMAC, (uint8_t *)&WetterDach, sizeof(WetterDach));
      
      if (result == ESP_OK) {
        Serial.println("✓ Daten via ESP-NOW gesendet!");
      } else {
        Serial.println("✗ ESP-NOW Sendefehler!");
      }
      
      // Debug-Ausgabe
      Serial.println("\n=== Weitergeleitete Wetter_Dach-Daten ===");
      Serial.printf("Temp: %.2f °C\n", WetterDach.Temp);
      Serial.printf("Luftdruck: %.2f hPa\n", WetterDach.Press);
      Serial.printf("Wind: %.2f km/h\n", WetterDach.Speed);
      Serial.printf("Regen-Ticks: %.0f\n", WetterDach.Regentic);
      Serial.println("========================================\n");
    }
  }
  
  // Mitternachts-Reset für sectic-Zähler
  static int lastResetDay = -1;
  struct tm timeinfo;
  
  if(getLocalTime(&timeinfo)) {
    if(timeinfo.tm_hour == 0 && timeinfo.tm_min == 0 && timeinfo.tm_mday != lastResetDay) {
      Serial.println("\n=== Mitternachts-Reset wird ausgeführt ===");
      Serial.println("Sende 0x1E an DachSerial (2x mit 2 Sekunden Pause)...");
      
      // Ersten Reset senden
      DachSerial.write(0x1E);
      Serial.println("Erste 0x1E gesendet");
      delay(2000);
      
      // Zweiten Reset senden
      DachSerial.write(0x1E);
      Serial.println("Zweite 0x1E gesendet");
      delay(3000);
      
      // Prüfe ob sectic auf 0 zurückgesetzt wurde
      Serial.print("Prüfe sectic-Wert: ");
      Serial.println(WetterDach.sectic, 0);
      
      if(WetterDach.sectic == 0) {
        Serial.println("✓ sectic erfolgreich auf 0 zurückgesetzt!");
      } else {
        Serial.print("✗ Warnung: sectic ist nicht 0, aktueller Wert: ");
        Serial.println(WetterDach.sectic, 0);
      }
      
      lastResetDay = timeinfo.tm_mday;
      Serial.println("=== Mitternachts-Reset abgeschlossen ===\n");
    }
  }
  
  
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
      
      // Sicherheit: Wenn zu viele Bytes im Buffer (mehr als 16), zurücksetzen
      if (collectCount > 16) {
        Serial.println("⚠ Buffer-Overflow! Zurücksetzen...");
        collectCount = 0;
        rfm12_reset_fifo();
        rfm12_trans(0x82D9);  // Enable Receiver
      }
      
      // FIFO reset nur wenn wir fertig sind (nach Timeout)
      // NICHT sofort nach jedem Interrupt!
    }
  }
  
  // Wenn 50ms nichts mehr kam UND mindestens 4 Bytes vorhanden, verarbeite Daten
  if (collectCount >= 4 && (millis() - lastReceiveTime) > 50) {
    
    // Suche nach bekannten 4-Byte Mustern im GESAMTEN Buffer (nicht nur an 4-Byte Grenzen!)
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
          patternPosition = i;
          
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
        
        // Per ESP-NOW an Wetterstation senden (ohne Dauer, da Alarm läuft)
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

void sendAlarmViaESPNOW(const char* alarmCode, unsigned long duration) {
  // Nachricht vorbereiten
  strncpy(alarmMsg.alarmType, alarmCode, 4);
  alarmMsg.alarmType[4] = '\0';
  alarmMsg.timestamp = time(nullptr);  // Unix-Timestamp
  alarmMsg.alarmDuration = duration;   // Alarmdauer in Sekunden
  
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
    
    if (duration > 0) {
      Serial.print(", Dauer: ");
      Serial.print(duration);
      Serial.print("s");
    }
    
    Serial.println(")");
  } else {
    Serial.println("  → ESP-NOW Sendefehler!");
  }
}
