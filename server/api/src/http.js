'use strict';
// Risposte JSON, lettura del corpo ed errori HTTP condivisi dagli endpoint.

const MAX_BODY = 1024 * 1024;

class HttpError extends Error {
  constructor(status, code, detail, headers) {
    super(detail || code);
    this.status = status;
    this.code = code;
    if (headers) this.headers = headers;
  }
}

// 429 con Retry-After, per i limiti di frequenza.
function retryLater(code, seconds, detail) {
  return new HttpError(429, code, detail, { 'retry-after': String(seconds) });
}

function send(res, status, body, headers = {}) {
  const payload = body === undefined ? '' : JSON.stringify(body);
  res.writeHead(status, {
    'content-type': 'application/json; charset=utf-8',
    'content-length': Buffer.byteLength(payload),
    ...headers,
  });
  res.end(payload);
}

function readBody(req, max = MAX_BODY) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    let size = 0;
    req.on('data', (c) => {
      size += c.length;
      if (size > max) {
        reject(new HttpError(413, 'payload_too_large'));
        req.destroy();
        return;
      }
      chunks.push(c);
    });
    req.on('end', () => resolve(Buffer.concat(chunks)));
    req.on('error', reject);
  });
}

async function readJson(req) {
  const raw = await readBody(req);
  if (raw.length === 0) return {};
  try {
    return JSON.parse(raw.toString('utf8'));
  } catch {
    throw new HttpError(400, 'invalid_json');
  }
}

module.exports = { HttpError, retryLater, send, readBody, readJson };
