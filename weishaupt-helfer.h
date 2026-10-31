// weishaupt-helfer.h - Hilfsfunktionen fuer olimex-weishaupt.yaml (C++ kann nicht in YAML stehen).
// Zusammengefuehrt am 31.10.2026 aus drei Teilen:
//   1. kraiz-per-can.h          Tabelle der CAN-Objekte, Planer, Decoder fuer SDO-Antworten (kraiz-Werte per CAN)
//   2. kraiz-per-can-texte.h    Wertetexte (aus kraiz/hassio-weishaupt, erzeugt)
//   3. weishaupt-fehlercodes.h  Warn-/Fehlercodes als Klartext (Handbuch WTC-GW 15 ... 32-B, Kapitel 10)
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// ===================== kraiz-per-can.h =====================
// Kern des Pakets kraiz-per-can: Tabelle der zu lesenden CAN-Objekte, Planer, Decoder fuer SDO-Antworten.
// Reines C++ ohne ESPHome-Typen. Gelesen wird ausschliesslich per SDO-Upload (Kommandobyte 0x40).
// Die Eintraege kommen aus pakete/kraiz-per-can.yaml (erzeugt aus kraiz/hassio-weishaupt).

namespace kc {

struct Eintrag {
  uint8_t kn = 0;
  uint16_t idx = 0;
  uint8_t sub = 0;
  bool vorzeichen = false;
  float faktor = 1.0f;
  uint32_t periode_ms = 60000;
  uint32_t faellig = 0;
  uint32_t gesendet = 0;
  bool offen = false;      // Anfrage gesendet, Antwort steht aus
  bool fehlt = false;      // der Knoten lehnt das Objekt ab (Abbruch 0x80): bis zum Neustart nicht mehr fragen
  std::function<void(int32_t, float)> melden;   // (roher Wert, skalierter Wert)
};

inline std::vector<Eintrag> tabelle;
inline bool fehler_wartet = false;
inline bool schreiben_gesendet = false;

// Objekt eintragen. Die erste Anfrage kommt frueh (nach dem Anlauf des Boards), die weiteren versetzt.
inline void anlegen(uint8_t kn, uint16_t idx, uint8_t sub, bool vorzeichen, float faktor, uint32_t periode_s,
                    std::function<void(int32_t, float)> melden) {
  Eintrag e;
  e.kn = kn; e.idx = idx; e.sub = sub; e.vorzeichen = vorzeichen; e.faktor = faktor;
  e.periode_ms = periode_s * 1000u;
  e.faellig = 20000u + (uint32_t) tabelle.size() * 3000u;
  e.melden = std::move(melden);
  tabelle.push_back(std::move(e));
}

// naechste faellige Anfrage (die am laengsten ueberfaellige); false = nichts zu tun
inline bool naechste(uint32_t jetzt, uint8_t &kn, std::vector<uint8_t> &daten) {
  Eintrag *best = nullptr;
  for (auto &e : tabelle) {
    if (e.offen && (int32_t) (jetzt - e.gesendet) > 10000) e.offen = false;   // Antwort blieb aus
    if (e.fehlt || e.offen) continue;
    if ((int32_t) (jetzt - e.faellig) < 0) continue;
    if (best == nullptr || (int32_t) (best->faellig - e.faellig) > 0) best = &e;
  }
  if (best == nullptr) return false;
  best->offen = true;
  best->gesendet = jetzt;
  best->faellig = jetzt + best->periode_ms;
  kn = best->kn;
  daten = {0x40, (uint8_t) (best->idx & 0xFF), (uint8_t) (best->idx >> 8), best->sub, 0, 0, 0, 0};
  return true;
}

// ein Objekt in ein paar Sekunden noch einmal lesen (Kontrolle nach einem Schreibbefehl)
inline void bald(uint8_t kn, uint16_t idx, uint8_t sub) {
  for (auto &e : tabelle)
    if (e.kn == kn && e.idx == idx && e.sub == sub) { e.faellig = e.gesendet + 4000u; e.fehlt = false; e.offen = false; }
}

// jede SDO-Antwort von Knoten 1 (0x581) und 2 (0x582): gehoert sie zur Tabelle, wird sie veroeffentlicht
inline void antwort(uint32_t can_id, const std::vector<uint8_t> &x) {
  if (x.size() < 8 || can_id < 0x581 || can_id > 0x582) return;
  uint8_t kn = (uint8_t) (can_id - 0x580);
  uint8_t b = x[0];
  uint16_t idx = (uint16_t) (x[1] | (x[2] << 8));
  uint8_t sub = x[3];
  for (auto &e : tabelle) {
    if (e.kn != kn || e.idx != idx || e.sub != sub) continue;
    if (b == 0x80) {                         // Abbruch: nur werten, wenn wir selbst gefragt haben
      if (e.offen) { e.fehlt = true; e.offen = false; }
      return;
    }
    if ((b & 0xE0) != 0x40 || !(b & 0x02)) return;      // nur Upload-Antwort mit Wert in den Bytes 4-7
    int n = (b & 0x01) ? 4 - ((b >> 2) & 0x03) : 4;
    if (n < 1) return;
    uint32_t u = 0;
    for (int i = n - 1; i >= 0; i--) u = (u << 8) | x[4 + i];
    int32_t v = (int32_t) u;
    if (e.vorzeichen) {
      if (n == 1) v = (int8_t) u;
      else if (n == 2) v = (int16_t) u;
    }
    e.offen = false;
    bool ungueltig = e.vorzeichen && n == 2 && (u == 0x7FFF || u == 0x8000);   // "kein Fuehler"
    if (e.melden) e.melden(v, ungueltig ? NAN : v * e.faktor);
    return;
  }
}

inline std::string hex(const std::vector<uint8_t> &d) {
  std::string s;
  char t[4];
  for (size_t i = 0; i < d.size(); i++) {
    snprintf(t, sizeof(t), i ? " %02X" : "%02X", d[i]);
    s += t;
  }
  return s;
}

// Fehler/Warnung-Status aus Byte 2-3 des Fehlerblocks (Auswertung wie kraiz, parsing.py)
inline std::string fehler_text(uint16_t roh) {
  switch (roh & 0x0F) {
    case 1: return "Fehler";
    case 2: return "Warnung";
    case 3: return "Info";
    default: break;
  }
  if ((roh >> 8) > 0) return "Fehler aktiv";
  return "Keine Meldung";
}

}  // namespace kc

// ===================== kraiz-per-can-texte.h =====================
// ERZEUGT von kraiz_erzeugen.js aus kraiz/hassio-weishaupt (sensors.py) - NICHT VON HAND AENDERN.

namespace kc {

inline std::string text_BETRIEBSART_AKTUELL_MAP(int v) {
  switch (v) {
    case 1: return "System Standby";
    case 2: return "System Sommer";
    case 3: return "Estrich: Funktionsheizen";
    case 4: return "Estrich: Belegreifheizen";
    case 5: return "Urlaub";
    case 6: return "Zeitprogramm 1";
    case 7: return "Zeitprogramm 2";
    case 8: return "Zeitprogramm 3";
    case 9: return "Sommer";
    case 10: return "Absenk";
    case 11: return "Normal";
    case 12: return "Komfort";
    case 13: return "Standby";
    default: { char b[32]; snprintf(b, sizeof(b), "Unbekannt (%d)", v); return b; }
  }
}

inline std::string text_STATUS_HK_MAP(int v) {
  switch (v) {
    case 1: return "Raumfrostschutz";
    case 2: return "Anlagenfrostschutz";
    case 16: return "Eingang H1: Sonder";
    case 17: return "Eingang H1: Komfort";
    case 18: return "Eingang H1: Normal";
    case 19: return "Eingang H1: Absenk";
    case 20: return "Eingang H1: Standby";
    case 21: return "Eingang H1: Not-Aus";
    case 24: return "Party";
    case 32: return "Optimierung";
    case 33: return "Niveauanhebung Außentemperatur";
    case 48: return "ext. Notabnahme";
    case 49: return "ext. Überangebot";
    case 50: return "ext. Heizvorgabe";
    case 51: return "Warmwasservorrang";
    case 64: return "Sommerbetrieb witterungsgeführt";
    case 65: return "Heizgrenze Raum";
    case 66: return "Heizgrenze VL";
    case 67: return "Raumthermostat";
    case 80: return "Komfort";
    case 81: return "Normal";
    case 82: return "Absenken";
    case 83: return "Frost";
    case 84: return "Frostschutz aktiv";
    default: { char b[32]; snprintf(b, sizeof(b), "Unbekannt (%d)", v); return b; }
  }
}

inline std::string text_SOWI_UMSCHALTUNG_MAP(int v) {
  switch (v) {
    case 0: return "Aus";
    case 1: return "Ein";
    default: { char b[32]; snprintf(b, sizeof(b), "Unbekannt (%d)", v); return b; }
  }
}

inline std::string text_STATUS_WW_MAP(int v) {
  switch (v) {
    case 0: return "Standby: inaktiv";
    case 1: return "Zeitprogramm – Normal";
    case 2: return "Zeitprogramm – Absenk";
    case 3: return "Warmwasserladung aktiv";
    case 4: return "Service";
    case 5: return "Übertemperatur Alternativenergie";
    case 6: return "Überschuss Alternativenergie";
    case 7: return "Akzeptanz WW Alternativenergie";
    case 8: return "Frostschutz";
    case 9: return "Standby durch Systemprogrammschalter";
    case 10: return "Standby durch Eingang H2";
    case 12: return "Normal durch Eingang H2";
    case 13: return "Absenk durch Eingang H2";
    default: { char b[32]; snprintf(b, sizeof(b), "Unbekannt (%d)", v); return b; }
  }
}

inline std::string text_PUMPE_MAP(int v) {
  switch (v) {
    case 0: return "Aus";
    case 1: return "Ein";
    default: { char b[32]; snprintf(b, sizeof(b), "Unbekannt (%d)", v); return b; }
  }
}

inline std::string text_FOLGEWECHSEL_MAP(int v) {
  switch (v) {
    case 0: return "normal";
    case 1: return "invertiert";
    case 2: return "zeitlich";
    case 3: return "Betriebsstunden";
    default: { char b[32]; snprintf(b, sizeof(b), "Unbekannt (%d)", v); return b; }
  }
}

inline std::string text_BETRIEBSPHASE_WTC_MAP(int v) {
  switch (v) {
    case 0: return "Normalbetrieb";
    case 10: return "Pumpennachlauf";
    case 15: return "Brennertaktsperre Heizen";
    case 20: return "Sperre Mindestheizleistung";
    case 24: return "Adaption Gasstellglied läuft";
    case 25: return "Verzögerter Heizbetrieb";
    case 30: return "Softstart Warmwasser";
    case 35: return "Abregelfunktion Fernsteuerung";
    case 40: return "Spreizung Vorlauf/Abgas";
    case 45: return "Spreizung Vorlauf/Rücklauf";
    case 50: return "Abregelfunktion Abgastemperatur";
    case 55: return "Abschaltung Fernsteuerung";
    case 60: return "Abschaltung Mindestumlauf";
    case 70: return "SCOT Kalibration läuft";
    default: { char b[32]; snprintf(b, sizeof(b), "Unbekannt (%d)", v); return b; }
  }
}

}  // namespace kc

// ===================== weishaupt-fehlercodes.h =====================
// Warn- und Fehlercodes der Weishaupt WTC-GW 15 ... 32-B als Klartext.
// Quelle: Montage- und Betriebsanleitung Gas-Brennwertgeraet WTC-GW 15 ... 32-B, Art.-Nr. 83307201, 1/2022-04,
// Kapitel 10 Fehlersuche: 10.2 Warncode (Seite 134-137), 10.3 Fehlercode (Seite 138-143). Nur die Ursache, ohne Behebung.
// Gleiche Nummer, mehrere Eintraege: das Handbuch fuehrt dieselbe Nummer je nach Modul mit verschiedenen Ursachen auf.
//
// Zuordnung Fehlerblock -> Code (wie kraiz/hassio-weishaupt): Status Byte 2-3, Nummer Byte 4-5 des Objekts 0x273F.
// Typ aus dem unteren Halbbyte des Status: 1 = Fehler (F), 2 = Warnung (W). UNGEPRUEFT an einer echten Stoerung.

namespace kc {

struct Code { char typ; uint16_t nr; const char *text; };

inline const Code CODES[] = {
  // --- 10.2 Warncode ---
  {'W', 1, "Raumfeuchte zu hoch"},
  {'W', 2, "Raumfeuchte zu gering"},
  {'W', 3, "Keine SD-Karte vorhanden"},
  {'W', 7, "EM Warmwasser: Zirkulationsfühler nicht aktiv"},
  {'W', 8, "EM Warmwasser: Quellenfühler defekt"},
  {'W', 10, "Volumenstrom zu niedrig"},
  {'W', 11, "Not-Aus"},
  {'W', 12, "Temperatur am Vorlauffühler > 95 °C"},
  {'W', 14, "Vorlauftemperatur steigt zu schnell an (Gradient)"},
  {'W', 15, "Differenz Vorlauf- und Abgastemperatur zu groß"},
  {'W', 16, "Abgastemperatur zu hoch"},
  {'W', 17, "Differenz Vorlauf- und Rücklauftemperatur zu groß"},
  {'W', 18, "Differenz Vorlauf-(eSTB) und Vorlauftemperatur (VPT) zu groß"},
  {'W', 19, "Vorlauftemperatur (VPT) steigt zu schnell an (Gradient)"},
  {'W', 20, "Flammenausfall in Sicherheitszeit"},
  {'W', 21, "Keine Flammenbildung beim Brennerstart"},
  {'W', 22, "Flammenausfall im Betrieb"},
  {'W', 25, "Flammenausfall in Stabilisierungszeit"},
  {'W', 27, "Gasdruck zu niedrig"},
  {'W', 36, "Anlagendruck zu niedrig"},
  {'W', 40, "Pumpe intern meldet Warnung"},
  {'W', 42, "Pumpe intern Rückmeldesignal fehlerhaft"},
  {'W', 43, "Gebläsedrehzahl außerhalb Bereich"},
  {'W', 48, "Luft im System"},
  {'W', 61, "Ionisationssignal außerhalb Toleranz"},
  {'W', 62, "Stellsignal vom Gasstellglied oder Gebläse außerhalb Toleranz"},
  {'W', 63, "SCOT-Systemfehler"},
  {'W', 66, "Kalibrierung nicht erfolgreich"},
  {'W', 69, "Teillast: Stabiler Zustand nicht erreicht"},
  // --- 10.3 Fehlercode ---
  {'F', 1, "EM Heizkreis: Kommunikationsfehler EM Heizkreis"},
  {'F', 1, "EM Solar: Kollektorfühler (T1) defekt"},
  {'F', 1, "Kaskadenregler: Fühler T1 defekt"},
  {'F', 2, "EM Heizkreis: Außenfühler (T1) vom EM Heizkreis defekt"},
  {'F', 2, "EM Solar: Speicherfühler unten (T2) defekt"},
  {'F', 2, "Kaskadenregler: Fühler T2 defekt"},
  {'F', 3, "EM Heizkreis: Vorlauffühler (B6) vom EM Heizkreis defekt"},
  {'F', 3, "EM Solar: Vorlauffühler Solar (T3) defekt"},
  {'F', 4, "Rücklauffühler Solar (T4) defekt"},
  {'F', 5, "Pufferfühler oben (B10) defekt"},
  {'F', 5, "EM Warmwasser: Zirkulationsfühler (T1) vom EM Warmwasser defekt"},
  {'F', 6, "Pufferfühler unten (B11) defekt"},
  {'F', 6, "EM Warmwasser: Warmwasserfühler (B6) vom EM Warmwasser defekt"},
  {'F', 10, "EM Solar: Kommunikationsfehler EM Solar"},
  {'F', 10, "Kaskadenregler: Fehler Kommunikation"},
  {'F', 10, "EM Warmwasser: Kommunikationsfehler EM Warmwasser"},
  {'F', 11, "Temperatur am Vorlauffühler > 105 °C"},
  {'F', 13, "Abgastemperatur zu hoch"},
  {'F', 14, "Vorlauftemperatur steigt zu schnell an (Gradient)"},
  {'F', 15, "Differenz Vorlauf- und Abgastemperatur zu groß"},
  {'F', 19, "Vorlauftemperatur (VPT) steigt zu schnell an (Gradient)"},
  {'F', 20, "Feuerungsautomat: Flammenausfall in Sicherheitszeit"},
  {'F', 20, "EM Solar: Kein Volumenstrom"},
  {'F', 21, "Feuerungsautomat: Keine Flammenbildung beim Brennerstart"},
  {'F', 21, "EM Solar: Fehler im DTR (Differenztemperatur-Regler)"},
  {'F', 23, "Flammenvortäuschung"},
  {'F', 24, "Brennersperr-Funktion aktiv"},
  {'F', 29, "Warmwasser-Auslauffühler defekt (Ausführung C)"},
  {'F', 30, "Vorlauffühler (eSTB) defekt"},
  {'F', 31, "Abgasfühler defekt"},
  {'F', 32, "Weichenfühler (B2) defekt"},
  {'F', 33, "Außenfühler (B1) defekt"},
  {'F', 34, "Warmwasserfühler (B3) defekt"},
  {'F', 36, "Anlagendruck außerhalb Bereich"},
  {'F', 37, "Wasserströmungssensor defekt (Ausführung C)"},
  {'F', 38, "T1-Fühler am Zusatzmodul defekt"},
  {'F', 39, "T2-Fühler am Zusatzmodul defekt"},
  {'F', 40, "Pumpe intern meldet Elektronikfehler"},
  {'F', 41, "Gasventilkontrolle fehlerhaft"},
  {'F', 42, "Pumpe intern meldet Blockadefehler"},
  {'F', 43, "Gebläsedrehzahl wird nicht erreicht"},
  {'F', 44, "Gebläsestillstand fehlerhaft"},
  {'F', 45, "Ventilströme außerhalb Toleranz"},
  {'F', 46, "Fehler Multifunktionssensor VPT"},
  {'F', 47, "Multifunktionssensor VPT Versionsfehler"},
  {'F', 49, "Datensatz-Fehler Feuerungsautomat"},
  {'F', 50, "Interner Fehler"},
  {'F', 51, "Datensatz-Fehler Kessel"},
  {'F', 52, "Datensatz-Fehler Brenner"},
  {'F', 53, "Spannungsversorgung außerhalb Toleranz"},
  {'F', 54, "Elektronikfehler"},
  {'F', 55, "Speicherfehler"},
  {'F', 56, "Ionisationsmessung fehlerhaft"},
  {'F', 57, "Zusatzmodul nicht mehr vorhanden"},
  {'F', 58, "Zu viele Entriegelungen innerhalb kurzer Zeit"},
  {'F', 59, "Kein Datensatz vorhanden"},
  {'F', 60, "Kalibrierung: SCOT-Basiswert zu gering"},
  {'F', 61, "Ionisationssignal außerhalb Toleranz"},
  {'F', 62, "Stellsignal vom Gasstellglied oder Gebläse außerhalb Toleranz"},
  {'F', 63, "SCOT-Systemfehler"},
  {'F', 64, "Kalibrierung: SCOT-Basiswert zu hoch"},
  {'F', 65, "SCOT-Basiswert weicht zu stark vom Vorgängerwert ab"},
  {'F', 66, "Kalibrierung konnte nicht durchgeführt werden"},
  {'F', 67, "SCOT-Basiswert fehlerhaft gespeichert"},
  {'F', 68, "Gasventil: Offset außerhalb Bereich"},
  {'F', 70, "Datensatz-Fehler BCC"},
  {'F', 71, "Datensatz-Fehler BCC fehlt"},
  {'F', 72, "Datensatz-Fehler BCC"},
  {'F', 73, "Datensatz-Fehler: BCC nicht kompatibel"},
  {'F', 74, "BCC-Update angefordert: Neustart erforderlich"},
  {'F', 75, "Datensatz-Fehler BCC"},
  {'F', 80, "Fernsteuersignal (N1) zu klein"},
  {'F', 81, "Fernsteuersignal (N1) zu groß"},
  {'F', 88, "Interner Fehler"},
  {'F', 90, "Kommunikationsfehler ChipCom"},
  {'F', 91, "Kommunikationsfehler Systemgerät / Feuerungsautomat"},
  {'F', 92, "Kommunikationsfehler CAN"},
  {'F', 93, "Kommunikationsfehler Serial Flash"},
  {'F', 94, "Kommunikationsfehler VPT Modbus"},
  {'F', 95, "Interner Fehler"},
  {'F', 96, "Kommunikationsfehler VPT Daten"},
};

// Warnungen 1101-1925: Kommunikationsfehler zu einem Busteilnehmer, die letzten beiden Stellen sind seine Nummer
inline std::string warn_kommunikation(uint16_t nr) {
  struct Bereich { uint16_t von, bis; const char *name; };
  static const Bereich B[] = {
    {1101, 1112, "SG"}, {1201, 1212, "FA"}, {1302, 1325, "EM-HK"}, {1401, 1401, "SOL"}, {1501, 1532, "RG2"},
    {1601, 1632, "RF"}, {1701, 1732, "RG1"}, {1800, 1800, "EM-KA"}, {1902, 1925, "EM-WW"},
  };
  for (const auto &b : B) {
    if (nr >= b.von && nr <= b.bis) {
      char t[64];
      snprintf(t, sizeof(t), "Kommunikationsfehler: %s#%u", b.name, (unsigned) (nr % 100));
      return t;
    }
  }
  return "";
}

inline std::string texte_zu(char typ, uint16_t nr) {
  std::string s;
  for (const auto &c : CODES) {
    if (c.typ != typ || c.nr != nr) continue;
    if (!s.empty()) s += " / ";
    s += c.text;
  }
  if (s.empty() && typ == 'W') s = warn_kommunikation(nr);
  return s;
}

// Klartext zu Fehler/Warnung-Status (Byte 2-3) und Fehlernummer (Byte 4-5) des Fehlerblocks 0x273F
inline std::string klartext(uint16_t status, uint16_t nummer) {
  uint8_t art = status & 0x0F;
  if (nummer == 0 && art == 0 && (status >> 8) == 0) return "Keine Meldung";
  std::string f = art != 2 ? texte_zu('F', nummer) : "";
  std::string w = art != 1 ? texte_zu('W', nummer) : "";
  char k[24];
  std::string out;
  if (!f.empty()) { snprintf(k, sizeof(k), "F %u: ", (unsigned) nummer); out += k; out += f; }
  if (!w.empty()) { if (!out.empty()) out += "  ODER  "; snprintf(k, sizeof(k), "W %u: ", (unsigned) nummer); out += k; out += w; }
  if (out.empty()) {
    snprintf(k, sizeof(k), "Nummer %u", (unsigned) nummer);
    out = k; out += " nicht in der Tabelle (Handbuch Kap. 10)";
  }
  return out;
}

}  // namespace kc

