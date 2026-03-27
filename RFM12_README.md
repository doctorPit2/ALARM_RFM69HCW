# RFM12 Alarm Empfänger für ESP32

**Portiert von ATmega8 Bascom-Code:** `Innen_Alarm_ELV_Modul.bas`

## 📡 Technische Daten

- **Funkmodul:** RFM12B
- **Frequenz:** 434,15 MHz
- **Datenrate:** 9600 bps
- **Controller:** ESP32 (DevKit v1)
- **Framework:** Arduino + RadioHead Library

---

## 🔌 Verdrahtung ESP32 ↔ RFM12

### RFM12 SPI-Verbindung
| RFM12 Pin | ESP32 Pin | Beschreibung       |
|-----------|-----------|-------------------|
| SDO       | GPIO 19   | MISO (Master In)  |
| nIRQ      | GPIO 4    | Interrupt         |
| SCK       | GPIO 18   | SPI Clock         |
| SDI       | GPIO 23   | MOSI (Master Out) |
| nSEL      | GPIO 5    | Chip Select (CS)  |
| VDD       | 3V3       | Versorgung 3,3V   |
| GND       | GND       | Masse             |

> **⚠️ WICHTIG:** RFM12 mit **3,3V** betreiben, NICHT 5V!

### Alarm-Ausgänge
| Funktion        | ESP32 Pin | Beschreibung                    |
|-----------------|-----------|--------------------------------|
| LED_FUNK        | GPIO 25   | Blinkt bei Datenempfang        |
| LED_STAT        | GPIO 26   | Status-LED (Alarm enabled)     |
| BEEP            | GPIO 14   | Beeper bei Sabotage            |
| MUTE_SOUND      | GPIO 12   | Mute für Sound-Modul           |
| SOUND1          | GPIO 13   | Trigger Sound 1 (Hund1)        |
| ENABLE_SOUND    | GPIO 32   | Enable Sound-Modul (LOW=aktiv) |
| SOUND2          | GPIO 33   | Trigger Sound 2 (Hund2)        |

### Alarm-Eingänge
| Funktion | ESP32 Pin | Beschreibung           |
|----------|-----------|------------------------|
| TASTER   | GPIO 27   | Alarm EIN/AUS (Pullup) |

---

## 📨 Protokoll - Empfangene Befehle

Der Empfänger erwartet **4-Byte ASCII-Pakete** vom RFM12-Sender:

| Befehl   | Bedeutung                     | Aktion                        |
|----------|-------------------------------|-------------------------------|
| `"nixx"` | Kein Alarm                    | Toggle Mute                   |
| `"TAVO"` | Tamper/Sabotage vorne         | Sabotage-Flag setzen          |
| `"TAHI"` | Tamper/Sabotage hinten        | Sabotage-Flag setzen          |
| `"ALVO"` | Alarm vorne                   | Startet ALVO-Timer (2s)       |
| `"ALHI"` | Alarm hinten                  | Startet ALHI-Timer (2s)       |
| `"dark"` | Dunkelheit erkannt            | Reset Timer                   |

---

## ⏱️ Timing & Ablauf

### Alarm-Verzögerung
1. **ALVO/ALHI empfangen** → Signal muss **2 Sekunden** anstehen
2. Wenn 2s erreicht → Flag wird gesetzt
3. **3 Sekunden Verzögerung** → Dann wird Sound abgespielt
4. **Sound läuft 40 Sekunden** (Hund bellt)
5. Nach 40s → Bereit für neuen Alarm

### Sabotage-Beep
- Bei Sabotage: **Beep alle 2 Sekunden** + SOUND1 Toggle

---

## 🔊 Sound-Modul Ansteuerung

Das Projekt verwendet ein **MP3-Sound-Modul** (z.B. DFPlayer Mini oder ähnlich):

```
ENABLE_SOUND = LOW  → Modul einschalten
SOUND1 = LOW        → Sound 1 triggern (900ms)
SOUND2 = LOW        → Sound 2 triggern (900ms)
ENABLE_SOUND = HIGH → Modul ausschalten
```

**Sound-Dateien:**
- **Sound 1:** Hund bellt (Alarm vorne)
- **Sound 2:** Hund bellt anders (Alarm hinten)

---

## 🛠️ Installation & Kompilierung

### 1. PlatformIO Projekt compilieren
```bash
pio run
```

### 2. Auf ESP32 hochladen
```bash
pio run --target upload
```

### 3. Seriellen Monitor öffnen
```bash
pio device monitor
```

---

## 🧪 Test & Debugging

### Erwartete Ausgabe im Serial Monitor:
```
========================================
RFM12 ALARM Empfänger - ESP32
Frequenz: 434,15 MHz | 9600 bps
========================================
✓ RFM12 Init erfolgreich!
✓ Frequenz: 434,15 MHz
✓ Warte auf Daten...

📡 Empfangen: ALVO
  → ALVO: Alarm vorne
🐕 Spiele Hund1...
```

### Fehlerbehebung

| Problem                  | Lösung                                      |
|--------------------------|---------------------------------------------|
| RFM12 Init fehlgeschlagen | Verdrahtung prüfen, CS/SCK/MOSI/MISO        |
| Keine Daten empfangen    | Frequenz prüfen (434,15 MHz), Sender aktiv? |
| Falsche Daten            | Datenrate prüfen (9600 bps)                 |

---

## 🔄 Kompatibilität mit ATmega8 Sender

Dieser Code ist **100% kompatibel** mit dem Original-Bascom-Sender:
- ✅ Gleiche Frequenz: 434,15 MHz
- ✅ Gleiche Datenrate: 9600 bps
- ✅ Gleiche RFM12-Konfiguration
- ✅ Gleiche Befehlsstruktur (4 Bytes)

---

## 📝 Original Bascom-Code

Der Original-Code befindet sich in: `Innen_Alarm_ELV_Modul.bas`

---

## 📚 Verwendete Bibliotheken

- **RadioHead v1.120** - RFM12 Support
  - Driver: `RH_RF12`
  - https://www.airspayce.com/mikem/arduino/RadioHead/

---

## ⚠️ Wichtige Hinweise

1. **RFM12 vs. RFM69:** Dies ist NICHT kompatibel mit RFM69-Modulen!
2. **Spannung:** RFM12 nur mit 3,3V betreiben
3. **Antenne:** Unbedingt Antenne anschließen (1/4 Lambda ≈ 17,3 cm für 434 MHz)
4. **Reichweite:** Ohne Antenne nur wenige Meter!

---

**Viel Erfolg! 🎉**
