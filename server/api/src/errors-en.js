'use strict';
// "detail" degli errori in inglese, per le richieste in una lingua diversa
// dall'italiano. È solo una cortesia per chi legge le risposte a mano: l'app
// traduce da sé il codice "error" e non deve dipendere da questi testi.
// Un dettaglio non elencato resta in italiano.
const RULES = [
  [/^accesso amministratore richiesto$/, 'administrator access required'],
  [/^serve un token di sessione: (.+)$/, 'a session token is required: $1'],
  [/^online_id: 3-16 caratteri, lettera iniziale, (.+)$/, 'online_id: 3-16 characters, starting with a letter, $1'],
  [/^la password deve avere tra (\d+) e (\d+) caratteri$/, 'the password must be between $1 and $2 characters long'],
  [/^la password non può contenere l'online_id$/, 'the password cannot contain the online_id'],
  [/^serve accettare termini d'uso e informativa privacy$/, 'you must accept the terms of use and the privacy notice'],
  [/^serve la chiave di registrazione$/, 'a registration key is required'],
  [/^online_id o password non validi$/, 'invalid online_id or password'],
  [/^credenziali non valide$/, 'invalid credentials'],
  [/^password attuale non valida$/, 'current password is not valid'],
  [/^password non valida$/, 'invalid password'],
  [/^account sospeso per violazione dei termini d'uso$/, 'account suspended for violating the terms of use'],
  [/^intestazione X-Omega-Admin mancante$/, 'missing X-Omega-Admin header'],
  [/^questo account non è amministratore$/, 'this account is not an administrator'],
  [/^conferma di essere l'autore o che la licenza permette la redistribuzione$/, 'confirm that you are the author or that the license allows redistribution'],
  [/^serve un URL http\(s\)$/, 'an http(s) URL is required'],
  [/^imposta prima l'URL della tua libreria$/, 'first set the URL of your library'],
  [/^il JSON non è valido o non è raggiungibile$/, 'the JSON is invalid or unreachable'],
  [/^nome da (\d+) a (\d+) caratteri$/, 'name of $1 to $2 characters'],
  [/^da (\d+) a (\d+) amici$/, '$1 to $2 friends'],
  [/^massimo (\d+) caratteri$/, 'maximum $1 characters'],
  [/^massimo (\d+) membri$/, 'maximum $1 members'],
  [/^massimo (\d+) byte$/, 'maximum $1 bytes'],
  [/^massimo (\d+) MB$/, 'maximum $1 MB'],
  [/^versione attuale: (.+)$/, 'current version: $1'],
];

function toEnglish(detail) {
  if (typeof detail !== 'string') return detail;
  for (const [re, en] of RULES) if (re.test(detail)) return detail.replace(re, en);
  return detail;
}

module.exports = { toEnglish };
