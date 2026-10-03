'use strict';
// Ruolo amministratore da riga di comando. Serve per il primo admin: gli altri
// si nominano anche dal pannello /admin.
//
//   node src/admin.js grant|revoke <online_id>
//   node src/admin.js list
const db = require('./db');

async function main() {
  const [cmd, onlineId] = process.argv.slice(2);
  if (cmd === 'list') {
    const r = await db.query(`SELECT online_id, created_at FROM lab_account WHERE role='admin' ORDER BY online_id`);
    if (!r.rowCount) console.log('nessun amministratore');
    for (const a of r.rows) console.log(`${a.online_id}\t(dal ${a.created_at.toISOString().slice(0, 10)})`);
    return 0;
  }
  if ((cmd !== 'grant' && cmd !== 'revoke') || !onlineId) {
    console.error('uso: node src/admin.js grant|revoke <online_id>  |  node src/admin.js list');
    return 2;
  }
  const role = cmd === 'grant' ? 'admin' : 'user';
  const r = await db.query('UPDATE lab_account SET role=$2 WHERE lower(online_id)=lower($1) AND password_hash IS NOT NULL RETURNING online_id', [onlineId, role]);
  if (!r.rowCount) { console.error(`account "${onlineId}" non trovato`); return 1; }
  if (role === 'user') await db.query('DELETE FROM lab_admin_session s USING lab_account a WHERE a.account_id=s.account_id AND a.online_id=$1', [r.rows[0].online_id]);
  await db.query('INSERT INTO lab_admin_log (admin_name, action, target_type, detail) VALUES ($1,$2,$3,$4)',
    ['(riga di comando)', cmd === 'grant' ? 'user_make_admin' : 'user_remove_admin', 'user', r.rows[0].online_id]);
  console.log(`${r.rows[0].online_id}: ruolo ${role}`);
  return 0;
}

main().then((code) => db.pool.end().then(() => process.exit(code)))
  .catch((err) => { console.error(err.message); process.exit(1); });
