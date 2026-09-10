#!/usr/bin/env python3
"""Rebuild ethercat_config_builder.html from its base64 .txt twin.

Security bridges strip <script> blocks out of .html files in transit (a 247 KB
file arriving as ~8 KB is the tell-tale). Copy the .txt across instead, then run
this on the offline PC:

    python restore_html.py

Windows built-in alternative, nothing to copy over:

    certutil -decode ethercat_config_builder.html.txt ethercat_config_builder.html
"""

import base64
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_TXT = os.path.join(HERE, "ethercat_config_builder.html.txt")


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_TXT
    dst = sys.argv[2] if len(sys.argv) > 2 else src[:-4] if src.endswith(".txt") else src + ".html"

    if not os.path.isfile(src):
        sys.stderr.write("ERROR: %s not found.\n" % src)
        sys.exit(1)

    with open(src, "r") as fh:
        text = "".join(fh.read().split())

    try:
        raw = base64.b64decode(text)
    except Exception as exc:
        sys.stderr.write("ERROR: %s is not valid base64 (%s).\n"
                         "The file was probably damaged in transit -- copy it again.\n"
                         % (src, exc))
        sys.exit(1)

    if not raw.lstrip()[:9].lower().startswith(b"<!doctype"):
        sys.stderr.write("ERROR: decoded data is not the HTML page -- copy the .txt again.\n")
        sys.exit(1)

    with open(dst, "wb") as fh:
        fh.write(raw)

    print("wrote %s" % dst)
    print("  bytes  : %d" % len(raw))
    print("  sha256 : %s" % hashlib.sha256(raw).hexdigest())
    print("\nCompare the numbers above with the ones printed when the file was built.")
    print("If they match, double-click %s to open the config builder." % os.path.basename(dst))


if __name__ == "__main__":
    main()
