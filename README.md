# bitSolarMonitor

Ein kleiner PV-Monitor für den Waveshare ESP32-S3-Touch-AMOLED-1.8. Er liest
Shelly-Geräte direkt im lokalen Netz aus und zeigt, was die Photovoltaik-Anlage
gerade produziert. Ohne Cloud, ohne Konto, ohne Zwischenserver.

Wie das Projekt entstanden ist, steht ausführlich im Blogbeitrag:
**[bitSolarMonitor auf bitlager.de](BLOG_POST_URL)**. Weitere ESP32-Projekte
gibt es auf der [Projektseite](https://blog.bitlager.de/esp32/).

## Was es anzeigt

Drei Seiten, gewischt wird mit dem Finger. Drei Punkte am unteren Rand zeigen,
wo man gerade ist.

| Erzeugung | Tagesverlauf | Status |
|:---:|:---:|:---:|
| ![Hauptseite mit Erzeugungsring](docs/images/seite1-erzeugung.jpg) | ![Tagesverlauf als Kurve](docs/images/seite2-tagesverlauf.jpg) | ![Statusseite mit Netzspannung und WLAN](docs/images/seite3-status.jpg) |
| Aktuelle Leistung im Ring, dazu Netz und Versorgungslage. Oben Temperatur und Uhrzeit. | Ertrag und Verlauf über 24 Stunden im Fünf-Minuten-Raster. | Netzspannung je Phase, WLAN, Laufzeit, gefundene Geräte. |

Die Aufnahmen entstanden am Abend, deshalb 0 Watt und eine noch leere Kurve.

Der obere der beiden Knöpfe schaltet die Helligkeit in drei Stufen durch
(25, 50, 100 Prozent). Nach 30 Sekunden ohne Berührung dimmt das Display auf
ein Viertel herunter, ganz aus geht es nie.

## Ausbaustufen

Das Projekt funktioniert auch mit nur einem Gerät. Was fehlt, wird nicht
angezeigt, statt eine Fehlermeldung zu produzieren.

1. **Shelly Plug** am Wechselrichter. Zeigt die Erzeugung.
2. **Plus Shelly Pro 3EM** am Netzübergabepunkt. Zeigt Netzbezug und Einspeisung.
3. **Plus Speicherdaten.** Noch nicht umgesetzt.

## Voraussetzungen

* Waveshare ESP32-S3-Touch-AMOLED-1.8 (16 MB Flash, 8 MB PSRAM)
* Ein Shelly mit Leistungsmessung, etwa Outdoor Plug S oder Plug S
* **USB-C auf USB-C Datenkabel.** Ein reines Ladekabel reicht nicht.
* WLAN im 2,4-GHz-Band
* ESP-IDF v5.5 oder neuer

## Installation

ESP-IDF einmalig einrichten:

```bash
git clone -b v5.5.5 --depth 1 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf
cd ~/esp/esp-idf && ./install.sh esp32s3
```

Projekt holen, konfigurieren, flashen:

```bash
git clone https://github.com/thisdev/bitSolarMonitor.git
cd bitSolarMonitor
cp local.defaults.example local.defaults    # WLAN eintragen
source activate.sh
idf.py flash monitor
```

`activate.sh` sucht ESP-IDF an den üblichen Stellen, stellt bei Bedarf eine
passende Python-Version voran und ermittelt den Port des angeschlossenen
Boards.

## Konfiguration

Alles Persönliche steht in `local.defaults`, die per `.gitignore` außen vor
bleibt. Als Vorlage dient `local.defaults.example`.

```ini
CONFIG_PVMON_WIFI_SSID="MeinWLAN"
CONFIG_PVMON_WIFI_PASS="MeinPasswort"

# Maximale Leistung am Messpunkt. Skaliert Ring und Kurve.
CONFIG_PVMON_INVERTER_LIMIT_W=800

# Stufe 2
CONFIG_PVMON_EM_ENABLE=y

# Sitzt ein Batteriespeicher zwischen dem gemessenen Wechselrichter und dem
# Netzübergabepunkt? Dann ist der Hausverbrauch nicht ableitbar.
CONFIG_PVMON_STORAGE_BETWEEN=y
```

Die IP-Adressen der Shellys müssen nicht eingetragen werden. Der Monitor sucht
sie per mDNS und ordnet sie danach zu, **wie sie antworten**, nicht nach
Modellnummer:

* antwortet auf `EM.GetStatus` mit `total_act_power` → Energiezähler
* antwortet auf `Switch.GetStatus` mit `apower` → Plug

Damit funktioniert die Erkennung auch mit Modellen, die es beim Schreiben
dieses Codes noch nicht gab. Eine fest eingetragene Adresse hat Vorrang.

## Aufbau

```
main/
  bitsolarmonitor.c   Ablauf: Anzeige starten, WLAN, Geräte suchen, Abrufschleife
  energy.c/h      Datenmodell. Jeder Messwert weiß selbst, ob er gilt
  shelly.c/h      HTTP-Abruf, JSON, Gerätesuche per mDNS
  net.c/h         WLAN-Verbindung
  clock.c/h       Uhrzeit per NTP
  history.c/h     Tagesverlauf im Fünf-Minuten-Raster, Ertragsrechnung
  ui.c/h          Oberfläche mit LVGL, drei Seiten
  backlight.c/h   Helligkeitsstufen und Ruheabsenkung
```

Der Kern ist ein Messwert, der seine eigene Gültigkeit kennt:

```c
typedef struct {
    bool  valid;
    float value;
} measurement_t;
```

Die Oberfläche zeigt nur, was gilt. Deshalb braucht keine Ausbaustufe eine
Sonderbehandlung in der Anzeige.

## Bekannte Einschränkungen

* **Hausverbrauch bei Speicheranlagen.** Sitzt ein Speicher zwischen Messpunkt
  und Netzzähler, puffert er dazwischen. Ladeleistung und Verbrauch sind dann
  nicht zu trennen, der Hausverbrauch ist rechnerisch nicht zugänglich. Statt
  einer falschen Zahl zeigt das Gerät, ob die Anlage das Haus gerade trägt.
* **Ertrag heißt "seit Start"**, bis das Gerät einen Mitternachtswechsel
  miterlebt hat. Der Shelly Plug führt keine Tageshistorie.
* **Der Tagesverlauf lebt im Arbeitsspeicher.** Ein Neustart löscht die Kurve.
* **Seitenwechsel direkt nach dem Start** reagiert gelegentlich noch nicht.
* **Zugangsdaten sind einkompiliert.** Ein fertiges Abbild darf deshalb nicht
  weitergegeben werden, es enthält WLAN-Name und Passwort im Klartext.

## Lizenz

MIT, siehe [LICENSE](LICENSE).
