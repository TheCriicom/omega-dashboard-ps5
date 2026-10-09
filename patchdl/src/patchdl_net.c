/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Knutwurst
 *
 * PatchDL is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version. See the LICENSE file in the project root for details.
 */

#include "patchdl_net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef PATCHDL_HAVE_CURL
#include <curl/curl.h>
#include <openssl/evp.h>
#include "patchdl_ca.h"
#endif

#define DNS_SERVER     "1.1.1.1"
#define DNS_PORT       53
#define DNS_TIMEOUT_MS 3000

/* Manifest sanity caps — reject anything bigger than a real PS5 patch. The
   largest title we've seen tops out around 70 GB / 18 pieces. */
#define PATCHDL_MAX_PIECES         4096
#define PATCHDL_MAX_PIECE_BYTES    (8ULL  * 1024 * 1024 * 1024)   /* 8 GiB  */
#define PATCHDL_MAX_TOTAL_BYTES    (200ULL * 1024 * 1024 * 1024)  /* 200 GiB */

/* In-RAM buffer caps for full HTTP body fetches. version.xml is a few KB,
   manifest JSON is a few MB at most — fail-closed beyond that. The
   manifest cap is sized for ~4096 pieces × ~3 KB JSON each with plenty of
   headroom; real PS5 manifests are 1-2 MB. */
#define PATCHDL_BUF_MAX_VERXML     (4   * 1024 * 1024)
#define PATCHDL_BUF_MAX_MANIFEST   (16  * 1024 * 1024)

#ifdef PATCHDL_HAVE_CURL
/* Replacement for fopen("wb"/"r+b") that refuses to follow a symlink at the
   destination (would let a malicious symlink redirect the download) and pins
   the new file's mode to 0600. Returns NULL on any open error. */
static FILE *
fopen_safe(const char *path, int rw_existing) {
    int flags = O_CLOEXEC | O_NOFOLLOW;
    int fd;
    flags |= rw_existing ? O_RDWR : (O_WRONLY | O_CREAT | O_TRUNC);
    fd = open(path, flags, 0600);
    if (fd < 0) return NULL;
    return fdopen(fd, rw_existing ? "r+b" : "wb");
}
#endif

#ifdef PATCHDL_HAVE_CURL

static const char *ALLOWED_HOSTS[] = {
    "sgst.prod.dl.playstation.net",
    "gst.prod.dl.playstation.net",
    "gs2.ww.prod.dl.playstation.net",
    NULL,
};

static int
host_allowed(const char *host) {
    size_t hlen = strlen(host);
    /* DNS is case-insensitive; an upstream redirect to "SGST.prod..." would
       otherwise drop out of the allowlist. */
    for (int i = 0; ALLOWED_HOSTS[i]; i++) {
        if (!strcasecmp(host, ALLOWED_HOSTS[i]))
            return 1;
        size_t alen = strlen(ALLOWED_HOSTS[i]);
        if (hlen > alen + 1 &&
            host[hlen - alen - 1] == '.' &&
            !strcasecmp(host + hlen - alen, ALLOWED_HOSTS[i]))
            return 1;
    }
    return 0;
}

/* Extract hostname from https://host/path or http://host/path */
static int
url_host(const char *url, char *out, size_t out_sz) {
    const char *p = strstr(url, "://");
    const char *end;
    size_t len;

    if (!p) return -1;
    p += 3;
    end = strpbrk(p, "/:?");
    len = end ? (size_t)(end - p) : strlen(p);
    if (len == 0 || len >= out_sz) return -1;
    memcpy(out, p, len);
    out[len] = '\0';
    return 0;
}

/* ---------- raw UDP DNS resolver ---------------------------------------- */

/* Diagnostics from the last dns_resolve() call (read by patchdl_net_diag).
   step: 0=ok 1=socket 2=sendto 3=recv<12 4=no-answer. */
static int g_dns_step, g_dns_errno, g_dns_n, g_dns_ancount;

static int
dns_encode_name(const char *host, uint8_t *buf, size_t bufsz) {
    size_t pos = 0;
    const char *p = host;

    while (*p) {
        const char *dot = strchr(p, '.');
        size_t lablen = dot ? (size_t)(dot - p) : strlen(p);
        if (lablen > 63 || pos + 1 + lablen + 1 >= bufsz) return -1;
        buf[pos++] = (uint8_t)lablen;
        memcpy(buf + pos, p, lablen);
        pos += lablen;
        p += lablen;
        if (*p == '.') p++;
    }
    buf[pos++] = 0;
    return (int)pos;
}

static int
dns_resolve(const char *host, char *ip_out, size_t ip_sz) {
    int              fd;
    struct sockaddr_in srv;
    uint8_t          pkt[512], resp[512];
    ssize_t          n;
    int              name_len;
    size_t           pos;
    struct timeval   tv;

    g_dns_step = 0; g_dns_errno = 0; g_dns_n = 0; g_dns_ancount = 0;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { g_dns_step = 1; g_dns_errno = errno; return -1; }

    tv.tv_sec  = DNS_TIMEOUT_MS / 1000;
    tv.tv_usec = (DNS_TIMEOUT_MS % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    memset(&srv, 0, sizeof(srv));
    srv.sin_family      = AF_INET;
    srv.sin_port        = htons(DNS_PORT);
    srv.sin_addr.s_addr = inet_addr(DNS_SERVER);

    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0xDE; pkt[1] = 0xAD; /* ID */
    pkt[2] = 0x01; pkt[3] = 0x00; /* standard query, RD */
    pkt[4] = 0x00; pkt[5] = 0x01; /* QDCOUNT = 1 */
    pos = 12;

    name_len = dns_encode_name(host, pkt + pos, sizeof(pkt) - pos - 4);
    if (name_len < 0) { close(fd); return -1; }
    pos += (size_t)name_len;
    pkt[pos++] = 0x00; pkt[pos++] = 0x01; /* QTYPE = A */
    pkt[pos++] = 0x00; pkt[pos++] = 0x01; /* QCLASS = IN */

    if (sendto(fd, pkt, pos, 0,
               (struct sockaddr *)&srv, sizeof(srv)) < 0) {
        g_dns_step = 2; g_dns_errno = errno; close(fd); return -1;
    }

    n = recv(fd, resp, sizeof(resp), 0);
    g_dns_n = (int)n;
    if (n < 0) g_dns_errno = errno;
    close(fd);
    if (n < 12) { g_dns_step = 3; return -1; }

    uint16_t ancount = (uint16_t)((resp[6] << 8) | resp[7]);
    g_dns_ancount = ancount;
    if (!ancount) { g_dns_step = 4; return -1; }

    /* Skip header and question section */
    pos = 12;
    while (pos < (size_t)n) {
        if (!resp[pos]) { pos++; break; }
        if ((resp[pos] & 0xC0) == 0xC0) { pos += 2; break; }
        /* Bounds-check the label length BEFORE the increment — a malformed
           response with a 0xFF label byte near the end would otherwise walk
           past `n`. */
        if (pos + 1 + (size_t)resp[pos] >= (size_t)n) { g_dns_step = 5; return -1; }
        pos += 1 + resp[pos];
    }
    if (pos + 4 > (size_t)n) { g_dns_step = 5; return -1; }
    pos += 4; /* QTYPE + QCLASS */

    /* Walk every answer RR; the CDN returns a CNAME chain, so skip
       non-A records and return the first A (type 1, rdlen 4). */
    for (int a = 0; a < ancount && pos < (size_t)n; a++) {
        if ((resp[pos] & 0xC0) == 0xC0) {
            pos += 2;
        } else {
            while (pos < (size_t)n && resp[pos]) {
                if (pos + 1 + (size_t)resp[pos] >= (size_t)n) break;
                pos += 1 + resp[pos];
            }
            pos++;
        }
        if (pos + 10 > (size_t)n) break;

        uint16_t type  = (uint16_t)((resp[pos] << 8) | resp[pos + 1]); pos += 2;
        pos += 6; /* CLASS + TTL */
        uint16_t rdlen = (uint16_t)((resp[pos] << 8) | resp[pos + 1]); pos += 2;

        if (type == 1 && rdlen == 4 && pos + 4 <= (size_t)n) {
            snprintf(ip_out, ip_sz, "%u.%u.%u.%u",
                     resp[pos], resp[pos + 1], resp[pos + 2], resp[pos + 3]);
            return 0;
        }
        pos += rdlen; /* skip this RR's data (e.g. a CNAME target) */
    }

    g_dns_step = 6; /* no A record among the answers */
    return -1;
}

/* Cache resolved IPs by host. The batch fetch hits the same CDN host dozens
   of times; a burst of identical raw-UDP queries gets dropped/rate-limited,
   so resolve once (with retries) and reuse. */
static pthread_mutex_t dns_cache_mtx = PTHREAD_MUTEX_INITIALIZER;
static struct { char host[256]; char ip[INET_ADDRSTRLEN]; } dns_cache[64];
static int dns_cache_n;

static int
dns_lookup(const char *host, char *ip_out, size_t ip_sz) {
    int rc = -1;

    pthread_mutex_lock(&dns_cache_mtx);
    for (int i = 0; i < dns_cache_n; i++) {
        if (!strcmp(dns_cache[i].host, host)) {
            strncpy(ip_out, dns_cache[i].ip, ip_sz - 1);
            ip_out[ip_sz - 1] = '\0';
            pthread_mutex_unlock(&dns_cache_mtx);
            return 0;
        }
    }
    /* Cold miss: resolve while HOLDING the cache lock (single-flight). N pool
       workers needing the same CDN host would otherwise each blast Sony's
       rate-limited resolver; this way one resolves and the rest get the cache.
       It also serializes dns_resolve so its diagnostic globals can't be raced.
       (This is the DNS lock, independent of the pool lock.) */
    for (int attempt = 0; attempt < 4 && rc; attempt++)
        rc = dns_resolve(host, ip_out, ip_sz);
    if (!rc && dns_cache_n < (int)(sizeof(dns_cache) / sizeof(dns_cache[0]))) {
        strncpy(dns_cache[dns_cache_n].host, host,
                sizeof(dns_cache[0].host) - 1);
        strncpy(dns_cache[dns_cache_n].ip, ip_out,
                sizeof(dns_cache[0].ip) - 1);
        dns_cache_n++;
    }
    pthread_mutex_unlock(&dns_cache_mtx);
    return rc;
}

/* ---------- HTTP GET via curl ------------------------------------------- */

static size_t
write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    patchdl_buf_t *b     = userdata;
    size_t         total = size * nmemb;
    char          *newp;
    /* Overflow guard before the cap check (b->size+total may wrap on 32-bit). */
    if (total > (size_t)-1 - b->size - 1) return 0;
    /* Cap accumulation so a hostile CDN can't drive unbounded RAM growth. */
    if (b->max && b->size + total > b->max) return 0;
    newp = realloc(b->data, b->size + total + 1);
    if (!newp) return 0;
    b->data = newp;
    memcpy(b->data + b->size, ptr, total);
    b->size += total;
    b->data[b->size] = '\0';
    return total;
}

int
patchdl_http_get(const char *url, patchdl_buf_t *out) {
    CURL             *curl;
    CURLcode          res;
    char              host[256], ip[INET_ADDRSTRLEN];
    char              resolve_str[512];
    struct curl_slist *resolve_list = NULL;
    struct curl_blob  ca_blob;

    if (url_host(url, host, sizeof(host))) return -1;
    if (!host_allowed(host)) return -1;
    if (dns_lookup(host, ip, sizeof(ip))) return -1;

    snprintf(resolve_str, sizeof(resolve_str), "%s:443:%s", host, ip);
    resolve_list = curl_slist_append(NULL, resolve_str);

    /* Also cover port 80 in case a redirect lands there */
    char resolve_80[512];
    snprintf(resolve_80, sizeof(resolve_80), "%s:80:%s", host, ip);
    resolve_list = curl_slist_append(resolve_list, resolve_80);

    {
        size_t caller_max = out->max;
        memset(out, 0, sizeof(*out));
        out->max = caller_max;
    }

    curl = curl_easy_init();
    if (!curl) { curl_slist_free_all(resolve_list); return -1; }

    /* Trust only the embedded SCEI DNAS Root (Sony's private CDN CA).
       CAINFO_BLOB keeps the cert in memory — no file written anywhere. */
    ca_blob.data  = (void *)PATCHDL_SCEI_DNAS_ROOT_PEM;
    ca_blob.len   = strlen(PATCHDL_SCEI_DNAS_ROOT_PEM);
    ca_blob.flags = CURL_BLOB_COPY;

    curl_easy_setopt(curl, CURLOPT_URL,             url);
    curl_easy_setopt(curl, CURLOPT_RESOLVE,         resolve_list);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,   write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,       out);
    curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB,     &ca_blob);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER,  1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST,  2L);
    /* Some Sony CDN edges still serve SHA-1-signed leaf certs, which
       OpenSSL 3.x rejects at the default security level. Lower it so the
       chain still verifies against the pinned SCEI root. */
    curl_easy_setopt(curl, CURLOPT_SSL_CIPHER_LIST, "DEFAULT@SECLEVEL=0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,         15L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION,  1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS,       3L);
    /* Redirects must stay on HTTPS — host_allowed gates the initial URL, but
       once libcurl follows a 302 we want the protocol pinned too. The _STR
       variants replaced the bitfield options in libcurl 7.85. */
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR,       "https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL,        1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,       "patchdl/1.0");

    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    curl_slist_free_all(resolve_list);

    if (res != CURLE_OK) {
        free(out->data);
        out->data = NULL;
        out->size = 0;
        return -1;
    }
    return 0;
}

/* Write sink: tees the body to the file and, when verifying, into a running
   SHA-256. curl always calls with size==1, so nmemb is the byte count. */
typedef struct {
    FILE       *fp;
    EVP_MD_CTX *md;   /* NULL when not verifying */
} write_sink_t;

static size_t
file_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    write_sink_t *s = (write_sink_t *)userdata;
    size_t written = fwrite(ptr, size, nmemb, s->fp);
    if (s->md && written)
        EVP_DigestUpdate(s->md, ptr, written * size);
    return written;
}

/* Hex-encode a digest, lowercase. */
static void
hex_encode(const unsigned char *d, unsigned int len, char *out, size_t out_sz) {
    static const char hexd[] = "0123456789abcdef";
    unsigned int i;
    for (i = 0; i < len && (2u * i + 2u) < out_sz; i++) {
        out[2 * i]     = hexd[(d[i] >> 4) & 0xf];
        out[2 * i + 1] = hexd[d[i] & 0xf];
    }
    out[2 * i] = '\0';
}

typedef struct {
    patchdl_download_progress_cb cb;
    void *ctx;
    long long base;
    long long total;
} progress_state_t;

static int
curl_progress_cb(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                 curl_off_t ultotal, curl_off_t ulnow) {
    progress_state_t *p = (progress_state_t *)clientp;
    long long total;

    (void)ultotal;
    (void)ulnow;
    if (!p || !p->cb) return 0;

    total = p->total > 0 ? p->total : (long long)dltotal;
    /* A non-zero return aborts the transfer (CURLE_ABORTED_BY_CALLBACK),
       which is how a cancel request stops a piece mid-flight. */
    return p->cb(p->ctx, p->base + (long long)dlnow, total);
}

/* Returns 0 on success, -1 on download/network failure, -2 when an expected
   SHA-256 was given and the downloaded bytes did not match it, -3 when a byte
   range was requested (range_start>0) but the server ignored it (no HTTP 206).
   When range_start>0 the body is appended at the file's current position, so
   the caller must have it positioned at range_start and must not verify. */
static int
http_download_to_file_progress(const char *url, FILE *fp, long long *bytes_out,
                               progress_state_t *progress,
                               const char *expected_sha256_hex,
                               long long range_start) {
    CURL             *curl;
    CURLcode          res;
    char              host[256], ip[INET_ADDRSTRLEN], rs443[512], rs80[512];
    char              range_hdr[48];
    long              http_code = 0;
    struct curl_slist *rl = NULL;
    struct curl_blob  ca_blob;
    curl_off_t        dl = 0;
    write_sink_t      sink = { fp, NULL };
    int               verify = (expected_sha256_hex && expected_sha256_hex[0]);

    if (bytes_out) *bytes_out = 0;
    if (url_host(url, host, sizeof(host))) return -1;
    if (!host_allowed(host)) return -1;
    if (dns_lookup(host, ip, sizeof(ip))) return -1;

    if (verify) {
        sink.md = EVP_MD_CTX_new();
        if (sink.md)
            EVP_DigestInit_ex(sink.md, EVP_sha256(), NULL);
    }

    snprintf(rs443, sizeof(rs443), "%s:443:%s", host, ip);
    rl = curl_slist_append(NULL, rs443);
    snprintf(rs80, sizeof(rs80), "%s:80:%s", host, ip);
    rl = curl_slist_append(rl, rs80);

    ca_blob.data  = (void *)PATCHDL_SCEI_DNAS_ROOT_PEM;
    ca_blob.len   = strlen(PATCHDL_SCEI_DNAS_ROOT_PEM);
    ca_blob.flags = CURL_BLOB_COPY;

    curl = curl_easy_init();
    if (!curl) {
        curl_slist_free_all(rl);
        if (sink.md) EVP_MD_CTX_free(sink.md);
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL,             url);
    curl_easy_setopt(curl, CURLOPT_RESOLVE,         rl);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,   file_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,       &sink);
    curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB,     &ca_blob);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER,  1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST,  2L);
    curl_easy_setopt(curl, CURLOPT_SSL_CIPHER_LIST, "DEFAULT@SECLEVEL=0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION,  1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS,       5L);
    /* HTTPS pin removed on the streaming download path: the Sony CDN sometimes
       302s a piece URL to a signed http:// edge inside its own infrastructure,
       and refusing those redirects was breaking real-world downloads.
       host_allowed + TLS-against-pinned-root on every leg already gate the
       hosts we'll talk to. NOSIGNAL stays — it protects the worker from a
       SIGPIPE on connection RST. */
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL,        1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,  20L);
    /* No total timeout (patches can be large); abort only on a long stall. */
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME,  30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,       "patchdl/1.0");
    if (range_start > 0) {
        snprintf(range_hdr, sizeof(range_hdr), "%lld-", range_start);
        curl_easy_setopt(curl, CURLOPT_RANGE, range_hdr);
    }
    if (progress && progress->cb) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS,       0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curl_progress_cb);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA,     progress);
    }

    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD_T, &dl);
    curl_easy_cleanup(curl);
    curl_slist_free_all(rl);

    if (res != CURLE_OK) {
        if (sink.md) EVP_MD_CTX_free(sink.md);
        return -1;
    }
    /* Asked for a byte range but the server sent the whole file (no 206): the
       caller must drop the piece and re-fetch it whole. */
    if (range_start > 0 && http_code != 206) {
        if (sink.md) EVP_MD_CTX_free(sink.md);
        return -3;
    }

    if (sink.md) {
        unsigned char dig[EVP_MAX_MD_SIZE];
        unsigned int  dlen = 0;
        char          hex[2 * EVP_MAX_MD_SIZE + 1];
        int           ok = EVP_DigestFinal_ex(sink.md, dig, &dlen);
        EVP_MD_CTX_free(sink.md);
        /* Fail-closed on a digest API failure — otherwise hex would be empty
           and we'd silently report -2 with no diagnostic. */
        if (ok != 1 || dlen == 0) return -2;
        hex_encode(dig, dlen, hex, sizeof(hex));
        if (strcasecmp(hex, expected_sha256_hex) != 0)
            return -2;          /* integrity mismatch */
    }

    if (bytes_out) *bytes_out = (long long)dl;
    return 0;
}

int
patchdl_http_download_progress(const char *url, const char *dest_path,
                               long long *bytes_out,
                               patchdl_download_progress_cb cb, void *ctx) {
    FILE *fp = fopen_safe(dest_path, 0);
    progress_state_t progress = { cb, ctx, 0, 0 };
    int rc;

    if (!fp) return -1;
    rc = http_download_to_file_progress(url, fp, bytes_out, &progress, NULL, 0);
    fclose(fp);

    if (rc) {
        unlink(dest_path);
        return -1;
    }
    return 0;
}

int
patchdl_http_download(const char *url, const char *dest_path,
                      long long *bytes_out) {
    return patchdl_http_download_progress(url, dest_path, bytes_out, NULL, NULL);
}

/* Substring scan bounded to [p, limit). NULL limit means search to NUL.
   Returns NULL if needle is not found before limit. */
static const char *
strstr_bounded(const char *p, const char *needle, const char *limit) {
    const char *hit = strstr(p, needle);
    if (!hit) return NULL;
    if (limit && hit >= limit) return NULL;
    return hit;
}

/* Read "key": "value" starting from p. Search and read are bounded by `limit`
   (pass NULL to search to end of buffer). Decodes \\ \" \/ \n \r \t \b \f; any
   other \X is copied without the backslash. \uXXXX is left as the raw 6 bytes
   (we don't need Unicode for manifest fields). limit==NULL keeps legacy
   end-of-string scope for callers that don't need the cap. */
static int
json_string_after(const char *p, const char *key, char *out, size_t out_sz,
                  const char *limit) {
    char needle[48];
    const char *q;
    size_t n = 0;

    if (!p || !out || out_sz == 0) return -1;
    out[0] = '\0';
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    q = strstr_bounded(p, needle, limit);
    if (!q) return -1;
    q += strlen(needle);
    while ((!limit || q < limit) &&
           (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n')) q++;
    if (limit && q >= limit) return -1;
    if (*q++ != ':') return -1;
    while ((!limit || q < limit) &&
           (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n')) q++;
    if (limit && q >= limit) return -1;
    if (*q++ != '"') return -1;

    while ((!limit || q < limit) && *q && *q != '"' && n + 1 < out_sz) {
        if (*q == '\\' && q[1] && (!limit || q + 1 < limit)) {
            q++;
            switch (*q) {
            case '"':  out[n++] = '"';  break;
            case '\\': out[n++] = '\\'; break;
            case '/':  out[n++] = '/';  break;
            case 'n':  out[n++] = '\n'; break;
            case 'r':  out[n++] = '\r'; break;
            case 't':  out[n++] = '\t'; break;
            case 'b':  out[n++] = '\b'; break;
            case 'f':  out[n++] = '\f'; break;
            default:   out[n++] = *q;   break; /* unknown escape: keep payload */
            }
            q++;
        } else {
            out[n++] = *q++;
        }
    }
    out[n] = '\0';
    return n ? 0 : -1;
}

static int
json_u64_after(const char *p, const char *key, unsigned long long *out,
               const char *limit) {
    char needle[48];
    const char *q;

    if (!p || !out) return -1;
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    q = strstr_bounded(p, needle, limit);
    if (!q) return -1;
    q += strlen(needle);
    while ((!limit || q < limit) &&
           (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n')) q++;
    if (limit && q >= limit) return -1;
    if (*q++ != ':') return -1;
    while ((!limit || q < limit) &&
           (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n')) q++;
    if (limit && q >= limit) return -1;
    if (*q < '0' || *q > '9') return -1;
    *out = strtoull(q, NULL, 10);
    return 0;
}

int
patchdl_http_download_manifest_progress(const char *manifest_url,
                                        const char *dest_path,
                                        long long *bytes_out,
                                        patchdl_download_progress_cb cb,
                                        void *ctx, int verify, int resume) {
    patchdl_buf_t manifest;
    const char *pieces, *pieces_end, *p;
    FILE *fp = NULL;
    long long total = 0, have = 0;
    unsigned long long manifest_total = 0;
    int count = 0, started, rc = -1;

    if (bytes_out) *bytes_out = 0;
    memset(&manifest, 0, sizeof(manifest));
    manifest.max = PATCHDL_BUF_MAX_MANIFEST;
    if (patchdl_http_get(manifest_url, &manifest))
        return -1;
    if (!manifest.data || !manifest.size) {
        free(manifest.data);
        return -1;
    }

    pieces = strstr(manifest.data, "\"pieces\"");
    if (!pieces || !(pieces = strchr(pieces, '['))) {
        free(manifest.data);
        return -1;
    }
    /* Bound the scan to the pieces array; otherwise a later "url" key in the
       manifest (e.g. playgoChunkCrcUrl) could be appended as a bogus piece. */
    pieces_end = strchr(pieces, ']');
    json_u64_after(manifest.data, "originalFileSize", &manifest_total, NULL);

    /* Resume: reopen the existing partial and keep its bytes; else start clean.
       Fully-downloaded pieces are skipped; the one piece that was only partially
       written continues mid-piece via an HTTP byte range (with a fall back to
       re-fetching it whole if the CDN ignores the range). */
    if (resume) {
        fp = fopen_safe(dest_path, 1);
        if (fp) { fseek(fp, 0, SEEK_END); have = ftell(fp); if (have < 0) have = 0; }
    }
    if (!fp) { fp = fopen_safe(dest_path, 0); have = 0; }
    if (!fp) { free(manifest.data); return -1; }
    started = (have <= 0);

    p = pieces;
    while ((p = strstr(p, "\"url\"")) && (!pieces_end || p < pieces_end)) {
        char url[768];
        char hash[80] = {0};
        long long got = 0, range_start = 0;
        unsigned long long expected = 0;
        unsigned long long offset = 0;
        int have_offset, drc;
        const char *want_hash;
        const char *obj_end = strchr(p, '}');
        const char *piece_limit = (obj_end && (!pieces_end || obj_end < pieces_end))
                                  ? obj_end : pieces_end;

        if (json_string_after(p, "url", url, sizeof(url), piece_limit))
            break;
        json_u64_after(p, "fileSize", &expected, piece_limit);
        have_offset = (json_u64_after(p, "fileOffset", &offset, piece_limit) == 0);

        /* Piece already fully present from a previous run: skip the download. */
        if (!started && have_offset && expected &&
            have >= (long long)(offset + expected)) {
            total = (long long)(offset + expected);
            count++;
            if (cb && cb(ctx, total, manifest_total ? (long long)manifest_total : total))
                goto done;
            p = obj_end ? obj_end + 1 : p + 5;
            continue;
        }

        /* First piece to (re)download while resuming. If part of it is already
           on disk, resume WITHIN it with a byte range; otherwise drop any stray
           bytes and fetch it whole. After this, every piece is fetched whole. */
        if (!started) {
            if (have_offset && expected && have > (long long)offset &&
                have < (long long)(offset + expected)) {
                range_start = have - (long long)offset;  /* this piece's bytes on disk */
                fseek(fp, 0, SEEK_END);                   /* append at `have` */
                total = have;
            } else {
                long long start_at = have_offset ? (long long)offset : 0;
                fflush(fp);
                if (ftruncate(fileno(fp), (off_t)start_at) != 0)
                    goto done;             /* can't resume cleanly; keep partial */
                fseek(fp, 0, SEEK_END);
                total = start_at;
            }
            started = 1;
        }

        /* Whole pieces are concatenated in array order; a ranged (partial) piece
           starts mid-piece, so the contiguity guard applies only to whole ones. */
        if (have_offset && range_start == 0 && offset != (unsigned long long)total)
            goto done;

        /* A ranged piece can't be hashed (only its tail is fetched). */
        want_hash = NULL;
        if (range_start == 0 && verify) {
            json_string_after(p, "hashValue", hash, sizeof(hash), piece_limit);
            want_hash = hash[0] ? hash : NULL;
        }

        {
            progress_state_t progress = {
                cb, ctx, total, manifest_total ? (long long)manifest_total : 0
            };
            /* drc: 0 ok, -1 network/cancel, -2 SHA-256, -3 range ignored. */
            drc = http_download_to_file_progress(url, fp, &got, &progress,
                                                 want_hash, range_start);
            if (drc == -3) {
                /* Server ignored the range: drop the piece and fetch it whole. */
                fflush(fp);
                if (ftruncate(fileno(fp), (off_t)offset) != 0)
                    goto done;
                fseek(fp, 0, SEEK_END);
                total = (long long)offset;
                range_start = 0;
                if (verify) {
                    json_string_after(p, "hashValue", hash, sizeof(hash),
                                      piece_limit);
                    want_hash = hash[0] ? hash : NULL;
                }
                progress.base = total;
                drc = http_download_to_file_progress(url, fp, &got, &progress,
                                                     want_hash, 0);
            }
        }
        if (drc) {
            if (drc == -2) rc = -2;
            goto done;
        }
        /* range_start + got = this piece's bytes now on disk. */
        if (expected && (unsigned long long)(range_start + got) != expected)
            goto done;

        total += got;
        /* A non-zero callback return between pieces means cancel requested. */
        if (cb && cb(ctx, total, manifest_total ? (long long)manifest_total : total))
            goto done;
        count++;
        p = obj_end ? obj_end + 1 : p + 5;
    }

    if (count > 0) {
        rc = 0;
        if (bytes_out) *bytes_out = total;
    }

done:
    fclose(fp);
    free(manifest.data);
    /* Keep the partial on failure so it can be resumed; the caller deletes it
       on cancel or on a corrupt-verify (-2). */
    return rc;
}

int
patchdl_http_download_manifest(const char *manifest_url, const char *dest_path,
                               long long *bytes_out) {
    return patchdl_http_download_manifest_progress(manifest_url, dest_path,
                                                  bytes_out, NULL, NULL, 0, 0);
}

/* ---- global init + parallel piece download (connection pool) ----------- */

void patchdl_net_global_init(void)    { curl_global_init(CURL_GLOBAL_ALL); }
void patchdl_net_global_cleanup(void) { curl_global_cleanup(); }

/* Write sink for one piece: pwrite at a fixed base offset (concurrent
   non-overlapping pieces of the same fd are safe), tee into SHA-256 if asked,
   and publish bytes-so-far for live progress. */
typedef struct {
    int                 fd;
    long long           base;
    long long           written;
    EVP_MD_CTX         *md;
    volatile long long *bytes_slot;
} piece_sink_t;

static size_t
piece_write_cb(void *ptr, size_t size, size_t nmemb, void *ud) {
    piece_sink_t *s = (piece_sink_t *)ud;
    size_t        n = size * nmemb;
    ssize_t       w;
    if (n == 0) return 0;
    w = pwrite(s->fd, ptr, n, (off_t)(s->base + s->written));
    if (w < 0 || (size_t)w != n) return 0;   /* short write -> curl errors out */
    if (s->md) EVP_DigestUpdate(s->md, ptr, n);
    s->written += (long long)n;
    if (s->bytes_slot) *s->bytes_slot = s->written;
    return n;
}

static int
piece_xfer_cb(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
              curl_off_t ultotal, curl_off_t ulnow) {
    volatile int *abort_flag = (volatile int *)clientp;
    (void)dltotal; (void)dlnow; (void)ultotal; (void)ulnow;
    return (abort_flag && *abort_flag) ? 1 : 0;   /* non-zero aborts the transfer */
}

int
patchdl_http_download_piece(const char *url, int fd,
                            long long file_offset, long long file_size,
                            const char *expected_sha256_or_null,
                            patchdl_piece_ctx_t *ctx,
                            int *curl_rc_out, long *http_code_out) {
    CURL             *curl;
    CURLcode          res;
    long              http_code = 0;
    char              host[256], ip[INET_ADDRSTRLEN], rs443[512], rs80[512];
    struct curl_slist *rl = NULL;
    struct curl_blob  ca_blob;
    piece_sink_t      sink;
    int               verify = (expected_sha256_or_null && expected_sha256_or_null[0]);

    if (curl_rc_out)   *curl_rc_out   = 0;
    if (http_code_out) *http_code_out = 0;

    if (url_host(url, host, sizeof(host))) return -1;
    if (!host_allowed(host)) return -1;
    if (dns_lookup(host, ip, sizeof(ip))) return -1;

    memset(&sink, 0, sizeof(sink));
    sink.fd         = fd;
    sink.base       = file_offset;
    sink.bytes_slot = ctx ? ctx->bytes_slot : NULL;
    if (verify) {
        sink.md = EVP_MD_CTX_new();
        if (sink.md) EVP_DigestInit_ex(sink.md, EVP_sha256(), NULL);
    }

    snprintf(rs443, sizeof(rs443), "%s:443:%s", host, ip);
    rl = curl_slist_append(NULL, rs443);
    snprintf(rs80, sizeof(rs80), "%s:80:%s", host, ip);
    rl = curl_slist_append(rl, rs80);

    ca_blob.data  = (void *)PATCHDL_SCEI_DNAS_ROOT_PEM;
    ca_blob.len   = strlen(PATCHDL_SCEI_DNAS_ROOT_PEM);
    ca_blob.flags = CURL_BLOB_COPY;

    curl = curl_easy_init();
    if (!curl) {
        curl_slist_free_all(rl);
        if (sink.md) EVP_MD_CTX_free(sink.md);
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL,             url);
    curl_easy_setopt(curl, CURLOPT_RESOLVE,         rl);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,   piece_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,       &sink);
    curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB,     &ca_blob);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER,  1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST,  2L);
    curl_easy_setopt(curl, CURLOPT_SSL_CIPHER_LIST, "DEFAULT@SECLEVEL=0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION,  1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS,       5L);
    /* HTTPS pin removed on the streaming piece path — see the same change in
       http_download_to_file_progress for the why. */
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL,        1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR,     1L);   /* 4xx/5xx -> error, no body written */
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,  20L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME,  30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,       "patchdl/1.0");
    if (ctx && ctx->abort) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS,       0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, piece_xfer_cb);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA,     (void *)ctx->abort);
    }

    res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    curl_slist_free_all(rl);

    if (curl_rc_out)   *curl_rc_out   = (int)res;
    if (http_code_out) *http_code_out = http_code;

    if (res != CURLE_OK) {
        if (sink.md) EVP_MD_CTX_free(sink.md);
        return -1;                          /* network error / abort */
    }
    if (file_size > 0 && sink.written != file_size) {
        if (sink.md) EVP_MD_CTX_free(sink.md);
        return -1;                          /* short or over-long -> failed */
    }
    if (sink.md) {
        unsigned char dig[EVP_MAX_MD_SIZE];
        unsigned int  dl = 0;
        char          hex[2 * EVP_MAX_MD_SIZE + 1];
        int           ok = EVP_DigestFinal_ex(sink.md, dig, &dl);
        EVP_MD_CTX_free(sink.md);
        if (ok != 1 || dl == 0) return -2;
        hex_encode(dig, dl, hex, sizeof(hex));
        if (strcasecmp(hex, expected_sha256_or_null) != 0)
            return -2;                      /* integrity mismatch */
    }
    fdatasync(fd);                          /* durable before the caller sets the done bit */
    return 0;
}

/* Read-only: SHA-256 a [offset, offset+size) region of fd into out_hex (>=65
   bytes). Uses pread so it doesn't disturb the fd offset. 0 on success. */
int
patchdl_sha256_fd_region(int fd, long long offset, long long size, char *out_hex) {
    EVP_MD_CTX   *md;
    unsigned char *buf;
    long long      pos = offset, remaining = size;
    const size_t   CHUNK = 1u << 20;

    out_hex[0] = '\0';
    if (fd < 0 || size < 0) return -1;
    md = EVP_MD_CTX_new();
    if (!md) return -1;
    buf = malloc(CHUNK);
    if (!buf) { EVP_MD_CTX_free(md); return -1; }
    EVP_DigestInit_ex(md, EVP_sha256(), NULL);
    while (remaining > 0) {
        size_t  want = remaining > (long long)CHUNK ? CHUNK : (size_t)remaining;
        ssize_t got  = pread(fd, buf, want, (off_t)pos);
        if (got <= 0) { free(buf); EVP_MD_CTX_free(md); return -1; }
        EVP_DigestUpdate(md, buf, (size_t)got);
        pos += got; remaining -= got;
    }
    {
        unsigned char dig[EVP_MAX_MD_SIZE];
        unsigned int  dl = 0, i;
        int           ok = EVP_DigestFinal_ex(md, dig, &dl);
        if (ok != 1 || dl == 0) { free(buf); EVP_MD_CTX_free(md); return -1; }
        for (i = 0; i < dl; i++) snprintf(out_hex + 2 * i, 3, "%02x", dig[i]);
        out_hex[2 * dl] = '\0';
    }
    free(buf);
    EVP_MD_CTX_free(md);
    return 0;
}

void
patchdl_manifest_free(patchdl_manifest_t *m) {
    if (!m || !m->pieces) return;
    for (int i = 0; i < m->count; i++) free(m->pieces[i].url);
    free(m->pieces);
    m->pieces = NULL;
    m->count = 0;
}

int
patchdl_fetch_manifest(const char *manifest_url, patchdl_manifest_t *out) {
    patchdl_buf_t buf;
    const char *pieces, *pieces_end, *p;
    int       cap = 0, n = 0;
    long long running = 0;

    memset(out, 0, sizeof(*out));
    memset(&buf, 0, sizeof(buf));
    buf.max = PATCHDL_BUF_MAX_MANIFEST;
    if (patchdl_http_get(manifest_url, &buf)) return -1;
    if (!buf.data || !buf.size) { free(buf.data); return -1; }

    pieces = strstr(buf.data, "\"pieces\"");
    if (!pieces || !(pieces = strchr(pieces, '['))) { free(buf.data); return -1; }
    pieces_end = strchr(pieces, ']');

    for (p = pieces; (p = strstr(p, "\"url\"")) && (!pieces_end || p < pieces_end); p += 5)
        cap++;
    if (cap <= 0) { free(buf.data); return -1; }
    out->pieces = calloc((size_t)cap, sizeof(patchdl_piece_t));
    if (!out->pieces) { free(buf.data); return -1; }

    /* Sanity caps: refuse a manifest that would let a CDN drive multi-TB
       allocations or millions of pieces. The biggest real PS5 patch we've
       seen is ~70 GB / 18 pieces; these limits leave room to spare. */
    if (cap > PATCHDL_MAX_PIECES) { free(buf.data); return -1; }

    p = pieces;
    while ((p = strstr(p, "\"url\"")) && (!pieces_end || p < pieces_end) && n < cap) {
        char               url[768] = {0};
        unsigned long long sz = 0, off = 0;
        const char        *obj_end = strchr(p, '}');
        const char        *piece_limit = (obj_end && (!pieces_end || obj_end < pieces_end))
                                         ? obj_end : pieces_end;

        if (json_string_after(p, "url", url, sizeof(url), piece_limit))
            break;
        json_u64_after(p, "fileSize", &sz, piece_limit);
        if (json_u64_after(p, "fileOffset", &off, piece_limit) != 0)
            off = (unsigned long long)running;   /* no offset -> assume contiguous */

        /* Validate tiling: pieces must be in order, contiguous, non-empty,
           and each individually under the per-piece cap. */
        if ((long long)off != running || sz == 0 || sz > PATCHDL_MAX_PIECE_BYTES) {
            patchdl_manifest_free(out);
            free(buf.data);
            return -1;
        }
        if ((unsigned long long)running + sz > PATCHDL_MAX_TOTAL_BYTES) {
            patchdl_manifest_free(out);
            free(buf.data);
            return -1;
        }
        out->pieces[n].url    = strdup(url);
        out->pieces[n].offset = (long long)off;
        out->pieces[n].size   = (long long)sz;
        json_string_after(p, "hashValue", out->pieces[n].hash,
                          sizeof(out->pieces[n].hash), piece_limit);
        if (!out->pieces[n].url) {
            patchdl_manifest_free(out);
            free(buf.data);
            return -1;
        }
        running += (long long)sz;
        n++;
        out->count = n;          /* keep current so manifest_free frees exactly n */
        p = obj_end ? obj_end + 1 : p + 5;
    }
    free(buf.data);
    if (n == 0) { patchdl_manifest_free(out); return -1; }
    out->total = running;        /* authoritative assembled size */
    return 0;
}

void
patchdl_net_diag(const char *url, char *out_json, size_t sz) {
    char     host[256] = {0}, ip[INET_ADDRSTRLEN] = {0};
    int      hostrc, allowrc, dnsrc;
    long     http_code = 0;
    CURLcode res = CURLE_OK;
    int      did_curl = 0;

    hostrc  = url_host(url, host, sizeof(host));
    allowrc = (hostrc == 0) ? host_allowed(host) : 0;
    dnsrc   = (hostrc == 0) ? dns_resolve(host, ip, sizeof(ip)) : -1;

    if (hostrc == 0 && allowrc && dnsrc == 0) {
        patchdl_buf_t buf;
        memset(&buf, 0, sizeof(buf));
        CURL *curl = curl_easy_init();
        if (curl) {
            char rs[512];
            struct curl_slist *rl;
            struct curl_blob ca_blob;
            snprintf(rs, sizeof(rs), "%s:443:%s", host, ip);
            rl = curl_slist_append(NULL, rs);
            ca_blob.data = (void *)PATCHDL_SCEI_DNAS_ROOT_PEM;
            ca_blob.len = strlen(PATCHDL_SCEI_DNAS_ROOT_PEM);
            ca_blob.flags = CURL_BLOB_COPY;
            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_RESOLVE, rl);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
            curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &ca_blob);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
            curl_easy_setopt(curl, CURLOPT_SSL_CIPHER_LIST, "DEFAULT@SECLEVEL=0");
            curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR,       "https");
            curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
            curl_easy_setopt(curl, CURLOPT_NOSIGNAL,        1L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
            res = curl_easy_perform(curl);
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            curl_easy_cleanup(curl);
            curl_slist_free_all(rl);
            free(buf.data);
            did_curl = 1;
        }
    }

    snprintf(out_json, sz,
             "{\"host\":\"%s\",\"host_ok\":%d,\"allowed\":%d,"
             "\"dns_rc\":%d,\"ip\":\"%s\","
             "\"dns_step\":%d,\"dns_errno\":%d,\"dns_recv\":%d,\"dns_ancount\":%d,"
             "\"curl_ran\":%d,\"curl_rc\":%d,\"curl_err\":\"%s\",\"http_code\":%ld}",
             host, hostrc == 0, allowrc, dnsrc, ip,
             g_dns_step, g_dns_errno, g_dns_n, g_dns_ancount,
             did_curl, (int)res,
             did_curl ? curl_easy_strerror(res) : "n/a", http_code);
}

#else /* !PATCHDL_HAVE_CURL */

int
patchdl_http_get(const char *url, patchdl_buf_t *out) {
    (void)url;
    (void)out;
    return -1; /* curl not available in this build; set CURL_DIR= to enable */
}

int
patchdl_http_download(const char *url, const char *dest_path,
                      long long *bytes_out) {
    (void)url; (void)dest_path;
    if (bytes_out) *bytes_out = 0;
    return -1;
}

int
patchdl_http_download_progress(const char *url, const char *dest_path,
                               long long *bytes_out,
                               patchdl_download_progress_cb cb, void *ctx) {
    (void)cb; (void)ctx;
    return patchdl_http_download(url, dest_path, bytes_out);
}

int
patchdl_http_download_manifest(const char *manifest_url, const char *dest_path,
                               long long *bytes_out) {
    (void)manifest_url; (void)dest_path;
    if (bytes_out) *bytes_out = 0;
    return -1;
}

int
patchdl_http_download_manifest_progress(const char *manifest_url,
                                        const char *dest_path,
                                        long long *bytes_out,
                                        patchdl_download_progress_cb cb,
                                        void *ctx, int verify, int resume) {
    (void)cb; (void)ctx; (void)verify; (void)resume;
    return patchdl_http_download_manifest(manifest_url, dest_path, bytes_out);
}

void
patchdl_net_diag(const char *url, char *out_json, size_t sz) {
    (void)url;
    snprintf(out_json, sz, "{\"error\":\"curl not built\"}");
}

#endif /* PATCHDL_HAVE_CURL */

patchdl_buf_t *
patchdl_buf_new(void) {
    return calloc(1, sizeof(patchdl_buf_t));
}

void
patchdl_buf_free(patchdl_buf_t *b) {
    if (!b) return;
    free(b->data);
    free(b);
}
