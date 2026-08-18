#!/usr/bin/env python3
"""Serve the Inx web UI locally without PlatformIO or third-party packages."""

import argparse
import importlib.util
import json
import mimetypes
import sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, unquote, urlparse

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = Path(__file__).resolve().parent
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))

SHELL_MODULE_PATH = SCRIPTS / "shared_web_shell.py"


def load_shared_web_shell():
    """Load shell helpers from disk every time so preview picks up edits without a stuck import."""
    spec = importlib.util.spec_from_file_location("shared_web_shell_live", SHELL_MODULE_PATH)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod

HTML_ROOT = ROOT / "src" / "network" / "html"
JS_ROOT = ROOT / "data" / "js"
SAMPLE_FILES = [
    {"name": "Books", "size": 0, "isDirectory": True, "isEpub": False, "contents": "5 items", "updated": "May 1, 2025"},
    {"name": "School", "size": 0, "isDirectory": True, "isEpub": False, "contents": "12 items", "updated": "Apr 18, 2025"},
    {"name": "The Brothers Karamazov.pdf", "size": 184320, "isDirectory": False, "isEpub": False, "contents": "2 pages", "updated": "Aug 5, 2026"},
    {"name": "a-yen-for-yen.epub", "size": 92160, "isDirectory": False, "isEpub": True, "title": "A yen for yen", "contents": "15 pages", "updated": "Aug 4, 2026"},
    {"name": "Family business.pdf", "size": 245760, "isDirectory": False, "isEpub": False, "contents": "14 pages", "updated": "Aug 3, 2026"},
    {"name": "Atmospheric pressure.pdf", "size": 102400, "isDirectory": False, "isEpub": False, "contents": "15 pages", "updated": "Jul 12, 2026"},
    {"name": "GMO pups.pdf", "size": 153600, "isDirectory": False, "isEpub": False, "contents": "14 pages", "updated": "Jun 2, 2026"},
]

SAMPLE_TRASH = [
    {
        "name": "medium-growth-analysis-what-works-in-2026.pdf",
        "size": 220000,
        "isDirectory": False,
        "isEpub": False,
        "contents": "18 pages",
        "updated": "Aug 6, 2026",
    },
    {
        "name": "My Honest Dating Advice.pdf",
        "size": 180000,
        "isDirectory": False,
        "isEpub": False,
        "contents": "24 pages",
        "updated": "Aug 5, 2026",
    },
    {
        "name": "Old drafts",
        "size": 0,
        "isDirectory": True,
        "isEpub": False,
        "contents": "3 items",
        "updated": "Jul 28, 2026",
    },
]


class PreviewHandler(BaseHTTPRequestHandler):
    preview_root = None
    trash_items = [dict(item) for item in SAMPLE_TRASH]

    def log_message(self, message, *args):
        print("[preview] " + message % args)

    def apply_shell(self, html, filename):
        return load_shared_web_shell().apply_shared_shell(html, filename)

    def send_bytes(self, payload, content_type, cache_control=None):
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(payload)))
        if cache_control:
            self.send_header("Cache-Control", cache_control)
        self.end_headers()
        self.wfile.write(payload)

    def send_json(self, value):
        self.send_bytes(json.dumps(value).encode("utf-8"), "application/json; charset=utf-8")

    def send_redirect(self, location):
        body = b"Redirecting"
        self.send_response(302)
        self.send_header("Location", location)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _form_fields(self, body: bytes) -> dict:
        text = body.decode("utf-8", errors="ignore")
        if "=" in text and "Content-Disposition" not in text:
            parsed = parse_qs(text)
            return {k: (v[0] if v else "") for k, v in parsed.items()}
        fields = {}
        for part in text.split("Content-Disposition:"):
            if 'name="' not in part:
                continue
            name = part.split('name="', 1)[1].split('"', 1)[0]
            value = part.split("\r\n\r\n", 1)[-1].rsplit("\r\n", 1)[0].strip("\r\n")
            fields[name] = value
        return fields

    def do_GET(self):
        parsed = urlparse(self.path)
        path = unquote(parsed.path)
        query = parse_qs(parsed.query)
        if path == "/epub":
            file_path = HTML_ROOT / "EpubPage.html"
            payload = self.apply_shell(file_path.read_text(encoding="utf-8"), file_path.name)
            self.send_bytes(payload.encode("utf-8"), "text/html; charset=utf-8", "no-store")
            return
        if path in ("/device-library", "/device-library.html"):
            mock = SCRIPTS / "preview_device_library.html"
            self.send_bytes(mock.read_text(encoding="utf-8").encode("utf-8"), "text/html; charset=utf-8", "no-store")
            return
        if path == "/api/status":
            self.send_json({"version": "preview", "ip": "127.0.0.1", "mode": "PREVIEW", "rssi": -42,
                            "freeHeap": 248832, "uptime": 3721})
            return
        if path == "/api/recent-books":
            self.send_json([
                {
                    "path": "/The Brothers Karamazov.pdf",
                    "name": "The Brothers Karamazov.pdf",
                    "title": "The Brothers Karamazov",
                    "author": "Dostoevsky",
                    "isEpub": False,
                    "progress": 0.12,
                    "coverUrl": "",
                },
                {
                    "path": "/A yen for yen.epub",
                    "name": "A yen for yen.epub",
                    "title": "A yen for yen",
                    "author": "",
                    "isEpub": True,
                    "progress": 0.48,
                    "coverUrl": "",
                },
            ])
            return
        if path == "/api/device-identity":
            self.send_json({"ok": True, "hasCard": False, "hasPhoto": False, "name": "", "label": "",
                            "link": "", "template": "photo"})
            return
        if path == "/api/files":
            self.send_json(self.list_files(query.get("path", ["/"])[0]))
            return
        if path == "/api/book-tags":
            self.send_json({
                "indexed": True,
                "tags": ["research", "fiction", "school"],
                "books": [
                    {"title": "The Brothers Karamazov", "path": "/The Brothers Karamazov.pdf", "tag": "fiction"},
                    {"title": "A yen for yen", "path": "/A yen for yen.epub", "tag": "research"},
                    {"title": "Family business", "path": "/Family business.pdf", "tag": ""},
                    {"title": "Atmospheric pressure", "path": "/Atmospheric pressure.pdf", "tag": "school"},
                ],
            })
            return
        if path == "/api/library-index/status":
            self.send_json({"indexing": False, "current": 0, "total": 0})
            return
        if path == "/api/export-notes":
            self.send_json({
                "ok": True,
                "items": [
                    {
                        "id": "1",
                        "type": "bookmark",
                        "book": "A yen for yen",
                        "author": "",
                        "page": 11,
                        "pageCount": 15,
                        "text": "Currency as habit.",
                    },
                    {
                        "id": "2",
                        "type": "annotation",
                        "book": "The Brothers Karamazov",
                        "author": "Dostoevsky",
                        "page": 40,
                        "pageCount": 820,
                        "text": "Underline on faith and doubt.",
                        "pageText": "If God does not exist…",
                    },
                ],
            })
            return
        pages = {
            "/": "HomePage.html",
            "/files": "FilesPage.html",
            "/settings": "SettingsPage.html",
            "/tags": "TagsPage.html",
            "/export": "ExportPage.html",
            "/trash": "TrashPage.html",
            "/font-manager": "FontManagerPage.html",
        }
        if path in pages:
            file_path = HTML_ROOT / pages[path]
            payload = self.apply_shell(file_path.read_text(encoding="utf-8"), file_path.name)
            self.send_bytes(payload.encode("utf-8"), "text/html; charset=utf-8", "no-store")
            return
        if path.startswith("/js/"):
            self.send_file(JS_ROOT / Path(path).name, cache_control="no-store")
            return
        self.send_response(404)
        self.end_headers()

    def send_file(self, file_path, cache_control=None):
        if not file_path.is_file() or ROOT not in file_path.parents:
            self.send_response(404)
            self.end_headers()
            return
        content_type = mimetypes.guess_type(file_path.name)[0] or "text/plain"
        if content_type.startswith("text/"):
            content_type += "; charset=utf-8"
        self.send_bytes(file_path.read_bytes(), content_type, cache_control)

    def list_files(self, requested_path):
        if self.preview_root is None:
            if requested_path.rstrip("/") == "/fonts":
                return [
                    {"name": "Literata", "size": 0, "isDirectory": True, "isEpub": False, "contents": "4 items", "updated": "Aug 1, 2026"},
                    {"name": "SourceSerif", "size": 0, "isDirectory": True, "isEpub": False, "contents": "2 items", "updated": "Jul 20, 2026"},
                ]
            if requested_path.rstrip("/").startswith("/fonts/"):
                return [
                    {"name": "Regular_14.bin", "size": 12000, "isDirectory": False, "isEpub": False, "contents": "BIN", "updated": "Aug 1, 2026"},
                    {"name": "Regular_16.bin", "size": 14000, "isDirectory": False, "isEpub": False, "contents": "BIN", "updated": "Aug 1, 2026"},
                ]
            if requested_path.rstrip("/") == "/Trash":
                return list(PreviewHandler.trash_items)
            return SAMPLE_FILES if requested_path == "/" else []
        relative = requested_path.strip("/")
        folder = (self.preview_root / relative).resolve()
        if self.preview_root not in folder.parents and folder != self.preview_root:
            return []
        if not folder.is_dir():
            return []
        entries = []
        for item in sorted(folder.iterdir(), key=lambda entry: (not entry.is_dir(), entry.name.lower())):
            if item.name.startswith(".") or (relative in ("", "/") and item.name == "Trash"):
                continue
            pages = None
            if item.is_file() and item.suffix.lower() in (".pdf", ".epub"):
                pages = 2 + (sum(ord(c) for c in item.name) % 40)
            entries.append({
                "name": item.name,
                "size": item.stat().st_size if item.is_file() else 0,
                "isDirectory": item.is_dir(),
                "isEpub": item.suffix.lower() == ".epub",
                "contents": (
                    f"{sum(1 for _ in item.iterdir() if not _.name.startswith('.'))} items"
                    if item.is_dir()
                    else (f"{pages} pages" if pages else item.suffix[1:].upper() or "File")
                ),
                "updated": "Local preview",
            })
        return entries

    def do_POST(self):
        parsed = urlparse(self.path)
        path = unquote(parsed.path)
        length = int(self.headers.get("Content-Length", "0") or 0)
        body = self.rfile.read(length) if length else b""
        form = self._form_fields(body)
        if path == "/api/trash/empty":
            PreviewHandler.trash_items = []
            self.send_bytes(b"Trash emptied", "text/plain; charset=utf-8")
            return
        if path == "/api/trash/restore":
            name = form.get("name", "")
            PreviewHandler.trash_items = [i for i in PreviewHandler.trash_items if i["name"] != name]
            self.send_bytes(b"Restored", "text/plain; charset=utf-8")
            return
        if path == "/delete":
            item_path = form.get("path", "")
            permanent = form.get("permanent", "0") == "1"
            name = item_path.rstrip("/").split("/")[-1]
            # Mirror firmware: permanent wipe only for Trash/fonts; otherwise soft-delete.
            under_trash = item_path.startswith("/Trash/")
            under_fonts = item_path.startswith("/fonts/")
            if permanent and not (under_trash or under_fonts):
                self.send_response(403)
                self.send_header("Content-Type", "text/plain; charset=utf-8")
                self.end_headers()
                self.wfile.write(b"Permanent delete is only allowed for Trash and fonts\n")
                return
            if permanent or under_trash:
                PreviewHandler.trash_items = [i for i in PreviewHandler.trash_items if i["name"] != name]
                self.send_bytes(b"Deleted successfully", "text/plain; charset=utf-8")
            else:
                moved = next((dict(item) for item in SAMPLE_FILES if item["name"] == name), None)
                if moved:
                    PreviewHandler.trash_items.append(moved)
                self.send_bytes(b"Moved to Trash", "text/plain; charset=utf-8")
            return
        if path == "/upload":
            self.send_bytes(b"OK", "text/plain; charset=utf-8")
            return
        if path == "/mkdir":
            self.send_bytes(b"OK", "text/plain; charset=utf-8")
            return
        if path == "/api/fonts/rescan":
            self.send_json({"ok": True})
            return
        if path == "/rename":
            self.send_bytes(b"OK", "text/plain; charset=utf-8")
            return
        self.send_response(501)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.end_headers()
        self.wfile.write(b"Preview mode is read-only. Use the device or simulator for mutations.\n")


def main():
    parser = argparse.ArgumentParser(description="Preview the Inx web UI without firmware tooling.")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--root", type=Path, help="Optional SD-card export or fixture directory to browse")
    args = parser.parse_args()
    PreviewHandler.preview_root = args.root.resolve() if args.root else None
    server = ThreadingHTTPServer(("127.0.0.1", args.port), PreviewHandler)
    print(f"Inx web preview: http://127.0.0.1:{args.port}/")
    print(f"Device library mock: http://127.0.0.1:{args.port}/device-library")
    print("Press Ctrl-C to stop.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print()
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
