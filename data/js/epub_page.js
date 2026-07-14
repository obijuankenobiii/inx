const SYSTEM_FOLDERS = ["fonts", "sleep"];
let currentPath="/",generatePackagedThumbnail=!1,jszipLoadPromise=null;function loadJsZip(){return"undefined"!=typeof JSZip?Promise.resolve():(jszipLoadPromise||(jszipLoadPromise=new Promise(function(e,t){var o=document.createElement("script");o.src="/js/jszip.min.js";o.async=!0;o.onload=function(){"undefined"!=typeof JSZip?e():(jszipLoadPromise=null,t(new Error("JSZip init failed")))};o.onerror=function(){jszipLoadPromise=null,t(new Error("JSZip load failed"))};document.head.appendChild(o)})),jszipLoadPromise)}const epubThumbCheckbox=document.getElementById("epubGeneratePackagedThumbnailCheckbox");function isPackagedDeviceThumbnailPath(e){return typeof e=="string"&&e.replace(/\\/g,"/").toLowerCase()=="meta-inf/thumbnail.jpg"}function updateToggleUI(){epubThumbCheckbox&&(epubThumbCheckbox.checked=generatePackagedThumbnail);const e=document.getElementById("optimizerSummaryBanner");if(e){const t="Preserve formats: JPEG/JPG resized and re-encoded in place to max 480×800 at JPEG quality 100%; PNG and others unchanged.",o=generatePackagedThumbnail?" Embeds META-INF/thumbnail.jpg from a cover-like image for fast imports.":" Omits packaged thumbnail; reader builds thumb.bmp from cover on device.";e.textContent=t+o}}function toggleEpubGeneratePackagedThumbnail(){generatePackagedThumbnail=epubThumbCheckbox?epubThumbCheckbox.checked:!generatePackagedThumbnail,localStorage.setItem("epubGeneratePackagedThumbnail",generatePackagedThumbnail),updateToggleUI(),addModalLog("modalLog",generatePackagedThumbnail?"Device thumbnail: will embed META-INF/thumbnail.jpg on import.":"Device thumbnail: will not embed (and strips it if present when re-importing).","success")}function addModalLog(e,t,o="info"){const a=document.getElementById(e);if(a){e=(new Date).toLocaleTimeString();const n=document.createElement("div");n.className=o,n.innerHTML=`[${e}] ${t}`,a.appendChild(n),n.scrollIntoView({behavior:"smooth",block:"nearest"})}}function clearModalLog(e){const t=document.getElementById(e);t&&(t.innerHTML='<div class="info">Ready</div>')}function isCoverImage(e){var t=e.toLowerCase();for(const o of[/cover/i,/titlepage/i,/front[-_]?cover/i,/thumbnail/i,/\/cover\//i,/\/images\/cover/i,/\/img\/cover/i,/\/metadata\/cover/i,/^cover\./i,/^title\./i])if(o.test(t))return!0;return!1}async function resizeJpegInPlace(blob,path,opts){
opts=opts||{};const maxW=void 0!==opts.maxW?opts.maxW:480,maxH=void 0!==opts.maxH?opts.maxH:800,quality=void 0!==opts.quality?opts.quality:1;const ab=await blob.arrayBuffer(),typed=new Blob([ab],{type:"image/jpeg"});let sw,sh,drawSrc;try{if(typeof createImageBitmap=="function"){drawSrc=await createImageBitmap(typed);sw=drawSrc.width;sh=drawSrc.height}else throw 0}catch(_){await new Promise((ok,err)=>{const I=new Image,u=URL.createObjectURL(typed);I.onload=()=>{URL.revokeObjectURL(u);drawSrc=I;sw=I.width;sh=I.height;ok()};I.onerror=()=>{URL.revokeObjectURL(u);err(new Error("Failed to load image: "+path))};I.src=u})}
let tw=sw,th=sh;const needsResize=maxW<sw||maxH<sh;if(needsResize){const scale=Math.min(maxW/sw,maxH/sh);tw=Math.max(1,Math.floor(sw*scale));th=Math.max(1,Math.floor(sh*scale))}
const U=document.createElement("canvas");U.width=tw;U.height=th;const x=U.getContext("2d");x.imageSmoothingEnabled=!0;x.imageSmoothingQuality="high";x.drawImage(drawSrc,0,0,tw,th);
const outBlob=await new Promise((ok,err)=>U.toBlob(b=>b?ok(b):err(new Error("JPEG encode failed for "+path)),"image/jpeg",quality));
try{drawSrc.close&&drawSrc.close()}catch(_){ }
return{blob:outBlob,oldPath:path,newPath:path,originalSize:blob.size,newSize:outBlob.size,width:tw,height:th,resized:needsResize};}

async function rasterBlobToDeviceThumbnailJpeg(blob,maxW,maxH,quality){maxW=void 0!==maxW?maxW:225,maxH=void 0!==maxH?maxH:340,quality=void 0!==quality?quality:.88;const ab=await blob.arrayBuffer(),ext=(blob.type&&blob.type.startsWith("image/")?blob.type:null)||"image/jpeg",typed=new Blob([ab],{type:ext});let sw,sh,drawSrc;try{if(typeof createImageBitmap=="function"){drawSrc=await createImageBitmap(typed),sw=drawSrc.width,sh=drawSrc.height}else throw 0}catch(_){await new Promise((ok,err)=>{const I=new Image,u=URL.createObjectURL(typed);I.onload=()=>{URL.revokeObjectURL(u),drawSrc=I,sw=I.width,sh=I.height,ok()},I.onerror=()=>{URL.revokeObjectURL(u),err(new Error("thumb decode"))},I.src=u})}let tw=sw,th=sh;const nr=maxW<sw||maxH<sh;if(nr){const sc=Math.min(maxW/sw,maxH/sh);tw=Math.max(1,Math.floor(sw*sc)),th=Math.max(1,Math.floor(sh*sc))}const U=document.createElement("canvas");U.width=tw,U.height=th;const x=U.getContext("2d");x.imageSmoothingEnabled=!0,x.imageSmoothingQuality="high",x.drawImage(drawSrc,0,0,tw,th);const outBlob=await new Promise((ok,err)=>U.toBlob(e=>e?ok(e):err(new Error("JPEG encode failed")),"image/jpeg",quality));try{drawSrc.close&&drawSrc.close()}catch(_){}return outBlob}async function applyPackagedThumbnailToZip(n){const k="META-INF/thumbnail.jpg";if(!generatePackagedThumbnail)return void(n.files[k]&&(n.remove(k),addModalLog("modalLog","Stripped META-INF/thumbnail.jpg (toggle off).","info")));const raster=/\.(jpe?g|png|gif|bmp|webp|tiff?|avif|jxl)$/i;let found=null;for(const[p,f]of Object.entries(n.files))if(!f.dir&&raster.test(p)&&isCoverImage(p)&&!isPackagedDeviceThumbnailPath(p))try{found={path:p,blob:await f.async("blob")};break}catch(e){}if(!found)return n.files[k]&&n.remove(k),void addModalLog("modalLog","No cover-like raster for device thumbnail; omitted "+k+".","error");try{const j=await rasterBlobToDeviceThumbnailJpeg(found.blob,225,340,.88);n.file(k,j),addModalLog("modalLog","Embedded "+k+" from "+found.path.split("/").pop()+" ("+(j.size/1024).toFixed(1)+" KiB).","success")}catch(e){n.files[k]&&n.remove(k),addModalLog("modalLog","Device thumbnail failed: "+e.message,"error")}}

async function optimizeEPUB(file) {
  try {
    await loadJsZip();
    addModalLog("modalLog", "Starting image pass for " + file.name, "info");
    const zip = await JSZip.loadAsync(file);
    addModalLog("modalLog", "Opened EPUB archive.", "info");
    await applyPackagedThumbnailToZip(zip);

    const keepRegex = /\.jpe?g$/i;
    const candidates = [];
    for (const [path, entry] of Object.entries(zip.files)) {
      if (!entry.dir && keepRegex.test(path) && !isPackagedDeviceThumbnailPath(path)) {
        candidates.push({ path, entry });
      }
    }
    addModalLog("modalLog", "Preserve mode: JPEG candidates " + candidates.length + ".", "info");

    let done = 0;
    for (let idx = 0; idx < candidates.length; idx++) {
      const { path, entry } = candidates[idx];
      const pct = Math.round((100 * (idx + 1)) / Math.max(1, candidates.length));
      addModalLog("modalLog", "[JPEG " + (idx + 1) + "/" + candidates.length + "] [" + pct + "%] " + path, "info");
      try {
        const optimized = await resizeJpegInPlace(await entry.async("blob"), path, { maxW: 480, maxH: 800, quality: 1 });
        zip.remove(path);
        zip.file(path, optimized.blob);
        done++;
        addModalLog(
          "modalLog",
          "Optimized JPEG " + path.split("/").pop() + " (" + optimized.width + "x" + optimized.height + ", " +
            (optimized.originalSize / 1024).toFixed(1) + " -> " + (optimized.newSize / 1024).toFixed(1) + " KiB)",
          "success"
        );
      } catch (e) {
        addModalLog("modalLog", "JPEG optimization failed on " + path + ": " + e.message, "error");
      }
    }

    const out = await zip.generateAsync({ type: "blob", compression: "DEFLATE", compressionOptions: { level: 6 } });
    addModalLog("modalLog", "Preserve mode complete: " + done + " JPEG(s) optimized.", "success");
    addModalLog("modalLog", "Repacked archive (" + (out.size / 1024).toFixed(1) + " KiB).", "success");
    return out;
  } catch (e) {
    addModalLog("modalLog", "Optimization failed: " + e.message, "error");
    return file;
  }
}
async function uploadBlobToPath(blob, filename, destPath) {
  const formData = new FormData();
  formData.append("file", blob, filename);
  const res = await fetch("/upload?path=" + encodeURIComponent(destPath === "/" ? "" : destPath), {
    method: "POST",
    body: formData,
  });
  if (!res.ok) throw new Error(await res.text());
  return true;
}

function getCurrentPath() {
  try {
    return decodeURIComponent(new URLSearchParams(window.location.search).get("path") || "/");
  } catch (e) {
    return "/";
  }
}

function escapeHtml(s) {
  return s ? s.replace(/[&<>]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;" })[c]) : "";
}

function escapeAttr(s) {
  return escapeHtml(s).replace(/"/g, "&quot;");
}

function formatFileSize(bytes) {
  if (!bytes) return "0 B";
  const i = Math.floor(Math.log(bytes) / Math.log(1024));
  return (bytes / Math.pow(1024, i)).toFixed(1) + " " + ["B", "KB", "MB", "GB"][i];
}

function formatBreadcrumb(path) {
  if (!path || path === "/") return '<span class="current">/</span>';
  const parts = path.replace(/\/$/, "").split("/").filter(Boolean);
  let html = '<span class="sep">/</span>';
  let acc = "";
  for (let i = 0; i < parts.length; i++) {
    acc += "/" + parts[i];
    html +=
      i === parts.length - 1
        ? `<span class="current">${escapeHtml(parts[i])}</span>`
        : `<a href="/epub?path=${encodeURIComponent(acc)}">${escapeHtml(parts[i])}</a><span class="sep">/</span>`;
  }
  return html;
}

function hideEpubFolderImportResults() {
  const el = document.getElementById("epubFolderImportResults");
  if (!el) return;
  el.style.display = "none";
  el.className = "";
  el.innerHTML = "";
}

function showEpubFolderImportResults(okCount, failNames, files) {
  const box = document.getElementById("epubFolderImportResults");
  if (!box) return;
  let html = "";
  const hasFailures = failNames.length > 0;
  for (const f of files) {
    const bad = failNames.indexOf(f.name) >= 0;
    html +=
      "<li><span>" + escapeHtml(f.name) + '</span><span style="flex-shrink:0;font-weight:600;color:' +
      (bad ? "#ff3b30" : "#34c759") + '">' + (bad ? "Failed" : "OK") + "</span></li>";
  }
  box.className = "import-summary " + (hasFailures ? "warn" : "ok");
  box.innerHTML =
    '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">' +
    (hasFailures
      ? '<path d="M10 3 2.5 17h15L10 3Z"/><path d="M10 8v4M10 15h.01"/>'
      : '<path d="M16.5 5.5 8 14l-4.5-4.5"/>') +
    '</svg><div class="import-summary-body"><div class="import-summary-title">Last import: ' +
    okCount + " succeeded, " + failNames.length + " failed</div>" +
    (html ? "<ul>" + html + "</ul>" : "") +
    "</div>";
  box.style.display = "block";
}

function setUploadStatus(text, count, percent, visible) {
  const box = document.getElementById("upload-status");
  const label = document.getElementById("upload-status-text");
  const counter = document.getElementById("upload-status-count");
  const fill = document.getElementById("progress-fill");
  if (box && visible !== undefined) box.style.display = visible ? "block" : "none";
  if (label && text !== undefined) label.textContent = text;
  if (counter && count !== undefined) counter.textContent = count;
  if (fill && percent !== undefined) fill.style.width = Math.max(0, Math.min(100, percent)) + "%";
}

function filterEpubFiles(fileList) {
  return Array.from(fileList).filter((f) => f.name.toLowerCase().endsWith(".epub"));
}

// Optimizes and uploads a batch of EPUB files straight to destPath, with progress shown inline on the
// page (no modal, no separate confirm step) - used by both the page-level dropzone and folder-row drop
// targets, and by picking files via the dropzone's click-to-browse fallback.
async function uploadEpubFiles(files, destPath) {
  if (!files.length) return;
  clearModalLog("modalLog");
  hideEpubFolderImportResults();
  setUploadStatus("Preparing import…", "0/" + files.length, 0, true);
  addModalLog("modalLog", "Importing " + files.length + " file(s) into " + destPath + ".", "info");
  addModalLog(
    "modalLog",
    "Preserve mode: JPEG in-place optimization at quality 100%" +
      (generatePackagedThumbnail ? "; packaged device thumbnail on." : "; packaged device thumbnail off.") + ".",
    "info"
  );

  const prepared = [];
  for (let idx = 0; idx < files.length; idx++) {
    const file = files[idx];
    const prepPct = Math.round((idx / files.length) * 50);
    setUploadStatus("Preparing " + file.name, idx + 1 + "/" + files.length, prepPct, true);
    addModalLog("modalLog", "--- " + file.name + " ---", "info");
    const blob = await optimizeEPUB(file);
    prepared.push({ blob, name: file.name });
  }

  let succeeded = 0;
  const failed = [];
  for (let idx = 0; idx < prepared.length; idx++) {
    const { blob, name } = prepared[idx];
    const uploadPct = 50 + Math.round((idx / prepared.length) * 50);
    setUploadStatus("Uploading " + name, idx + 1 + "/" + prepared.length, uploadPct, true);
    try {
      await uploadBlobToPath(blob, name, destPath);
      addModalLog("modalLog", "Uploaded successfully: " + name, "success");
      succeeded++;
    } catch (e) {
      addModalLog("modalLog", "Upload failed: " + name + " (" + e.message + ")", "error");
      failed.push(name);
    }
  }

  setUploadStatus("Import complete", succeeded + "/" + files.length, 100, true);
  addModalLog("modalLog", "Import finished. Succeeded: " + succeeded + ". Failed: " + failed.length + ".", "success");
  showEpubFolderImportResults(succeeded, failed, files);
  await hydrate();
}

function handleFileInputChange() {
  const input = document.getElementById("fileInput");
  if (!input.files || !input.files.length) return;
  const files = filterEpubFiles(input.files);
  input.value = "";
  if (!files.length) {
    addModalLog("modalLog", "No .epub files selected.", "error");
    return;
  }
  uploadEpubFiles(files, currentPath).catch((e) => addModalLog("modalLog", "Import failed: " + e.message, "error"));
}

async function promptNewFolder() {
  const name = (prompt("New folder name") || "").trim();
  if (!name) return;
  if (!/^(?!\.{1,2}$)[^"*:<>?\\/|]+$/.test(name)) {
    alert("Invalid folder name");
    return;
  }
  const formData = new FormData();
  formData.append("name", name);
  formData.append("path", currentPath);
  const res = await fetch("/mkdir", { method: "POST", body: formData });
  if (res.ok) {
    await hydrate();
  } else {
    alert("Unable to create folder");
  }
}

function getSelectedItems() {
  return Array.from(document.querySelectorAll(".select-box:checked")).map((box) => ({
    path: box.dataset.path || "",
    name: box.dataset.name || "",
    type: box.dataset.type || "file",
  })).filter((item) => item.path);
}

function updateBulkActions() {
  const selected = getSelectedItems();
  const bar = document.getElementById("bulk-actions");
  const count = document.getElementById("bulk-count");
  if (bar) bar.classList.toggle("active", selected.length > 0);
  if (count) count.textContent = selected.length + " selected";
}

async function deleteOnePath(path, type) {
  const formData = new FormData();
  formData.append("path", path);
  formData.append("type", type);
  const res = await fetch("/delete", { method: "POST", body: formData });
  if (!res.ok) throw new Error(await res.text());
}

async function deletePathRecursive(path, type) {
  if (type === "folder") {
    const res = await fetch("/api/files?path=" + encodeURIComponent(path));
    if (!res.ok) throw new Error(await res.text());
    const children = await res.json();
    for (const child of children) {
      const childPath = path.replace(/\/$/, "") + "/" + child.name;
      await deletePathRecursive(childPath, child.isDirectory ? "folder" : "file");
    }
  }
  await deleteOnePath(path, type);
}

async function deleteSelectedItems() {
  const selected = getSelectedItems();
  if (!selected.length) return;
  const folders = selected.filter((item) => item.type === "folder").length;
  const books = selected.length - folders;
  const label = [
    books ? books + " book" + (books === 1 ? "" : "s") : "",
    folders ? folders + " folder" + (folders === 1 ? "" : "s") : "",
  ].filter(Boolean).join(" and ");
  if (!confirm("Delete " + label + "? Folders and their contents will be deleted.")) return;

  const button = document.getElementById("bulk-delete-btn");
  if (button) button.disabled = true;
  clearModalLog("modalLog");
  hideEpubFolderImportResults();
  setUploadStatus("Deleting…", "0/" + selected.length, 0, true);

  let deleted = 0;
  const failed = [];
  for (let i = 0; i < selected.length; i++) {
    const item = selected[i];
    setUploadStatus("Deleting " + item.name, i + 1 + "/" + selected.length, Math.round((i / selected.length) * 100), true);
    try {
      await deletePathRecursive(item.path, item.type);
      deleted++;
      addModalLog("modalLog", "Deleted: " + item.name, "success");
    } catch (e) {
      failed.push(item.name);
      addModalLog("modalLog", "Delete failed: " + item.name + " (" + e.message + ")", "error");
    }
  }

  setUploadStatus("Delete complete", deleted + "/" + selected.length, 100, true);
  showEpubFolderImportResults(deleted, failed, selected.map((item) => ({ name: item.name })));
  if (button) button.disabled = false;
  await hydrate();
}

async function promptDeleteItem(path, name, type) {
  const isFolder = type === "folder";
  const copy = isFolder
    ? 'Delete "' + name + '" and everything inside it? This cannot be undone.'
    : 'Delete "' + name + '"? This cannot be undone.';
  if (!confirm(copy)) return;
  try {
    await deletePathRecursive(path, type);
    await hydrate();
  } catch (e) {
    alert((e && e.message) || "Unable to delete item");
  }
}

async function promptRename(path, currentName, type) {
  const isFolder = type === "folder";
  const dot = isFolder ? -1 : currentName.lastIndexOf(".");
  const base = dot > 0 ? currentName.slice(0, dot) : currentName;
  const ext = dot > 0 ? currentName.slice(dot) : "";
  const input = (prompt("Rename to", base) || "").trim();
  if (!input || input === base) return;
  if (!/^(?!\.{1,2}$)[^"*:<>?\\/|]+$/.test(input)) {
    alert("Invalid name");
    return;
  }
  const formData = new FormData();
  formData.append("path", path);
  formData.append("name", input + ext);
  const res = await fetch("/rename", { method: "POST", body: formData });
  if (res.ok) {
    await hydrate();
  } else {
    alert((await res.text()) || "Unable to rename item");
  }
}

function addDropHandlers(el, onDrop) {
  ["dragenter", "dragover"].forEach((evt) =>
    el.addEventListener(evt, (e) => {
      e.preventDefault();
      e.stopPropagation();
      el.classList.add("dragover");
    })
  );
  ["dragleave", "dragend"].forEach((evt) =>
    el.addEventListener(evt, (e) => {
      e.preventDefault();
      e.stopPropagation();
      el.classList.remove("dragover");
    })
  );
  el.addEventListener("drop", (e) => {
    e.preventDefault();
    e.stopPropagation();
    el.classList.remove("dragover");
    if (!e.dataTransfer || !e.dataTransfer.files || !e.dataTransfer.files.length) return;
    const files = filterEpubFiles(e.dataTransfer.files);
    if (!files.length) {
      addModalLog("modalLog", "Dropped file(s) ignored - only .epub files are accepted.", "error");
      return;
    }
    onDrop(files);
  });
}

// Page-level dropzone: click opens the file picker, drop uploads to the current folder.
function initEpubDropzone() {
  const zone = document.getElementById("epubDropzone");
  const fileInput = document.getElementById("fileInput");
  if (!zone || !fileInput) return;
  zone.addEventListener("click", (e) => {
    if (e.target === fileInput) return;
    e.preventDefault();
    fileInput.click();
  });
  addDropHandlers(zone, (files) =>
    uploadEpubFiles(files, currentPath).catch((e) => addModalLog("modalLog", "Import failed: " + e.message, "error"))
  );
}

// Inline folder/file listing - replaces the old destination-picker modal. Folder rows are their own
// drop targets: dropping an EPUB directly on a folder imports it there without navigating into it.
async function hydrate() {
  currentPath = getCurrentPath();
  const crumbs = document.getElementById("directory-breadcrumbs");
  if (crumbs) crumbs.innerHTML = formatBreadcrumb(currentPath);

  const table = document.getElementById("file-table");
  if (!table) return;
  try {
    const res = await fetch("/api/files?path=" + encodeURIComponent(currentPath));
    const items = await res.json();

    // "fonts" and "sleep" are system folders (custom fonts, sleep-screen images) - not book storage.
    const visible = items.filter((i) =>
      i.isDirectory ? !SYSTEM_FOLDERS.includes(i.name.toLowerCase()) : i.name.toLowerCase().endsWith(".epub")
    );

    let folderCount = 0;
    let epubCount = 0;
    visible.forEach((i) => (i.isDirectory ? folderCount++ : epubCount++));
    const summary = document.getElementById("folder-summary");
    if (summary) summary.textContent = folderCount + " folder(s), " + epubCount + " book(s)";

    if (!visible.length) {
      table.innerHTML =
        '<div class="empty-state"><svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"><path d="M4 6.5A2.5 2.5 0 0 1 6.5 4H10l2 2h5.5A2.5 2.5 0 0 1 20 8.5v8A2.5 2.5 0 0 1 17.5 19h-11A2.5 2.5 0 0 1 4 16.5v-10Z"/></svg><div>No folders or EPUB files here yet</div></div>';
      updateBulkActions();
      return;
    }
    visible.sort((a, b) =>
      a.isDirectory === b.isDirectory ? (a.name || "").localeCompare(b.name || "") : a.isDirectory ? -1 : 1
    );

    let html = '<div class="file-list">';
    for (const item of visible) {
      const itemPath = currentPath.replace(/\/$/, "") + "/" + item.name;
      const itemPathAttr = escapeAttr(itemPath);
      const itemNameAttr = escapeAttr(item.name);
      const renameBtn =
        '<button type="button" class="row-action rename-btn" data-path="' + itemPathAttr + '" data-name="' + itemNameAttr +
        '" data-type="' + (item.isDirectory ? "folder" : "file") + '" onclick="promptRename(this.dataset.path,this.dataset.name,this.dataset.type)" title="Rename" aria-label="Rename">' +
        '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="m13.5 3.5 3 3L7 16H4v-3l9.5-9.5Z"/></svg></button>';
      const deleteBtn =
        '<button type="button" class="row-action danger delete-btn" data-path="' + itemPathAttr + '" data-name="' + itemNameAttr +
        '" data-type="' + (item.isDirectory ? "folder" : "file") + '" onclick="promptDeleteItem(this.dataset.path,this.dataset.name,this.dataset.type)" title="Delete" aria-label="Delete">' +
        '<svg viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"><path d="M4 6h12M8 3.5h4L13 6H7l1-2.5ZM6 6l.7 10h6.6L14 6M8.5 8.5v5M11.5 8.5v5"/></svg></button>';

      if (item.isDirectory) {
        html +=
          '<div class="file-row is-folder folder-row" data-path="' + itemPathAttr + '">' +
          '<input class="select-box" type="checkbox" data-path="' + itemPathAttr + '" data-name="' + itemNameAttr + '" data-type="folder" onchange="updateBulkActions()">' +
          '<a class="row-main" href="/epub?path=' + encodeURIComponent(itemPath) + '">' +
          '<svg class="icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"><path d="M4 6.5A2.5 2.5 0 0 1 6.5 4H10l2 2h5.5A2.5 2.5 0 0 1 20 8.5v8A2.5 2.5 0 0 1 17.5 19h-11A2.5 2.5 0 0 1 4 16.5v-10Z"/></svg>' +
          '<span class="name">' + escapeHtml(item.name) + '</span><span class="meta">Folder</span>' +
          '<svg class="chevron" viewBox="0 0 20 20" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="m7 4 6 6-6 6"/></svg></a>' +
          renameBtn + deleteBtn + '</div>';
      } else {
        html +=
          '<div class="file-row epub-file">' +
          '<input class="select-box" type="checkbox" data-path="' + itemPathAttr + '" data-name="' + itemNameAttr + '" data-type="file" onchange="updateBulkActions()">' +
          '<button type="button" class="row-main" onclick="window.location.href=\'/epub-viewer.html?path=' +
          encodeURIComponent(itemPath) + "'\">" +
          '<svg class="icon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"><path d="M6 4.5h9.5A2.5 2.5 0 0 1 18 7v12.5H7.5A2.5 2.5 0 0 1 5 17V5.5A1 1 0 0 1 6 4.5Z"/><path d="M7.5 19.5A2.5 2.5 0 0 1 7.5 14H18"/></svg>' +
          '<span class="name">' + escapeHtml(item.name) + '<span class="epub-badge">EPUB</span></span>' +
          '<span class="meta">' + formatFileSize(item.size) + '</span></button>' +
          renameBtn + deleteBtn + '</div>';
      }
    }
    html += "</div>";
    table.innerHTML = html;
    updateBulkActions();

    table.querySelectorAll(".folder-row").forEach((row) => {
      const folderPath = row.getAttribute("data-path");
      addDropHandlers(row, (files) =>
        uploadEpubFiles(files, folderPath).catch((e) => addModalLog("modalLog", "Import failed: " + e.message, "error"))
      );
    });
  } catch (e) {
    table.innerHTML = '<div class="empty-state">Unable to load folder</div>';
    updateBulkActions();
  }
}

function init() {
  try {
    generatePackagedThumbnail = localStorage.getItem("epubGeneratePackagedThumbnail") === "true";
  } catch (e) {
    generatePackagedThumbnail = false;
  }
  updateToggleUI();
  initEpubDropzone();
  hydrate();
}
document.readyState === "loading" ? document.addEventListener("DOMContentLoaded", init) : init();
