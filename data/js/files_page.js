let currentPath = "/";
let pendingRename = null;
let pendingDelete = null;
let pendingBulkDelete = false;
let toastTimer = null;

const ICONS = {
  folder:
    '<svg class="icon" viewBox="0 0 48 48" fill="currentColor" aria-hidden="true"><path d="M21.9891 7L24.9891 14H45.5V41H3.5V7H21.9891ZM21.7252 14L20.0109 10H8C7.17157 10 6.5 10.6716 6.5 11.5V24C6.5 18.4772 10.9772 14 16.5 14H21.7252Z"/></svg>',
  file:
    '<svg class="icon" viewBox="0 0 48 48" fill="currentColor" aria-label="PDF document"><path d="M10 23V6H26V18H38V23H41V15H29L29 3H7V23H10ZM29 7.24604L36.754 15H29V7.24604Z"/><path d="M32 27H41V30H35V33H40V36H35V41H32V27Z"/><path d="M7 27H11.5C12.8924 27 14.2277 27.5531 15.2123 28.5377C16.1969 29.5223 16.75 30.8576 16.75 32.25C16.75 33.6424 16.1969 34.9777 15.2123 35.9623C14.2277 36.9469 12.8924 37.5 11.5 37.5H10V41H7V27ZM10 34.5H11.5C12.0966 34.5 12.669 34.2627 13.091 33.841C13.5127 33.419 13.75 32.8466 13.75 32.25C13.75 31.6534 13.5127 31.081 13.091 30.659C12.669 30.2373 12.0966 30 11.5 30H10V34.5Z"/><path d="M23.5 27H19V41H23.5C25.2919 41 27.0083 40.2853 28.273 39.0207C29.5383 37.7553 30.25 36.0409 30.25 34.25C30.25 33.3726 30.079 32.4002 29.768 31.5008C29.4608 30.6121 28.9777 29.6818 28.273 28.977C27.0071 27.7112 25.2902 27 23.5 27ZM22 38V30H23.5C24.4946 30 25.4484 30.3951 26.1517 31.0983C26.4315 31.3782 26.7172 31.8578 26.9327 32.481C27.1445 33.0935 27.25 33.735 27.25 34.25C27.25 35.2439 26.8554 36.1956 26.1517 36.8993C25.4472 37.6038 24.4929 38 23.5 38H22Z"/></svg>',
  book:
    '<svg class="icon" viewBox="0 0 48 48" fill="currentColor" aria-label="Ebook document"><path d="M41 3H7V45H41V3ZM27.5 42H10V6H14V24.1213L19 19.1213L24 24.1213V6H38V31.5C38 37.299 33.299 42 27.5 42Z"/></svg>',
  rename:
    '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"><path d="M4 20h4L18.5 9.5a2.1 2.1 0 0 0 0-3L17.5 5.5a2.1 2.1 0 0 0-3 0L4 16v4Z"/><path d="M13.5 6.5 17.5 10.5"/></svg>',
  image:
    '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="4" width="14" height="12" rx="1"/><path d="m5.5 13 3-3 2.3 2.3 1.7-1.7 2 2.4"/><circle cx="13.5" cy="7.5" r="1"/></svg>',
  download:
    '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"><path d="M10 4v9m-3.5-3.5L10 13l3.5-3.5M4 16h12"/></svg>',
  trash:
    '<svg viewBox="0 0 48 48" fill="currentColor" aria-hidden="true"><path d="M21 18V33H18V18H21Z"/><path d="M30 33V18H27V33H30Z"/><path d="M33 3H15V9H6V12H9V42H39V12H42V9H33V3ZM30 9H18V6H30V9ZM22.5 12H36V39H12V22.5C12 16.701 16.701 12 22.5 12Z"/></svg>',
  star:
    '<svg viewBox="0 0 48 48" fill="currentColor" aria-hidden="true"><path d="M25.3451 4.33615C25.0924 3.82415 24.571 3.5 24 3.5C23.429 3.5 22.9076 3.82415 22.6549 4.33615L16.9791 15.8366L4.28764 17.6807C3.72261 17.7628 3.25319 18.1586 3.07675 18.7016C2.90032 19.2446 3.04746 19.8407 3.45632 20.2393L12.6399 29.1911L10.472 41.8313C10.3755 42.394 10.6068 42.9628 11.0687 43.2984C11.5306 43.634 12.143 43.6782 12.6484 43.4125L24 37.4447C27.7839 39.434 31.5677 41.4233 35.3516 43.4125C35.857 43.6782 36.4694 43.634 36.9313 43.2984C37.3932 42.9628 37.6245 42.394 37.528 41.8313L35.36 29.1911L44.5437 20.2393C44.9525 19.8407 45.0997 19.2446 44.9232 18.7016C44.7468 18.1586 44.2774 17.7628 43.7124 17.6807L31.0209 15.8366L25.3451 4.33615ZM25.3451 11.1148L29.0287 18.5786L37.2655 19.7755C38.4953 19.9542 38.9867 21.465 38.0979 22.3329L38.0968 22.334L32.1366 28.1437L33.5436 36.3472C33.7538 37.5725 32.4676 38.5067 31.3672 37.9285L24 34.0553L16.6328 37.9285C15.5331 38.5067 14.2479 37.5738 14.456 36.3496L14.4564 36.3472L15.8634 28.1437L9.9032 22.334L9.90104 22.3319C9.01373 21.4637 9.50519 19.9541 10.7345 19.7755L18.9713 18.5786L22.6549 11.1148L22.6557 11.1131C23.2067 9.99994 24.7952 10.0005 25.3451 11.1148Z"/></svg>',
  starOutline:
    '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linejoin="round"><path d="m12 3.2 2.5 5.1 5.6.8-4 3.9.9 5.6L12 16.9 7 18.6l.9-5.6-4-3.9 5.6-.8L12 3.2Z"/></svg>',
  more:
    '<svg viewBox="0 0 48 48" fill="currentColor" aria-hidden="true"><circle cx="10" cy="24" r="3.5"/><circle cx="24" cy="24" r="3.5"/><circle cx="38" cy="24" r="3.5"/></svg>',
  sortDesc:
    '<svg viewBox="0 0 48 48" fill="currentColor" aria-hidden="true"><path d="M7.5 10.5H36V13.5H7.5V10.5Z"/><path d="M7.5 22.5H24V25.5H7.5V22.5Z"/><path d="M21 34.5H7.5V37.5H21V34.5Z"/><path d="M27 29.3787L30.4393 32.818C31.3686 33.742 33 33.0723 33 31.7574V19.5H36V31.7574C36 33.0929 37.6137 33.7621 38.5589 32.8198L42 29.3787L44.1213 31.5L34.5 41.1213L24.8787 31.5L27 29.3787Z"/></svg>',
  sortAsc:
    '<svg viewBox="0 0 48 48" fill="currentColor" aria-hidden="true"><path d="M44.1213 16.5L42 18.6213L38.5607 15.182C37.6157 14.2371 36 14.9063 36 16.2427V28.5H33V16.2427C33 14.9063 31.3843 14.2371 30.4393 15.182L27 18.6213L24.8787 16.5L34.5 6.87866L44.1213 16.5Z"/><path d="M7.5 10.5H21V13.5H7.5V10.5Z"/><path d="M7.5 22.5H24V25.5H7.5V22.5Z"/><path d="M36 34.5H7.5V37.5H36V34.5Z"/></svg>',
  sortChevron:
    '<svg xmlns="http://www.w3.org/2000/svg" width="20" fill="currentColor" viewBox="0 0 48 48" aria-hidden="true"><path d="M16.0607 18.9395L13.9393 21.0608L24 31.1214L34.0606 21.0608L31.9393 18.9395L26.1199 24.7589C24.9482 25.929 23.0498 25.9286 21.8787 24.7575L16.0607 18.9395Z"/></svg>',
};

function getCurrentPath() {
  try {
    return decodeURIComponent(new URLSearchParams(window.location.search).get("path") || "/");
  } catch (_) {
    return "/";
  }
}

function joinPath(parent, name) {
  return (parent === "/" ? "" : parent.replace(/\/$/, "")) + "/" + name;
}

function escapeHtml(value) {
  return value ? String(value).replace(/[&<>]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;" })[c]) : "";
}

function escapeAttr(value) {
  return escapeHtml(value).replace(/"/g, "&quot;");
}

function formatFileSize(bytes) {
  if (!bytes) return "0 B";
  const unit = Math.min(3, Math.floor(Math.log(bytes) / Math.log(1024)));
  return (bytes / Math.pow(1024, unit)).toFixed(unit ? 1 : 0) + " " + ["B", "KB", "MB", "GB"][unit];
}

function formatBreadcrumb(path) {
  if (!path || path === "/") return "";
  const parts = path.replace(/\/$/, "").split("/").filter(Boolean);
  let html = '<a href="/files">My files</a>';
  let accumulated = "";
  parts.forEach((part, index) => {
    accumulated += "/" + part;
    html += '<span class="sep">›</span>';
    html +=
      index === parts.length - 1
        ? '<span class="current">' + escapeHtml(part) + "</span>"
        : '<a href="/files?path=' + encodeURIComponent(accumulated) + '">' + escapeHtml(part) + "</a>";
  });
  return html;
}

function validName(name) {
  return /^(?!\.{1,2}$)[^"*:<>?\\/|]+$/.test(name);
}

function showToast(message, error) {
  const toast = document.getElementById("toast");
  if (!toast) return;
  clearTimeout(toastTimer);
  toast.textContent = message;
  toast.className = "toast show" + (error ? " error" : "");
  toastTimer = setTimeout(() => {
    toast.className = "toast";
  }, 2600);
}

function openModal(id) {
  const modal = document.getElementById(id);
  if (modal) modal.classList.add("open");
}

function closeModal(modal) {
  const element = typeof modal === "string" ? document.getElementById(modal) : modal;
  if (element) element.classList.remove("open");
}

function setUploadStatus(text, count, percent, visible) {
  const box = document.getElementById("upload-status");
  if (box) box.style.display = visible ? "block" : "none";
  document.getElementById("upload-status-text").textContent = text || "";
  document.getElementById("upload-status-count").textContent = count || "";
  document.getElementById("upload-progress").style.width = Math.max(0, Math.min(100, percent || 0)) + "%";
}

async function uploadBlobToPath(blob, filename, destination) {
  const form = new FormData();
  form.append("file", blob, filename);
  const response = await fetch("/upload?path=" + encodeURIComponent(destination === "/" ? "" : destination), {
    method: "POST",
    body: form,
  });
  if (!response.ok) throw new Error((await response.text()) || "Upload failed");
}

async function uploadFiles(files, destination) {
  if (!files.length) return;
  setUploadStatus("Uploading files", "0/" + files.length, 0, true);
  let completed = 0;
  const failures = [];
  for (let index = 0; index < files.length; index++) {
    const file = files[index];
    setUploadStatus("Uploading " + file.name, index + 1 + "/" + files.length, (index / files.length) * 100, true);
    try {
      await uploadBlobToPath(file, file.name, destination);
      completed++;
    } catch (error) {
      failures.push(file.name + ": " + error.message);
    }
  }
  setUploadStatus("Upload complete", completed + "/" + files.length, 100, true);
  await hydrate();
  showToast(
    failures.length ? completed + " uploaded, " + failures.length + " failed" : completed + " file(s) uploaded",
    failures.length > 0
  );
  setTimeout(() => setUploadStatus("", "", 0, false), 1800);
}

function getSelectedItems() {
  return Array.from(document.querySelectorAll(".select-box:checked")).map((box) => ({
    path: box.dataset.path,
    name: box.dataset.name,
    type: box.dataset.type,
  }));
}

function updateBulkActions() {
  const selected = getSelectedItems();
  const bar = document.getElementById("bulk-actions");
  const count = document.getElementById("bulk-count");
  if (bar) bar.classList.toggle("active", selected.length > 0);
  if (count) count.textContent = selected.length + " selected";
}

async function deleteOnePath(path, type) {
  const form = new FormData();
  form.append("path", path);
  form.append("type", type);
  const response = await fetch("/delete", { method: "POST", body: form });
  if (!response.ok) throw new Error((await response.text()) || "Delete failed");
}

async function deletePathRecursive(path, type) {
  // Soft-delete: move the whole item (file or folder tree) in one shot.
  await deleteOnePath(path, type);
}

async function deleteSelectedItems() {
  const selected = getSelectedItems();
  if (!selected.length) return;
  const button = document.getElementById("bulk-delete-btn");
  button.disabled = true;
  setUploadStatus("Deleting items", "0/" + selected.length, 0, true);
  let deleted = 0;
  for (let index = 0; index < selected.length; index++) {
    const item = selected[index];
    setUploadStatus("Deleting " + item.name, index + 1 + "/" + selected.length, (index / selected.length) * 100, true);
    try {
      await deletePathRecursive(item.path, item.type);
      deleted++;
    } catch (error) {
      showToast(item.name + ": " + error.message, true);
    }
  }
  button.disabled = false;
  setUploadStatus("Delete complete", deleted + "/" + selected.length, 100, true);
  await hydrate();
  setTimeout(() => setUploadStatus("", "", 0, false), 1600);
}

function openRename(path, name, type) {
  pendingRename = { path, name, type };
  const input = document.getElementById("rename-name");
  document.getElementById("rename-title").textContent = type === "folder" ? "Rename folder" : "Rename file";
  document.getElementById("rename-error").textContent = "";
  input.value = name;
  openModal("rename-modal");
  requestAnimationFrame(() => {
    input.focus();
    const dot = type === "file" ? name.lastIndexOf(".") : -1;
    input.setSelectionRange(0, dot > 0 ? dot : name.length);
  });
}

async function submitRename() {
  if (!pendingRename) return;
  const input = document.getElementById("rename-name");
  const errorBox = document.getElementById("rename-error");
  const newName = input.value.trim();
  errorBox.textContent = "";
  if (!newName || !validName(newName)) {
    errorBox.textContent = "Enter a valid name without / \\ : * ? \" < > or |.";
    return;
  }
  if (newName === pendingRename.name) {
    closeModal("rename-modal");
    return;
  }
  const button = document.getElementById("rename-submit");
  button.disabled = true;
  const form = new FormData();
  form.append("path", pendingRename.path);
  form.append("name", newName);
  const response = await fetch("/rename", { method: "POST", body: form });
  button.disabled = false;
  if (!response.ok) {
    errorBox.textContent = (await response.text()) || "Unable to rename item.";
    return;
  }
  closeModal("rename-modal");
  showToast("Renamed to " + newName, false);
  await hydrate();
}

function openDelete(path, name, type) {
  pendingBulkDelete = false;
  pendingDelete = { path, name, type };
  document.getElementById("delete-copy").textContent =
    type === "folder"
      ? 'Move "' + name + '" and everything inside it to Trash?'
      : 'Move "' + name + '" to Trash?';
  document.getElementById("delete-error").textContent = "";
  openModal("delete-modal");
}

async function submitDelete() {
  if (pendingBulkDelete) {
    pendingBulkDelete = false;
    closeModal("delete-modal");
    await deleteSelectedItems();
    return;
  }
  if (!pendingDelete) return;
  const button = document.getElementById("delete-submit");
  const errorBox = document.getElementById("delete-error");
  button.disabled = true;
  try {
    await deletePathRecursive(pendingDelete.path, pendingDelete.type);
    closeModal("delete-modal");
    showToast("Moved to Trash", false);
    await hydrate();
  } catch (error) {
    errorBox.textContent = error.message;
  } finally {
    button.disabled = false;
  }
}

function openFolderModal() {
  document.getElementById("folder-location").textContent = currentPath;
  document.getElementById("folder-name").value = "";
  document.getElementById("folder-error").textContent = "";
  openModal("folder-modal");
  requestAnimationFrame(() => document.getElementById("folder-name").focus());
}

async function createFolder() {
  const name = document.getElementById("folder-name").value.trim();
  const errorBox = document.getElementById("folder-error");
  errorBox.textContent = "";
  if (!name || !validName(name)) {
    errorBox.textContent = "Enter a valid folder name.";
    return;
  }
  const button = document.getElementById("folder-submit");
  button.disabled = true;
  const form = new FormData();
  form.append("name", name);
  form.append("path", currentPath);
  const response = await fetch("/mkdir", { method: "POST", body: form });
  button.disabled = false;
  if (!response.ok) {
    errorBox.textContent = (await response.text()) || "Unable to create folder.";
    return;
  }
  closeModal("folder-modal");
  showToast("Folder created", false);
  await hydrate();
}

async function decodeImage(file) {
  const blob = file.slice(0, file.size, file.type || "image/*");
  if (typeof createImageBitmap === "function") {
    try {
      return await createImageBitmap(blob);
    } catch (_) {}
  }
  return await new Promise((resolve, reject) => {
    const image = new Image();
    const url = URL.createObjectURL(blob);
    image.onload = () => {
      URL.revokeObjectURL(url);
      resolve(image);
    };
    image.onerror = () => {
      URL.revokeObjectURL(url);
      reject(new Error("Unable to read image"));
    };
    image.src = url;
  });
}

async function imageToJpeg(file, maxWidth, maxHeight, cropSquare, quality) {
  const image = await decodeImage(file);
  const sourceWidth = image.width;
  const sourceHeight = image.height;
  let targetWidth;
  let targetHeight;
  let sourceX = 0;
  let sourceY = 0;
  let drawWidth = sourceWidth;
  let drawHeight = sourceHeight;
  if (cropSquare) {
    const side = Math.min(sourceWidth, sourceHeight);
    sourceX = (sourceWidth - side) / 2;
    sourceY = (sourceHeight - side) / 2;
    drawWidth = side;
    drawHeight = side;
    targetWidth = maxWidth;
    targetHeight = maxHeight;
  } else {
    const scale = Math.min(1, maxWidth / sourceWidth, maxHeight / sourceHeight);
    targetWidth = Math.max(1, Math.floor(sourceWidth * scale));
    targetHeight = Math.max(1, Math.floor(sourceHeight * scale));
  }
  const canvas = document.createElement("canvas");
  canvas.width = targetWidth;
  canvas.height = targetHeight;
  const context = canvas.getContext("2d");
  context.fillStyle = "#fff";
  context.fillRect(0, 0, targetWidth, targetHeight);
  context.imageSmoothingEnabled = true;
  context.imageSmoothingQuality = "high";
  context.drawImage(image, sourceX, sourceY, drawWidth, drawHeight, 0, 0, targetWidth, targetHeight);
  try {
    if (image.close) image.close();
  } catch (_) {}
  return await new Promise((resolve, reject) =>
    canvas.toBlob((blob) => (blob ? resolve(blob) : reject(new Error("JPEG conversion failed"))), "image/jpeg", quality ?? 0.82)
  );
}

async function uploadFolderThumbnail(path) {
  const input = document.createElement("input");
  input.type = "file";
  input.accept = "image/*,.bmp";
  input.onchange = async () => {
    if (!input.files || !input.files[0]) return;
    try {
      const jpeg = await imageToJpeg(input.files[0], 200, 200, true);
      await uploadBlobToPath(jpeg, "thumb.jpg", path);
      showToast("Folder thumbnail updated", false);
    } catch (error) {
      showToast(error.message, true);
    }
  };
  input.click();
}

function openCoverModal() {
  document.getElementById("cover-input").value = "";
  document.getElementById("cover-error").textContent = "";
  openModal("cover-modal");
}

async function uploadCovers() {
  const input = document.getElementById("cover-input");
  const files = Array.from(input.files || []);
  const errorBox = document.getElementById("cover-error");
  if (!files.length) {
    errorBox.textContent = "Select at least one image.";
    return;
  }
  const button = document.getElementById("cover-submit");
  button.disabled = true;
  closeModal("cover-modal");
  setUploadStatus("Preparing cover art", "0/" + files.length, 0, true);
  let completed = 0;
  for (let index = 0; index < files.length; index++) {
    const file = files[index];
    setUploadStatus("Converting " + file.name, index + 1 + "/" + files.length, (index / files.length) * 100, true);
    try {
      const jpeg = await imageToJpeg(file, 480, 800, false, 1);
      const outputName = file.name.replace(/\.[^.]+$/, "") + ".jpg";
      await uploadBlobToPath(jpeg, outputName, "/sleep");
      completed++;
    } catch (error) {
      showToast(file.name + ": " + error.message, true);
    }
  }
  button.disabled = false;
  setUploadStatus("Cover upload complete", completed + "/" + files.length, 100, true);
  showToast(completed + " cover(s) uploaded", completed !== files.length);
  setTimeout(() => setUploadStatus("", "", 0, false), 1800);
}

function displayName(name, isEpub, title) {
  if (title && String(title).trim()) return String(title).trim();
  if (!name) return "";
  if (isEpub) return String(name).replace(/\.epub$/i, "");
  return String(name);
}

function fileBadge(name, isEpub) {
  if (isEpub) return "";
  const lower = String(name || "").toLowerCase();
  if (lower.endsWith(".pdf")) return '<span class="badge">PDF</span>';
  const dot = name.lastIndexOf(".");
  if (dot <= 0 || dot === name.length - 1) return "";
  return '<span class="badge">' + escapeHtml(name.slice(dot + 1).toUpperCase()) + "</span>";
}

function isFavoritePath(path) {
  try {
    return JSON.parse(localStorage.getItem("inxFavorites") || "[]").some((item) => item && item.path === path);
  } catch (_) {
    return false;
  }
}

function rememberRecentFile(path, name, isEpub, pages) {
  try {
    const previous = JSON.parse(localStorage.getItem("inxRecentFiles") || "[]");
    const next = [
      { path, name: displayName(name, isEpub), isEpub, pages: pages || null },
      ...previous.filter((item) => item && item.path !== path),
    ].slice(0, 8);
    localStorage.setItem("inxRecentFiles", JSON.stringify(next));
  } catch (_) {}
}

function actionButton(action, path, name, type, icon, label, danger, extraClass) {
  return (
    '<button type="button" class="row-action' +
    (danger ? " danger" : "") +
    (extraClass ? " " + extraClass : "") +
    '" data-action="' +
    action +
    '" data-path="' +
    escapeAttr(path) +
    '" data-name="' +
    escapeAttr(name) +
    '" data-type="' +
    type +
    '" title="' +
    label +
    '" aria-label="' +
    label +
    '">' +
    icon +
    "</button>"
  );
}

function closeMoreMenu() {
  document.querySelectorAll(".more-menu").forEach((menu) => menu.remove());
}

function toggleFavorite(path, name, button) {
  const key = "inxFavorites";
  const favorites = JSON.parse(localStorage.getItem(key) || "[]");
  const index = favorites.findIndex((item) => item.path === path);
  if (index >= 0) favorites.splice(index, 1);
  else favorites.push({ path, name });
  localStorage.setItem(key, JSON.stringify(favorites));
  const active = index < 0;
  button.classList.toggle("active", active);
  button.innerHTML = active ? ICONS.star : ICONS.starOutline;
  button.setAttribute("aria-label", "Favorite");
  button.title = "Favorite";
}

function openMoreMenu(button) {
  closeMoreMenu();
  const favorited = isFavoritePath(button.dataset.path);
  const starBtn = button.closest(".row-actions")?.querySelector('[data-action="favorite"]');
  const menu = document.createElement("div");
  menu.className = "more-menu";
  menu.innerHTML =
    '<button data-menu-action="rename">' + ICONS.rename + "<span>Rename</span></button>" +
    '<button data-menu-action="favorite">' +
    (favorited ? ICONS.star : ICONS.starOutline) +
    "<span>Favorite</span></button>" +
    (button.dataset.type === "folder"
      ? '<button data-menu-action="thumbnail">' + ICONS.image + "<span>Add cover</span></button>"
      : "") +
    '<button data-menu-action="delete">' + ICONS.trash + "<span>Move to Trash</span></button>";
  document.body.appendChild(menu);
  const rect = button.getBoundingClientRect();
  menu.style.top = Math.min(window.innerHeight - menu.offsetHeight - 12, rect.bottom + 6) + "px";
  menu.style.left = Math.max(12, rect.right - menu.offsetWidth) + "px";
  menu.querySelector('[data-menu-action="rename"]').onclick = () => {
    closeMoreMenu();
    openRename(button.dataset.path, button.dataset.name, button.dataset.type);
  };
  menu.querySelector('[data-menu-action="delete"]').onclick = () => {
    closeMoreMenu();
    openDelete(button.dataset.path, button.dataset.name, button.dataset.type);
  };
  const thumbnail = menu.querySelector('[data-menu-action="thumbnail"]');
  if (thumbnail) thumbnail.onclick = () => { closeMoreMenu(); uploadFolderThumbnail(button.dataset.path); };
  menu.querySelector('[data-menu-action="favorite"]').onclick = () => {
    closeMoreMenu();
    if (starBtn) toggleFavorite(button.dataset.path, button.dataset.name, starBtn);
    else toggleFavorite(button.dataset.path, button.dataset.name, button);
  };
  setTimeout(() => document.addEventListener("click", closeMoreMenu, { once: true }), 0);
}

function currentSortMode() {
  const active = document.querySelector("#sort-menu [data-sort].active");
  return (active && active.dataset.sort) || "Last modified";
}

function currentSortDir() {
  const button = document.getElementById("sort-direction");
  return (button && button.dataset.dir) || "desc";
}

function parseUpdatedValue(value) {
  if (!value || value === "—" || value === "Local preview") return 0;
  const parsed = Date.parse(value);
  return Number.isFinite(parsed) ? parsed : 0;
}

function recentOpenScore(item) {
  try {
    const recents = JSON.parse(localStorage.getItem("inxRecentFiles") || "[]");
    const name = item.name || "";
    const idx = recents.findIndex((entry) => {
      const path = entry.path || "";
      return entry.name === name || path === name || path.endsWith("/" + name);
    });
    return idx === -1 ? Number.NEGATIVE_INFINITY : -idx;
  } catch (error) {
    return Number.NEGATIVE_INFINITY;
  }
}

function sortFileItems(items) {
  const mode = currentSortMode();
  const dir = currentSortDir();
  const direction = dir === "asc" ? 1 : -1;
  return items.slice().sort((a, b) => {
    if (a.isDirectory !== b.isDirectory) return a.isDirectory ? -1 : 1;
    let cmp = 0;
    if (mode === "Alphabetical") {
      const aName = a.title || a.name || "";
      const bName = b.title || b.name || "";
      cmp = aName.localeCompare(bName, undefined, { sensitivity: "base" });
    } else if (mode === "Last opened") {
      cmp = recentOpenScore(a) - recentOpenScore(b);
      if (cmp === 0) {
        const aName = a.title || a.name || "";
        const bName = b.title || b.name || "";
        cmp = aName.localeCompare(bName, undefined, { sensitivity: "base" });
      }
    } else {
      cmp = parseUpdatedValue(a.updated) - parseUpdatedValue(b.updated);
      if (cmp === 0) {
        const aName = a.title || a.name || "";
        const bName = b.title || b.name || "";
        cmp = aName.localeCompare(bName, undefined, { sensitivity: "base" });
      }
    }
    return cmp * direction;
  });
}

async function hydrate() {
  currentPath = getCurrentPath();
  document.getElementById("directory-breadcrumbs").innerHTML = formatBreadcrumb(currentPath);
  const list = document.getElementById("file-list");
  try {
    const response = await fetch("/api/files?path=" + encodeURIComponent(currentPath));
    if (!response.ok) throw new Error("Unable to load folder");
    const items = sortFileItems(await response.json());
    const folders = items.filter((item) => item.isDirectory).length;
    const bytes = items.reduce((sum, item) => sum + (item.isDirectory ? 0 : item.size || 0), 0);
    document.getElementById("folder-summary").textContent =
      folders + " folder(s), " + (items.length - folders) + " file(s) · " + formatFileSize(bytes);
    if (!items.length) {
      list.innerHTML =
        '<div class="empty-state">' +
        ICONS.folder +
        "<div>This folder is empty</div></div>";
      updateBulkActions();
      return;
    }

    let html = '<div class="file-list">';
    for (let index = 0; index < items.length; index++) {
      const item = items[index];
      const path = joinPath(currentPath, item.name);
      const pathAttr = escapeAttr(path);
      const nameAttr = escapeAttr(item.name);
      const type = item.isDirectory ? "folder" : "file";
      const isEpub = !item.isDirectory && (item.isEpub || item.name.toLowerCase().endsWith(".epub"));
      const shownName = displayName(item.name, isEpub, item.title);
      const contents = item.contents || (item.isDirectory ? "Folder" : "—");
      const updated = item.updated || "—";
      const favorited = isFavoritePath(path);
      html +=
        '<div class="file-row' +
        (item.isDirectory ? " is-folder" : "") +
        '">' +
        '<input class="select-box" type="checkbox" data-path="' +
        pathAttr +
        '" data-name="' +
        nameAttr +
        '" data-type="' +
        type +
        '" aria-label="Select ' +
        nameAttr +
        '">';
      if (item.isDirectory) {
        html +=
          '<a class="row-main" href="/files?path=' +
          encodeURIComponent(path) +
          '">' +
          ICONS.folder +
          '<span class="name">' +
          escapeHtml(item.name) +
          "</span>" +
          '<span class="meta">Folder</span></a>';
      } else {
        const destination = isEpub
          ? "/epub-viewer.html?path=" + encodeURIComponent(path)
          : "/download?path=" + encodeURIComponent(path);
        html +=
          '<a class="row-main file-link" data-path="' +
          pathAttr +
          '" data-name="' +
          escapeAttr(shownName) +
          '" data-is-epub="' +
          (isEpub ? "1" : "0") +
          '" href="' +
          destination +
          '">' +
          (isEpub ? ICONS.book : ICONS.file) +
          '<span class="name">' +
          escapeHtml(shownName) +
          fileBadge(item.name, isEpub) +
          "</span>" +
          '<span class="meta">' +
          formatFileSize(item.size) +
          "</span></a>";
      }
      html += '<span class="row-contents">' + escapeHtml(contents) + '</span><span class="row-updated">' + escapeHtml(updated) + '</span><div class="row-actions">';
      html += actionButton(
        "favorite",
        path,
        item.name,
        type,
        favorited ? ICONS.star : ICONS.starOutline,
        "Favorite",
        false,
        "favorite-action" + (favorited ? " active" : "")
      );
      html += actionButton("more", path, item.name, type, ICONS.more, "More actions", false, "more-action");
      html += "</div></div>";
    }
    html += "</div>";
    list.innerHTML = html;
    list.querySelectorAll(".select-box").forEach((box) => box.addEventListener("change", updateBulkActions));
    list.querySelectorAll(".file-link").forEach((link) =>
      link.addEventListener("click", () => {
        const row = link.closest(".file-row");
        const contents = row && row.querySelector(".row-contents");
        const pagesMatch = contents && /(\d+)\s*pages?/i.exec(contents.textContent || "");
        rememberRecentFile(
          link.dataset.path,
          link.dataset.name,
          link.dataset.isEpub === "1",
          pagesMatch ? pagesMatch[1] : null
        );
      })
    );
    list.querySelectorAll(".row-action").forEach((button) =>
      button.addEventListener("click", () => {
        const action = button.dataset.action;
        const path = button.dataset.path;
        const name = button.dataset.name;
        const type = button.dataset.type;
        if (action === "rename") openRename(path, name, type);
        if (action === "delete") openDelete(path, name, type);
        if (action === "thumbnail") uploadFolderThumbnail(path);
        if (action === "download") window.location.href = "/download?path=" + encodeURIComponent(path);
        if (action === "favorite") toggleFavorite(path, name, button);
        if (action === "more") openMoreMenu(button);
      })
    );
    updateBulkActions();
  } catch (error) {
    list.innerHTML = '<div class="empty-state">Unable to load folder</div>';
    showToast(error.message, true);
  }
}

function initDropzone() {
  const input = document.getElementById("file-input");
  if (!input) return;
  input.addEventListener("change", () => {
    const files = Array.from(input.files || []);
    input.value = "";
    uploadFiles(files, currentPath);
  });

  let overlay = document.getElementById("files-drop-overlay");
  if (!overlay) {
    overlay = document.createElement("div");
    overlay.id = "files-drop-overlay";
    overlay.className = "files-drop-overlay";
    overlay.setAttribute("aria-hidden", "true");
    overlay.innerHTML =
      '<div class="files-drop-overlay-inner">' +
      '<div class="files-drop-cloud" aria-hidden="true">' +
      '<svg viewBox="0 0 120 90" fill="none" xmlns="http://www.w3.org/2000/svg">' +
      '<path d="M58 66H22C11.5 66 4.5 57 4.5 45.5C4.5 35 12 27 22.5 25.5C25 12.5 36.5 4.5 51 4.5C62.5 4.5 72 11.5 75.5 22C87.5 23.5 96.5 33 96.5 42" stroke="currentColor" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"/>' +
      '<path d="M72.5 73H76.5V57.5H87L74.5 43.5L62 57.5H72.5V73Z" fill="currentColor"/>' +
      "</svg></div>" +
      "<h2>Drop file to upload</h2>" +
      "<p>Import PDF, JPG, PNG, EPUB, and font files up to 100 MB.</p>" +
      "</div>";
    document.body.appendChild(overlay);
  }

  const hasFiles = (event) => {
    const types = event.dataTransfer && event.dataTransfer.types;
    if (!types) return false;
    return Array.from(types).includes("Files");
  };

  let dragDepth = 0;
  const showOverlay = () => {
    overlay.classList.add("is-visible");
    overlay.setAttribute("aria-hidden", "false");
  };
  const hideOverlay = () => {
    dragDepth = 0;
    overlay.classList.remove("is-visible");
    overlay.setAttribute("aria-hidden", "true");
  };

  window.addEventListener("dragenter", (event) => {
    if (!hasFiles(event)) return;
    event.preventDefault();
    dragDepth += 1;
    showOverlay();
  });
  window.addEventListener("dragover", (event) => {
    if (!hasFiles(event)) return;
    event.preventDefault();
    event.dataTransfer.dropEffect = "copy";
    showOverlay();
  });
  window.addEventListener("dragleave", (event) => {
    if (!hasFiles(event)) return;
    dragDepth = Math.max(0, dragDepth - 1);
    if (dragDepth === 0) hideOverlay();
  });
  window.addEventListener("drop", (event) => {
    if (!hasFiles(event)) return;
    event.preventDefault();
    hideOverlay();
    const files = Array.from((event.dataTransfer && event.dataTransfer.files) || []);
    if (files.length) uploadFiles(files, currentPath);
  });
}

function initModals() {
  document.querySelectorAll(".modal-overlay").forEach((overlay) => {
    overlay.addEventListener("click", (event) => {
      if (event.target === overlay) closeModal(overlay);
    });
    overlay.querySelectorAll(".modal-close,.modal-cancel").forEach((button) =>
      button.addEventListener("click", () => closeModal(overlay))
    );
  });
  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape") document.querySelectorAll(".modal-overlay.open").forEach(closeModal);
    if (event.key === "Enter" && document.getElementById("rename-modal").classList.contains("open")) submitRename();
    if (event.key === "Enter" && document.getElementById("folder-modal").classList.contains("open")) createFolder();
  });
}

function init() {
  initDropzone();
  initModals();
  document.getElementById("new-folder-btn").addEventListener("click", openFolderModal);
  document.getElementById("sort-direction").addEventListener("click", (event) => {
    const button = event.currentTarget;
    const next = button.dataset.dir === "desc" ? "asc" : "desc";
    button.dataset.dir = next;
    button.innerHTML = next === "desc" ? ICONS.sortDesc : ICONS.sortAsc;
    hydrate();
  });
  const sortMode = document.getElementById("sort-mode");
  const sortMenu = document.getElementById("sort-menu");
  sortMode.addEventListener("click", () => {
    const open = !sortMenu.hasAttribute("hidden");
    sortMenu.hidden = open;
    sortMode.setAttribute("aria-expanded", String(!open));
  });
  sortMenu.querySelectorAll("[data-sort]").forEach((option) => option.addEventListener("click", () => {
    sortMode.innerHTML = "<span>" + option.dataset.sort + "</span>" + ICONS.sortChevron;
    sortMenu.querySelectorAll("[data-sort]").forEach((item) => item.classList.toggle("active", item === option));
    sortMenu.hidden = true;
    sortMode.setAttribute("aria-expanded", "false");
    hydrate();
  }));
  const coverButton = document.getElementById("cover-upload-btn");
  if (coverButton) coverButton.addEventListener("click", openCoverModal);
  document.getElementById("folder-submit").addEventListener("click", createFolder);
  document.getElementById("rename-submit").addEventListener("click", submitRename);
  document.getElementById("delete-submit").addEventListener("click", submitDelete);
  document.getElementById("cover-submit").addEventListener("click", uploadCovers);
  document.getElementById("bulk-delete-btn").addEventListener("click", () => {
    const selected = getSelectedItems();
    if (!selected.length) return;
    pendingBulkDelete = true;
    pendingDelete = null;
    document.getElementById("delete-copy").textContent =
      "Move " + selected.length + " selected item(s) to Trash?";
    document.getElementById("delete-error").textContent = "";
    openModal("delete-modal");
  });
  hydrate();
}

document.readyState === "loading" ? document.addEventListener("DOMContentLoaded", init) : init();
