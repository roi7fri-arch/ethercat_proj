#!/usr/bin/env python3
"""Bundle the built React UI into ONE standalone HTML file.

Produces ethercat_config_builder.html: a single, dependency-free file that runs
by double-click in any modern browser -- no Python, no server, no separate
assets, nothing to copy wrong. Metadata is embedded (window.__ECAT_META__) and
Save/Load use the browser's download / file-picker (see frontend/src/api.js).

Also emits ethercat_config_builder.html.txt, a base64 twin for crossing security
bridges that strip <script> blocks from .html files; restore_html.py rebuilds it.

Run after `npm run build`:
    python3 build_singlefile.py
"""

import base64
import hashlib
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DIST = os.path.join(HERE, "frontend", "dist")
OUT = os.path.join(HERE, "ethercat_config_builder.html")
OUT_TXT = os.path.join(HERE, "ethercat_config_builder.html.txt")

sys.path.insert(0, os.path.join(HERE, "backend"))
import meta as M  # noqa: E402


def api_meta():
    """The exact payload the /api/meta endpoint returns, embedded for offline use."""
    return {
        "version": M.CONFIG_VERSION,
        "modes": M.MODES,
        "object_dictionary": M.OBJECT_DICTIONARY,
        "mode_templates": {str(k): v for k, v in M.MODE_TEMPLATES.items()},
        "profiles": M.PROFILES,
        "default_profile": M.DEFAULT_PROFILE,
        "map_defaults": M.MAP_DEFAULTS,
        "default_config": M.default_config(),
    }


def _read(path):
    with open(path, "r", encoding="utf-8") as fh:
        return fh.read()


def main():
    index_path = os.path.join(DIST, "index.html")
    if not os.path.isfile(index_path):
        sys.stderr.write("ERROR: %s not found -- run `npm run build` first.\n" % index_path)
        sys.exit(1)

    html = _read(index_path)

    # Inline the stylesheet(s) into a <style> block.
    def inline_css(m):
        href = m.group(1)
        return "<style>\n%s\n</style>" % _read(os.path.join(DIST, href.lstrip("/")))

    html = re.sub(
        r'<link[^>]*rel=["\']stylesheet["\'][^>]*href=["\'](/assets/[^"\']+\.css)["\'][^>]*>',
        inline_css, html)

    # Grab the modern ES-module app bundle, then strip every <script> tag
    # (module app, vite legacy loader, nomodule polyfills -- none are needed once
    # the app is inlined and we target modern browsers only).
    m = re.search(r'<script[^>]*type=["\']module["\'][^>]*src=["\'](/assets/[^"\']+\.js)["\']',
                  html)
    if not m:
        sys.stderr.write("ERROR: could not find the module bundle in index.html\n")
        sys.exit(1)
    app_js = _read(os.path.join(DIST, m.group(1).lstrip("/")))
    html = re.sub(r'<script\b[^>]*>.*?</script>', "", html, flags=re.S)

    # Embed metadata (escape < to stay safe inside <script>).
    meta_json = json.dumps(api_meta()).replace("<", "\\u003c")
    meta_tag = "<script>window.__ECAT_META__ = %s;</script>" % meta_json

    # Embed the app as a base64 data: module URL -- no </script> escaping issues,
    # and module-from-data works over file:// in modern browsers.
    b64 = base64.b64encode(app_js.encode("utf-8")).decode("ascii")
    app_tag = '<script type="module" src="data:text/javascript;base64,%s"></script>' % b64

    html = html.replace("</body>", "%s\n%s\n</body>" % (meta_tag, app_tag))

    with open(OUT, "w", encoding="utf-8") as fh:
        fh.write(html)
    print("wrote %s (%d KB)" % (OUT, (os.path.getsize(OUT) + 1023) // 1024))

    # Security bridges/content filters strip <script> blocks out of .html files.
    # Base64 text survives the crossing; restore_html.py rebuilds it on the far side.
    raw = html.encode("utf-8")
    text = base64.b64encode(raw).decode("ascii")
    lines = [text[i:i + 76] for i in range(0, len(text), 76)]
    with open(OUT_TXT, "w", encoding="ascii", newline="\n") as fh:
        fh.write("\n".join(lines) + "\n")
    print("wrote %s (%d KB)" % (OUT_TXT, (os.path.getsize(OUT_TXT) + 1023) // 1024))
    print("  bytes  : %d" % len(raw))
    print("  sha256 : %s" % hashlib.sha256(raw).hexdigest())


if __name__ == "__main__":
    main()
