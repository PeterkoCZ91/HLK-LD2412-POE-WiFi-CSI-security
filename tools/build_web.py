#!/usr/bin/env python3
"""Build include/web_interface.h from web/src/* (IMPROVEMENTS T7).

The dashboard used to live as one 2984-line R"rawliteral(...)" PROGMEM blob
with HTML+CSS+JS+i18n all inline — hard to diff, lint or minify. Sources now
live in web/src/{index.html,style.css,i18n.js,app.js}; this script minifies
CSS/JS via npx (csso-cli, terser — Node 20, no new deps to vendor) and
reassembles include/web_interface.h, keeping the PROGMEM/rawliteral wrapper
byte-identical in shape.

Minification is best-effort: if npx/node or the minifier package can't be
reached (offline dev machine, no npm registry), each asset falls back to its
unminified source with a warning — the build must still succeed.

Usage:
    python3 tools/build_web.py              # regenerate include/web_interface.h
    python3 tools/build_web.py --check       # exit 1 if regenerating would change it
    python3 tools/build_web.py --no-minify   # skip npx, always use raw sources
"""
import argparse
import gzip
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WEB_SRC = ROOT / "web" / "src"
OUT_HEADER = ROOT / "include" / "web_interface.h"
OUT_GZ = ROOT / "dist" / "index.html.gz"

HEADER_TOP = "#ifndef WEB_INTERFACE_H\n#define WEB_INTERFACE_H\n\nconst char index_html[] PROGMEM = R\"rawliteral(\n"
HEADER_BOTTOM = ")rawliteral\";\n\n#endif\n"


def _npx(args, text, label):
    """Run `npx --yes <args>` feeding `text` on stdin, return (output, ok)."""
    try:
        proc = subprocess.run(
            ["npx", "--yes"] + args,
            input=text.encode("utf-8"),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=60,
        )
    except (OSError, subprocess.TimeoutExpired) as e:
        print(f"warning: {label} unavailable ({e}) — using unminified source", file=sys.stderr)
        return text, False
    if proc.returncode != 0 or not proc.stdout.strip():
        err = proc.stderr.decode("utf-8", "replace").strip()
        print(f"warning: {label} failed ({err or 'no output'}) — using unminified source", file=sys.stderr)
        return text, False
    return proc.stdout.decode("utf-8"), True


# Pinned exact versions — `npx --yes <pkg>` alone resolves whatever is
# currently latest on the npm registry, so an unpinned minifier makes
# `--check` non-deterministic across time/machines (a newer minifier release
# reformats bytes with no actual web/src/* change, and CI would report a
# false STALE). Bump deliberately, together with a regenerated
# include/web_interface.h in the same commit, not silently.
CSSO_VERSION = "csso-cli@4.0.2"  # `csso-cli --version` prints 5.0.5 — that's the
# bundled csso *engine* version, not the installable csso-cli package version.
TERSER_VERSION = "terser@5.51.2"


def minify_css(css_text):
    return _npx([CSSO_VERSION], css_text, "csso-cli (CSS minify)")


def minify_js(js_text, label):
    # No --toplevel: top-level const/function declarations in i18n.js
    # (I18N, LANG, t, setLang, applyLang) are read by app.js as globals
    # across the two <script> tags — toplevel mangling would rename them
    # independently per file and break that cross-script binding.
    return _npx([TERSER_VERSION, "--compress", "--mangle"], js_text, f"terser ({label})")


def build_html(minify):
    index_html = (WEB_SRC / "index.html").read_text(encoding="utf-8")
    style_css = (WEB_SRC / "style.css").read_text(encoding="utf-8")
    i18n_js = (WEB_SRC / "i18n.js").read_text(encoding="utf-8")
    app_js = (WEB_SRC / "app.js").read_text(encoding="utf-8")

    if minify:
        style_css, _ = minify_css(style_css)
        i18n_js, _ = minify_js(i18n_js, "i18n.js")
        app_js, _ = minify_js(app_js, "app.js")

    if not style_css.endswith("\n"):
        style_css += "\n"
    if not i18n_js.endswith("\n"):
        i18n_js += "\n"
    if not app_js.endswith("\n"):
        app_js += "\n"

    html = index_html.replace("__BUILD_CSS__\n", style_css, 1) \
                      .replace("__BUILD_I18N__\n", i18n_js, 1) \
                      .replace("__BUILD_APP__\n", app_js, 1)
    return HEADER_TOP + html + HEADER_BOTTOM


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true",
                     help="exit 1 if include/web_interface.h is stale relative to web/src/*, without writing")
    ap.add_argument("--no-minify", action="store_true", help="skip npx minification, use raw sources")
    args = ap.parse_args()

    generated = build_html(minify=not args.no_minify)

    if args.check:
        current = OUT_HEADER.read_text(encoding="utf-8") if OUT_HEADER.exists() else ""
        if current == generated:
            print("include/web_interface.h is up to date with web/src/*")
            return 0
        cur_lines = current.splitlines()
        gen_lines = generated.splitlines()
        print(f"STALE: include/web_interface.h does not match web/src/* "
              f"({len(cur_lines)} lines on disk vs {len(gen_lines)} regenerated)", file=sys.stderr)
        print("Run: python3 tools/build_web.py", file=sys.stderr)
        return 1

    OUT_HEADER.write_text(generated, encoding="utf-8")
    print(f"wrote {OUT_HEADER} ({len(generated)} bytes)")

    OUT_GZ.parent.mkdir(parents=True, exist_ok=True)
    # The served page body is the raw HTML (no PROGMEM/rawliteral wrapper) —
    # this is what tools/upload_www.sh should eventually push to LittleFS
    # instead of slicing it out of the generated header by line number.
    body = generated[len(HEADER_TOP):-len(HEADER_BOTTOM)]
    with gzip.GzipFile(OUT_GZ, "wb", mtime=0) as gz:
        gz.write(body.encode("utf-8"))
    print(f"wrote {OUT_GZ} ({OUT_GZ.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
