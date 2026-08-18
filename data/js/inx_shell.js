/* Identical Inx web shell: sidebar, updates drawer, feedback tray. */
(function () {
  const NAV = [
    { href: "/", icon: "home", label: "Home" },
    { href: "/files", icon: "files", label: "My files" },
    { href: "/tags", icon: "tags", label: "Tags" },
    { href: "/font-manager", icon: "fonts", label: "Fonts" },
    { href: "/export", icon: "bookmark", label: "Bookmarks & annotations" },
    { href: "/trash", icon: "trash", label: "Trash" },
  ];

  const FEEDBACK_URL = "https://github.com/obijuankenobiii/inx/issues/new";

  const ICONS = {
    mark: '<svg class="inx-mark" viewBox="0 0 48 48" fill="currentColor" aria-hidden="true"><g transform="translate(24 24) rotate(42)"><rect x="-16" y="-9" width="32" height="6" rx="3"/><rect x="-9" y="4" width="20" height="6" rx="3"/></g></svg>',
    home: '<svg viewBox="0 0 48 48" fill="currentColor" aria-hidden="true"><path d="M24 3.37891L42 21.3789V42.0002H27V30.1503C23.5766 30.8452 21 33.8718 21 37.5002V42.0002H6V21.3789L24 3.37891ZM18 27.0002H30V39.0002H39V22.6215L24 7.62155L9 22.6215V39.0002H18V27.0002Z"/></svg>',
    files: '<svg viewBox="0 0 48 48" fill="currentColor" aria-hidden="true"><path d="M39 6H9V3H39V6Z"/><path d="M34 35H14V24H34V35ZM17 27V32H31V27H17Z"/><path d="M3 15H45V45H3V15ZM6 22.5V42H42V18H10.5C8.01472 18 6 20.0147 6 22.5Z"/><path d="M6 12H42V9H6V12Z"/></svg>',
    tags: '<svg viewBox="0 0 48 48" fill="currentColor" aria-hidden="true"><path d="M10 45H38V19L24 3L10 19V45ZM13 41.5V20.5L24 8.5L35 20.5V41.5H13Z"/><path d="M24 15.5C22.6193 15.5 21.5 16.6193 21.5 18C21.5 19.3807 22.6193 20.5 24 20.5C25.3807 20.5 26.5 19.3807 26.5 18C26.5 16.6193 25.3807 15.5 24 15.5Z"/></svg>',
    fonts: '<svg viewBox="0 0 48 48" fill="currentColor" aria-hidden="true"><path d="M7 3H41V45H7V3ZM10 6V42H38V6H10ZM16 12H32V15H16V12ZM16 19H32V22H16V19ZM16 26H27V29H16V26Z"/></svg>',
    bookmark:
      '<svg viewBox="0 0 48 48" fill="currentColor" aria-hidden="true"><path d="M12 4H36V44L24 35.2L12 44V4ZM15 7V37.4L24 30.8L33 37.4V7H15Z"/></svg>',
    trash:
      '<svg viewBox="0 0 48 48" fill="currentColor" aria-hidden="true"><path d="M21 18V33H18V18H21Z"/><path d="M30 33V18H27V33H30Z"/><path d="M33 3H15V9H6V12H9V42H39V12H42V9H33V3ZM30 9H18V6H30V9ZM22.5 12H36V39H12V22.5C12 16.701 16.701 12 22.5 12Z"/></svg>',
    settings:
      '<svg viewBox="0 0 48 48" fill="currentColor" aria-hidden="true"><path d="M6 12H22V15H6V12Z"/><path d="M34 12H42V15H34V12Z"/><path fill-rule="evenodd" d="M31.5 8C28.4624 8 26 10.4624 26 13.5C26 16.5376 28.4624 19 31.5 19C34.5376 19 37 16.5376 37 13.5C37 10.4624 34.5376 8 31.5 8ZM31.5 11C30.1193 11 29 12.1193 29 13.5C29 14.8807 30.1193 16 31.5 16C32.8807 16 34 14.8807 34 13.5C34 12.1193 32.8807 11 31.5 11Z"/><path d="M6 22.5H14V25.5H6V22.5Z"/><path d="M26 22.5H42V25.5H26V22.5Z"/><path fill-rule="evenodd" d="M19.5 18.5C16.4624 18.5 14 20.9624 14 24C14 27.0376 16.4624 29.5 19.5 29.5C22.5376 29.5 25 27.0376 25 24C25 20.9624 22.5376 18.5 19.5 18.5ZM19.5 21.5C18.1193 21.5 17 22.6193 17 24C17 25.3807 18.1193 26.5 19.5 26.5C20.8807 26.5 22 25.3807 22 24C22 22.6193 20.8807 21.5 19.5 21.5Z"/><path d="M6 33H28V36H6V33Z"/><path d="M40 33H42V36H40V33Z"/><path fill-rule="evenodd" d="M33.5 29C30.4624 29 28 31.4624 28 34.5C28 37.5376 30.4624 40 33.5 40C36.5376 40 39 37.5376 39 34.5C39 31.4624 36.5376 29 33.5 29ZM33.5 32C32.1193 32 31 33.1193 31 34.5C31 35.8807 32.1193 37 33.5 37C34.8807 37 36 35.8807 36 34.5C36 33.1193 34.8807 32 33.5 32Z"/></svg>',
    updates: '<svg viewBox="0 0 48 48" fill="currentColor" aria-hidden="true"><path d="M10.8644 10.3147L28.7326 16H35.3419C37.5922 16 39.7811 16.8025 41.4178 18.2908C43.0579 19.7821 44 21.8278 44 24C44 26.1682 43.061 28.2307 41.4178 29.7248C39.9362 31.0721 38.0083 31.8398 36 31.9775V42H27V32.5515L5 39.5515V8.44873L10.8531 10.3111C10.8569 10.3123 10.8606 10.3135 10.8644 10.3147ZM8 35.4487L27 29.4033V18.5969L13.8629 14.4169C13.8596 14.4159 13.8562 14.4138 13.8529 14.4138C10.9543 13.4997 8 15.664 8 18.7056V35.4487ZM30 29H35.3419C36.9013 29 38.3548 28.4553 39.3995 27.5053C40.4439 26.5556 41 25.2879 41 24C41 22.7161 40.447 21.4629 39.3995 20.5104C38.3487 19.5548 36.8924 19 35.3419 19H30V29ZM30 39H33V32.2561C31.2522 32.8738 30 34.5407 30 36.5V39Z"/></svg>',
    feedback: '<svg viewBox="0 0 48 48" fill="currentColor" aria-hidden="true"><path d="M16 19H32V22H16V19Z"/><path d="M16 26H32V29H16V26Z"/><path d="M44 9H4V45.6746L14.922 39H44V9ZM17.5 12H41V36H14.078L7 40.3254V22.5C7 16.701 11.701 12 17.5 12Z"/></svg>',
    pdf: '<svg viewBox="0 0 48 48" fill="currentColor" aria-hidden="true"><path d="M10 23V6H26V18H38V23H41V15H29L29 3H7V23H10ZM29 7.24604L36.754 15H29V7.24604Z"/><path d="M32 27H41V30H35V33H40V36H35V41H32V27Z"/><path d="M7 27H11.5C12.8924 27 14.2277 27.5531 15.2123 28.5377C16.1969 29.5223 16.75 30.8576 16.75 32.25C16.75 33.6424 16.1969 34.9777 15.2123 35.9623C14.2277 36.9469 12.8924 37.5 11.5 37.5H10V41H7V27ZM10 34.5H11.5C12.0966 34.5 12.669 34.2627 13.091 33.841C13.5127 33.419 13.75 32.8466 13.75 32.25C13.75 31.6534 13.5127 31.081 13.091 30.659C12.669 30.2373 12.0966 30 11.5 30H10V34.5Z"/><path d="M23.5 27H19V41H23.5C25.2919 41 27.0083 40.2853 28.273 39.0207C29.5383 37.7553 30.25 36.0409 30.25 34.25C30.25 33.3726 30.079 32.4002 29.768 31.5008C29.4608 30.6121 28.9777 29.6818 28.273 28.977C27.0071 27.7112 25.2902 27 23.5 27ZM22 38V30H23.5C24.4946 30 25.4484 30.3951 26.1517 31.0983C26.4315 31.3782 26.7172 31.8578 26.9327 32.481C27.1445 33.0935 27.25 33.735 27.25 34.25C27.25 35.2439 26.8554 36.1956 26.1517 36.8993C25.4472 37.6038 24.4929 38 23.5 38H22Z"/></svg>',
    epub: '<svg viewBox="0 0 48 48" fill="currentColor" aria-hidden="true"><path d="M41 3H7V45H41V3ZM27.5 42H10V6H14V24.1213L19 19.1213L24 24.1213V6H38V31.5C38 37.299 33.299 42 27.5 42Z"/></svg>',
  };

  const UPDATES = [
    {
      id: "redesign",
      title: "Redesigned web library",
      date: "August 2026",
      body: "A quieter paper-like shell across Home, My files, Tags, Fonts, and more — shared navigation, calmer typography, and cover-aware recents.",
    },
    {
      id: "device-library",
      title: "Refined device library",
      date: "August 2026",
      body: "Shelf and list views get clearer hierarchy, sharper icons, and status that shows the full book title when you select an item.",
    },
    {
      id: "import",
      title: "Faster imports",
      date: "August 2026",
      body: "Drag files onto Home or use Import in My files. PDF, JPG, PNG, and EPUB up to 100 MB — EPUB titles come from the book metadata, not the filename.",
    },
  ];

  const NEXT = [
    {
      id: "auto-update",
      title: "Better auto-updating",
      body: "Scheduled Wi‑Fi checks, quieter update prompts, and a short log of what changed after each install.",
    },
    {
      id: "performance",
      title: "Performance improvements",
      body: "Faster library scans, snappier page turns, and lighter memory use while reading large books.",
    },
    {
      id: "drm",
      title: "DRM reading",
      body: "Open DRM-protected books you own, with the same calm reading focus as the rest of the device.",
    },
  ];

  function escapeHtml(value) {
    return String(value || "").replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
  }

  function displayName(name, isEpub, title) {
    if (title && String(title).trim()) return String(title).trim();
    if (!name) return "File";
    if (isEpub) return String(name).replace(/\.epub$/i, "");
    return String(name);
  }

  function currentPath() {
    const mount = document.getElementById("inx-shell-mount");
    if (mount && mount.dataset.active) return mount.dataset.active;
    const path = location.pathname.replace(/\/$/, "") || "/";
    return path === "" ? "/" : path;
  }

  function recentItems() {
    return [];
  }

  async function recentItemsOrLibrary() {
    try {
      const res = await fetch("/api/recent-books");
      if (!res.ok) return [];
      const items = await res.json();
      if (!Array.isArray(items)) return [];
      return items.slice(0, 6).map((item) => ({
        name: item.title || item.name || item.path || "Book",
        path: item.path || "",
        isEpub: !!item.isEpub,
        pages: typeof item.progress === "number" && item.progress >= 0 ? Math.round(item.progress * 100) + "%" : undefined,
        coverUrl: item.coverUrl || "",
      }));
    } catch (_) {
      return [];
    }
  }

  function sidebarHtml(active, recents) {
    const links = NAV.map((item) => {
      const isActive = item.href === active;
      return (
        '<a href="' +
        item.href +
        '"' +
        (isActive ? ' class="active" aria-current="page"' : "") +
        ">" +
        ICONS[item.icon] +
        "<span>" +
        escapeHtml(item.label) +
        "</span></a>"
      );
    }).join("");

    const list = recents || [];
    const recentsHtml =
      '<div class="inx-recents"><button type="button" class="inx-recents-toggle" aria-expanded="true">Recents</button><div class="inx-recents-list">' +
      (list.length
        ? list
            .map((item) => {
              const href = item.isEpub
                ? "/epub-viewer.html?path=" + encodeURIComponent(item.path || "")
                : "/download?path=" + encodeURIComponent(item.path || "");
              return (
                '<a href="' +
                href +
                '">' +
                (item.isEpub ? ICONS.epub : ICONS.pdf) +
                "<span>" +
                escapeHtml(displayName(item.name || item.path, item.isEpub)) +
                "</span></a>"
              );
            })
            .join("")
        : '<div class="inx-recents-empty">No recent files</div>') +
      "</div></div>";

    return (
      '<a class="inx-brand" href="/" aria-label="inx home">' +
      ICONS.mark +
      "<span>inx</span></a>" +
      '<nav class="inx-side-nav" aria-label="Main navigation">' +
      links +
      "</nav>" +
      recentsHtml +
      '<div class="inx-sidebar-end">' +
      '<a class="inx-settings-link' +
      (active === "/settings" ? " active" : "") +
      '" href="/settings">' +
      ICONS.settings +
      "<span>Settings</span></a>" +
      "</div>"
    );
  }

  function cornerTrayHtml() {
    return (
      '<div class="inx-corner-tray" id="inx-corner-tray" role="group" aria-label="Updates and feedback">' +
      '<button type="button" class="inx-tray-btn" data-inx-open="updates" aria-label="What\'s new" title="What\'s new">' +
      ICONS.updates +
      '<span class="inx-tray-tip">What\'s new</span></button>' +
      '<a class="inx-tray-btn" data-inx-open="feedback" href="' +
      FEEDBACK_URL +
      '" target="_blank" rel="noopener noreferrer" aria-label="Leave feedback" title="Leave feedback">' +
      ICONS.feedback +
      '<span class="inx-tray-tip">Leave feedback</span></a>' +
      "</div>"
    );
  }

  function updatesHtml(tab) {
    if (tab === "next") {
      return NEXT.map(
        (item) =>
          '<article class="inx-update"><h3 class="inx-update-title">' +
          escapeHtml(item.title) +
          "</h3><p>" +
          escapeHtml(item.body) +
          "</p></article>"
      ).join("");
    }
    return UPDATES.map(
      (item) =>
        '<article class="inx-update"><h3 class="inx-update-title">' +
        escapeHtml(item.title) +
        '</h3><div class="inx-update-date">' +
        escapeHtml(item.date) +
        "</div><p>" +
        escapeHtml(item.body) +
        "</p></article>"
    ).join("");
  }

  function drawerHtml() {
    return (
      '<div class="inx-drawer-backdrop" id="inx-updates-backdrop" hidden data-inx-close></div>' +
      '<aside class="inx-drawer" id="inx-updates-drawer" hidden aria-label="Updates">' +
      '<div class="inx-drawer-head">' +
      '<div class="inx-drawer-tabs">' +
      '<button type="button" class="active" data-inx-tab="new">What\'s new</button>' +
      '<button type="button" data-inx-tab="next">What\'s next</button>' +
      "</div>" +
      '<button type="button" class="inx-drawer-close" data-inx-close aria-label="Close">×</button>' +
      "</div>" +
      '<div class="inx-drawer-body" id="inx-drawer-body">' +
      updatesHtml("new") +
      "</div>" +
      "</aside>"
    );
  }

  function renderDrawer(tab) {
    const body = document.getElementById("inx-drawer-body");
    if (!body) return;
    body.innerHTML = updatesHtml(tab === "next" ? "next" : "new");
  }

  function openDrawer(tab) {
    const which = tab === "next" ? "next" : "new";
    const drawer = document.getElementById("inx-updates-drawer");
    const backdrop = document.getElementById("inx-updates-backdrop") || document.querySelector(".inx-drawer-backdrop");
    if (!drawer || !backdrop) return;
    renderDrawer(which);
    document.querySelectorAll("[data-inx-tab]").forEach((btn) => {
      btn.classList.toggle("active", btn.dataset.inxTab === which);
    });
    drawer.hidden = false;
    backdrop.hidden = false;
    drawer.style.removeProperty("display");
    backdrop.style.removeProperty("display");
    requestAnimationFrame(() => {
      drawer.classList.add("is-open");
      backdrop.classList.add("is-open");
    });
    document.body.classList.add("inx-drawer-open");
  }

  function closeDrawer() {
    const drawer = document.getElementById("inx-updates-drawer");
    const backdrop = document.getElementById("inx-updates-backdrop") || document.querySelector(".inx-drawer-backdrop");
    document.body.classList.remove("inx-drawer-open");
    if (drawer) drawer.classList.remove("is-open");
    if (backdrop) backdrop.classList.remove("is-open");
    const finish = () => {
      if (drawer) {
        drawer.hidden = true;
        drawer.style.removeProperty("display");
      }
      if (backdrop) {
        backdrop.hidden = true;
        backdrop.style.removeProperty("display");
      }
    };
    // Hide after the slide finishes; also force-hide if transitions are missing.
    window.setTimeout(finish, 300);
  }

  function bindDrawerControls() {
    if (window.__inxShellDelegates) return;
    window.__inxShellDelegates = true;

    document.addEventListener("click", (event) => {
      const openBtn = event.target.closest("[data-inx-open]");
      if (openBtn) {
        if (openBtn.dataset.inxOpen === "feedback") return;
        event.preventDefault();
        event.stopPropagation();
        openDrawer("new");
        return;
      }
      const closeEl = event.target.closest("[data-inx-close]");
      if (closeEl) {
        event.preventDefault();
        event.stopPropagation();
        closeDrawer();
        return;
      }
      const tabBtn = event.target.closest("[data-inx-tab]");
      if (tabBtn) {
        const drawer = document.getElementById("inx-updates-drawer");
        if (drawer && drawer.contains(tabBtn)) {
          document.querySelectorAll("[data-inx-tab]").forEach((b) => b.classList.toggle("active", b === tabBtn));
          renderDrawer(tabBtn.dataset.inxTab);
        }
      }
    });

    document.addEventListener("keydown", (event) => {
      if (event.key === "Escape") closeDrawer();
    });
  }

  async function mount() {
    const active = currentPath();
    let mountEl = document.getElementById("inx-shell-mount");
    if (!mountEl) {
      const oldAside = document.querySelector(".inx-sidebar, .sidebar, aside");
      if (oldAside) {
        mountEl = document.createElement("aside");
        mountEl.id = "inx-shell-mount";
        mountEl.className = "inx-sidebar";
        mountEl.dataset.active = active;
        oldAside.replaceWith(mountEl);
      }
    }
    if (!mountEl) return;
    mountEl.className = "inx-sidebar";
    mountEl.dataset.active = active;

    // Prefer in-place hydration of the server-rendered sidebar (no flicker).
    // Rebuild when the mount is empty or missing nav links.
    const hydrated = hydrateExistingSidebar(mountEl, active);
    if (!hydrated) {
      mountEl.innerHTML = sidebarHtml(active, recentItems());
      bindRecentsToggle(mountEl);
    }

    // Soft-refresh recents without wiping the whole sidebar.
    recentItemsOrLibrary().then((recents) => {
      const list = mountEl.querySelector(".inx-recents-list");
      if (!list) return;
      const html = (recents || []).length
        ? recents
            .map((item) => {
              const href = item.isEpub
                ? "/epub-viewer.html?path=" + encodeURIComponent(item.path || "")
                : "/download?path=" + encodeURIComponent(item.path || "");
              return (
                '<a href="' +
                href +
                '">' +
                (item.isEpub ? ICONS.epub : ICONS.pdf) +
                "<span>" +
                escapeHtml(displayName(item.name || item.path, item.isEpub)) +
                "</span></a>"
              );
            })
            .join("")
        : '<div class="inx-recents-empty">No recent files</div>';
      if (list.innerHTML !== html) list.innerHTML = html;
    });

    // Create updates UI once; never remount on every page (that caused flicker).
    if (!document.getElementById("inx-updates-drawer")) {
      document.body.insertAdjacentHTML("beforeend", drawerHtml());
    }

    if (!document.getElementById("inx-corner-tray")) {
      document.body.insertAdjacentHTML("beforeend", cornerTrayHtml());
    }

    bindDrawerControls();
    document.querySelectorAll(".inx-feedback-tray").forEach((el) => el.remove());
    if (document.body.classList.contains("inx-drawer-open")) closeDrawer();
    else {
      const drawer = document.getElementById("inx-updates-drawer");
      const backdrop = document.getElementById("inx-updates-backdrop");
      if (drawer) {
        drawer.classList.remove("is-open");
        drawer.hidden = true;
      }
      if (backdrop) {
        backdrop.classList.remove("is-open");
        backdrop.hidden = true;
      }
    }
  }

  function hydrateExistingSidebar(mountEl, active) {
    const nav = mountEl.querySelector(".inx-side-nav");
    if (!nav || !nav.querySelector("a")) return false;
    nav.querySelectorAll("a").forEach((link) => {
      const href = link.getAttribute("href") || "";
      const isActive = href === active;
      link.classList.toggle("active", isActive);
      if (isActive) link.setAttribute("aria-current", "page");
      else link.removeAttribute("aria-current");
    });
    const settings = mountEl.querySelector(".inx-settings-link");
    if (settings) settings.classList.toggle("active", active === "/settings");
    bindRecentsToggle(mountEl);
    return true;
  }

  function bindRecentsToggle(mountEl) {
    const toggle = mountEl.querySelector(".inx-recents-toggle");
    if (!toggle || toggle.dataset.inxBound === "1") return;
    toggle.dataset.inxBound = "1";
    toggle.addEventListener("click", () => {
      const open = toggle.getAttribute("aria-expanded") === "true";
      toggle.setAttribute("aria-expanded", String(!open));
      const list = mountEl.querySelector(".inx-recents-list");
      if (list) list.hidden = open;
    });
  }

  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", mount);
  else mount();

  window.InxShell = { ICONS, openDrawer, closeDrawer, mount };
})();
