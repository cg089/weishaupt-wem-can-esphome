// Vergleicht die aus kraiz abgeleiteten CAN-Adressen mit den Sdo-Konstanten aus register_gen.h des Repos.
const fs = require('fs'), cp = require('child_process'), path = require('path');
const [kraizPy, regGen, tableJs] = process.argv.slice(2);
const rows = JSON.parse(cp.execFileSync('node', [tableJs, kraizPy, 'json'], { encoding: 'utf8' }));
const have = {};
for (const m of fs.readFileSync(regGen, 'utf8').matchAll(/constexpr Sdo (\w+)\{(\d+), 0x([0-9A-Fa-f]+), 0x([0-9A-Fa-f]+), ([0-9.]+)f\};\s*\/\/ (.*)/g)) {
  have[`K${m[2]} 0x${m[3].toUpperCase()}/${parseInt(m[4], 16)}`] = `${m[1]} (${m[6]})`;
}
const skip = ['sg_device_time'];
let neu = 0;
for (const r of rows) {
  if (r.can === '?' || skip.includes(r.key)) continue;
  const h = have[r.can];
  if (h) console.log('SCHON  ', r.can.padEnd(16), r.key.padEnd(44), '= repo', h);
  else { neu++; console.log('NEU    ', r.can.padEnd(16), r.key.padEnd(44), `vs${r.vs} x${r.scale} ${r.signed ? 's' : 'u'} ${r.vm}`); }
}
console.log('neu:', neu);
