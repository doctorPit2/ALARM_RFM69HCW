#include <Arduino.h>
#include <RH_RF69.h>
#include <SPI.h>

// Pin-Konfiguration für ESP32
#define RFM69_CS      5   // Chip Select (NSS)
#define RFM69_INT     4   // Interrupt (DIO0)
#define RFM69_RST     2   // Reset

// Frequenz: 434 MHz
#define RF69_FREQ 434.0

// RFM69 Instanz erstellen
RH_RF69 rf69(RFM69_CS, RFM69_INT);

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("RFM69HCW Receiver - 434 MHz");
  
  // Reset Pin konfigurieren
  pinMode(RFM69_RST, OUTPUT);
  digitalWrite(RFM69_RST, LOW);
  delay(10);
  digitalWrite(RFM69_RST, HIGH);
  delay(10);
  digitalWrite(RFM69_RST, LOW);
  delay(10);
  
  // RFM69 initialisieren
  if (!rf69.init()) {
    Serial.println("RFM69 Init fehlgeschlagen!");
    while (1);
  }
  Serial.println("RFM69 Init erfolgreich!");
  
  // Frequenz setzen
  if (!rf69.setFrequency(RF69_FREQ)) {
    Serial.println("Frequenz konnte nicht gesetzt werden!");
    while (1);
  }
  Serial.print("Frequenz gesetzt auf: ");
  Serial.print(RF69_FREQ);
  Serial.println(" MHz");
  
  // Sendeleistung: -14 bis +20 dBm (für RFM69HCW)
  // Für Empfang nicht kritisch, aber kann gesetzt werden
  rf69.setTxPower(14, true);  // true für HCW/HW Module
  
  // Optional: Verschlüsselung setzen (16 Bytes)
  // uint8_t key[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
  //                   0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  // rf69.setEncryptionKey(key);
  
  Serial.println("Warte auf Daten...");
}

void loop() {
  // Prüfen ob Daten verfügbar sind
  if (rf69.available()) {
    uint8_t buf[RH_RF69_MAX_MESSAGE_LEN];
    uint8_t len = sizeof(buf);
    
    if (rf69.recv(buf, &len)) {
      // Empfangene Daten ausgeben
      Serial.print("Empfangen [");
      Serial.print(len);
      Serial.print(" Bytes]: ");
      
      // Als String ausgeben
      buf[len] = '\0'; // Null-Terminierung
      Serial.println((char*)buf);
      
      // RSSI (Signalstärke) ausgeben
      Serial.print("RSSI: ");
      Serial.println(rf69.lastRssi(), DEC);
      
      Serial.println("---");
    } else {
      Serial.println("Empfang fehlgeschlagen");
    }
  }
  
  delay(10);
}