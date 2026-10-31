# Olimex ESP32-EVB (LAN) – Variante für die Weishaupt WTC-GW

Dieser Fork ergänzt das Projekt [jvejmelka/weishaupt-wem-can-esphome](https://github.com/jvejmelka/weishaupt-wem-can-esphome)
um eine fertige Konfiguration für das **Olimex ESP32-EVB** (Ethernet statt WLAN, CAN-Transceiver onboard) und um
Funktionen, die eine Home-Assistant-Integration über die WEM-JSON-Schnittstelle bietet – hier **direkt per CAN-Bus**.
Die Firmware-Komponente `components/weishaupt_can/` und alle Dateien des Originals sind unverändert. Neu sind nur:

| Datei | Inhalt |
|---|---|
| `olimex-weishaupt.yaml` | **eine** YAML mit allem (Pakete des Originals zusammengeführt und angepasst, dazu die Ergänzungen unten) |
| `weishaupt-helfer.h` | C++-Hilfsfunktionen: kraiz-Werte per CAN, Wertetexte, Warn-/Fehlercodes als Klartext |
| `secrets-olimex.yaml.example` | Vorlage für `secrets.yaml` |
| `werkzeuge/kraiz-per-can/` | Skripte (Node.js), mit denen die kraiz-Objekte in CAN-Adressen umgerechnet wurden |

## Unterschiede zum Original

- **Ethernet** (LAN8720: MDC GPIO23, MDIO GPIO18, Takt extern auf GPIO0, PHY-Power GPIO12) statt WLAN. Alle
  WLAN-Entitäten des Pakets `kessel` (WLAN-Wechsel, SSID, Signal) entfallen.
- **CAN-Pins** des Olimex ESP32-EVB: TX **GPIO5**, RX **GPIO35** (laut [Schaltplan Rev I](https://github.com/OLIMEX/ESP32-EVB/files/11994875/ESP32-EVB_Rev_I_Schematic.pdf)).
- **Kein SD-Kartenleser und kein LittleFS** (`sd_karte: false`, `dateisystem: false`; das Board hat nur 4 MB Flash).
- Aktive Funktionen des Originals: `kessel`, `warmwasser`, `wem-schalten` (Schalten per CAN ist ab Werk an),
  `warmwasser-schalten`. Nicht eingebunden: `mqtt`, `app`, `verdacht`, `zusatz`, `json-lesen`.
- Verbindung zu Home Assistant über die ESPHome-API (kein MQTT).

## Ergänzungen

### Alle Werte der kraiz-Integration per CAN (`kraiz-per-can`)
Die JSON-Objekte der Integration [kraiz/hassio-weishaupt](https://github.com/kraiz/hassio-weishaupt) liegen am CAN-Bus
je Modul um einen festen Betrag verschoben (siehe `PROTOKOLL.md`, „Abbildung JSON-Objekt → CAN-Objekt"):

| JSON-Modul | CAN-Knoten | Versatz |
|---|---|---|
| MI 01 (SYSTEM0) | 1 | +0x100 |
| MI 02 (Heizkreis HZK0) | 1 | +0x400 |
| MI 03 (Warmwasser WW0) | 1 | +0x500 |
| MI 07 (Kessel WE0) | 2 | gleich |
| MI 09 / MX 01 (Kessel-Gerätemodul) | 2 | +0x100 |

Darüber werden 29 Werte gelesen, die das Original nicht liefert (Raum-/Vorlaufsollwerte Komfort/Normal/Absenk,
Status Heizkreis und Warmwasser, Betriebsphase WTC, Kaskade u. a.). Module, die die Anlage nicht hat (Heizkreis 2/3, Solar),
sind nicht enthalten. Die Abfrage läuft nur bei eingeschaltetem Schalter „Eigene CAN-Anfragen", höchstens eine Anfrage
alle 3 s, jedes Objekt höchstens alle 1 bis 10 Minuten; vom Knoten abgelehnte Objekte werden nicht erneut gefragt.

### Warmwasser-Push
Ein Knopf schreibt `1` auf `0x2A49/2` des WEM (JSON `MI 03 / 0x2549/2` wie bei kraiz). Die Schreib-Schutzfunktionen des
Originals (Sperrminute, Kontrolle am Bus) gelten auch hier. Nur an einer Anlage erprobt.

### Fehlerblock und Fehler-Klartext
Der 8-Byte-Fehlerblock des WEM (`0x273F`) wird alle 2 Minuten gelesen: Status, Fehlernummer, Modul und ein Klartext.
Dazu sieht die Konfiguration CANopen-Emergency-Telegramme (`0x081`/`0x082`). Der Klartext stammt aus **Kapitel 10 der
Montage- und Betriebsanleitung Gas-Brennwertgerät WTC-GW 15 … 32-B (Art.-Nr. 83307201, 1/2022-04)**, abgetippt
(nur die Ursache, ohne Behebung). Nummern, die das Handbuch je nach Modul mehrfach belegt, zeigt der Klartext mit allen
Varianten. **Ungeprüft:** ob die Nummer im Fehlerblock der Handbuchnummer entspricht und ob das Typ-Bit „Fehler" von
„Warnung" trennt; eine echte Störung lag noch nicht vor. Rechte am Handbuchtext liegen bei Weishaupt.

### Dallas-Temperaturfühler (DS18B20)
Fünf Fühler parallel an **GPIO16** (UEXT Pin 5, I²C-SCL): Umgebung, Warmwasser-Speicher oben, Vorlauf, Rücklauf
Heizkreis 1 und 2. **Die Adressen in `olimex-weishaupt.yaml` sind die des Autors – ersetzen!** Ein Tisch-Test mit Fühlern
ohne Adresse (`index: 0 …`) schreibt die Adressen beim Start ins Log; Zuordnung durch Anwärmen. Empfohlen: externer
4,7-kΩ-Pull-up gegen 3,3 V.

## Installation

1. `olimex-weishaupt.yaml`, `weishaupt-helfer.h`, den Ordner `components/` (aus diesem Fork) in das ESPHome-Verzeichnis kopieren
   bzw. diesen Fork klonen.
2. `secrets-olimex.yaml.example` nach `secrets.yaml` kopieren und ausfüllen (`ota_password`, `web_server_password`,
   `wem_ip`, `wem_user`, `wem_pass`).
3. Dallas-Adressen anpassen oder den Abschnitt `one_wire` / `dallas_temp` entfernen.
4. `esphome run olimex-weishaupt.yaml` (erstes Mal per USB, dann OTA). Getestet mit ESPHome 2026.10.
5. In der Weboberfläche oder in Home Assistant „Eigene CAN-Anfragen" einschalten, wenn das Mithören stabil läuft.

## Stand der Prüfung (Autor, Anlage mit WTC-GW …-B)

- Die per CAN gelesenen kraiz-Werte stimmen mit der laufenden kraiz-Integration überein (Raum-/Vorlaufsollwerte,
  Betriebsart und Status HK1, Warmwasserstatus, Betriebsphase WTC, Druck, Volumenstrom).
- Warmwasser-Push über CAN wurde ausgelöst und lud den Speicher.
- Abgas- und Rücklauftemperatur: Das Original benennt `K2 0x2537` als Abgas und `K2 0x2533/2` als Rücklauf. kraiz
  nennt beide umgekehrt. Am Kesseldisplay (Rücklauf 68,4 °C, Abgas 67 °C) stimmt die Benennung des Originals.

## Lizenz und Quellen

- Original: MIT, © 2026 Juergen Vejmelka (siehe `LICENSE`).
- Wertetabellen und Objektadressen sind aus kraiz/hassio-weishaupt abgeleitet: MIT, © 2026 Lars Kreisz.
- Warn-/Fehlertexte: siehe oben, Weishaupt-Handbuch.
- Die Ergänzungen dieses Forks stehen wie das Original unter der MIT-Lizenz.
