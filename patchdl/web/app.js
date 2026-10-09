const API = {
  status: "/api/status",
  titles: "/api/titles",
  config: "/api/config",
  downloads: "/api/downloads",
  installStatus: "/api/installstatus",
  action: (titleId, action) => `/api/titles/${encodeURIComponent(titleId)}/${action}`,
};

const SOURCE_TYPES = {
  official:    { label: "Official",    className: "ok",       installAllowed: true,  description: "Native console installation" },
  external:    { label: "External",    className: "external", installAllowed: true,  description: "Real external installation" },
  shadowmount: { label: "Shadowmount", className: "shadow",   installAllowed: false, description: "Download only" },
  unknown:     { label: "Unknown",     className: "blocked",  installAllowed: false, description: "Blocked" },
};

const fallback = {
  status: {
    firmware: "11.60",
    firmware_build: "0x11600000",
    dns_guard: "Active",
    resolver: "Internal allowlist",
    free_space_mb: 771916,
    download_dir: "/data/patchdl (internal)",
  },
  config: {
    default_policy: "deny",
    download_dir: "/data/patchdl (internal)",
    install_after_download: false,
    delete_pkg_after_install: true,
    verify_downloads: false,
    home_shortcut: true,
    max_connections: 4,
    source_policy: {
      official: { allow_check: true, allow_download: true, allow_install: true },
      external: { allow_check: true, allow_download: true, allow_install: true },
      shadowmount: { allow_check: true, allow_download: true, allow_install: false },
      unknown: { allow_check: true, allow_download: false, allow_install: false },
    },
    cdn_allowlist: [
      "sgst.prod.dl.playstation.net",
      "gst.prod.dl.playstation.net",
      "gs2.ww.prod.dl.playstation.net",
    ],
  },
  titles: [
    {
      title_id: "PPSA01628_00", name: "Call of Duty Black Ops Cold War",
      content_id: "UP0002-PPSA01628_00-CODCWTHEGAME0001",
      installed_version: "01.032.000", compatible_version: "01.041.000",
      latest_version: "01.041.000", latest_required_fw: "11.60",
      source_type: "official", source_path: "/system_ex/app/PPSA01628_00",
      patch_storage_match: true,
      mount_from: "/dev/ssd0.system_ex", enabled: true, status: "available",
    },
    {
      title_id: "PPSA01284_00", name: "Demon's Souls",
      content_id: "UP9000-PPSA01284_00-DEMONSSOULS00001",
      installed_version: "01.004.000", compatible_version: "01.004.000",
      latest_version: "01.004.000", latest_required_fw: "10.01",
      source_type: "external", source_path: "/system_data/priv/appmeta/external/PPSA01284_00",
      patch_storage_match: true,
      mount_from: "/mnt/ext0/user/app/PPSA01284_00", enabled: true, status: "up_to_date",
    },
    {
      title_id: "PPSA90001_00", name: "Shadowmounted Test Title",
      content_id: "UP0000-PPSA90001_00-SHADOWMOUNT0001",
      installed_version: "01.000.000", compatible_version: "01.006.000",
      latest_version: "01.009.000", latest_required_fw: "12.00",
      source_type: "shadowmount", source_path: "/system_ex/app/PPSA90001_00",
      patch_storage_match: true,
      mount_from: "/mnt/usb0/itemzflow/Shadowmounted Test Title", enabled: true, status: "available",
    },
  ],
  downloads: [],
  logs: [],
};

let state = {
  status: fallback.status,
  config: fallback.config,
  titles: fallback.titles,
  downloads: fallback.downloads,
  logs: fallback.logs,
  view: "games",
  filter: "updatable",
  query: "",
  usingFallback: false,
};

let downloadPollTimer = null;
let installPollTimer = null;
let emptyPolls = 0;
const dlMeta = {}; // per-title speed tracking: { bytes, t, speed }

const els = {};

document.addEventListener("DOMContentLoaded", () => {
  bindElements();
  bindEvents();
  loadInitialData();
});

function bindElements() {
  Object.assign(els, {
    firmwareValue: document.getElementById("firmwareValue"),
    firmwareBuild: document.getElementById("firmwareBuild"),
    dnsValue: document.getElementById("dnsValue"),
    resolverValue: document.getElementById("resolverValue"),
    spaceValue: document.getElementById("spaceValue"),
    downloadDirValue: document.getElementById("downloadDirValue"),
    railFw: document.getElementById("railFw"),
    railSpace: document.getElementById("railSpace"),
    gameGrid: document.getElementById("gameGrid"),
    logOutput: document.getElementById("logOutput"),
    allowlistHosts: document.getElementById("allowlistHosts"),
    sourcePolicyList: document.getElementById("sourcePolicyList"),
    searchInput: document.getElementById("searchInput"),
    defaultPolicy: document.getElementById("defaultPolicy"),
    downloadDir: document.getElementById("downloadDir"),
    installAfterDownload: document.getElementById("installAfterDownload"),
    deleteAfterInstall: document.getElementById("deleteAfterInstall"),
    verifyDownloads: document.getElementById("verifyDownloads"),
    homeShortcut: document.getElementById("homeShortcut"),
    connValue: document.getElementById("connValue"),
    connMinus: document.getElementById("connMinus"),
    connPlus: document.getElementById("connPlus"),
    refreshBtn: document.getElementById("refreshBtn"),
    updateAllBtn: document.getElementById("updateAllBtn"),
    brandVersion: document.getElementById("brandVersion"),
    globalDl:         document.getElementById("globalDl"),
    globalDlState:    document.getElementById("globalDlState"),
    globalDlName:     document.getElementById("globalDlName"),
    globalDlPosition: document.getElementById("globalDlPosition"),
    globalDlPct:      document.getElementById("globalDlPct"),
    globalDlSpeed:    document.getElementById("globalDlSpeed"),
    globalDlEta:      document.getElementById("globalDlEta"),
    globalDlBar:      document.getElementById("globalDlBar"),
    saveBtn: document.getElementById("saveBtn"),
    clearLogBtn: document.getElementById("clearLogBtn"),
    toast: document.getElementById("toast"),
  });
}

function bindEvents() {
  document.querySelectorAll(".nav-link").forEach((b) =>
    b.addEventListener("click", () => setView(b.dataset.view)));

  els.searchInput.addEventListener("input", (e) => {
    state.query = e.target.value.trim().toLowerCase();
    renderGames();
  });

  document.querySelectorAll(".segmented button").forEach((button) => {
    button.addEventListener("click", () => {
      document.querySelectorAll(".segmented button").forEach((i) => {
        i.classList.remove("is-selected");
        i.setAttribute("aria-pressed", "false");
      });
      button.classList.add("is-selected");
      button.setAttribute("aria-pressed", "true");
      state.filter = button.dataset.filter;
      renderGames();
    });
  });

  els.refreshBtn.addEventListener("click", loadInitialData);
  if (els.updateAllBtn) els.updateAllBtn.addEventListener("click", updateAll);
  els.saveBtn.addEventListener("click", saveConfig);
  els.clearLogBtn.addEventListener("click", () => { state.logs = []; renderLogs(); });
  if (els.connMinus)
    els.connMinus.addEventListener("click", () => setConnections(clampConn(state.config.max_connections) - 1));
  if (els.connPlus)
    els.connPlus.addEventListener("click", () => setConnections(clampConn(state.config.max_connections) + 1));
}

function setView(view) {
  state.view = view;
  document.querySelectorAll(".nav-link").forEach((b) => {
    const on = b.dataset.view === view;
    b.classList.toggle("is-active", on);
    if (on) b.setAttribute("aria-current", "page");
    else b.removeAttribute("aria-current");
  });
  document.querySelectorAll(".view").forEach((s) => s.classList.toggle("is-active", s.dataset.view === view));
}

async function loadInitialData() {
  state.usingFallback = false;
  const [status, config, titles, downloads] = await Promise.all([
    getJson(API.status, fallback.status),
    getJson(API.config, fallback.config),
    getJson(API.titles, fallback.titles),
    getJson(API.downloads, fallback.downloads),
  ]);

  // /api/titles carries no client-only progress flags, so preserve them across a
  // refresh — otherwise an in-flight download/install flips back to a clickable
  // button mid-operation.
  // Carry client-only flags across a refresh; the pool's job list is the source
  // of truth for download state and is reconciled right after.
  const prev = new Map(state.titles.map((g) => [g.title_id, g]));
  titles.forEach((g) => {
    const old = prev.get(g.title_id);
    if (old && g.status !== "up_to_date") {
      if (old.downloaded) g.downloaded = true;
      if (old.installing) g.installing = true;
      if (old._autoInstalled) g._autoInstalled = true;
      if (old._localDownloading) g._localDownloading = true;
    }
  });

  state = { ...state, status, config, titles, downloads };
  reconcileFromJobs(downloads);

  render();
  if (downloads.some((j) => j.state === "active" || j.state === "queued") ||
      state.titles.some((g) => g._localDownloading))
    startDownloadPolling();
  if (state.titles.some((g) => g.installing)) startInstallPolling();
  showToast(state.usingFallback ? "Demo data loaded. API is not reachable yet." : "Data refreshed.");
}

async function getJson(url, fallbackValue) {
  try {
    const response = await fetch(url, { cache: "no-store" });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return await response.json();
  } catch (error) {
    state.usingFallback = true;
    return clone(fallbackValue);
  }
}

function render() {
  renderStatus();
  renderSettings();
  renderGames();
  renderLogs();
}

function renderStatus() {
  els.firmwareValue.textContent = state.status.firmware || "--";
  els.firmwareBuild.textContent = state.status.firmware_build || "System version";
  els.dnsValue.textContent = state.status.dns_guard || "--";
  els.resolverValue.textContent = state.status.resolver || "--";
  const space = formatMb(state.status.free_space_mb);
  els.spaceValue.textContent = space;
  els.downloadDirValue.textContent = state.status.download_dir || state.config.download_dir || "Download target";
  if (els.railFw) els.railFw.textContent = `FW ${state.status.firmware || "--"}`;
  if (els.railSpace) els.railSpace.textContent = `${space} free`;
  if (els.brandVersion)
    els.brandVersion.textContent = state.status.version ? `v${state.status.version}` : "v--";
}

const CONN_MIN = 1, CONN_MAX = 16;
function clampConn(n) {
  n = parseInt(n, 10);
  if (!Number.isFinite(n)) n = 4;
  return Math.max(CONN_MIN, Math.min(CONN_MAX, n));
}

function renderConnStepper() {
  const n = clampConn(state.config.max_connections);
  if (els.connValue) els.connValue.textContent = String(n);
  if (els.connMinus) els.connMinus.disabled = n <= CONN_MIN;
  if (els.connPlus) els.connPlus.disabled = n >= CONN_MAX;
}

let connSaveTimer = null;
// Stepper +/-: update + re-render immediately, then persist just this field
// (debounced) so rapid taps collapse into one POST and other unsaved form
// fields stay untouched. The server applies the new count live (no restart).
function setConnections(n) {
  const v = clampConn(n);
  if (v === clampConn(state.config.max_connections)) { renderConnStepper(); return; }
  state.config.max_connections = v;
  renderConnStepper();
  clearTimeout(connSaveTimer);
  connSaveTimer = setTimeout(async () => {
    try {
      await postJson(API.config, { max_connections: v });
      showToast(`Parallel connections: ${v}`);
    } catch (e) {
      showToast("Could not save connections — API not reachable.");
    }
  }, 450);
}

function renderSettings() {
  els.defaultPolicy.value = state.config.default_policy || "deny";
  els.downloadDir.value = state.config.download_dir || "";
  els.installAfterDownload.checked = Boolean(state.config.install_after_download);
  els.deleteAfterInstall.checked = Boolean(state.config.delete_pkg_after_install);
  if (els.verifyDownloads) els.verifyDownloads.checked = Boolean(state.config.verify_downloads);
  if (els.homeShortcut) els.homeShortcut.checked = state.config.home_shortcut !== false;
  renderConnStepper();
  els.allowlistHosts.replaceChildren(...(state.config.cdn_allowlist || []).map((host) => {
    const chip = document.createElement("span");
    chip.className = "host-chip";
    chip.textContent = host;
    return chip;
  }));
  renderSourcePolicies();
}

/* ---------------- games ---------------- */

function renderGames() {
  renderGlobalStatus();
  const visible = state.titles.filter(matchesFilter).filter(matchesQuery);
  els.gameGrid.replaceChildren();

  if (!visible.length) {
    const empty = document.createElement("div");
    empty.className = "empty-state";
    empty.textContent = "No titles match this filter.";
    els.gameGrid.appendChild(empty);
    return;
  }
  visible.forEach((game) => els.gameGrid.appendChild(createGameCard(game)));
}

// Mutually-exclusive bucket per game for the filter chips.
function gameCategory(game) {
  // Updating is the queue — jobs waiting for a pool slot. An actively
  // downloading game stays under Updatable so you don't lose sight of it
  // while drilling into the queue; same for paused (the user knows where
  // it is and can resume from the card) and installing.
  if (game._jobState === "queued") return "updating";
  // checking is transient (version lookup still running); keep it visible under
  // Updatable rather than letting it fall out of every specific filter.
  if (game.status === "checking") return "updatable";
  if (game.patch_title_match === false) return "blocked";
  if (game.status === "available" && isDownloadAllowed(game)) return "updatable";
  if (!sourcePolicy(game).allow_install && !sourcePolicy(game).allow_download) return "blocked";
  if (game.status === "incompatible_fw") return "needsfw";
  return "uptodate";
}

function matchesFilter(game) {
  if (state.filter === "all") return true;
  return gameCategory(game) === state.filter;
}

function matchesQuery(game) {
  if (!state.query) return true;
  return [game.name, game.title_id, game.content_id, game.source_type, game.source_path, game.mount_from]
    .filter(Boolean)
    .some((v) => v.toLowerCase().includes(state.query));
}

function createGameCard(game) {
  const card = document.createElement("article");
  card.className = "game-card";
  card.dataset.titleId = game.title_id;
  if (game.downloading) card.classList.add("is-downloading");
  if (game.installing) card.classList.add("is-installing");

  // ---- top row: icon (+ toggle) | body | action ----
  const row = document.createElement("div");
  row.className = "card-row";

  const lead = document.createElement("div");
  lead.className = "lead";
  const cover = document.createElement("div");
  cover.className = "cover";
  cover.textContent = initials(game.name);
  lead.appendChild(cover);
  if (sourcePolicy(game).allow_install) lead.appendChild(buildToggle(game));

  const body = document.createElement("div");
  body.className = "card-body";
  body.innerHTML = `
    <h3>${escapeHtml(game.name)}</h3>
    <div class="subline">
      <span>${escapeHtml(game.title_id)}</span>
      <span>${escapeHtml(game.content_id || "")}</span>
    </div>
    <div class="pills">
      ${game.downloading && game._jobState === "queued" ? `<span class="pill">Queued</span>` : ""}
      ${game.downloading && game._jobState !== "queued" ? `<span class="pill live">Downloading</span>` : ""}
      ${game.resumable && !game.downloading ? `<span class="pill warn">Paused</span>` : ""}
      ${statusPill(game)}
      ${storagePill(game)}
      ${sourcePill(game)}
    </div>
    <div class="versions">
      <span>Installed <b>${escapeHtml(game.installed_version || "--")}</b></span>
      <span>Compatible <b>${escapeHtml(game.compatible_version || "None")}</b></span>
      <span>Latest <b>${escapeHtml(game.latest_version || "--")}</b>${game.latest_required_fw ? ` · FW ${escapeHtml(game.latest_required_fw)}` : ""}</span>
    </div>
  `;

  const actions = document.createElement("div");
  actions.className = "card-actions";
  [primaryButton(game), stopButton(game)].forEach((act) => {
    if (!act) return;
    const btn = document.createElement("button");
    btn.className = `row-button is-${act.variant}`;
    btn.textContent = act.label;
    if (act.hint) btn.title = act.hint;
    if (act.disabled) btn.disabled = true;
    else btn.addEventListener("click", () => runTitleAction(game.title_id, act.action));
    actions.appendChild(btn);
  });

  row.append(lead, body, actions);
  card.appendChild(row);

  // ---- progress block (only while downloading) ----
  if (game.downloading) {
    const d = state.downloads.find((x) => x.title_id === game.title_id) || {};
    const prog = document.createElement("div");
    prog.className = "card-progress";
    prog.innerHTML = `
      <div class="progress"><i style="width:${pctOf(d)}%"></i></div>
      <div class="progress-meta">${progressMetaHtml(d)}</div>
    `;
    card.appendChild(prog);
  } else if (game.installing) {
    const pct = Math.max(0, Math.min(100, Number(game.installProgress) || 0));
    const note = document.createElement("div");
    note.className = "card-progress";
    note.innerHTML = `
      <div class="progress"><i style="width:${pct}%"></i></div>
      <div class="progress-meta">${installProgressHtml(game)}</div>
    `;
    card.appendChild(note);
  } else if (game.resumable && game.partial_bytes > 0) {
    // ---- paused partial (survived a reboot) ----
    const note = document.createElement("div");
    note.className = "card-progress";
    note.innerHTML = `
      <div class="progress-meta">
        <span>Paused — <b>${formatBytes(game.partial_bytes)}</b> downloaded</span>
        <span>Resume to continue</span>
      </div>
    `;
    card.appendChild(note);
  }

  return card;
}

function buildToggle(game) {
  const toggle = document.createElement("label");
  toggle.className = "toggle";
  const cb = document.createElement("input");
  cb.type = "checkbox";
  cb.checked = game.enabled !== false;
  cb.setAttribute("aria-label", `Enable updates for ${game.name}`);
  const track = document.createElement("span");
  track.className = "track";
  const lbl = document.createElement("span");
  lbl.className = "label";
  lbl.textContent = cb.checked ? "On" : "Off";
  cb.addEventListener("change", () => {
    lbl.textContent = cb.checked ? "On" : "Off";
    updateGame(game.title_id, { enabled: cb.checked });
  });
  toggle.append(cb, track, lbl);
  return toggle;
}

// Primary play/pause button (green to go, amber while downloading). Fixed width
// (CSS) so the label never reflows. Returns null when there is nothing to do.
function primaryButton(game) {
  if (game.installing) return { label: "Installing…", variant: "ghost", disabled: true };
  if (game.downloading) return { label: "Pause", action: "pause", variant: "pause", hint: "Pause the download (keeps what was downloaded)." };
  if (game.patch_title_match === false) return null;
  if (game.resumable && isDownloadAllowed(game))
    return { label: "Resume", action: "download", variant: "update", hint: "Continue the paused download where it stopped." };
  if (game.downloaded && game.status === "available" && isInstallAllowed(game))
    return { label: "Install", action: "install", variant: "update", hint: "Install the downloaded patch (modifies the game)." };
  if (game.downloaded && game.status === "available") return null;
  if (game.status !== "available") return null;
  if (!isDownloadAllowed(game)) return null;
  return state.config.install_after_download && isInstallAllowed(game)
    ? { label: "Update", action: "update", variant: "update", hint: "Download and install the update." }
    : { label: "Download", action: "download", variant: "update", hint: "Download the patch internally." };
}

// Red stop button: present whenever there is a download to stop or discard.
// Cancel stops AND deletes (unlike Pause, which keeps the partial).
function stopButton(game) {
  if (game.installing) return null;
  if (game.downloading || game.resumable || game.downloaded)
    return { label: "Cancel", action: "cancel", variant: "cancel", hint: "Stop and delete the download." };
  return null;
}

function statusPill(game) {
  if (game.installing) return `<span class="pill warn">Installing…</span>`;
  if (game.status === "checking") return `<span class="pill">Checking…</span>`;
  if (game.patch_title_match === false) return `<span class="pill blocked">Title mismatch</span>`;
  if (game.status === "available") return `<span class="pill ok">Update available</span>`;
  if (game.status === "incompatible_fw") return `<span class="pill warn">Needs FW ${escapeHtml(game.latest_required_fw || "")}</span>`;
  if (game.status === "up_to_date") return `<span class="pill">Up to date</span>`;
  return `<span class="pill">No patch info</span>`;
}

function storagePill(game) {
  return hasSharedStorage(game) ? `<span class="pill warn">Shared master</span>` : "";
}

function sourcePill(game) {
  const info = sourceInfo(game);
  return `<span class="pill ${info.className}">${escapeHtml(info.label)}</span>`;
}
function sourceInfo(game) { return SOURCE_TYPES[sourceType(game)] || SOURCE_TYPES.unknown; }
function sourceType(game) { return game.source_type || "unknown"; }

/* ---------------- download progress (in-tile) ---------------- */

function pctOf(d) {
  const done = Number(d.bytes) || 0;
  const total = Number(d.total_bytes) || 0;
  if (total > 0) return Math.max(0, Math.min(100, Math.round((done * 100) / total)));
  return Math.max(0, Math.min(100, Number(d.progress) || 0));
}

function progressMetaHtml(d) {
  const done = Number(d.bytes) || 0;
  const total = Number(d.total_bytes) || 0;
  const speed = Number(d._speed) || 0;
  if (d.state === "queued") return `<span>Queued — waiting for a free slot</span>`;
  const parts = [];
  parts.push(`<span><b>${formatBytes(done)}</b>${total > 0 ? ` / ${formatBytes(total)}` : ""}</span>`);
  if (speed > 0) parts.push(`<span><b>${formatBytes(speed)}/s</b></span>`);
  if (speed > 0 && total > done) parts.push(`<span>ETA <b>${formatEta((total - done) / speed)}</b></span>`);
  else if (!total) parts.push(`<span>Fetching manifest…</span>`);
  parts.push(`<span><b>${pctOf(d)}%</b></span>`);
  return parts.join("");
}

function installProgressHtml(game) {
  const status = game.installStatus || "waiting";
  const done = Number(game.installDone) || 0;
  const total = Number(game.installTotal) || 0;
  const parts = [`<span>Status <b>${escapeHtml(status)}</b></span>`];
  if (total > 0) parts.push(`<span><b>${formatBytes(done)}</b> / ${formatBytes(total)}</span>`);
  parts.push(`<span><b>${Math.max(0, Math.min(100, Number(game.installProgress) || 0))}%</b></span>`);
  return parts.join("");
}

function startDownloadPolling() {
  emptyPolls = 0;
  if (downloadPollTimer) return;
  downloadPollTimer = setInterval(refreshDownloads, 2000);
  refreshDownloads();
}

function stopDownloadPolling() {
  if (!downloadPollTimer) return;
  clearInterval(downloadPollTimer);
  downloadPollTimer = null;
}

// Map the pool's job list onto per-title flags, and auto-install once a job
// finishes if "install after download" is on.
function reconcileFromJobs(jobs) {
  const byId = new Map((jobs || []).map((j) => [j.title_id, j]));
  state.titles.forEach((g) => {
    const j = byId.get(g.title_id);
    if (!j) {
      // The pool never produced a job for our local intent: time it out so the
      // card can't wedge in "Downloading" forever (server restart between POST
      // and poll, or an unexpected response shape).
      if (g._localDownloading && g._localSince &&
          Date.now() - g._localSince > 12000) {
        g._localDownloading = false;
      }
      if (g.downloading && !g._localDownloading) g.downloading = false;
      g._jobState = null;
      return;
    }
    g._localDownloading = false; // the pool now tracks it
    g._jobState = j.state;
    if (j.state === "active" || j.state === "queued") {
      g.downloading = true;
      g.resumable = false;
      g._wasActive = true;
    } else if (j.state === "paused") {
      g.downloading = false;
      g.resumable = true;
      g.partial_bytes = Number(j.bytes) || g.partial_bytes || 0;
      g._wasActive = false;
    } else if (j.state === "done") {
      g.downloading = false;
      g.resumable = false;
      g.downloaded = true;
      g._wasActive = false;
      if (state.config.install_after_download && isInstallAllowed(g) &&
          !g.installing && !g._autoInstalled) {
        g._autoInstalled = true;
        doInstall(g);
      }
    } else if (j.state === "error") {
      // The server keeps the partial + sidecar (resumable) on a post-retry
      // network failure. Reflect that so primaryButton shows "Resume" and the
      // red Cancel stays available to delete the kept partial.
      const bytes = Number(j.bytes) || 0;
      if (g._wasActive) {
        showToast(`${g.name}: download failed${bytes > 0 ? " — partial kept, press Resume to continue" : "."}`);
      }
      g._wasActive = false;
      g.downloading = false;
      g.resumable = bytes > 0;
      g.partial_bytes = bytes || g.partial_bytes || 0;
    }
  });
}

function startInstallPolling() {
  if (installPollTimer) return;
  installPollTimer = setInterval(refreshInstallStatus, 2000);
  refreshInstallStatus();
}

function stopInstallPolling() {
  if (!installPollTimer) return;
  clearInterval(installPollTimer);
  installPollTimer = null;
}

async function refreshInstallStatus() {
  let s;
  try {
    const response = await fetch(API.installStatus, { cache: "no-store" });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    s = await response.json();
  } catch (error) {
    return;
  }

  if (!s || !s.active) {
    if (!state.titles.some((g) => g.installing)) stopInstallPolling();
    return;
  }

  const titleId = s.target_title_id || "";
  const game = state.titles.find((g) => g.title_id === titleId || g.title_id.slice(0, 9) === titleId.slice(0, 9));
  if (!game) return;

  game.installing = !s.terminal;
  game.installStatus = s.status || "running";
  game.installProgress = Number(s.progress) || 0;
  game.installDone = Number(s.downloaded_size) || 0;
  game.installTotal = Number(s.total_size) || 0;
  renderGames();

  if (s.terminal) {
    const ok = s.status === "playable";
    state.logs.push(`[${timeNow()}] Install ${ok ? "completed" : "stopped"} for ${game.title_id}: ${s.status || "unknown"}${s.error_code ? ` (0x${Number(s.error_code >>> 0).toString(16)})` : ""}`);
    renderLogs();
    stopInstallPolling();
    if (ok) loadInitialData();
  }
}

async function refreshDownloads() {
  let jobs;
  try {
    const response = await fetch(API.downloads, { cache: "no-store" });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    jobs = await response.json();
  } catch (error) {
    return; // keep last state on a transient failure
  }

  const now = Date.now();
  const ids = new Set();
  jobs.forEach((j) => {
    ids.add(j.title_id);
    const done = Number(j.bytes) || 0;
    const prev = dlMeta[j.title_id];
    if (prev && now > prev.t) {
      if (done >= prev.bytes) {
        const inst = ((done - prev.bytes) * 1000) / (now - prev.t); // bytes/s
        prev.speed = prev.speed ? prev.speed * 0.5 + inst * 0.5 : inst; // smoothed
      } else {
        prev.speed = 0; // counter went backwards -> re-baseline
      }
    }
    const meta = prev || (dlMeta[j.title_id] = { speed: 0 });
    meta.bytes = done;
    meta.t = now;
    j._speed = meta.speed || 0;
  });
  Object.keys(dlMeta).forEach((id) => { if (!ids.has(id)) delete dlMeta[id]; });

  state.downloads = jobs;
  const before = downloadingIds();
  reconcileFromJobs(jobs);
  if (downloadingIds() !== before) renderGames();
  else applyDownloadProgress();

  const busy = jobs.some((j) => j.state === "active" || j.state === "queued") ||
               state.titles.some((g) => g._localDownloading);
  if (!busy) { if (++emptyPolls >= 3) stopDownloadPolling(); }
  else emptyPolls = 0;
}

function downloadingIds() {
  return state.titles.filter((g) => g.downloading).map((g) => g.title_id).join(",");
}

// Top-of-page status banner. Visible while any job in this batch is queued,
// active, or has just finished (done jobs linger one or two polls before the
// pool reaps them, which is what gives us the "5 of 9" position).
function renderGlobalStatus() {
  const el = els.globalDl;
  if (!el) return;
  const jobs   = state.downloads || [];
  const batch  = jobs.filter((j) => ["queued", "active", "done"].includes(j.state));
  const flying = batch.filter((j) => j.state === "queued" || j.state === "active");

  if (flying.length === 0) { el.hidden = true; return; }
  el.hidden = false;

  const active = batch.find((j) => j.state === "active") || flying[0];
  const total  = batch.length;
  const done   = batch.filter((j) => j.state === "done").length;
  const pos    = Math.min(total, done + 1);
  const isActive = active && active.state === "active";

  // Eyebrow + name
  els.globalDlState.textContent = isActive ? "Downloading" : "Queued";
  els.globalDlName.textContent  = active.name || active.title_id || "—";

  // Position chip ("3 of 9") only when there's more than one game in this run
  if (total > 1) {
    els.globalDlPosition.hidden = false;
    els.globalDlPosition.textContent = `${pos} of ${total}`;
  } else {
    els.globalDlPosition.hidden = true;
  }

  // Progress line
  const pct   = pctOf(active);
  const speed = Number(active._speed) || 0;
  const done_ = Number(active.bytes) || 0;
  const total_ = Number(active.total_bytes) || 0;
  els.globalDlPct.innerHTML   = isActive ? `<b>${pct}%</b>` : `<b>—</b> waiting`;
  els.globalDlSpeed.innerHTML = isActive && speed > 0
    ? `<b>${formatBytes(speed)}/s</b>`
    : (isActive && !total_ ? "fetching manifest…" : "—");
  els.globalDlEta.innerHTML = isActive && speed > 0 && total_ > done_
    ? `ETA <b>${formatEta((total_ - done_) / speed)}</b>`
    : "";

  // Bar: animated indeterminate while fetching manifest, otherwise width=pct
  if (isActive && total_ > 0) {
    els.globalDlBar.classList.remove("is-indeterminate");
    els.globalDlBar.style.width = pct + "%";
  } else {
    els.globalDlBar.classList.add("is-indeterminate");
  }
}

// Update the progress bar/meta in place to avoid rebuilding every card each tick.
function applyDownloadProgress() {
  renderGlobalStatus();
  let needRender = false;
  state.downloads.forEach((d) => {
    // Only titles currently downloading render a progress tile; paused/done/error
    // jobs linger in the pool list but have no .progress bar — skip them so a
    // missing tile for a non-downloading title doesn't force a full rebuild.
    const g = state.titles.find((t) => t.title_id === d.title_id);
    if (!g || !g.downloading) return;
    const card = els.gameGrid.querySelector(`[data-title-id="${d.title_id}"]`);
    const bar = card && card.querySelector(".card-progress .progress > i");
    const meta = card && card.querySelector(".card-progress .progress-meta");
    if (!bar || !meta) { needRender = true; return; }
    bar.style.width = pctOf(d) + "%";
    meta.innerHTML = progressMetaHtml(d);
  });
  if (needRender) renderGames();
}

/* ---------------- settings ---------------- */

function renderSourcePolicies() {
  const rows = Object.keys(SOURCE_TYPES).map((type) => {
    const info = SOURCE_TYPES[type];
    const policy = sourcePolicyForType(type);
    const row = document.createElement("div");
    row.className = "policy-row";
    row.innerHTML = `
      <span class="pill ${info.className}">${escapeHtml(info.label)}</span>
      <strong>${policy.allow_install ? "Install allowed" : "Install blocked"}</strong>
      <em>${policy.allow_download ? "Download allowed" : "Download blocked"} - ${escapeHtml(info.description)}</em>
    `;
    return row;
  });
  els.sourcePolicyList.replaceChildren(...rows);
}

function renderLogs() {
  els.logOutput.textContent = state.logs.length ? state.logs.join("\n") : "No events yet.";
}

async function saveConfig() {
  const config = {
    ...state.config,
    default_policy: els.defaultPolicy.value,
    download_dir: els.downloadDir.value.trim(),
    install_after_download: els.installAfterDownload.checked,
    delete_pkg_after_install: els.deleteAfterInstall.checked,
    verify_downloads: els.verifyDownloads ? els.verifyDownloads.checked : Boolean(state.config.verify_downloads),
    home_shortcut: els.homeShortcut ? els.homeShortcut.checked : state.config.home_shortcut !== false,
    max_connections: clampConn(state.config.max_connections),
  };
  try {
    await postJson(API.config, config);
    state.config = config;
    showToast("Configuration saved.");
  } catch (error) {
    state.config = config;
    showToast("Configuration updated locally. API is not reachable yet.");
  }
  renderSettings();
  renderGames(); // install_after_download changes the button label
}

/* ---------------- actions (data layer) ---------------- */

// Queue a download for every game that has an available update AND could
// actually be installed afterwards. Shadowmounts pass isDownloadAllowed but
// fail isInstallAllowed (their app slot has no real source medium), so a
// sweep would otherwise pull tens of GB that AppInstUtil will refuse — the
// user picks those up by hand when the disc is ready. The server tolerates
// duplicate requests, so a second click is harmless. If install_after_download
// is on, the per-job auto-install pipeline kicks in once each download
// finishes — no further client action needed.
async function updateAll() {
  const targets = state.games.filter((g) =>
    g.status === "available" && isInstallAllowed(g) &&
    !g.downloading && !g.downloaded);
  if (!targets.length) {
    showToast("No installable updates to queue.");
    return;
  }
  showToast(`Queueing ${targets.length} update${targets.length === 1 ? "" : "s"}…`);
  for (const g of targets) {
    // Sequential await: the pool returns quickly (202 Accepted) and we want
    // a stable order in the queue, not a thundering-herd of concurrent POSTs.
    try { await doDownload(g); } catch (_) { /* per-job errors already toast */ }
  }
}

// Enqueue a download. The pool returns immediately (202); progress, completion
// and (if configured) auto-install are driven by reconcileFromJobs() on poll.
async function doDownload(game) {
  game.downloading = true;
  game._localDownloading = true;   // until the pool reports a job for this title
  game._localSince = Date.now();   // bounded in reconcileFromJobs if no job appears
  game._autoInstalled = false;
  state.downloads = state.downloads.filter((i) => i.title_id !== game.title_id);
  state.downloads.push({ title_id: game.title_id, name: game.name,
                         version: game.compatible_version || "",
                         state: "queued", progress: 0, bytes: 0, total_bytes: 0 });
  renderGames();
  startDownloadPolling();

  try {
    const r = await postJson(API.action(game.title_id, "download"), {});
    if (r && r.downloaded && r.already) {
      game.downloading = false;
      game._localDownloading = false;
      game.downloaded = true;
      renderGames();
    } else {
      state.logs.push(`[${timeNow()}] Download queued: ${game.title_id} ${game.compatible_version || ""}`);
      renderLogs();
    }
  } catch (error) {
    game.downloading = false;
    game._localDownloading = false;
    state.downloads = state.downloads.filter((i) => i.title_id !== game.title_id);
    const why = reasonText(error);
    state.logs.push(`[${timeNow()}] download ${game.title_id} blocked: ${why}`);
    showToast(`${game.name}: ${why}`);
    renderGames(); renderLogs();
  }
}

async function doInstall(game) {
  game.installing = true;
  game.installStatus = "starting";
  game.installProgress = 0;
  game.downloaded = false; // the package is being consumed by the install
  renderGames();
  try {
    await postJson(API.action(game.title_id, "install"), {});
  } catch (error) {
    game.installing = false;
    const why = reasonText(error);
    state.logs.push(`[${timeNow()}] install ${game.title_id} blocked: ${why}`);
    showToast(`${game.name}: ${why}`);
    renderGames(); renderLogs();
    return false;
  }
  state.logs.push(`[${timeNow()}] Install started for ${game.title_id} ${game.compatible_version} — running in PS5 background`);
  showToast(`${game.name}: installing update.`);
  startInstallPolling();
  renderGames(); renderLogs();
  return true;
}

// Cancel a running download (server aborts + deletes the partial) or delete a
// finished package. Same endpoint handles both.
async function cancelDownload(titleId) {
  const game = state.titles.find((g) => g.title_id === titleId);
  try {
    await postJson(API.action(titleId, "cancel"), {});
    showToast(`${game ? game.name : titleId}: download cancelled / deleted.`);
  } catch (error) {
    showToast(`${game ? game.name : titleId}: ${reasonText(error)}`);
  }
  if (game) {
    game.downloading = false;
    game._localDownloading = false;
    game.downloaded = false;
    game.resumable = false;
    game.partial_bytes = 0;
  }
  state.downloads = state.downloads.filter((i) => i.title_id !== titleId);
  state.logs.push(`[${timeNow()}] Download cancelled / deleted: ${titleId}`);
  renderGames(); renderLogs();
  refreshDownloads();
}

async function runTitleAction(titleId, action) {
  const game = state.titles.find((i) => i.title_id === titleId);
  if (!game) return;
  // "update" and "download" both just enqueue; for "update" (install-after-
  // download on) the reconciler auto-installs once the pool reports it done.
  if (action === "download" || action === "update") await doDownload(game);
  else if (action === "install") await doInstall(game);
  else if (action === "pause") await doPause(game);
  else if (action === "cancel") await cancelDownload(titleId);
}

// Pause only sends the signal; the in-flight doDownload() request returns its
// "paused" result and updates the card (resumable + partial bytes).
async function doPause(game) {
  try {
    await postJson(API.action(game.title_id, "pause"), {});
    showToast(`${game.name}: pausing…`);
  } catch (error) {
    showToast(`${game.name}: ${reasonText(error)}`);
  }
}

function updateGame(titleId, patch) {
  const game = state.titles.find((i) => i.title_id === titleId);
  if (!game) return;
  Object.assign(game, patch);
  renderGames();

  const wantEnabled = game.enabled !== false;
  postJson(API.action(titleId, wantEnabled ? "enable" : "disable"), {})
    .then(() => { state.logs.push(`[${timeNow()}] Saved: ${titleId} ${wantEnabled ? "enabled" : "disabled"}`); renderLogs(); })
    .catch(() => showToast(`${titleId}: could not save toggle (API not reachable).`));
}

/* ---------------- policy helpers ---------------- */

function isInstallBlocked(game) { return !sourcePolicy(game).allow_install; }
function hasSharedStorage(game) {
  if (game.patch_storage_match === false) return true;
  const storage = (game.patch_storage_title_id || "").slice(0, 9);
  const target = (game.title_id || "").slice(0, 9);
  return Boolean(storage && target && storage !== target);
}
function isDownloadAllowed(game) {
  return Boolean(
    game.enabled !== false &&
    game.compatible_version &&
    game.patch_title_match !== false &&
    sourcePolicy(game).allow_download
  );
}
function isInstallAllowed(game) {
  return Boolean(
    game.enabled !== false &&
    game.compatible_version &&
    game.patch_title_match !== false &&
    !hasSharedStorage(game) &&
    sourcePolicy(game).allow_install
  );
}
function sourcePolicy(game) { return sourcePolicyForType(sourceType(game)); }
function sourcePolicyForType(type) {
  const fb = fallback.config.source_policy[type] || fallback.config.source_policy.unknown;
  const cfg = (state.config.source_policy || {})[type] || {};
  return { ...fb, ...cfg };
}

async function postJson(url, body) {
  const response = await fetch(url, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(body),
  });
  const data = await response.json().catch(() => ({}));
  if (!response.ok) {
    const err = new Error(data.reason || data.message || `HTTP ${response.status}`);
    err.body = data;
    err.status = response.status;
    throw err;
  }
  return data;
}

const REASON_TEXT = {
  patch_title_mismatch: "Patch metadata targets a different title - install blocked.",
  cross_region_storage_unsupported: "Patch bytes are signed for a shared master title; this standalone installer cannot retarget them.",
  install_not_allowed_for_source: "Install blocked for this source.",
  source_unknown: "Source unknown — blocked.",
  no_compatible_patch: "No compatible patch available.",
  download_failed: "Download failed.",
  download_in_progress: "Another download is already running.",
  piece_verify_failed: "A downloaded piece failed its SHA-256 check.",
  title_disabled: "This title is disabled.",
  download_paused: "Download paused.",
  not_downloading: "Nothing is downloading for this title.",
};
function reasonText(error) {
  const r = error && error.body && error.body.reason;
  return (r && REASON_TEXT[r]) || (error && error.message) || "failed";
}

/* ---------------- formatting ---------------- */

function initials(name) {
  return (name || "PD").split(/\s+/).filter(Boolean).slice(0, 2).map((p) => p[0]).join("").toUpperCase();
}

// Auto-scaled byte sizes: B / KB / MB / GB / TB.
function formatBytes(bytes) {
  const n = Number(bytes);
  if (!Number.isFinite(n) || n < 0) return "--";
  if (n < 1024) return `${n} B`;
  const units = ["KB", "MB", "GB", "TB", "PB"];
  let v = n / 1024, i = 0;
  while (v >= 1024 && i < units.length - 1) { v /= 1024; i++; }
  return `${v.toFixed(v >= 100 || i === 0 ? 0 : 1)} ${units[i]}`;
}

// Free space comes from the backend in MB.
function formatMb(mb) {
  const n = Number(mb);
  if (!Number.isFinite(n)) return "--";
  return formatBytes(n * 1024 * 1024);
}

function formatEta(seconds) {
  const s = Math.round(Number(seconds) || 0);
  if (!Number.isFinite(s) || s <= 0) return "--";
  if (s < 60) return `${s}s`;
  const m = Math.floor(s / 60);
  if (m < 60) return `${m}m ${s % 60}s`;
  const h = Math.floor(m / 60);
  return `${h}h ${m % 60}m`;
}

function escapeHtml(value) {
  return String(value ?? "")
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&#039;");
}

function clone(value) { return JSON.parse(JSON.stringify(value)); }
function timeNow() { return new Date().toLocaleTimeString("en-US", { hour12: false }); }

let toastTimer;
function showToast(message) {
  els.toast.textContent = message;
  els.toast.classList.add("is-visible");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => els.toast.classList.remove("is-visible"), 2600);
}
