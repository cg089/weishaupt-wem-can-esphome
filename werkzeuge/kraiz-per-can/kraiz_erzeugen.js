// Erzeugt aus den kraiz-Definitionen (als Daten gelesen) das ESPHome-Paket kraiz-per-can.yaml
// und die Wertetexte kraiz-per-can-texte.h.   node kraiz_erzeugen.js <sensors.py> <ausgabeordner>
const fs = require('fs'), path = require('path');
const [sensorsPy, outDir] = process.argv.slice(2);
const src = fs.readFileSync(sensorsPy, 'utf8');

// --- Wertetabellen (Python-Dicts) ---
const maps = {};
for (const m of src.matchAll(/^([A-Z_]+_MAP) = \{([\s\S]*?)^\}/gm)) {
  maps[m[1]] = [...m[2].matchAll(/(\d+):\s*"([^"]*)"/g)].map(e => [Number(e[1]), e[2]]);
}

// --- Definitionen ---
const num = (b, k) => { const m = b.match(new RegExp('\\b' + k + '=(0x[0-9A-Fa-f]+|\\d+)')); return m ? Number(m[1]) : null; };
const str = (b, k) => { const m = b.match(new RegExp('\\b' + k + '="([^"]*)"')); return m ? m[1] : null; };
const MAP = { 1: [1, 0x100], 2: [1, 0x400], 3: [1, 0x500], 7: [2, 0], 9: [2, 0x100] };   // am Bus bestaetigt (PROTOKOLL.md)
const defs = {};
for (const b of src.split('WeishauptSensorDefinition(').slice(1)) {
  const key = str(b, 'key'); if (!key) continue;
  const mi = num(b, 'mi'), mx = num(b, 'mx'), ox = num(b, 'ox'), os = num(b, 'os');
  const m = MAP[mi];
  defs[key] = {
    key, name: (str(b, 'name') || '').split('/').join('-'), vs: num(b, 'vs'), scale: Number((b.match(/\bscale=([0-9.]+)/) || [])[1] || 1),
    signed: /signed=True/.test(b), vm: (b.match(/value_map=(\w+)/) || [])[1] || null,
    unit: (b.match(/\bunit=([\w.]+)/) || [])[1] || '', mi, mx,
    kn: m ? m[0] : null, idx: m ? ox + m[1] : null, sub: os,
  };
}

// --- Auswahl: nur was das Repo nicht ohnehin liest (Abgleich diff_repo.js) ---
const AUSWAHL = [
  // Heizkreis 1
  ['sg_betriebsart_hk1_aktuell', 60], ['sg_status_hk1', 60], ['sg_sowi_umschaltung_hk1', 600],
  ['sg_raumsolltemperatur_komfort', 600], ['sg_raumsolltemperatur_normal', 600], ['sg_raumsolltemperatur_absenk', 600],
  ['sg_vorlaufsolltemperatur_komfort', 600], ['sg_vorlaufsolltemperatur_normal', 600], ['sg_vorlaufsolltemperatur_absenk', 600],
  ['sg_vorlaufsolltemperatur_sonderniveau', 600], ['sg_vorlaufsolltemperatur_aktuell', 60],
  // Warmwasser
  ['sg_status_warmwasser', 60], ['sg_warmwasser_push', 60], ['sg_wwsolltemperatur_absenk', 600],
  ['sg_pumpe_warmwasser', 60], ['sg_waermeanforderung_warmwasser', 60], ['sg_ruecklauftemperatur_zirkulation', 60],
  // Systemgeraet
  ['sg_plattenwaermetauschertemperatur_b2', 60], ['sg_pufferspeicher_temperatur_oben', 60], ['sg_pufferspeicher_temperatur_unten', 60],
  ['sg_n1_vorgabe_ethernet', 60],
  // Kaskade
  ['sg_kaskade_sollwert_hz', 300], ['sg_kaskade_sollwert_ww', 300], ['sg_kaskade_sollmodulation_hz', 300], ['sg_kaskade_sollmodulation_ww', 300],
  ['sg_folgewechsel', 600], ['sg_wechselzeit', 600], ['sg_kaskade_abgleichtemperatur', 600],
  // Kessel
  ['wtc_betriebsphase', 60],
];
// in kraiz ohne Wert ("unavailable"/"unknown") oder fuer einen einzelnen Kessel ohne Belang: in HA ab Werk ausgeblendet
const AUS = new Set(['sg_n1_vorgabe_ethernet', 'sg_plattenwaermetauschertemperatur_b2', 'sg_pufferspeicher_temperatur_oben',
  'sg_pufferspeicher_temperatur_unten', 'sg_ruecklauftemperatur_zirkulation', 'sg_kaskade_sollwert_hz', 'sg_kaskade_sollwert_ww',
  'sg_kaskade_sollmodulation_hz', 'sg_kaskade_sollmodulation_ww', 'sg_folgewechsel', 'sg_wechselzeit', 'sg_kaskade_abgleichtemperatur']);

const eintraege = AUSWAHL.map(([key, periode]) => {
  const d = defs[key]; if (!d) throw new Error('fehlt in kraiz: ' + key);
  if (d.kn == null) throw new Error('keine CAN-Abbildung: ' + key);
  return { ...d, periode };
});

// --- Texte ---
const benutzt = [...new Set(eintraege.map(e => e.vm).filter(Boolean))];
let h = '// ERZEUGT von kraiz_erzeugen.js aus kraiz/hassio-weishaupt (sensors.py) - NICHT VON HAND AENDERN.\n#pragma once\n#include <cstdio>\n#include <string>\n\nnamespace kc {\n\n';
for (const n of benutzt) {
  h += `inline std::string text_${n}(int v) {\n  switch (v) {\n`;
  for (const [k, t] of maps[n]) h += `    case ${k}: return "${t}";\n`;
  h += `    default: { char b[32]; snprintf(b, sizeof(b), "Unbekannt (%d)", v); return b; }\n  }\n}\n\n`;
}
h += '}  // namespace kc\n';

// --- YAML ---
const slug = k => 'kc_' + k.replace(/^(sg|wtc)_/, '');
const einheit = u => /CELSIUS/.test(u) ? '°C' : /PERCENTAGE/.test(u) ? '%' : '';
let y = `# ERZEUGT von kraiz_erzeugen.js aus kraiz/hassio-weishaupt (sensors.py) - Aenderungen im Generator, nicht hier.
#
# Paket KRAIZ-PER-CAN: die Werte der Home-Assistant-Integration kraiz/hassio-weishaupt (WEM-JSON), hier per
# CAN gelesen. Die JSON-Objekte (MI/MX/OX/OS) sind je Modul um einen festen Betrag verschoben auf den Bus
# abgebildet (PROTOKOLL.md, "Abbildung JSON-Objekt -> CAN-Objekt"): MI01 Knoten 1 +0x100, MI02 +0x400,
# MI03 +0x500, MI07 Knoten 2 +0, MI09/MX01 Knoten 2 +0x100.  Nur LESEN (SDO-Upload 0x40) - plus der
# Schreibknopf Warmwasser-Push an den WEM (Knoten 1) wie in kraiz.
# Heizkreis 2/3 und Solar fehlen bewusst: diese Module gibt es an der Anlage nicht.
# Braucht die Pakete kessel (wcan, can_anfragen) und wem-schalten (Schreib-Helfer, Sperrminute).
# Abgefragt wird nur bei eingeschaltetem Schalter "Eigene CAN-Anfragen", hoechstens alle 3 s eine Anfrage,
# jedes Objekt hoechstens alle 60 s / 5 min / 10 min; Objekte, die der WEM mit Abbruch (0x80) ablehnt,
# werden bis zum Neustart nicht mehr gefragt.

esphome:
  includes:
    - weishaupt-wem-can/kraiz-per-can.h
    - weishaupt-wem-can/kraiz-per-can-texte.h
    - weishaupt-wem-can/weishaupt-fehlercodes.h
  on_boot:
    priority: -100
    then:
      - lambda: |-
`;
for (const e of eintraege) {
  const txt = e.vm != null;
  const body = txt
    ? `id(${slug(e.key)}).publish_state(kc::text_${e.vm}((int) r));`
    : `id(${slug(e.key)}).publish_state(v);`;
  const faktor = (Number.isInteger(e.scale) ? e.scale.toFixed(1) : String(e.scale)) + 'f';
  y += `          kc::anlegen(${e.kn}, 0x${e.idx.toString(16).toUpperCase()}, ${e.sub}, ${e.signed}, ${faktor}, ${e.periode}, [](int32_t r, float v) { ${body} });\n`;
}
y += `
web_server:
  sorting_groups:
    - id: g_kraiz
      name: "Weitere Werte (wie kraiz/JSON, per CAN)"
      sorting_weight: 35

interval:
  # reihum die naechste faellige Anfrage, hoechstens eine alle 3 s
  - interval: 3s
    then:
      - if:
          condition:
            lambda: 'return id(can_anfragen).state && id(wcan)->bus.senden_erlaubt(millis());'
          then:
            - lambda: |-
                uint8_t kn = 0; std::vector<uint8_t> d;
                if (kc::naechste(millis(), kn, d)) id(wcan)->senden(0x600 + kn, d);
  # Fehlerblock des WEM (8 Byte, Teilstuecke ueber die Lese-Engine des Repos)
  - interval: 120s
    then:
      - if:
          condition:
            lambda: 'return id(can_anfragen).state && id(wcan)->bus.senden_erlaubt(millis());'
          then:
            - script.execute: kc_fehler_lesen

canbus:
  - id: !extend my_can_bus
    on_frame:
      # SDO-Antworten von WEM (0x581) und Kessel (0x582): was zur Tabelle passt, wird veroeffentlicht
      - can_id: 0x581
        then:
          - lambda: 'kc::antwort(can_id, x);'
      - can_id: 0x582
        then:
          - lambda: 'kc::antwort(can_id, x);'
      # CANopen-Emergency (0x080 + Knoten): falls die Anlage Fehler so meldet
      - can_id: 0x081
        then:
          - lambda: |-
              std::string t = kc::hex(x);
              ESP_LOGW("kraiz", "Emergency Knoten 1: %s", t.c_str());
              id(kc_emergency).publish_state("WEM " + t);
      - can_id: 0x082
        then:
          - lambda: |-
              std::string t = kc::hex(x);
              ESP_LOGW("kraiz", "Emergency Knoten 2: %s", t.c_str());
              id(kc_emergency).publish_state("Kessel " + t);

script:
  - id: kc_fehler_lesen
    mode: single
    then:
      - lambda: |-
          kc::fehler_wartet = false;
          if (id(wcan)->lesen.aktiv) return;      // ein Lesebefehl von Hand laeuft
          std::string m; uint32_t cid = 0; std::vector<uint8_t> d;
          if (id(wcan)->lesen.starten("01 273F 00", id(wcan)->bus.tot(millis()), m, cid, d)) {
            id(wcan)->senden(cid, d);
            kc::fehler_wartet = true;
          }
      - if:
          condition:
            lambda: 'return kc::fehler_wartet;'
          then:
            - delay: 3s
            - lambda: |-
                auto &l = id(wcan)->lesen;
                kc::fehler_wartet = false;
                if (l.aktiv || l.kn != 1 || l.idx != 0x273F || l.puffer.size() < 8) return;
                const std::vector<uint8_t> &p = l.puffer;
                id(kc_fehlerblock).publish_state(kc::hex(p));
                id(kc_fehler_status).publish_state(kc::fehler_text((p[2] << 8) | p[3]));
                id(kc_fehlernummer).publish_state((p[4] << 8) | p[5]);
                id(kc_fehler_modul).publish_state((p[6] << 8) | p[7]);
                id(kc_fehler_klartext).publish_state(kc::klartext((p[2] << 8) | p[3], (p[4] << 8) | p[5]));

  # Schreiben an den WEM per CAN (Knoten 1, 1 Byte) - nur die hier freigegebenen Ziele, Ablauf wie "Freies CAN-Schreiben"
  - id: kc_schreiben
    mode: queued
    max_runs: 3
    parameters:
      idx: int
      sub: int
      wert: int
      was: string
    then:
      - lambda: |-
          kc::schreiben_gesendet = false;
          auto &sc = id(wcan)->schalten;
          if (id(wem_senden).is_running() || id(can_senden).is_running() || id(can_frei_senden).is_running() || id(sd_holen).is_running() || sc.sperrminute(millis())) {
            id(schalt_status).publish_state(was + ": bitte warten - Abstand zum letzten Befehl"); return;
          }
          if (id(wcan)->bus.tot(millis())) { id(schalt_status).publish_state(was + ": Bus tot - nicht gesendet"); return; }
          wc::CanSchreibbefehl c; c.kn = 1; c.idx = idx; c.sub = sub; c.daten = {(uint8_t) wert};
          sc.beginnen(millis(), (uint32_t) id(abstand_can).state * 1000); id(wcan)->anlass.eigener_befehl_ms = sc.letzter_befehl;
          uint32_t u = id(wcan)->unix_zeit();
          if (u != 0) bf::status().setze(bf::status().frei_ab, u + sc.sperre_s());
          uint32_t cid = 0; std::vector<uint8_t> d;
          sc.can_fertig = false; sc.can_text.clear();
          id(wcan)->schreiben.starten(c, cid, d);
          ESP_LOGW("kraiz", "CAN-Schreibbefehl %s: 01 %04X/%02X = %d", was.c_str(), idx, sub, wert);
          id(wcan)->senden(cid, d);
          kc::schreiben_gesendet = true;
      - if:
          condition:
            lambda: 'return kc::schreiben_gesendet;'
          then:
            - delay: 2s
            - lambda: |-
                auto &sc = id(wcan)->schalten;
                auto &w = id(wcan)->schreiben;
                kc::schreiben_gesendet = false;
                std::string t = sc.can_fertig ? sc.can_text : "keine Antwort vom WEM";
                if (!sc.can_fertig) w.aktiv = false;
                id(schalt_status).publish_state(was + " per CAN: " + t);
                id(protokoll_eintrag)->execute(was + " (Web/HA): " + t);
                ESP_LOGI("kraiz", "%s | roh: %s", t.c_str(), w.roh.c_str());
                kc::bald(1, idx, sub);        // Zielobjekt in ein paar Sekunden zur Kontrolle neu lesen

button:
  - platform: template
    name: "Warmwasser-Push"
    id: kc_ww_push
    icon: mdi:water-boiler-alert
    web_server: { sorting_group_id: g_schalten, sorting_weight: 3 }
    on_press:
      - script.execute:
          id: kc_schreiben
          idx: 0x2A49
          sub: 2
          wert: 1
          was: "Warmwasser-Push"

sensor:
`;
const sens = eintraege.filter(e => e.vm == null);
for (const e of sens) {
  const u = einheit(e.unit), dp = e.scale >= 1 ? 0 : (e.scale === 0.01 ? 2 : 1);
  y += `  - platform: template\n    name: "${e.name}"\n    id: ${slug(e.key)}\n`;
  if (u) y += `    unit_of_measurement: "${u}"\n`;
  if (u === '°C') y += `    device_class: temperature\n`;
  if (u) y += `    state_class: measurement\n`;
  y += `    accuracy_decimals: ${dp}\n`;
  if (AUS.has(e.key)) y += `    disabled_by_default: true\n`;
  y += `    web_server: { sorting_group_id: g_kraiz }\n`;
}
y += `  - platform: template
    name: "Fehlernummer"
    id: kc_fehlernummer
    icon: mdi:numeric
    entity_category: diagnostic
    accuracy_decimals: 0
    web_server: { sorting_group_id: g_diag }
  - platform: template
    name: "Fehlermodul"
    id: kc_fehler_modul
    icon: mdi:chip
    entity_category: diagnostic
    accuracy_decimals: 0
    web_server: { sorting_group_id: g_diag }

text_sensor:
`;
for (const e of eintraege.filter(e => e.vm != null)) {
  y += `  - platform: template\n    name: "${e.name}"\n    id: ${slug(e.key)}\n`;
  if (AUS.has(e.key)) y += `    disabled_by_default: true\n`;
  y += `    web_server: { sorting_group_id: g_kraiz }\n`;
}
y += `  - platform: template
    name: "Fehler-Warnung Status"
    id: kc_fehler_status
    icon: mdi:alert-circle-outline
    entity_category: diagnostic
    web_server: { sorting_group_id: g_diag }
  - platform: template
    name: "Fehler-Klartext"
    id: kc_fehler_klartext
    icon: mdi:alert-circle-check-outline
    entity_category: diagnostic
    web_server: { sorting_group_id: g_diag }
  - platform: template
    name: "CANopen Fehlerblock"
    id: kc_fehlerblock
    icon: mdi:alert-box-outline
    entity_category: diagnostic
    disabled_by_default: true
    web_server: { sorting_group_id: g_diag }
  - platform: template
    name: "Letzte Emergency-Meldung"
    id: kc_emergency
    icon: mdi:alert-octagon-outline
    entity_category: diagnostic
    web_server: { sorting_group_id: g_diag }
`;

fs.mkdirSync(path.join(outDir, 'pakete'), { recursive: true });
fs.writeFileSync(path.join(outDir, 'pakete', 'kraiz-per-can.yaml'), y);
fs.writeFileSync(path.join(outDir, 'kraiz-per-can-texte.h'), h);
console.log(`${eintraege.length} Objekte, ${benutzt.length} Wertetabellen, ${y.split('\n').length} Zeilen YAML`);
