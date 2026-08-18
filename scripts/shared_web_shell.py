"""Shared reMarkable-inspired shell CSS and page wiring for the Inx web UI."""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

FONT_LINKS = (
    '<link rel="preconnect" href="https://fonts.googleapis.com">'
    '<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>'
    '<link href="https://fonts.googleapis.com/css2?family=Source+Sans+3:wght@400;600;700&family=Source+Serif+4:opsz,wght@8..60,400;8..60,600&display=swap" rel="stylesheet">'
)

SHELL_CSS = """
@view-transition{navigation:auto}
:root{
  --paper:#f7f5f0;--sidebar:#f3f1ec;--panel:#fffdfa;--ink:#242321;--muted:#77736c;
  --line:#dedbd3;--soft:#ebe7dd;--active:#e7e2d7;--blue:#2559f4;--blue-hover:#1142d4;--radius:2px;
  --color-highlighter-orange:#ff9f47;--danger:#8e332d;
  --font-sans:"Source Sans 3",system-ui,sans-serif;
  --font-serif:"Source Serif 4",Georgia,"Times New Roman",serif;
  --sidebar-w:250px;
}
*{box-sizing:border-box;margin:0;padding:0}
html,body{min-height:100%}
body{
  background:var(--paper);color:var(--ink);font-family:var(--font-sans);line-height:1.45;
  -webkit-font-smoothing:antialiased;
}
button,input,select{font:inherit}
a{color:inherit}

.inx-app-shell{min-height:100vh;display:grid;grid-template-columns:var(--sidebar-w) minmax(0,1fr)}
.inx-sidebar{
  position:sticky;top:0;align-self:start;height:100vh;max-height:100vh;
  background:var(--sidebar);border-right:1px solid var(--line);
  padding:26px 14px 14px;display:flex;flex-direction:column;gap:6px;overflow:hidden;
  view-transition-name:inx-sidebar;
}
::view-transition-old(inx-sidebar),
::view-transition-new(inx-sidebar){animation:none;mix-blend-mode:normal;height:100%}
.inx-brand{
  display:flex;align-items:center;gap:10px;padding:2px 12px 22px;text-decoration:none;color:var(--ink);
  font:600 23px/1 var(--font-sans);letter-spacing:-.045em;text-transform:lowercase;
}
.inx-brand .inx-mark{width:26px;height:26px;display:block;flex:0 0 auto}
.inx-side-nav{display:grid;gap:3px}
.inx-side-nav a,.inx-settings-link{
  display:flex;align-items:center;gap:12px;min-height:42px;padding:9px 12px;border-radius:var(--radius);
  text-decoration:none;font-size:15px;font-weight:500;color:var(--ink);
}
.inx-side-nav a:hover,.inx-settings-link:hover{background:var(--soft)}
.inx-side-nav a.active,.inx-settings-link.active{background:var(--active)}
.inx-side-nav svg,.inx-settings-link svg,.inx-recents-list svg{
  width:22px;height:22px;flex:0 0 auto;display:block;shape-rendering:geometricPrecision;
}
.inx-recents{margin-top:14px;padding:0 4px;min-height:0;overflow:auto}
.inx-recents-toggle{
  width:100%;border:0;background:transparent;color:var(--muted);font-size:11px;font-weight:700;
  letter-spacing:.08em;text-transform:uppercase;text-align:left;padding:8px 8px 6px;cursor:pointer;
}
.inx-recents-list{display:grid;gap:2px}
.inx-recents-list a{
  display:flex;align-items:center;gap:10px;padding:7px 8px;border-radius:var(--radius);text-decoration:none;
  font-size:13px;font-weight:600;color:var(--ink);min-width:0;
}
.inx-recents-list a:hover{background:var(--soft)}
.inx-recents-list span{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.inx-recents-empty{padding:6px 8px 10px;font-size:12px;color:var(--muted)}
.inx-sidebar-end{margin-top:auto;padding-top:10px;border-top:1px solid var(--line);display:grid;gap:10px}
.inx-corner-tray{
  position:fixed;right:18px;bottom:18px;z-index:50;width:auto;height:auto;display:flex;align-items:center;
  justify-content:center;gap:4px;padding:4px;background:var(--panel);border:0;
  border-radius:6px;box-shadow:0 0 0 1px #2c28260a,0 1px 2px 0 #2c282614,0 2px 4px 0 #2c282614;
}
.inx-tray-btn{
  position:relative;width:32px;height:32px;border:0;border-radius:6px;background:transparent;color:var(--ink);
  cursor:pointer;display:flex;align-items:center;justify-content:center;text-decoration:none;
}
.inx-tray-btn:hover{background:var(--soft)}
.inx-tray-btn svg{width:1em;height:1em;font-size:20px}
.inx-tray-tip{
  position:absolute;bottom:calc(100% + 8px);left:50%;transform:translateX(-50%);white-space:nowrap;
  padding:6px 10px;border-radius:var(--radius);background:var(--ink);color:#fff;font-size:12px;font-weight:600;
  opacity:0;pointer-events:none;transition:opacity .12s;
}
.inx-tray-btn:hover .inx-tray-tip,.inx-tray-btn:focus-visible .inx-tray-tip{opacity:1}

.inx-main{min-width:0;padding:28px clamp(22px,4vw,52px) 56px}
.inx-page-title{font:600 22px/1.2 var(--font-sans);letter-spacing:-.02em;margin:0 0 20px;color:var(--ink);font-weight:600}

/* Secondary legacy .container pages */
body.inx-shared .container{
  max-width:none;min-height:100vh;margin:0;padding:0;
  display:grid;grid-template-columns:var(--sidebar-w) minmax(0,1fr);
}
body.inx-shared .container>.inx-sidebar{grid-column:1;grid-row:1 / span 40}
body.inx-shared .header{
  grid-column:2;text-align:left;margin:0;padding:28px clamp(22px,4vw,52px) 0;background:var(--paper);border:0;
}
body.inx-shared .header h1{font:600 22px/1.2 var(--font-sans);letter-spacing:-.02em;color:var(--ink)}
body.inx-shared .nav-links{display:none!important}
body.inx-shared .container>:not(.inx-sidebar):not(.header){
  grid-column:2;max-width:1080px;width:100%;padding:18px clamp(22px,4vw,52px) 56px;background:var(--paper);
}
body.inx-shared .card,body.inx-shared .panel,body.inx-shared .stage{
  border:0!important;border-radius:0!important;box-shadow:none!important;background:transparent!important;backdrop-filter:none;
  padding:0!important;margin:0 0 28px!important;
}
body.inx-shared .settings-section{
  border:1px solid var(--line);border-radius:0;box-shadow:none;background:var(--panel);backdrop-filter:none;
}
body.inx-shared .tabs{justify-content:flex-start;gap:8px;margin:0 0 18px;padding:0}
body.inx-shared .tab-btn{
  border:1px solid var(--line);border-radius:var(--radius);background:var(--panel);color:var(--ink);font-weight:600;
  padding:8px 16px;font-size:14px;
}
body.inx-shared .tab-btn.active{background:var(--ink);color:#fff;border-color:var(--ink)}
body.inx-shared input,body.inx-shared select,body.inx-shared .btn,body.inx-shared .add-btn,body.inx-shared .action-btn,
body.inx-shared .new-tag-input,body.inx-shared .search,body.inx-shared .tag-input,body.inx-shared .icon-btn{
  border-radius:var(--radius)!important;font-family:var(--font-sans)!important;
}
body.inx-shared .btn.primary,body.inx-shared .btn-primary,body.inx-shared .add-btn{background:var(--ink);color:#fff;border-radius:var(--radius)}
body.inx-shared .action-btn{border:1px solid var(--ink);background:transparent;color:var(--ink);font-weight:700}
body.inx-shared .action-btn:hover{background:var(--soft)}
body.inx-shared .action-btn.icon-only-action,body.inx-shared .icon-only-action{
  border:0!important;background:transparent!important;box-shadow:none!important;
}
body.inx-shared .action-btn.icon-only-action:hover,body.inx-shared .icon-only-action:hover{
  background:transparent!important;opacity:.72;
}
body.inx-shared .primary-action,body.inx-shared .action-btn.primary-action{background:var(--blue);color:#fff;border-color:var(--blue)}
body.inx-shared .toast{border-radius:var(--radius)}
body.inx-shared .tag-pill{border-radius:var(--radius)!important;background:var(--soft)!important}
body.inx-shared .toggle-slider{border-radius:var(--radius)}
body.inx-shared .toggle-slider:before{border-radius:1px}
body.inx-shared input:checked+.toggle-slider{background:var(--ink)}
body.inx-shared .setting-item{border-bottom:1px solid var(--line)!important;padding:14px 18px!important}
body.inx-shared .section-header{padding:16px 18px 8px!important}
body.inx-shared .section-header h3{font:600 16px/1.2 var(--font-sans)!important;color:var(--ink)!important}
body.inx-shared .setting-label,.inx-shared .title,.inx-shared .book-title{font-family:var(--font-sans)!important;color:var(--ink)!important}
body.inx-shared .header{border:0!important}
body.inx-shared .toolbar .title{font:600 16px/1.2 var(--font-sans)}
body.inx-shared .summary{color:var(--muted)!important}

.inx-drawer-backdrop[hidden],
.inx-drawer[hidden]{
  display:none!important;
}
.inx-drawer-backdrop{
  display:block;position:fixed;inset:0;z-index:60;
  background:rgba(33,30,28,.72);
  opacity:0;pointer-events:none;
  transition:opacity .22s cubic-bezier(0,0,.2,1);
}
.inx-drawer-backdrop.is-open{
  opacity:1;pointer-events:auto;
}
.inx-drawer{
  display:flex;position:fixed;top:0;right:0;width:40vw;min-width:min(360px,100vw);max-width:720px;
  height:100vh;background:var(--panel);border-left:1px solid var(--line);z-index:70;
  padding:28px 40px 40px;overflow:auto;flex-direction:column;text-align:left!important;
  box-shadow:-18px 0 56px rgba(33,30,28,.38),-4px 0 18px rgba(33,30,28,.16);
  transform:translateX(100%);pointer-events:none;
  transition:transform .28s cubic-bezier(0,0,.2,1);
}
.inx-drawer.is-open{
  transform:translateX(0);pointer-events:auto;
}
.inx-drawer-head{display:flex;align-items:center;justify-content:center;position:relative;gap:12px;margin-bottom:28px;text-align:left!important;min-height:36px}
.inx-drawer-tabs{display:flex;gap:28px;justify-content:center;flex:0 1 auto}
.inx-drawer-tabs button{
  border:0;background:transparent;padding:8px 0;font-size:15px;font-weight:700;color:var(--muted);cursor:pointer;
  border-bottom:2px solid transparent;text-align:center;
}
.inx-drawer-tabs button.active{color:var(--ink);border-bottom-color:var(--blue)}
.inx-drawer-close{
  position:absolute;right:0;top:50%;transform:translateY(-50%);
  width:36px;height:36px;border:0;background:transparent;font-size:26px;line-height:1;cursor:pointer;
  color:var(--muted);flex:0 0 auto;z-index:2;
}
.inx-drawer-close:hover{color:var(--ink)}
.inx-drawer-body,.inx-drawer-body *{text-align:left!important}
.inx-drawer-body{display:block!important;width:100%!important;max-width:none!important;margin:0!important;padding:0;min-height:120px;color:var(--ink)}
.inx-update{padding:0 0 28px;margin:0 0 28px;border-bottom:1px solid var(--line)}
.inx-update:last-child{border-bottom:0;margin-bottom:0;padding-bottom:0}
.inx-update-title{font:400 32px/1.2 var(--font-serif)!important;letter-spacing:-.02em;margin:0 0 10px;color:var(--ink)}
.inx-update-date{font-size:13px;color:var(--muted);margin:0 0 12px}
.inx-update p{font-size:15px;color:#4a4742;line-height:1.55;margin:0;max-width:36em}

@media(max-width:900px){
  .inx-app-shell,body.inx-shared .container{display:block}
  .inx-sidebar{
    position:relative;height:auto;max-height:none;padding:14px 12px;border-right:0;border-bottom:1px solid var(--line);
  }
  .inx-brand{padding:0 4px 10px;font-size:20px}
  .inx-brand .inx-mark{width:22px;height:22px}
  .inx-side-nav{display:flex;overflow:auto;gap:4px}
  .inx-side-nav a{min-height:38px;white-space:nowrap;font-size:14px}
  .inx-recents,.inx-sidebar-end .inx-settings-link span{display:none}
  .inx-sidebar-end{border-top:0;padding-top:0;display:flex;align-items:center;gap:8px}
  .inx-corner-tray{right:12px;bottom:12px;padding:4px;gap:4px}
  .inx-main,body.inx-shared .container>:not(.inx-sidebar):not(.header){padding:20px 16px 48px}
  body.inx-shared .header{padding:18px 16px 0}
  .inx-page-title,body.inx-shared .header h1{font-size:20px;margin-bottom:14px}
}

/* Device identity (Settings) */
.identity-section{padding-bottom:8px}
.identity-section .section-header{align-items:center}
.identity-section .field{display:grid;gap:6px;margin-bottom:14px;padding:0 24px}
.identity-section .field label{font-size:13px;font-weight:600;color:var(--muted)}
.identity-section .field input{
  width:100%;min-height:40px;border:1px solid var(--line);border-radius:var(--radius);background:#fff;color:var(--ink);
  padding:8px 12px;
}
.identity-section .segmented{
  display:flex;gap:4px;margin:0 24px 14px;padding:3px;background:var(--soft);border-radius:var(--radius);
}
.identity-section .segmented button{
  flex:1;border:0;background:transparent;color:var(--muted);padding:8px;border-radius:2px;
  font-size:13px;font-weight:600;cursor:pointer;
}
.identity-section .segmented button.active{background:var(--ink);color:#fff}
.identity-section .dropzone{
  display:block;margin:0 24px 14px;border:1.5px dashed var(--line);border-radius:var(--radius);background:var(--panel);
  padding:16px;text-align:center;color:var(--muted);cursor:pointer;position:relative;
}
.identity-section .dropzone strong{display:block;color:var(--ink);font-size:14px;margin-bottom:2px}
.identity-section .dropzone input{position:absolute;width:1px;height:1px;overflow:hidden;clip:rect(0 0 0 0)}
.identity-section .dropzone.disabled{opacity:.45;pointer-events:none}
.identity-actions{display:flex;flex-wrap:wrap;gap:8px;margin:0 24px 8px}
.identity-actions .action-btn{flex:1;min-width:100px;min-height:40px}
.identity-note{min-height:18px;font-size:12.5px;color:var(--muted);margin:0 24px 16px}
.identity-layout{
  display:grid;grid-template-columns:minmax(260px,1fr) minmax(220px,280px);gap:18px;align-items:start;padding:0 0 12px;
}
.identity-section.wide .identity-layout{display:grid}
.card-stage{background:var(--soft);border:1px solid var(--line);border-radius:var(--radius);padding:14px;display:flex;justify-content:center;margin:0 24px 16px 0}
.card-stage canvas{display:block;width:100%;max-width:260px;height:auto;background:#fff}
.identity-empty{display:flex;justify-content:center;padding:18px 24px 24px}
.identity-add{
  display:flex;flex-direction:column;align-items:center;gap:10px;border:1.5px dashed var(--line);border-radius:var(--radius);
  background:var(--panel);padding:24px 40px;cursor:pointer;min-width:200px;font:inherit;
}
.identity-add:hover{background:var(--soft)}
.identity-add .plus{
  width:36px;height:36px;border-radius:50%;background:var(--ink);color:#fff;display:flex;align-items:center;
  justify-content:center;font-size:20px;font-weight:700;
}
.identity-view{display:flex;justify-content:center;padding:8px 24px 24px}
.identity-view img{display:block;width:100%;max-width:220px;height:auto;border-radius:var(--radius);box-shadow:0 8px 24px #24232114}
@media(max-width:900px){
  .identity-layout{grid-template-columns:1fr}
  .card-stage{margin:0 24px 16px;order:-1}
}
"""


def ensure_font_links(html: str) -> str:
    html = re.sub(
        r'<link[^>]+fonts\.googleapis\.com/css2\?family=Atkinson[^>]*>',
        "",
        html,
    )
    html = re.sub(
        r'<link[^>]+fonts\.googleapis\.com/css2\?family=Source\+Sans[^>]*>',
        "",
        html,
    )
    if "fonts.googleapis.com" not in html:
        if "<head>" in html.lower():
            html = re.sub(r"(<head[^>]*>)", r"\1" + FONT_LINKS, html, count=1, flags=re.I)
        else:
            html = FONT_LINKS + html
    elif "Source+Sans+3" not in html and "Source Sans 3" not in html:
        html = html.replace("<head>", "<head>" + FONT_LINKS, 1)
        html = html.replace("<head ", "<head " + "data-x=1 ", 1)  # noop safety
        if FONT_LINKS not in html:
            html = re.sub(r"(<head[^>]*>)", r"\1" + FONT_LINKS, html, count=1, flags=re.I)
    html = html.replace(
        'font-family:"Atkinson Hyperlegible",system-ui,sans-serif',
        "font-family:var(--font-sans),system-ui,sans-serif",
    )
    return html


def inject_style(html: str, css: str) -> str:
    block = '<style id="inx-shell-css">' + css + "</style>"
    # Always replace — skipping left stale center-aligned drawer CSS baked into pages.
    if 'id="inx-shell-css"' in html:
        return re.sub(
            r'<style\b[^>]*\bid=["\']inx-shell-css["\'][^>]*>[\s\S]*?</style>',
            block,
            html,
            count=1,
            flags=re.I,
        )
    if "</head>" in html.lower():
        return re.sub(r"</head>", block + "</head>", html, count=1, flags=re.I)
    if "</style>" in html:
        return html.replace("</style>", "</style>" + block, 1)
    return block + html


def inject_shell_script(html: str) -> str:
    shell_js = Path(__file__).resolve().parents[1] / "data" / "js" / "inx_shell.js"
    version = int(shell_js.stat().st_mtime) if shell_js.exists() else 1
    # No defer: run as soon as parsed at end of body for faster sidebar hydration.
    tag = f'<script src="/js/inx_shell.js?v={version}"></script>'
    if "/js/inx_shell.js" in html:
        return re.sub(r'<script[^>]+/js/inx_shell\.js[^>]*></script>', tag, html, count=1, flags=re.I)
    if "</body>" in html.lower():
        return re.sub(r"</body>", tag + "</body>", html, count=1, flags=re.I)
    return html + tag


def detect_active_path(html: str, filename: str = "") -> str:
    mapping = {
        "HomePage.html": "/",
        "FilesPage.html": "/files",
        "TagsPage.html": "/tags",
        "FontManagerPage.html": "/font-manager",
        "ExportPage.html": "/export",
        "TrashPage.html": "/trash",
        "SettingsPage.html": "/settings",
        "EpubPage.html": "/files",
    }
    if filename in mapping:
        return mapping[filename]
    for path in ("/settings", "/font-manager", "/export", "/trash", "/tags", "/files", "/"):
        if f'href="{path}"' in html and "active" in html:
            # weak heuristic
            pass
    if 'href="/settings"' in html and "active" in html[html.find("settings") - 40 : html.find("settings") + 20]:
        return "/settings"
    return "/"


SIDEBAR_PLACEHOLDER = '<aside id="inx-shell-mount" class="inx-sidebar" data-active="{active}"></aside>'

# Keep in sync with data/js/inx_shell.js icon paths (first-paint only).
_SHELL_ICONS = {
    "mark": '<svg class="inx-mark" viewBox="0 0 48 48" fill="currentColor" aria-hidden="true"><g transform="translate(24 24) rotate(42)"><rect x="-16" y="-9" width="32" height="6" rx="3"/><rect x="-9" y="4" width="20" height="6" rx="3"/></g></svg>',
    "home": '<svg viewBox="0 0 48 48" fill="currentColor" aria-hidden="true"><path d="M24 3.37891L42 21.3789V42.0002H27V30.1503C23.5766 30.8452 21 33.8718 21 37.5002V42.0002H6V21.3789L24 3.37891ZM18 27.0002H30V39.0002H39V22.6215L24 7.62155L9 22.6215V39.0002H18V27.0002Z"/></svg>',
    "files": '<svg viewBox="0 0 48 48" fill="currentColor" aria-hidden="true"><path d="M39 6H9V3H39V6Z"/><path d="M34 35H14V24H34V35ZM17 27V32H31V27H17Z"/><path d="M3 15H45V45H3V15ZM6 22.5V42H42V18H10.5C8.01472 18 6 20.0147 6 22.5Z"/><path d="M6 12H42V9H6V12Z"/></svg>',
    "tags": '<svg viewBox="0 0 48 48" fill="currentColor" aria-hidden="true"><path d="M10 45H38V19L24 3L10 19V45ZM13 41.5V20.5L24 8.5L35 20.5V41.5H13Z"/><path d="M24 15.5C22.6193 15.5 21.5 16.6193 21.5 18C21.5 19.3807 22.6193 20.5 24 20.5C25.3807 20.5 26.5 19.3807 26.5 18C26.5 16.6193 25.3807 15.5 24 15.5Z"/></svg>',
    "fonts": '<svg viewBox="0 0 48 48" fill="currentColor" aria-hidden="true"><path d="M7 3H41V45H7V3ZM10 6V42H38V6H10ZM16 12H32V15H16V12ZM16 19H32V22H16V19ZM16 26H27V29H16V26Z"/></svg>',
    "bookmark": '<svg viewBox="0 0 48 48" fill="currentColor" aria-hidden="true"><path d="M12 4H36V44L24 35.2L12 44V4ZM15 7V37.4L24 30.8L33 37.4V7H15Z"/></svg>',
    "trash": '<svg viewBox="0 0 48 48" fill="currentColor" aria-hidden="true"><path d="M21 18V33H18V18H21Z"/><path d="M30 33V18H27V33H30Z"/><path d="M33 3H15V9H6V12H9V42H39V12H42V9H33V3ZM30 9H18V6H30V9ZM22.5 12H36V39H12V22.5C12 16.701 16.701 12 22.5 12Z"/></svg>',
    "settings": '<svg viewBox="0 0 48 48" fill="currentColor" aria-hidden="true"><path d="M6 12H22V15H6V12Z"/><path d="M34 12H42V15H34V12Z"/><path fill-rule="evenodd" d="M31.5 8C28.4624 8 26 10.4624 26 13.5C26 16.5376 28.4624 19 31.5 19C34.5376 19 37 16.5376 37 13.5C37 10.4624 34.5376 8 31.5 8ZM31.5 11C30.1193 11 29 12.1193 29 13.5C29 14.8807 30.1193 16 31.5 16C32.8807 16 34 14.8807 34 13.5C34 12.1193 32.8807 11 31.5 11Z"/><path d="M6 22.5H14V25.5H6V22.5Z"/><path d="M26 22.5H42V25.5H26V22.5Z"/><path fill-rule="evenodd" d="M19.5 18.5C16.4624 18.5 14 20.9624 14 24C14 27.0376 16.4624 29.5 19.5 29.5C22.5376 29.5 25 27.0376 25 24C25 20.9624 22.5376 18.5 19.5 18.5ZM19.5 21.5C18.1193 21.5 17 22.6193 17 24C17 25.3807 18.1193 26.5 19.5 26.5C20.8807 26.5 22 25.3807 22 24C22 22.6193 20.8807 21.5 19.5 21.5Z"/><path d="M6 33H28V36H6V33Z"/><path d="M40 33H42V36H40V33Z"/><path fill-rule="evenodd" d="M33.5 29C30.4624 29 28 31.4624 28 34.5C28 37.5376 30.4624 40 33.5 40C36.5376 40 39 37.5376 39 34.5C39 31.4624 36.5376 29 33.5 29ZM33.5 32C32.1193 32 31 33.1193 31 34.5C31 35.8807 32.1193 37 33.5 37C34.8807 37 36 35.8807 36 34.5C36 33.1193 34.8807 32 33.5 32Z"/></svg>',
}

_NAV = (
    ("/", "home", "Home"),
    ("/files", "files", "My files"),
    ("/tags", "tags", "Tags"),
    ("/font-manager", "fonts", "Fonts"),
    ("/export", "bookmark", "Bookmarks & annotations"),
    ("/trash", "trash", "Trash"),
)


def render_sidebar_html(active: str) -> str:
    links = []
    for href, icon, label in _NAV:
        cls = ' class="active" aria-current="page"' if href == active else ""
        links.append(f'<a href="{href}"{cls}>{_SHELL_ICONS[icon]}<span>{label}</span></a>')
    settings_cls = " active" if active == "/settings" else ""
    return (
        f'<a class="inx-brand" href="/" aria-label="inx home">{_SHELL_ICONS["mark"]}<span>inx</span></a>'
        f'<nav class="inx-side-nav" aria-label="Main navigation">{"".join(links)}</nav>'
        '<div class="inx-recents"><button type="button" class="inx-recents-toggle" aria-expanded="true">Recents</button>'
        '<div class="inx-recents-list"><div class="inx-recents-empty">No recent files</div></div></div>'
        f'<div class="inx-sidebar-end"><a class="inx-settings-link{settings_cls}" href="/settings">'
        f'{_SHELL_ICONS["settings"]}<span>Settings</span></a></div>'
    )


def fill_sidebar_mount(html: str, active: str) -> str:
    """Put real sidebar markup in the first paint so navigation does not flash empty."""
    inner = render_sidebar_html(active)
    pattern = re.compile(
        r'(<aside\b[^>]*\bid=["\']inx-shell-mount["\'][^>]*>)(.*?)(</aside>)',
        re.I | re.S,
    )

    def repl(match: re.Match[str]) -> str:
        open_tag = re.sub(r'\sdata-active=(["\'])[^"\']*\1', "", match.group(1), count=1, flags=re.I)
        open_tag = open_tag[:-1] + f' data-active="{active}">'
        return open_tag + inner + match.group(3)

    if pattern.search(html):
        return pattern.sub(repl, html, count=1)
    return html.replace(
        SIDEBAR_PLACEHOLDER.format(active=active),
        f'<aside id="inx-shell-mount" class="inx-sidebar" data-active="{active}">{inner}</aside>',
        1,
    )

IDENTITY_CARD_HTML = """
<div class="settings-section identity-section" id="identityCard">
  <div class="section-header"><h3>Device identity</h3>
    <button class="section-add-btn" id="identityEditBtn" onclick="enterIdentityEdit()" style="display:none">Edit</button>
  </div>
  <div class="identity-empty" id="identityEmpty" style="display:none">
    <button class="identity-add" type="button" onclick="enterIdentityEdit()">
      <span class="plus">+</span>
      <span>Add device identity</span>
    </button>
  </div>
  <div class="identity-view" id="identityView" style="display:none">
    <img id="identityViewImg" alt="Device identity card">
  </div>
  <div class="identity-layout" id="identityEditForm" style="display:none">
    <div>
      <div class="field">
        <label for="deviceName">Device name</label>
        <input id="deviceName" maxlength="64" placeholder="My Inx Reader" oninput="scheduleIdentityRender()">
      </div>
      <div class="field">
        <label for="identityLabel">Label (optional)</label>
        <input id="identityLabel" maxlength="48" placeholder="e.g. Personal Reader" oninput="scheduleIdentityRender()">
      </div>
      <div class="field">
        <label for="identityLink">QR redirect link</label>
        <input id="identityLink" maxlength="180" inputmode="url" placeholder="https://example.com" oninput="scheduleIdentityRender()">
      </div>
      <div class="segmented" id="templateSwitch">
        <button type="button" class="active" data-template="photo" onclick="setIdentityTemplate('photo')">With photo</button>
        <button type="button" data-template="minimal" onclick="setIdentityTemplate('minimal')">Minimal</button>
      </div>
      <label class="dropzone" id="identityDropzone" for="identityImage">
        <strong>Upload photo</strong>
        <span>PNG or JPEG, shown at the top of the card</span>
        <input id="identityImage" type="file" accept="image/png,image/jpeg,image/webp" onchange="loadIdentityImage(event)">
      </label>
      <div class="identity-actions">
        <button class="btn-primary action-btn" id="saveIdentityBtn" type="button" onclick="saveIdentity()">Save</button>
        <button class="btn-secondary action-btn" type="button" onclick="cancelIdentityEdit()">Cancel</button>
        <button class="btn-secondary action-btn" id="downloadIdentityBtn" type="button" onclick="downloadIdentityCard()">Download</button>
      </div>
      <div class="identity-note" id="identityStatus"></div>
    </div>
    <div class="card-stage"><canvas id="identityCanvas" width="640" height="1000"></canvas></div>
  </div>
</div>
"""


def inject_settings_identity(html: str) -> str:
    if 'id="identityCard"' in html:
        return html
    scripts = (
        '<script src="/js/qr_creator_logo.min.js"></script>'
        '<script defer src="/js/device_identity.js"></script>'
    )
    if "/js/device_identity.js" not in html:
        if "</body>" in html.lower():
            html = re.sub(r"</body>", scripts + "</body>", html, count=1, flags=re.I)
        else:
            html += scripts

    # Prefer inserting after System tab opens / first settings section
    marker = '<div class="active tab-content" id="system-tab">'
    if marker in html:
        return html.replace(marker, marker + IDENTITY_CARD_HTML, 1)
    marker_min = '<div class="active tab-content"id=system-tab>'
    if marker_min in html:
        return html.replace(marker_min, marker_min + IDENTITY_CARD_HTML, 1)
    # Fallback: before Save All actions
    if '<div class=action-buttons>' in html:
        return html.replace('<div class=action-buttons>', IDENTITY_CARD_HTML + '<div class=action-buttons>', 1)
    if '<div class="action-buttons">' in html:
        return html.replace('<div class="action-buttons">', IDENTITY_CARD_HTML + '<div class="action-buttons">', 1)
    return html


def apply_shared_shell(html: str, filename: str = "") -> str:
    """Normalize any page onto the shared sidebar shell."""
    active = detect_active_path(html, filename)
    html = ensure_font_links(html)
    html = inject_style(html, SHELL_CSS)
    html = inject_shell_script(html)
    if 'name="view-transition"' not in html and "<head" in html.lower():
        html = re.sub(
            r"(<head[^>]*>)",
            r'\1<meta name="view-transition" content="same-origin">',
            html,
            count=1,
            flags=re.I,
        )

    # Mark body
    if re.search(r"<body[^>]*class=", html, flags=re.I):
        if "inx-shared" not in html and "inx-app" not in html:
            html = re.sub(r"(<body[^>]*class=[\"'])", r"\1inx-shared ", html, count=1, flags=re.I)
    elif "<body" in html.lower():
        html = re.sub(r"<body", '<body class="inx-shared"', html, count=1, flags=re.I)
    else:
        # minified pages without body
        if "</style>" in html:
            html = html.replace("</style>", '</style><body class="inx-shared">', 1)
            if "</html>" in html and "</body>" not in html.lower():
                html = html.replace("</html>", "</body></html>")

    placeholder = SIDEBAR_PLACEHOLDER.format(active=active)

    # Replace existing sidebars
    if 'id="inx-shell-mount"' not in html:
        if re.search(r'<aside[^>]*class="[^"]*inx-sidebar', html):
            html = re.sub(r'<aside[^>]*class="[^"]*inx-sidebar[\s\S]*?</aside>', placeholder, html, count=1)
        elif re.search(r'<aside[^>]*class="[^"]*sidebar', html):
            html = re.sub(r'<aside[^>]*class="[^"]*sidebar[\s\S]*?</aside>', placeholder, html, count=1)
        elif '<div class="container">' in html or "<div class=container>" in html:
            html = re.sub(
                r'(<div class="?container"?>)',
                r"\1" + placeholder,
                html,
                count=1,
            )
        elif '<div class="inx-app-shell">' in html or '<div class="app-shell">' in html:
            html = re.sub(
                r'(<div class="(?:inx-)?app-shell">)',
                r"\1" + placeholder,
                html,
                count=1,
            )

    # Strip legacy floating trays / top nav from source; shell owns them
    html = re.sub(r'<div class="inx-feedback-tray">[\s\S]*?</div>', "", html)
    html = re.sub(r'<div class="?nav-links"?>[\s\S]*?</div>', "", html)
    html = re.sub(r'<a[^>]*href=["\']?/epub["\']?[^>]*>[^<]*</a>\s*', "", html)
    html = html.replace("Book Tags", "Tags")

    if filename == "SettingsPage.html" or active == "/settings":
        html = inject_settings_identity(html)

    html = fill_sidebar_mount(html, active)
    return html

