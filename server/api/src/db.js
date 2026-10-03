'use strict';
// Pool PostgreSQL condiviso dal processo.
const { Pool } = require('pg');
const config = require('./config');

const pool = new Pool({
  connectionString: config.databaseUrl,
  max: 10,
  idleTimeoutMillis: 30_000,
});

// Un client inattivo che cade non deve far morire il processo.
pool.on('error', (err) => {
  console.error(JSON.stringify({ ts: new Date().toISOString(), event: 'db_pool_error', error: err.message }));
});

const query = (text, params) => pool.query(text, params);

module.exports = { pool, query };
