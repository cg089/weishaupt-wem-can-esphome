// Liest kraiz sensors.py (als Daten) und gibt je Objekt JSON-Adresse und abgeleitete CAN-Adresse aus.
const fs = require('fs');
const src = fs.readFileSync(process.argv[2], 'utf8');
const blocks = src.split('WeishauptSensorDefinition(').slice(1);
const num = (b, k) => { const m = b.match(new RegExp('\\b' + k + '=(0x[0-9A-Fa-f]+|\\d+)')); return m ? Number(m[1]) : null; };
const str = (b, k) => { const m = b.match(new RegExp('\\b' + k + '="([^"]*)"')); return m ? m[1] : null; };
// MI -> [CAN-Knoten, Versatz]; nur die am Bus bestaetigten Module (PROTOKOLL.md, Abschnitt 5)
const MAP = { 1: [1, 0x100], 2: [1, 0x400], 3: [1, 0x500], 7: [2, 0], 9: [2, 0x100] };
const rows = [];
for (const b of blocks) {
  const key = str(b, 'key'); if (!key) continue;
  const mi = num(b, 'mi'), mx = num(b, 'mx'), ox = num(b, 'ox'), os = num(b, 'os'), vs = num(b, 'vs');
  const g = (b.match(/group=WeishauptDeviceGroup\.(\w+)/) || [])[1];
  const scale = (b.match(/\bscale=([0-9.]+)/) || [])[1] || '1';
  const unit = (b.match(/\bunit=([\w.]+)/) || [])[1] || '';
  const signed = /signed=True/.test(b), poll = !/poll=False/.test(b);
  const vm = (b.match(/value_map=(\w+)/) || [])[1] || '';
  const sk = str(b, 'source_key') || '';
  const m = MAP[mi];
  const can = (m && mx === (mi === 9 ? 1 : 0)) ? `K${m[0]} 0x${(ox + m[1]).toString(16).toUpperCase()}/${os}` : '?';
  rows.push({ key, name: str(b, 'name'), grp: g, mi, mx, ox: '0x' + ox.toString(16).toUpperCase(), os, vs, scale, unit, signed, poll, vm, sk, can });
}
if (process.argv[3] === 'json') console.log(JSON.stringify(rows, null, 1));
else for (const r of rows) console.log([r.grp, `MI${r.mi}/MX${r.mx}`, r.ox + '/' + r.os, 'vs' + r.vs, 'x' + r.scale, r.signed ? 's' : 'u', r.poll ? '' : 'nopoll', r.sk ? 'src:' + r.sk : '', r.vm, '->', r.can, '|', r.key].join(' '));
