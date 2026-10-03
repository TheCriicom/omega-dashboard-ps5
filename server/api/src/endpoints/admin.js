'use strict';
// Pannello di amministrazione web (/admin): segnalazioni, homebrew, commenti,
// bacheca, utenti e registro delle azioni.
//
// Accesso con un account che ha role='admin'. Sessione propria in un cookie
// HttpOnly, Secure, SameSite=Strict firmato con il segreto di sessione; le
// richieste che modificano qualcosa devono avere X-Omega-Admin: 1 (CSRF).
// Ogni azione finisce in lab_admin_log.
const fsp = require('node:fs/promises');
const path = require('node:path');
const config = require('../config');
const db = require('../db');
const passwords = require('../passwords');
const session = require('../session');
const limiter = require('../ratelimit');
const { HttpError, retryLater, readJson } = require('../http');
const { likePattern } = require('../text');
const { removeMedia } = require('./media');

const TTL_H = 8;
const COOKIE = 'omega_admin';
const SIGN_SCOPE = 'admin:';
const UI_FILE = path.join(__dirname, '..', '..', 'admin-ui', 'index.html');
const SECURE = config.adminInsecureCookie ? '' : ' Secure;';

function cookieOf(req) {
  const m = new RegExp(`(?:^|;\\s*)${COOKIE}=([^;]+)`).exec(req.headers.cookie || '');
  return m ? m[1] : null;
}

// L'admin della richiesta, oppure null.
async function resolve(req) {
  const v = cookieOf(req);
  const m = v && /^([0-9a-f-]{36})\.([A-Za-z0-9_-]+)$/.exec(v);
  if (!m || !session.signatureMatches(m[1], m[2], SIGN_SCOPE)) return null;
  const r = await db.query(
    `SELECT s.session_id, a.account_id, a.online_id FROM lab_admin_session s JOIN lab_account a USING (account_id)
      WHERE s.session_id=$1 AND s.expires_at > now() AND a.role='admin' AND a.banned_at IS NULL AND NOT a.disabled`, [m[1]]);
  if (!r.rowCount) return null;
  return { accountId: String(r.rows[0].account_id), onlineId: r.rows[0].online_id, sessionId: r.rows[0].session_id };
}

function csrf(req) {
  if (req.method !== 'GET' && req.headers['x-omega-admin'] !== '1') throw new HttpError(403, 'csrf', 'intestazione X-Omega-Admin mancante');
}

async function log(admin, action, type, id, detail) {
  await db.query('INSERT INTO lab_admin_log (admin_id, admin_name, action, target_type, target_id, detail) VALUES ($1,$2,$3,$4,$5,$6)',
    [admin.accountId, admin.onlineId, action, type, id == null ? null : String(id), detail ? String(detail).slice(0, 500) : null]);
}

function paging(url) {
  const offset = Math.max(0, Number.parseInt(url.searchParams.get('offset') || '0', 10) || 0);
  const limit = Math.min(200, Math.max(1, Number.parseInt(url.searchParams.get('limit') || '50', 10) || 50));
  return { offset, limit };
}

// GET /admin
async function page({ res }) {
  let html;
  try { html = await fsp.readFile(UI_FILE); } catch { throw new HttpError(503, 'admin_ui_missing'); }
  res.writeHead(200, {
    'content-type': 'text/html; charset=utf-8', 'content-length': html.length, 'cache-control': 'no-store',
    'content-security-policy': "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src 'self' data:; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'",
    'x-frame-options': 'DENY', 'referrer-policy': 'no-referrer', 'x-content-type-options': 'nosniff',
  });
  res.end(html);
  return { sent: true };
}

// POST /admin/api/login { online_id, password }
async function login({ req, res, clientIp }) {
  csrf(req);
  const body = await readJson(req);
  const onlineId = String(body.online_id || '');
  const wait = Math.max(limiter.hit(`admin-login|${clientIp}`, 10, 900), limiter.hit(`admin-login-acc|${onlineId.toLowerCase()}`, 6, 900));
  if (wait) throw retryLater('too_many_attempts', wait, `riprova tra ${wait}s`);
  const a = (await db.query(
    'SELECT account_id, online_id, password_hash, role, banned_at, disabled FROM lab_account WHERE lower(online_id)=lower($1)', [onlineId])).rows[0];
  const pw = typeof body.password === 'string' ? body.password : '';
  const ok = a && a.password_hash && !a.disabled && !a.banned_at ? await passwords.verify(pw, a.password_hash) : await passwords.verifyDecoy(pw);
  if (!ok) throw new HttpError(401, 'invalid_credentials', 'credenziali non valide');
  if (a.role !== 'admin') throw new HttpError(403, 'not_admin', 'questo account non è amministratore');
  const s = (await db.query(`INSERT INTO lab_admin_session (account_id, expires_at) VALUES ($1, now() + interval '${TTL_H} hours') RETURNING session_id`, [a.account_id])).rows[0];
  await db.query('DELETE FROM lab_admin_session WHERE expires_at < now()');
  await log({ accountId: String(a.account_id), onlineId: a.online_id }, 'login', null, null, clientIp);
  const payload = JSON.stringify({ online_id: a.online_id, role: a.role });
  res.writeHead(200, {
    'content-type': 'application/json; charset=utf-8', 'content-length': Buffer.byteLength(payload), 'cache-control': 'no-store',
    'set-cookie': `${COOKIE}=${s.session_id}.${session.sign(s.session_id, SIGN_SCOPE)}; HttpOnly;${SECURE} SameSite=Strict; Path=/admin; Max-Age=${TTL_H * 3600}`,
  });
  res.end(payload);
  return { sent: true };
}

// POST /admin/api/logout
async function logout({ req, res, admin }) {
  csrf(req);
  await db.query('DELETE FROM lab_admin_session WHERE session_id=$1', [admin.sessionId]);
  res.writeHead(204, { 'set-cookie': `${COOKIE}=; HttpOnly;${SECURE} SameSite=Strict; Path=/admin; Max-Age=0` });
  res.end();
  return { sent: true };
}

// GET /admin/api/me
async function me({ admin }) { return { status: 200, body: { online_id: admin.onlineId, role: 'admin' } }; }

// GET /admin/api/stats
async function stats() {
  const r = (await db.query(`SELECT
    (SELECT count(*)::int FROM lab_account WHERE password_hash IS NOT NULL) AS users,
    (SELECT count(*)::int FROM lab_account WHERE banned_at IS NOT NULL) AS banned,
    (SELECT count(*)::int FROM lab_account WHERE role='admin') AS admins,
    (SELECT count(*)::int FROM lab_store_app) AS apps,
    (SELECT count(*)::int FROM lab_store_app WHERE NOT published) AS hidden_apps,
    (SELECT count(*)::int FROM lab_store_comment) AS comments,
    (SELECT count(*)::int FROM lab_store_comment WHERE hidden) AS hidden_comments,
    (SELECT count(*)::int FROM lab_store_report WHERE status='open') AS open_reports,
    (SELECT count(*)::int FROM lab_library_item) AS library_items,
    (SELECT count(*)::int FROM lab_session WHERE NOT revoked AND expires_at > now()) AS active_sessions,
    (SELECT count(*)::int FROM lab_post) AS posts,
    (SELECT count(*)::int FROM lab_post WHERE hidden) AS hidden_posts,
    (SELECT count(*)::int FROM lab_social_report WHERE status='open') AS open_social_reports,
    (SELECT count(*)::int FROM lab_group) AS groups`)).rows[0];
  return { status: 200, body: r };
}

// GET /admin/api/reports?status=open|resolved|dismissed|all
async function reports({ url }) {
  const st = url.searchParams.get('status') || 'open';
  const args = [];
  let where = '';
  if (st !== 'all') { args.push(st); where = 'WHERE r.status=$1'; }
  const rows = (await db.query(
    `SELECT r.report_id::text, r.status, r.reason, r.note, r.created_at, r.resolved_at, r.app_id::text, r.comment_id::text,
            rep.online_id AS reporter, res.online_id AS resolved_by,
            s.title, s.published, au.online_id AS app_author,
            c.body AS comment_body, c.hidden AS comment_hidden, ca.online_id AS comment_author,
            (SELECT count(*)::int FROM lab_store_report x WHERE x.app_id=r.app_id AND coalesce(x.comment_id,0)=coalesce(r.comment_id,0) AND x.status='open') AS open_for_target
       FROM lab_store_report r
       JOIN lab_store_app s ON s.app_id=r.app_id
       JOIN lab_account au ON au.account_id=s.author_id
       LEFT JOIN lab_account rep ON rep.account_id=r.reporter_id
       LEFT JOIN lab_account res ON res.account_id=r.resolved_by
       LEFT JOIN lab_store_comment c ON c.comment_id=r.comment_id
       LEFT JOIN lab_account ca ON ca.account_id=c.account_id
       ${where} ORDER BY r.created_at DESC LIMIT 300`, args)).rows;
  return {
    status: 200,
    body: {
      reports: rows.map((x) => ({
        report_id: x.report_id, status: x.status, reason: x.reason, note: x.note, created_at: x.created_at,
        reporter: x.reporter, resolved_by: x.resolved_by, resolved_at: x.resolved_at,
        target_type: x.comment_id ? 'comment' : 'app',
        app: { app_id: x.app_id, title: x.title, published: x.published, author: x.app_author },
        comment: x.comment_id ? { comment_id: x.comment_id, body: x.comment_body, author: x.comment_author, hidden: x.comment_hidden } : null,
        open_for_target: x.open_for_target,
      })),
    },
  };
}

// POST /admin/api/reports/:id { action: dismiss|hide|remove, note? }
async function reportAction({ req, admin, params }) {
  csrf(req);
  const body = await readJson(req);
  const r = (await db.query('SELECT report_id, app_id, comment_id, status FROM lab_store_report WHERE report_id=$1', [Number(params.id) || 0])).rows[0];
  if (!r) throw new HttpError(404, 'report_not_found');
  const action = String(body.action || '');
  const note = body.note ? String(body.note).slice(0, 500) : null;
  const sameTarget = `app_id=$1 AND coalesce(comment_id,0)=coalesce($2::bigint,0) AND status='open'`;
  if (action === 'dismiss') {
    await db.query(`UPDATE lab_store_report SET status='dismissed', resolved_by=$3, resolved_at=now() WHERE ${sameTarget}`, [r.app_id, r.comment_id, admin.accountId]);
  } else if (action === 'hide') {
    if (r.comment_id) await db.query('UPDATE lab_store_comment SET hidden=true WHERE comment_id=$1', [r.comment_id]);
    else await db.query('UPDATE lab_store_app SET published=false, hidden_reason=$2 WHERE app_id=$1', [r.app_id, note || 'Oscurato dalla moderazione']);
    await db.query(`UPDATE lab_store_report SET status='resolved', resolved_by=$3, resolved_at=now() WHERE ${sameTarget}`, [r.app_id, r.comment_id, admin.accountId]);
  } else if (action === 'remove') {
    // prima si chiudono le segnalazioni (restano nel registro), poi si elimina il contenuto
    await db.query(`UPDATE lab_store_report SET status='resolved', resolved_by=$3, resolved_at=now() WHERE ${sameTarget}`, [r.app_id, r.comment_id, admin.accountId]);
    if (r.comment_id) await db.query('DELETE FROM lab_store_comment WHERE comment_id=$1', [r.comment_id]);
    else await db.query('DELETE FROM lab_store_app WHERE app_id=$1', [r.app_id]);
  } else throw new HttpError(400, 'invalid_action');
  await log(admin, `report_${action}`, r.comment_id ? 'comment' : 'app', r.comment_id || r.app_id, note);
  return { status: 200, body: { result: 'ok' } };
}

// GET /admin/api/apps?q=&status=all|published|hidden
async function apps({ url }) {
  const { offset, limit } = paging(url);
  const q = String(url.searchParams.get('q') || '').trim();
  const st = url.searchParams.get('status') || 'all';
  const where = []; const args = [];
  if (q) { args.push(likePattern(q)); where.push(`(a.title ILIKE $${args.length} OR au.online_id ILIKE $${args.length})`); }
  if (st === 'published') where.push('a.published');
  if (st === 'hidden') where.push('NOT a.published');
  const w = where.length ? `WHERE ${where.join(' AND ')}` : '';
  const total = (await db.query(`SELECT count(*)::int AS n FROM lab_store_app a JOIN lab_account au ON au.account_id=a.author_id ${w}`, args)).rows[0].n;
  args.push(limit, offset);
  const rows = (await db.query(
    `SELECT a.app_id::text, a.title, a.tagline, au.online_id AS author, a.published, a.hidden_reason, a.downloads::int, a.created_at,
            a.catalog_key, a.download_url, a.file_kind, a.platform, a.category,
            (SELECT count(*)::int FROM lab_store_vote v WHERE v.app_id=a.app_id AND v.value=1) AS likes,
            (SELECT count(*)::int FROM lab_store_vote v WHERE v.app_id=a.app_id AND v.value=-1) AS dislikes,
            (SELECT round(coalesce(avg(stars),0)::numeric, 2)::float FROM lab_store_rating r WHERE r.app_id=a.app_id) AS rating,
            (SELECT count(*)::int FROM lab_store_comment c WHERE c.app_id=a.app_id) AS comments,
            (SELECT count(*)::int FROM lab_store_report r WHERE r.app_id=a.app_id AND r.status='open') AS reports_open
       FROM lab_store_app a JOIN lab_account au ON au.account_id=a.author_id ${w}
      ORDER BY reports_open DESC, a.created_at DESC LIMIT $${args.length - 1} OFFSET $${args.length}`, args)).rows;
  return { status: 200, body: { apps: rows, total } };
}

// POST /admin/api/apps/:id { action: hide|show, reason? }
async function appAction({ req, admin, params }) {
  csrf(req);
  const id = Number(params.id) || 0;
  const body = await readJson(req);
  const reason = body.reason ? String(body.reason).slice(0, 300) : null;
  let r;
  if (body.action === 'hide') r = await db.query('UPDATE lab_store_app SET published=false, hidden_reason=$2 WHERE app_id=$1', [id, reason || 'Oscurato dalla moderazione']);
  else if (body.action === 'show') r = await db.query('UPDATE lab_store_app SET published=true, hidden_reason=NULL WHERE app_id=$1', [id]);
  else throw new HttpError(400, 'invalid_action');
  if (!r.rowCount) throw new HttpError(404, 'app_not_found');
  await log(admin, `app_${body.action}`, 'app', id, reason);
  return { status: 200, body: { result: 'ok' } };
}
// DELETE /admin/api/apps/:id
async function appDelete({ req, admin, params }) {
  csrf(req);
  const id = Number(params.id) || 0;
  const t = (await db.query('DELETE FROM lab_store_app WHERE app_id=$1 RETURNING title', [id])).rows[0];
  if (!t) throw new HttpError(404, 'app_not_found');
  await log(admin, 'app_delete', 'app', id, t.title);
  return { status: 200, body: { result: 'ok' } };
}

// GET /admin/api/comments?q=&app_id=&status=all|visible|hidden
async function comments({ url }) {
  const { offset, limit } = paging(url);
  const q = String(url.searchParams.get('q') || '').trim();
  const appId = Number(url.searchParams.get('app_id') || 0);
  const st = url.searchParams.get('status') || 'all';
  const where = []; const args = [];
  if (q) { args.push(likePattern(q)); where.push(`(c.body ILIKE $${args.length} OR au.online_id ILIKE $${args.length})`); }
  if (appId) { args.push(appId); where.push(`c.app_id=$${args.length}`); }
  if (st === 'visible') where.push('NOT c.hidden');
  if (st === 'hidden') where.push('c.hidden');
  const w = where.length ? `WHERE ${where.join(' AND ')}` : '';
  const total = (await db.query(`SELECT count(*)::int AS n FROM lab_store_comment c JOIN lab_account au ON au.account_id=c.account_id ${w}`, args)).rows[0].n;
  args.push(limit, offset);
  const rows = (await db.query(
    `SELECT c.comment_id::text, c.app_id::text, s.title AS app_title, au.online_id AS author, c.body, c.hidden, c.created_at,
            (SELECT count(*)::int FROM lab_store_report r WHERE r.comment_id=c.comment_id AND r.status='open') AS reports_open
       FROM lab_store_comment c JOIN lab_account au ON au.account_id=c.account_id JOIN lab_store_app s ON s.app_id=c.app_id ${w}
      ORDER BY reports_open DESC, c.comment_id DESC LIMIT $${args.length - 1} OFFSET $${args.length}`, args)).rows;
  return { status: 200, body: { comments: rows, total } };
}
// POST /admin/api/comments/:id { action: hide|show }
async function commentAction({ req, admin, params }) {
  csrf(req);
  const id = Number(params.id) || 0;
  const body = await readJson(req);
  if (body.action !== 'hide' && body.action !== 'show') throw new HttpError(400, 'invalid_action');
  const r = await db.query('UPDATE lab_store_comment SET hidden=$2 WHERE comment_id=$1', [id, body.action === 'hide']);
  if (!r.rowCount) throw new HttpError(404, 'comment_not_found');
  await log(admin, `comment_${body.action}`, 'comment', id, null);
  return { status: 200, body: { result: 'ok' } };
}
// DELETE /admin/api/comments/:id
async function commentDelete({ req, admin, params }) {
  csrf(req);
  const id = Number(params.id) || 0;
  const t = (await db.query('DELETE FROM lab_store_comment WHERE comment_id=$1 RETURNING body', [id])).rows[0];
  if (!t) throw new HttpError(404, 'comment_not_found');
  await log(admin, 'comment_delete', 'comment', id, t.body);
  return { status: 200, body: { result: 'ok' } };
}

// GET /admin/api/social-reports?status=open|resolved|dismissed|all
async function socialReports({ url }) {
  const st = url.searchParams.get('status') || 'open';
  const args = [];
  let where = '';
  if (st !== 'all') { args.push(st); where = 'WHERE r.status=$1'; }
  const rows = (await db.query(
    `SELECT r.report_id::text, r.target_type, r.target_id::text, r.status, r.reason, r.note, r.snapshot, r.created_at, r.resolved_at,
            rep.online_id AS reporter, res.online_id AS resolved_by, own.online_id AS owner,
            p.body AS post_body, p.hidden AS post_hidden, p.game_name AS post_game,
            c.body AS comment_body, c.hidden AS comment_hidden, c.post_id::text AS comment_post_id,
            u.banned_at AS user_banned_at,
            (SELECT count(*)::int FROM lab_social_report x WHERE x.target_type=r.target_type AND x.target_id=r.target_id AND x.status='open') AS open_for_target
       FROM lab_social_report r
       LEFT JOIN lab_account rep ON rep.account_id=r.reporter_id
       LEFT JOIN lab_account res ON res.account_id=r.resolved_by
       LEFT JOIN lab_account own ON own.account_id=r.target_owner_id
       LEFT JOIN lab_post p ON r.target_type='post' AND p.post_id=r.target_id
       LEFT JOIN lab_post_comment c ON r.target_type='post_comment' AND c.comment_id=r.target_id
       LEFT JOIN lab_account u ON r.target_type='user' AND u.account_id=r.target_id
       ${where} ORDER BY r.created_at DESC LIMIT 300`, args)).rows;
  return {
    status: 200,
    body: {
      reports: rows.map((x) => {
        const exists = x.target_type === 'post' ? x.post_body != null : x.target_type === 'post_comment' ? x.comment_body != null : x.owner != null;
        return {
          report_id: x.report_id, status: x.status, reason: x.reason, note: x.note, created_at: x.created_at,
          reporter: x.reporter, resolved_by: x.resolved_by, resolved_at: x.resolved_at,
          target_type: x.target_type, target_id: x.target_id, owner: x.owner, exists,
          // testo attuale (o quello del momento della segnalazione, se il contenuto non c'è più)
          text: x.target_type === 'post' ? (x.post_body != null ? x.post_body : x.snapshot)
            : x.target_type === 'post_comment' ? (x.comment_body != null ? x.comment_body : x.snapshot) : x.snapshot,
          hidden: x.target_type === 'post' ? !!x.post_hidden : x.target_type === 'post_comment' ? !!x.comment_hidden : false,
          post_id: x.target_type === 'post' ? x.target_id : x.comment_post_id || null,
          game_name: x.post_game || null,
          banned: x.target_type === 'user' ? !!x.user_banned_at : undefined,
          open_for_target: x.open_for_target,
        };
      }),
    },
  };
}

// POST /admin/api/social-reports/:id { action: dismiss|resolve|hide|remove, note? }
// Per gli utenti: solo dismiss o resolve (il ban si dà dalla scheda Utenti).
async function socialReportAction({ req, admin, params }) {
  csrf(req);
  const body = await readJson(req);
  const r = (await db.query('SELECT report_id, target_type, target_id FROM lab_social_report WHERE report_id=$1', [Number(params.id) || 0])).rows[0];
  if (!r) throw new HttpError(404, 'report_not_found');
  const action = String(body.action || '');
  const note = body.note ? String(body.note).slice(0, 500) : null;
  const close = (status) => db.query(
    `UPDATE lab_social_report SET status=$4, resolved_by=$3, resolved_at=now()
      WHERE target_type=$1 AND target_id=$2 AND status='open'`, [r.target_type, r.target_id, admin.accountId, status]);
  const isUser = r.target_type === 'user';
  if (action === 'dismiss') await close('dismissed');
  else if (action === 'resolve') await close('resolved');
  else if (action === 'hide' && !isUser) {
    if (r.target_type === 'post') await db.query('UPDATE lab_post SET hidden=true, hidden_reason=$2 WHERE post_id=$1', [r.target_id, note || 'Oscurato dalla moderazione']);
    else await db.query('UPDATE lab_post_comment SET hidden=true WHERE comment_id=$1', [r.target_id]);
    await close('resolved');
  } else if (action === 'remove' && !isUser) {
    await close('resolved');
    if (r.target_type === 'post') await db.query('DELETE FROM lab_post WHERE post_id=$1', [r.target_id]);
    else await db.query('DELETE FROM lab_post_comment WHERE comment_id=$1', [r.target_id]);
  } else throw new HttpError(400, 'invalid_action');
  await log(admin, `social_report_${action}`, r.target_type, r.target_id, note);
  return { status: 200, body: { result: 'ok' } };
}

// GET /admin/api/posts?q=&status=all|visible|hidden
async function posts({ url }) {
  const { offset, limit } = paging(url);
  const q = String(url.searchParams.get('q') || '').trim();
  const st = url.searchParams.get('status') || 'all';
  const where = []; const args = [];
  if (q) { args.push(likePattern(q)); where.push(`(p.body ILIKE $${args.length} OR au.online_id ILIKE $${args.length})`); }
  if (st === 'visible') where.push('NOT p.hidden');
  if (st === 'hidden') where.push('p.hidden');
  const w = where.length ? `WHERE ${where.join(' AND ')}` : '';
  const total = (await db.query(`SELECT count(*)::int AS n FROM lab_post p JOIN lab_account au ON au.account_id=p.author_id ${w}`, args)).rows[0].n;
  args.push(limit, offset);
  const rows = (await db.query(
    `SELECT p.post_id::text, au.online_id AS author, p.body AS text, p.game_id, p.game_name, p.hidden, p.hidden_reason, p.created_at,
            (SELECT count(*)::int FROM lab_post_like l WHERE l.post_id=p.post_id) AS likes,
            (SELECT count(*)::int FROM lab_post_comment c WHERE c.post_id=p.post_id) AS comments,
            (SELECT count(*)::int FROM lab_social_report r WHERE r.target_type='post' AND r.target_id=p.post_id AND r.status='open') AS reports_open
       FROM lab_post p JOIN lab_account au ON au.account_id=p.author_id ${w}
      ORDER BY reports_open DESC, p.post_id DESC LIMIT $${args.length - 1} OFFSET $${args.length}`, args)).rows;
  return { status: 200, body: { posts: rows, total } };
}

// POST /admin/api/posts/:id { action: hide|show, reason? }
async function postAction({ req, admin, params }) {
  csrf(req);
  const id = Number(params.id) || 0;
  const body = await readJson(req);
  const reason = body.reason ? String(body.reason).slice(0, 300) : null;
  let r;
  if (body.action === 'hide') r = await db.query('UPDATE lab_post SET hidden=true, hidden_reason=$2 WHERE post_id=$1', [id, reason || 'Oscurato dalla moderazione']);
  else if (body.action === 'show') r = await db.query('UPDATE lab_post SET hidden=false, hidden_reason=NULL WHERE post_id=$1', [id]);
  else throw new HttpError(400, 'invalid_action');
  if (!r.rowCount) throw new HttpError(404, 'post_not_found');
  await log(admin, `post_${body.action}`, 'post', id, reason);
  return { status: 200, body: { result: 'ok' } };
}

// DELETE /admin/api/posts/:id
async function postDelete({ req, admin, params }) {
  csrf(req);
  const id = Number(params.id) || 0;
  const t = (await db.query('DELETE FROM lab_post WHERE post_id=$1 RETURNING body', [id])).rows[0];
  if (!t) throw new HttpError(404, 'post_not_found');
  await log(admin, 'post_delete', 'post', id, t.body);
  return { status: 200, body: { result: 'ok' } };
}

// GET /admin/api/users?q=&status=all|banned|admins
async function users({ url }) {
  const { offset, limit } = paging(url);
  const q = String(url.searchParams.get('q') || '').trim();
  const st = url.searchParams.get('status') || 'all';
  const where = ['a.password_hash IS NOT NULL']; const args = [];
  if (q) { args.push(likePattern(q)); where.push(`(a.online_id ILIKE $${args.length} OR a.email ILIKE $${args.length})`); }
  if (st === 'banned') where.push('a.banned_at IS NOT NULL');
  if (st === 'admins') where.push(`a.role='admin'`);
  const w = `WHERE ${where.join(' AND ')}`;
  const total = (await db.query(`SELECT count(*)::int AS n FROM lab_account a ${w}`, args)).rows[0].n;
  args.push(limit, offset);
  const rows = (await db.query(
    `SELECT a.account_id::text, a.online_id, a.email, a.role, a.created_at, a.last_login_at, a.banned_at, a.ban_reason,
            (SELECT count(*)::int FROM lab_store_app s WHERE s.author_id=a.account_id) AS apps,
            (SELECT count(*)::int FROM lab_store_comment c WHERE c.account_id=a.account_id) AS comments,
            (SELECT count(*)::int FROM lab_store_report r JOIN lab_store_app s ON s.app_id=r.app_id
               LEFT JOIN lab_store_comment c ON c.comment_id=r.comment_id
              WHERE r.status='open' AND ((r.comment_id IS NULL AND s.author_id=a.account_id) OR c.account_id=a.account_id))
            + (SELECT count(*)::int FROM lab_social_report sr WHERE sr.status='open' AND sr.target_owner_id=a.account_id) AS reports_against
       FROM lab_account a ${w} ORDER BY a.banned_at DESC NULLS LAST, a.created_at DESC LIMIT $${args.length - 1} OFFSET $${args.length}`, args)).rows;
  return { status: 200, body: { users: rows, total } };
}

// POST /admin/api/users/:id { action: ban|unban|make_admin|remove_admin|logout_all, reason? }
async function userAction({ req, admin, params }) {
  csrf(req);
  const id = String(Number(params.id) || 0);
  const body = await readJson(req);
  const reason = body.reason ? String(body.reason).slice(0, 300) : null;
  const u = (await db.query('SELECT online_id, role FROM lab_account WHERE account_id=$1', [id])).rows[0];
  if (!u) throw new HttpError(404, 'user_not_found');
  const self = id === admin.accountId;
  switch (body.action) {
    case 'ban':
      if (self) throw new HttpError(400, 'cannot_ban_self');
      await db.query('UPDATE lab_account SET banned_at=now(), ban_reason=$2, disabled=true WHERE account_id=$1', [id, reason || 'Violazione dei termini d\'uso']);
      await session.revokeAll(id);
      await db.query('DELETE FROM lab_admin_session WHERE account_id=$1', [id]);
      // i suoi homebrew spariscono dallo Store finché il ban resta
      await db.query(`UPDATE lab_store_app SET published=false, hidden_reason='Autore sospeso' WHERE author_id=$1 AND published`, [id]);
      break;
    case 'unban':
      await db.query('UPDATE lab_account SET banned_at=NULL, ban_reason=NULL, disabled=false WHERE account_id=$1', [id]);
      await db.query(`UPDATE lab_store_app SET published=true, hidden_reason=NULL WHERE author_id=$1 AND hidden_reason='Autore sospeso'`, [id]);
      break;
    case 'make_admin': await db.query(`UPDATE lab_account SET role='admin' WHERE account_id=$1`, [id]); break;
    case 'remove_admin':
      if (self) throw new HttpError(400, 'cannot_demote_self');
      await db.query(`UPDATE lab_account SET role='user' WHERE account_id=$1`, [id]);
      await db.query('DELETE FROM lab_admin_session WHERE account_id=$1', [id]);
      break;
    case 'logout_all': await session.revokeAll(id); break;
    default: throw new HttpError(400, 'invalid_action');
  }
  await log(admin, `user_${body.action}`, 'user', id, `${u.online_id}${reason ? `: ${reason}` : ''}`);
  return { status: 200, body: { result: 'ok' } };
}

// DELETE /admin/api/users/:id — account e tutti i suoi contenuti
async function userDelete({ req, admin, params }) {
  csrf(req);
  const id = String(Number(params.id) || 0);
  if (id === admin.accountId) throw new HttpError(400, 'cannot_delete_self');
  const u = (await db.query('DELETE FROM lab_account WHERE account_id=$1 RETURNING online_id, avatar_media, cover_media', [id])).rows[0];
  if (!u) throw new HttpError(404, 'user_not_found');
  await removeMedia(u.avatar_media);
  await removeMedia(u.cover_media);
  await log(admin, 'user_delete', 'user', id, u.online_id);
  return { status: 200, body: { result: 'ok' } };
}

// GET /admin/api/log
async function auditLog({ url }) {
  const { offset, limit } = paging(url);
  const total = (await db.query('SELECT count(*)::int AS n FROM lab_admin_log')).rows[0].n;
  const rows = (await db.query(
    `SELECT created_at, admin_name AS admin, action, target_type, target_id, detail FROM lab_admin_log
      ORDER BY log_id DESC LIMIT $1 OFFSET $2`, [limit, offset])).rows;
  return { status: 200, body: { log: rows, total } };
}

module.exports = {
  resolve, page, login, logout, me, stats, reports, reportAction, apps, appAction, appDelete,
  socialReports, socialReportAction, posts, postAction, postDelete,
  comments, commentAction, commentDelete, users, userAction, userDelete, auditLog,
};
