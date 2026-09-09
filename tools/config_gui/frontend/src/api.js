const JSON_HEADERS = { "Content-Type": "application/json" };

// Single-file / no-server mode: the build injects window.__ECAT_META__ and there
// is no backend to fetch from, so metadata is embedded and file I/O goes through
// the browser (download to save, file picker to load).
const OFFLINE = typeof window !== "undefined" && !!window.__ECAT_META__;

async function handle(res) {
  if (!res.ok) {
    let detail = res.statusText;
    try {
      const body = await res.json();
      detail = body.detail || detail;
    } catch (_) {}
    throw new Error(detail);
  }
  return res.json();
}

function basename(p) {
  const s = String(p || "ethercat_config.json").replace(/\\/g, "/");
  const b = s.substring(s.lastIndexOf("/") + 1);
  return b || "ethercat_config.json";
}

function renumbered(config) {
  const data = JSON.parse(JSON.stringify(config));
  (data.slaves || []).forEach((s, i) => { s.position = i + 1; });
  return data;
}

function offlineSave(path, config) {
  const data = renumbered(config);
  const text = JSON.stringify(data, null, 2) + "\n";
  const blob = new Blob([text], { type: "application/json" });
  const a = document.createElement("a");
  a.href = URL.createObjectURL(blob);
  a.download = basename(path);
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
  return Promise.resolve({ path: a.download, bytes: text.length });
}

function offlineLoad() {
  return new Promise((resolve, reject) => {
    const input = document.createElement("input");
    input.type = "file";
    input.accept = ".json,application/json";
    input.onchange = () => {
      const file = input.files && input.files[0];
      if (!file) { reject(new Error("no file selected")); return; }
      const reader = new FileReader();
      reader.onload = () => {
        try {
          resolve(renumbered(JSON.parse(reader.result)));
        } catch (e) {
          reject(new Error("invalid JSON: " + e.message));
        }
      };
      reader.onerror = () => reject(new Error("cannot read file"));
      reader.readAsText(file);
    };
    input.click();
  });
}

export function getMeta() {
  if (OFFLINE) return Promise.resolve(window.__ECAT_META__);
  return fetch("/api/meta").then(handle);
}

export function saveConfig(path, config) {
  if (OFFLINE) return offlineSave(path, config);
  return fetch("/api/config/save", {
    method: "POST",
    headers: JSON_HEADERS,
    body: JSON.stringify({ path, config }),
  }).then(handle);
}

export function loadConfig(path) {
  if (OFFLINE) return offlineLoad();
  return fetch("/api/config/load?path=" + encodeURIComponent(path)).then(handle);
}

export function validateConfig(config) {
  if (OFFLINE) return Promise.resolve(renumbered(config));
  return fetch("/api/config/validate", {
    method: "POST",
    headers: JSON_HEADERS,
    body: JSON.stringify(config),
  }).then(handle);
}
