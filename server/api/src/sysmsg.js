'use strict';
// Messaggi di sistema di party e gruppi ("x si è unito al party"...): nel
// database restano in italiano, com'erano; si traducono quando si leggono,
// nella lingua di chi li legge (sezione "system" di src/i18n/<codice>.json).
const { t } = require('./messages');

const PATTERNS = [
  [/^· (\S+) ha creato il party$/, 'system.party_created', ['actor']],
  [/^· (\S+) si è unito al party$/, 'system.party_joined', ['actor']],
  [/^· (\S+) ha lasciato il party$/, 'system.party_left', ['actor']],
  [/^(\S+) ha creato il gruppo «(.*)»$/, 'system.group_created', ['actor', 'name']],
  [/^(\S+) ha aggiunto (\S+)$/, 'system.group_added', ['actor', 'user']],
  [/^(\S+) è uscito dal gruppo$/, 'system.group_left', ['actor']],
  [/^(\S+) ha rinominato il gruppo in «(.*)»$/, 'system.group_renamed', ['actor', 'name']],
  [/^(\S+) ha rimosso (\S+)$/, 'system.group_removed', ['actor', 'user']],
];

function translate(text, code) {
  if (!code || code === 'it' || typeof text !== 'string') return text;
  for (const [re, key, names] of PATTERNS) {
    const m = re.exec(text);
    if (!m) continue;
    const vars = Object.fromEntries(names.map((n, i) => [n, m[i + 1]]));
    const out = t(code, key, vars);
    return out === key ? text : out;
  }
  return text;
}

module.exports = { translate };
