let currentPath = "/";
let pendingRename = null;
let pendingDelete = null;
let pendingBulkDelete = false;
let toastTimer = null;

const ICONS = {
  folder:
    '<svg class="icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"><path d="M4 6.5A2.5 2.5 0 0 1 6.5 4H10l2 2h5.5A2.5 2.5 0 0 1 20 8.5v8A2.5 2.5 0 0 1 17.5 19h-11A2.5 2.5 0 0 1 4 16.5v-10Z"/></svg>',
  file:
    '<svg class="icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"><path d="M7 3.5h7l4 4v13H7a2 2 0 0 1-2-2v-13a2 2 0 0 1 2-2Z"/><path d="M14 3.5v4h4"/></svg>',
  book:
    '<svg class="icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"><path d="M6 4.5h9.5A2.5 2.5 0 0 1 18 7v12.5H7.5A2.5 2.5 0 0 1 5 17V5.5A1 1 0 0 1 6 4.5Z"/><path d="M7.5 19.5A2.5 2.5 0 0 1 7.5 14H18"/></svg>',
  chevron:
    '<svg class="chevron" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="m7 4 6 6-6 6"/></svg>',
  rename:
    '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="m13.5 3.5 3 3L7 16H4v-3l9.5-9.5Z"/></svg>',
  image:
    '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="4" width="14" height="12" rx="1"/><path d="m5.5 13 3-3 2.3 2.3 1.7-1.7 2 2.4"/><circle cx="13.5" cy="7.5" r="1"/></svg>',
  download:
    '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"><path d="M10 4v9m-3.5-3.5L10 13l3.5-3.5M4 16h12"/></svg>',
  trash:
    '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M4 6h12M8 3.5h4L13 6H7l1-2.5ZM6 6l.7 10h6.6L14 6M8.5 8.5v5M11.5 8.5v5"/></svg>',
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
  if (!path || path === "/") return '<span class="current">/</span>';
  const parts = path.replace(/\/$/, "").split("/").filter(Boolean);
  let html = '<a href="/files">/</a>';
  let accumulated = "";
  parts.forEach((part, index) => {
    accumulated += "/" + part;
    html += '<span class="sep">/</span>';
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
  if (type === "folder") {
    const response = await fetch("/api/files?path=" + encodeURIComponent(path));
    if (!response.ok) throw new Error("Unable to read folder");
    const children = await response.json();
    for (const child of children) {
      await deletePathRecursive(joinPath(path, child.name), child.isDirectory ? "folder" : "file");
    }
  }
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
      ? 'Delete "' + name + '" and everything inside it? This cannot be undone.'
      : 'Delete "' + name + '"? This cannot be undone.';
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
    showToast("Deleted " + pendingDelete.name, false);
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

function fileBadge(name, isEpub) {
  if (isEpub) return '<span class="badge">EPUB</span>';
  const dot = name.lastIndexOf(".");
  if (dot <= 0 || dot === name.length - 1) return "";
  return '<span class="badge">' + escapeHtml(name.slice(dot + 1).toUpperCase()) + "</span>";
}

function actionButton(action, path, name, type, icon, label, danger) {
  return (
    '<button type="button" class="row-action' +
    (danger ? " danger" : "") +
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

async function hydrate() {
  currentPath = getCurrentPath();
  document.getElementById("directory-breadcrumbs").innerHTML = formatBreadcrumb(currentPath);
  const list = document.getElementById("file-list");
  try {
    const response = await fetch("/api/files?path=" + encodeURIComponent(currentPath));
    if (!response.ok) throw new Error("Unable to load folder");
    const items = await response.json();
    items.sort((a, b) =>
      a.isDirectory === b.isDirectory ? (a.name || "").localeCompare(b.name || "") : a.isDirectory ? -1 : 1
    );
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
    for (const item of items) {
      const path = joinPath(currentPath, item.name);
      const pathAttr = escapeAttr(path);
      const nameAttr = escapeAttr(item.name);
      const type = item.isDirectory ? "folder" : "file";
      const isEpub = !item.isDirectory && (item.isEpub || item.name.toLowerCase().endsWith(".epub"));
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
          '<span class="meta">Folder</span>' +
          ICONS.chevron +
          "</a>";
      } else {
        const destination = isEpub
          ? "/epub-viewer.html?path=" + encodeURIComponent(path)
          : "/download?path=" + encodeURIComponent(path);
        html +=
          '<a class="row-main" href="' +
          destination +
          '">' +
          (isEpub ? ICONS.book : ICONS.file) +
          '<span class="name">' +
          escapeHtml(item.name) +
          fileBadge(item.name, isEpub) +
          "</span>" +
          '<span class="meta">' +
          formatFileSize(item.size) +
          "</span></a>";
      }
      html += '<div class="row-actions">';
      if (item.isDirectory) {
        html += actionButton("thumbnail", path, item.name, type, ICONS.image, "Change thumbnail", false);
      } else {
        html += actionButton("download", path, item.name, type, ICONS.download, "Download", false);
      }
      html += actionButton("rename", path, item.name, type, ICONS.rename, "Rename", false);
      html += actionButton("delete", path, item.name, type, ICONS.trash, "Delete", true);
      html += "</div></div>";
    }
    html += "</div>";
    list.innerHTML = html;
    list.querySelectorAll(".select-box").forEach((box) => box.addEventListener("change", updateBulkActions));
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
      })
    );
    updateBulkActions();
  } catch (error) {
    list.innerHTML = '<div class="empty-state">Unable to load folder</div>';
    showToast(error.message, true);
  }
}

function initDropzone() {
  const dropzone = document.getElementById("dropzone");
  const input = document.getElementById("file-input");
  input.addEventListener("change", () => {
    const files = Array.from(input.files || []);
    input.value = "";
    uploadFiles(files, currentPath);
  });
  ["dragenter", "dragover"].forEach((eventName) =>
    dropzone.addEventListener(eventName, (event) => {
      event.preventDefault();
      dropzone.classList.add("dragover");
    })
  );
  ["dragleave", "drop"].forEach((eventName) =>
    dropzone.addEventListener(eventName, (event) => {
      event.preventDefault();
      dropzone.classList.remove("dragover");
    })
  );
  dropzone.addEventListener("drop", (event) => {
    const files = Array.from((event.dataTransfer && event.dataTransfer.files) || []);
    uploadFiles(files, currentPath);
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
  document.getElementById("cover-upload-btn").addEventListener("click", openCoverModal);
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
      "Delete " + selected.length + " selected item(s), including folder contents? This cannot be undone.";
    document.getElementById("delete-error").textContent = "";
    openModal("delete-modal");
  });
  hydrate();
}

document.readyState === "loading" ? document.addEventListener("DOMContentLoaded", init) : init();
