// Incorpora remote/remote.html nel demone come stringa C (source/remote_page.c).
//   node tools/embed-remote.mjs
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
let html = fs.readFileSync(path.join(root, 'remote', 'remote.html'), 'utf8');
// le altre lingue (remote/i18n.json) entrano al posto del segnaposto /*T_MORE*/
const more = path.join(root, 'remote', 'i18n.json');
if (fs.existsSync(more)) {
  const tr = JSON.parse(fs.readFileSync(more, 'utf8'));
  const keys = Object.keys(JSON.parse(JSON.stringify(eval('(' + html.match(/en: (\{[\s\S]*?\}),\n    it:/)[1] + ')'))));
  for (const [code, d] of Object.entries(tr)) {
    const miss = keys.filter((k) => !d[k]);
    if (miss.length) throw new Error(`remote/i18n.json: ${code} senza ${miss.join(', ')}`);
  }
  html = html.replace('/*T_MORE*/', `Object.assign(T, ${JSON.stringify(tr)});`);
}
const lines = html.split('\n').map((l) => '  "' + l.replace(/\\/g, '\\\\').replace(/"/g, '\\"').replace(/\?\?/g, '?\\?') + '\\n"');
const out = `// Generato da tools/embed-remote.mjs da remote/remote.html: non modificare a mano.
#include <stddef.h>
const char REMOTE_HTML[] =
${lines.join('\n')};
const size_t REMOTE_HTML_LEN = sizeof REMOTE_HTML - 1;
`;
fs.writeFileSync(path.join(root, 'source', 'remote_page.c'), out);
console.log('remote_page.c:', Buffer.byteLength(html), 'byte');
