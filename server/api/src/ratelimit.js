'use strict';
// Limiti di frequenza in memoria, a finestra fissa. Valgono per una sola
// istanza dell'api e si azzerano al riavvio.
const buckets = new Map();

// Ritorna 0 se si può procedere, altrimenti i secondi da aspettare.
function hit(key, limit, windowSeconds) {
  const now = Date.now();
  const b = buckets.get(key);
  if (!b || b.resetAt <= now) {
    buckets.set(key, { count: 1, resetAt: now + windowSeconds * 1000 });
    return 0;
  }
  b.count += 1;
  return b.count > limit ? Math.ceil((b.resetAt - now) / 1000) : 0;
}

function reset(key) {
  buckets.delete(key);
}

setInterval(() => {
  const now = Date.now();
  for (const [k, b] of buckets) if (b.resetAt <= now) buckets.delete(k);
}, 60_000).unref();

module.exports = { hit, reset };
