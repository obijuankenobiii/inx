/**
 * @file Mobi.cpp
 * @brief Definitions for Mobi.
 */

#include "Mobi.h"

#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <miniz.h>

#include <cstring>
#include <vector>

namespace {

uint16_t be16(const uint8_t* p) { return (static_cast<uint16_t>(p[0]) << 8) | p[1]; }
uint32_t be32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

// Escapes the handful of characters that would otherwise break the XML we embed titles/paths into.
std::string xmlEscape(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (const char c : s) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      default:
        out += c;
    }
  }
  return out;
}

/** Decompresses one PalmDOC/LZ77-compressed record, appending the result to `out`. */
void depalmdocAppend(const uint8_t* data, const size_t len, std::vector<uint8_t>& out) {
  size_t i = 0;
  while (i < len) {
    const uint8_t c = data[i++];
    if (c == 0) {
      out.push_back(c);
    } else if (c <= 8) {
      const size_t take = std::min<size_t>(c, len - i);
      out.insert(out.end(), data + i, data + i + take);
      i += take;
    } else if (c <= 0x7f) {
      out.push_back(c);
    } else if (c >= 0xc0) {
      out.push_back(' ');
      out.push_back(static_cast<uint8_t>(c ^ 0x80));
    } else {
      if (i >= len) break;
      const uint8_t c2 = data[i++];
      const uint16_t m = (static_cast<uint16_t>(c) << 8) | c2;
      const uint16_t distance = (m >> 3) & 0x7ff;
      const uint16_t length = (m & 0x7) + 3;
      if (distance == 0 || distance > out.size()) break;  // corrupt record - stop rather than misread memory
      const size_t start = out.size() - distance;
      for (uint16_t j = 0; j < length; j++) {
        out.push_back(out[start + j]);
      }
    }
  }
}

/**
 * MOBI records can carry a variable-length "trailing entries" footer (multibyte character overlap,
 * indexing data, etc.) appended after the actual (possibly compressed) text - this must be stripped
 * before PalmDOC decompression. See the MOBI format notes on "extra data flags" in the MOBI header.
 */
size_t trailingEntrySize(const uint8_t* data, size_t size) {
  int bitpos = 0;
  size_t result = 0;
  while (size > 0) {
    const uint8_t v = data[size - 1];
    size--;
    result |= static_cast<size_t>(v & 0x7F) << bitpos;
    bitpos += 7;
    if ((v & 0x80) != 0 || bitpos >= 28) break;
  }
  return result;
}

size_t stripTrailingSize(const uint8_t* data, size_t size, const uint16_t extraFlags) {
  size_t num = 0;
  uint16_t testFlags = extraFlags >> 1;
  while (testFlags != 0) {
    if (testFlags & 1) {
      num += trailingEntrySize(data, size - num);
    }
    testFlags >>= 1;
  }
  if ((extraFlags & 1) != 0 && size > num) {
    num += (data[size - num - 1] & 0x3) + 1;
  }
  return size > num ? size - num : 0;
}

struct MobiHeader {
  uint16_t compression = 0;   // 1 = none, 2 = PalmDOC/LZ77
  uint16_t recordCount = 0;   // number of text records: records[1..recordCount]
  uint32_t firstImageIndex = 0;
  uint16_t extraFlags = 0;
  std::string title;
  std::string author;
};

/**
 * Pulls the author (EXTH record type 100) out of the EXTH metadata block, if present. EXTH immediately
 * follows the fixed 16-byte PalmDOC header + the MOBI header (whose length is stored at rec0[20:24]), so
 * unlike the full title this never needs a separate file read - it's already inside the buffered rec0.
 */
std::string readExthAuthor(const std::vector<uint8_t>& rec0) {
  const uint32_t exthFlags = be32(&rec0[128]);
  if ((exthFlags & 0x40) == 0) return "";  // no EXTH block present

  const uint32_t mobiHeaderLength = be32(&rec0[20]);
  const size_t exthStart = 16 + mobiHeaderLength;
  if (exthStart + 12 > rec0.size() || memcmp(&rec0[exthStart], "EXTH", 4) != 0) return "";

  const uint32_t recordCount = be32(&rec0[exthStart + 8]);
  size_t pos = exthStart + 12;
  for (uint32_t i = 0; i < recordCount && pos + 8 <= rec0.size(); i++) {
    const uint32_t type = be32(&rec0[pos]);
    const uint32_t len = be32(&rec0[pos + 4]);
    if (len < 8 || pos + len > rec0.size()) break;  // corrupt/truncated - stop rather than misread
    if (type == 100) {  // 100 = author (EXTH spec)
      return std::string(reinterpret_cast<const char*>(&rec0[pos + 8]), len - 8);
    }
    pos += len;
  }
  return "";
}

/**
 * Reads and validates the PalmDOC + MOBI header living in record 0. Record 0's raw bytes are in `rec0`;
 * rec0FileOffset is where that record actually starts in the file (offsets inside the header, like the
 * full title's location, are relative to the start of record 0, not to the start of the file).
 */
bool parseMobiHeader(FsFile& file, const std::vector<uint8_t>& rec0, const uint32_t rec0FileOffset,
                     MobiHeader* out) {
  if (rec0.size() < 232) return false;
  out->compression = be16(&rec0[0]);
  const uint16_t encryptionType = be16(&rec0[12]);
  out->recordCount = be16(&rec0[8]);
  if (memcmp(&rec0[16], "MOBI", 4) != 0) {
    Serial.printf("[MOBI] Not a recognized MOBI6 header (no 'MOBI' magic) - unsupported format\n");
    return false;
  }
  if (encryptionType != 0) {
    Serial.printf("[MOBI] DRM-protected book (encryption_type=%u) - unsupported\n", encryptionType);
    return false;
  }
  if (out->compression != 1 && out->compression != 2) {
    Serial.printf("[MOBI] Unsupported compression scheme %u (expected PalmDOC or none - HUFF/CDIC?)\n",
                  out->compression);
    return false;
  }

  out->firstImageIndex = be32(&rec0[108]);
  out->extraFlags = rec0.size() >= 244 ? be16(&rec0[242]) : 0;

  const uint32_t fullNameOffset = be32(&rec0[84]);
  const uint32_t fullNameLength = be32(&rec0[88]);
  if (fullNameLength > 0 && fullNameLength < 1024) {
    std::vector<uint8_t> nameBuf(fullNameLength);
    file.seek(rec0FileOffset + fullNameOffset);
    if (file.read(nameBuf.data(), fullNameLength) == static_cast<int>(fullNameLength)) {
      out->title.assign(reinterpret_cast<const char*>(nameBuf.data()), fullNameLength);
    }
  }
  // Trim trailing NULs/whitespace some encoders pad the name field with, which would otherwise end up
  // embedded (and escaped oddly, or worse - truncate XML parsing) inside the generated content.opf.
  while (!out->title.empty() && (out->title.back() == '\0' || isspace(static_cast<unsigned char>(out->title.back())))) {
    out->title.pop_back();
  }
  if (out->title.empty()) {
    out->title = "Untitled";
  }

  out->author = readExthAuthor(rec0);
  while (!out->author.empty() &&
        (out->author.back() == '\0' || isspace(static_cast<unsigned char>(out->author.back())))) {
    out->author.pop_back();
  }
  return true;
}

/** Minimal STORED-only (uncompressed) zip writer, written sequentially with no backward seeks. */
class ZipStoreWriter {
 public:
  bool begin(const std::string& outPath) { return SdMan.openFileForWrite("MOBI", outPath, file_); }

  /** Writes a whole entry whose size/content is already fully known (small strings, in-memory buffers). */
  bool addEntry(const std::string& name, const uint8_t* data, const size_t len) {
    const uint32_t crc = static_cast<uint32_t>(mz_crc32(MZ_CRC32_INIT, data, len));
    if (!writeLocalHeader(name, crc, len)) return false;
    if (len > 0 && static_cast<size_t>(file_.write(data, len)) != len) return false;
    entries_.push_back({name, crc, static_cast<uint32_t>(len), pendingOffset_});
    return true;
  }

  /** Streams an entry whose crc/size were computed separately in advance (see copyFileIntoEntry). */
  bool addEntryFromFile(const std::string& name, FsFile& src, const uint32_t size, const uint32_t crc) {
    if (!writeLocalHeader(name, crc, size)) return false;
    uint8_t buf[512];
    uint32_t remaining = size;
    while (remaining > 0) {
      const size_t chunk = std::min<uint32_t>(remaining, sizeof(buf));
      const int got = src.read(buf, chunk);
      if (got != static_cast<int>(chunk)) return false;
      if (static_cast<size_t>(file_.write(buf, chunk)) != chunk) return false;
      remaining -= static_cast<uint32_t>(chunk);
    }
    entries_.push_back({name, crc, size, pendingOffset_});
    return true;
  }

  bool finalize() {
    const uint32_t cdStart = static_cast<uint32_t>(file_.position());
    for (const auto& e : entries_) {
      uint8_t hdr[46];
      writeCentralHeader(hdr, e);
      if (file_.write(hdr, sizeof(hdr)) != sizeof(hdr)) return false;
      if (static_cast<size_t>(file_.write(e.name.data(), e.name.size())) != e.name.size()) return false;
    }
    const uint32_t cdSize = static_cast<uint32_t>(file_.position()) - cdStart;
    uint8_t eocd[22];
    writeEocd(eocd, static_cast<uint16_t>(entries_.size()), cdSize, cdStart);
    const bool ok = file_.write(eocd, sizeof(eocd)) == sizeof(eocd);
    file_.close();
    return ok;
  }

 private:
  struct Entry {
    std::string name;
    uint32_t crc;
    uint32_t size;
    uint32_t offset;
  };
  FsFile file_;
  std::vector<Entry> entries_;
  uint32_t pendingOffset_ = 0;

  static void putU16(uint8_t* p, const uint16_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
  }
  static void putU32(uint8_t* p, const uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
  }

  bool writeLocalHeader(const std::string& name, const uint32_t crc, const uint32_t size) {
    pendingOffset_ = static_cast<uint32_t>(file_.position());
    uint8_t hdr[30] = {0};
    putU32(hdr + 0, 0x04034b50);
    putU16(hdr + 4, 20);  // version needed to extract
    putU16(hdr + 8, 0);   // compression method = stored
    putU32(hdr + 14, crc);
    putU32(hdr + 18, size);  // compressed size == uncompressed size (stored)
    putU32(hdr + 22, size);
    putU16(hdr + 26, static_cast<uint16_t>(name.size()));
    if (file_.write(hdr, sizeof(hdr)) != sizeof(hdr)) return false;
    return static_cast<size_t>(file_.write(name.data(), name.size())) == name.size();
  }

  static void writeCentralHeader(uint8_t* hdr, const Entry& e) {
    memset(hdr, 0, 46);
    putU32(hdr + 0, 0x02014b50);
    putU16(hdr + 4, 20);
    putU16(hdr + 6, 20);
    putU32(hdr + 16, e.crc);
    putU32(hdr + 20, e.size);
    putU32(hdr + 24, e.size);
    putU16(hdr + 28, static_cast<uint16_t>(e.name.size()));
    putU32(hdr + 42, e.offset);
  }

  static void writeEocd(uint8_t* hdr, const uint16_t count, const uint32_t cdSize, const uint32_t cdStart) {
    putU32(hdr + 0, 0x06054b50);
    putU16(hdr + 8, count);
    putU16(hdr + 10, count);
    putU32(hdr + 12, cdSize);
    putU32(hdr + 16, cdStart);
    putU16(hdr + 20, 0);
  }
};

const char* imageExtensionFor(const uint8_t* data, const size_t len, const char** mediaType) {
  if (len >= 3 && data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
    *mediaType = "image/jpeg";
    return "jpg";
  }
  if (len >= 8 && memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0) {
    *mediaType = "image/png";
    return "png";
  }
  if (len >= 6 && (memcmp(data, "GIF87a", 6) == 0 || memcmp(data, "GIF89a", 6) == 0)) {
    *mediaType = "image/gif";
    return "gif";
  }
  return nullptr;  // not a recognized image record (FLIS/FCIS/EOF/index records etc.) - skip it
}

}  // namespace

namespace Mobi {

bool convertToEpub(const std::string& mobiPath, const std::string& outEpubPath) {
  FsFile src;
  if (!SdMan.openFileForRead("MOBI", mobiPath, src)) {
    Serial.printf("[MOBI] Could not open %s\n", mobiPath.c_str());
    return false;
  }

  uint8_t pdbHeader[78];
  if (src.read(pdbHeader, sizeof(pdbHeader)) != static_cast<int>(sizeof(pdbHeader))) {
    Serial.printf("[MOBI] File too small to be a PDB/MOBI container\n");
    return false;
  }
  const uint16_t numRecords = be16(&pdbHeader[76]);
  if (numRecords < 2) {
    Serial.printf("[MOBI] No content records (numRecords=%u)\n", numRecords);
    return false;
  }

  std::vector<uint32_t> records(numRecords + 1);
  {
    std::vector<uint8_t> recTable(numRecords * 8u);
    if (src.read(recTable.data(), recTable.size()) != static_cast<int>(recTable.size())) {
      Serial.printf("[MOBI] Failed to read record offset table\n");
      return false;
    }
    for (uint16_t i = 0; i < numRecords; i++) {
      records[i] = be32(&recTable[i * 8]);
    }
    records[numRecords] = static_cast<uint32_t>(src.size());
  }

  std::vector<uint8_t> rec0(records[1] - records[0]);
  src.seek(records[0]);
  if (src.read(rec0.data(), rec0.size()) != static_cast<int>(rec0.size())) {
    Serial.printf("[MOBI] Failed to read record 0 (PalmDOC/MOBI header)\n");
    return false;
  }

  MobiHeader header;
  if (!parseMobiHeader(src, rec0, records[0], &header)) {
    return false;
  }
  if (header.recordCount == 0 || header.recordCount >= numRecords) {
    Serial.printf("[MOBI] Implausible text record count %u (numRecords=%u)\n", header.recordCount, numRecords);
    return false;
  }
  Serial.printf("[MOBI] '%s' by '%s' compression=%u textRecords=%u firstImageIndex=%u\n", header.title.c_str(),
                header.author.empty() ? "Unknown" : header.author.c_str(), header.compression, header.recordCount,
                header.firstImageIndex);

  // --- Pass 1: decompress every text record into a scratch file on disk (bounded memory; never holds the
  // whole book in RAM), so its final size/CRC are known before we write the zip's local header for it. ---
  const std::string scratchPath = outEpubPath + ".tmp_text";
  {
    FsFile scratch;
    if (!SdMan.openFileForWrite("MOBI", scratchPath, scratch)) {
      Serial.printf("[MOBI] Could not open scratch file %s\n", scratchPath.c_str());
      return false;
    }
    std::vector<uint8_t> raw;
    std::vector<uint8_t> decompressed;
    for (uint16_t i = 1; i <= header.recordCount; i++) {
      const uint32_t recLen = records[i + 1] - records[i];
      raw.resize(recLen);
      src.seek(records[i]);
      if (src.read(raw.data(), recLen) != static_cast<int>(recLen)) {
        Serial.printf("[MOBI] Failed to read text record %u\n", i);
        scratch.close();
        SdMan.remove(scratchPath.c_str());
        return false;
      }
      const size_t usable = stripTrailingSize(raw.data(), raw.size(), header.extraFlags);
      if (header.compression == 2) {
        decompressed.clear();
        depalmdocAppend(raw.data(), usable, decompressed);
        scratch.write(decompressed.data(), decompressed.size());
      } else {
        scratch.write(raw.data(), usable);
      }
    }
    scratch.close();
  }

  // --- CRC + size of the scratch file, now that its content is finalized. ---
  uint32_t textCrc = MZ_CRC32_INIT;
  uint32_t textSize = 0;
  {
    FsFile scratch;
    if (!SdMan.openFileForRead("MOBI", scratchPath, scratch)) {
      Serial.printf("[MOBI] Could not reopen scratch file for CRC pass\n");
      return false;
    }
    uint8_t buf[512];
    int got;
    while ((got = scratch.read(buf, sizeof(buf))) > 0) {
      textCrc = static_cast<uint32_t>(mz_crc32(textCrc, buf, got));
      textSize += static_cast<uint32_t>(got);
    }
    scratch.close();
  }

  // --- Collect embedded images (best-effort; unrecognized trailing records like FLIS/FCIS/EOF are skipped). ---
  struct ImageInfo {
    std::string entryName;
    std::string mediaType;
    uint32_t recordIndex;
    uint32_t crc;
    uint32_t size;
  };
  std::vector<ImageInfo> images;
  if (header.firstImageIndex > 0 && header.firstImageIndex < numRecords) {
    std::vector<uint8_t> imgBuf;
    for (uint32_t i = header.firstImageIndex; i < numRecords; i++) {
      const uint32_t recLen = records[i + 1] - records[i];
      if (recLen == 0 || recLen > 2 * 1024 * 1024) continue;  // sanity bound
      imgBuf.resize(recLen);
      src.seek(records[i]);
      if (src.read(imgBuf.data(), recLen) != static_cast<int>(recLen)) continue;
      const char* mediaType = nullptr;
      const char* ext = imageExtensionFor(imgBuf.data(), imgBuf.size(), &mediaType);
      if (!ext) continue;
      ImageInfo info;
      info.recordIndex = i;
      info.entryName = "images/image" + std::to_string(images.size()) + "." + ext;
      info.mediaType = mediaType;
      info.crc = static_cast<uint32_t>(mz_crc32(MZ_CRC32_INIT, imgBuf.data(), imgBuf.size()));
      info.size = static_cast<uint32_t>(imgBuf.size());
      images.push_back(info);
    }
  }

  // --- Assemble the minimal EPUB zip. ---
  ZipStoreWriter zip;
  if (!zip.begin(outEpubPath)) {
    Serial.printf("[MOBI] Could not open output epub %s for write\n", outEpubPath.c_str());
    SdMan.remove(scratchPath.c_str());
    return false;
  }

  const std::string mimetype = "application/epub+zip";
  const std::string container =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?><container version=\"1.0\" "
      "xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\"><rootfiles><rootfile "
      "full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/></rootfiles></container>";

  bool ok = zip.addEntry("mimetype", reinterpret_cast<const uint8_t*>(mimetype.data()), mimetype.size());
  ok = ok && zip.addEntry("META-INF/container.xml", reinterpret_cast<const uint8_t*>(container.data()),
                          container.size());

  {
    FsFile scratch;
    ok = ok && SdMan.openFileForRead("MOBI", scratchPath, scratch);
    ok = ok && zip.addEntryFromFile("OEBPS/content.html", scratch, textSize, textCrc);
    if (scratch) scratch.close();
  }

  for (const auto& img : images) {
    const uint32_t recLen = records[img.recordIndex + 1] - records[img.recordIndex];
    src.seek(records[img.recordIndex]);
    ok = ok && zip.addEntryFromFile("OEBPS/" + img.entryName, src, recLen, img.crc);
  }

  std::string manifestImages;
  for (size_t i = 0; i < images.size(); i++) {
    manifestImages += "<item href=\"" + images[i].entryName + "\" id=\"img" + std::to_string(i) +
                      "\" media-type=\"" + images[i].mediaType + "\"/>";
  }
  const std::string creator =
      header.author.empty() ? "" : "<dc:creator>" + xmlEscape(header.author) + "</dc:creator>";
  const std::string opf =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<package xmlns=\"http://www.idpf.org/2007/opf\" unique-identifier=\"BookId\" version=\"3.0\">\n"
      "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
      "<dc:identifier id=\"BookId\">" +
      xmlEscape(header.title) +
      "</dc:identifier>"
      "<dc:title>" +
      xmlEscape(header.title) +
      "</dc:title>" +
      creator +
      "<dc:language>en</dc:language>"
      "</metadata>"
      "<manifest><item href=\"content.html\" id=\"content\" media-type=\"application/xhtml+xml\"/>" +
      manifestImages +
      "</manifest>"
      "<spine><itemref idref=\"content\"/></spine>"
      "</package>";
  ok = ok && zip.addEntry("OEBPS/content.opf", reinterpret_cast<const uint8_t*>(opf.data()), opf.size());

  ok = ok && zip.finalize();

  SdMan.remove(scratchPath.c_str());
  if (!ok) {
    Serial.printf("[MOBI] Transcode failed while writing %s\n", outEpubPath.c_str());
    SdMan.remove(outEpubPath.c_str());
    return false;
  }
  Serial.printf("[MOBI] Transcoded '%s' -> %s (text=%u bytes, %zu images)\n", header.title.c_str(),
                outEpubPath.c_str(), textSize, images.size());
  return true;
}

}  // namespace Mobi
