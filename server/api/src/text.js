'use strict';
// Pulizia dei testi inviati dagli utenti e pattern per le ricerche ILIKE.

// Caratteri di controllo tranne tab e a capo: i testi su più righe restano tali.
const CONTROL = /[\u0000-\u0008\u000b-\u001f\u007f]/g;
const CONTROL_ALL = /[\u0000-\u001f\u007f]/g;

const str = (v) => String(v == null ? '' : v);

// Testo su più righe, senza caratteri di controllo e spazi ai bordi.
const multiline = (v) => str(v).replace(CONTROL, '').trim();

// Testo su una riga: i caratteri di controllo diventano `sep`.
const oneLine = (v, sep = ' ') => str(v).replace(CONTROL_ALL, sep).trim();

// Lunghezza in caratteri (non in unità UTF-16: le emoji contano uno).
const charLength = (s) => [...s].length;

// `%q%` con i metacaratteri di LIKE resi letterali.
const likePattern = (q) => `%${String(q).replace(/[%_\\]/g, '\\$&')}%`;

module.exports = { multiline, oneLine, charLength, likePattern };
