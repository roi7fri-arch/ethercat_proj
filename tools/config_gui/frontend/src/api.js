const JSON_HEADERS = { "Content-Type": "application/json" };

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

export function getMeta() {
  return fetch("/api/meta").then(handle);
}

export function saveConfig(path, config) {
  return fetch("/api/config/save", {
    method: "POST",
    headers: JSON_HEADERS,
    body: JSON.stringify({ path, config }),
  }).then(handle);
}

export function loadConfig(path) {
  return fetch("/api/config/load?path=" + encodeURIComponent(path)).then(handle);
}

export function validateConfig(config) {
  return fetch("/api/config/validate", {
    method: "POST",
    headers: JSON_HEADERS,
    body: JSON.stringify(config),
  }).then(handle);
}
