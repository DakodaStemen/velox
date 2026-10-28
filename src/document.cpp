// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Velox Authors
#include "document.h"
#include <QTextCodec>
#ifndef Q_OS_WIN
#include <sys/stat.h>
#endif
static QByteArray hash(const QByteArray &b) {
  return QCryptographicHash::hash(b, QCryptographicHash::Sha256);
}
// Qt 5's UTF-32 codec writes a stray NUL per surrogate pair, so UTF-32 is
// converted here instead.
static bool decodeUtf32(const QByteArray &body, bool little, QString &out) {
  if (body.size() % 4)
    return false;
  QVector<uint> units;
  units.reserve(body.size() / 4);
  for (int i = 0; i < body.size(); i += 4) {
    const uchar *b = reinterpret_cast<const uchar *>(body.constData()) + i;
    const uint c = little ? uint(b[0]) | b[1] << 8 | b[2] << 16 | uint(b[3]) << 24
                          : uint(b[3]) | b[2] << 8 | b[1] << 16 | uint(b[0]) << 24;
    if (c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
      return false;
    units << c;
  }
  out = QString::fromUcs4(units.constData(), units.size());
  return true;
}
static QByteArray encodeUtf32(const QString &text, bool little) {
  const QVector<uint> units = text.toUcs4();
  QByteArray out;
  out.reserve(units.size() * 4);
  for (uint c : units)
    for (int i = 0; i < 4; ++i)
      out += char((c >> (little ? 8 * i : 8 * (3 - i))) & 0xff);
  return out;
}
bool DocumentData::read(const QString &name, DocumentData &out, QString &error,
                        const QString &fallback, ReadError *kind,
                        bool computeHash) {
  if (kind)
    *kind = ReadError::Other;
  // Checked before opening: opening a FIFO blocks, and devices never end.
  // QFileInfo::isFile() is true only for regular files on every platform.
  const QFileInfo probe(name);
  if (probe.exists() && !probe.isFile()) {
    error = probe.isDir() ? "This is a folder, not a file."
                          : "Only regular files can be opened (this is "
                            "a device, pipe or socket).";
    return false;
  }
  QFile f(name);
  if (!f.open(QIODevice::ReadOnly)) {
    error = f.errorString();
    return false;
  }
  QByteArray raw = f.readAll();
  if (f.error() != QFileDevice::NoError) {
    error = f.errorString();
    return false;
  }
  DocumentData result;
  result.path = QFileInfo(name).absoluteFilePath();
  result.exists = true;
  if (computeHash) // search-only reads skip the SHA-256 pass
    result.diskHash = hash(raw);
  // UTF-32 BOMs must be tested before UTF-16LE, whose BOM is their prefix.
  const bool utf32le = raw.startsWith(QByteArray::fromHex("fffe0000")),
             utf32be = raw.startsWith(QByteArray::fromHex("0000feff"));
  if (utf32le || utf32be) {
    if (raw.size() % 4) {
      error = "This UTF-32 file is truncated (its size is not a multiple of "
              "four bytes).";
      return false;
    }
    result.encoding = utf32le ? "UTF-32LE" : "UTF-32BE";
    result.bom = raw.left(4);
  } else if (raw.startsWith(QByteArray::fromHex("efbbbf")))
    result.bom = raw.left(3);
  else if (raw.startsWith(QByteArray::fromHex("fffe"))) {
    result.encoding = "UTF-16LE";
    result.bom = raw.left(2);
  } else if (raw.startsWith(QByteArray::fromHex("feff"))) {
    result.encoding = "UTF-16BE";
    result.bom = raw.left(2);
  } else if (!fallback.isEmpty())
    result.encoding = fallback;
  if (result.encoding.startsWith("UTF-32")) {
    QString text;
    if (!decodeUtf32(raw.mid(result.bom.size()), result.encoding == "UTF-32LE",
                     text)) {
      error = "This file is not valid " + result.encoding + ".";
      return false;
    }
    if (text.contains(QChar(0))) {
      error = "This appears to be a binary file (contains NUL characters).";
      return false;
    }
    result.utf8 = text.toUtf8();
    result.stamp();
    out = result;
    if (kind)
      *kind = ReadError::None;
    return true;
  }
  QTextCodec *codec = QTextCodec::codecForName(result.encoding.toLatin1());
  if (!codec) {
    error = "Unknown encoding: " + result.encoding;
    return false;
  }
  // Decode in chunks so only one chunk of UTF-16 text is alive at a time.
  // For UTF-8 the validated raw bytes are reused directly as the result.
  const bool isUtf8 = codec->mibEnum() == 106;
  QTextCodec::ConverterState state(QTextCodec::IgnoreHeader);
  const char *body = raw.constData() + result.bom.size();
  const qint64 bodySize = raw.size() - result.bom.size();
  const qint64 chunk = 1 << 20;
  QString pending;
  if (!isUtf8)
    result.utf8.reserve(int(qMin<qint64>(bodySize * 2, (1 << 30))));
  for (qint64 at = 0; at < bodySize; at += chunk) {
    const int n = int(qMin<qint64>(chunk, bodySize - at));
    QString part = pending + codec->toUnicode(body + at, n, &state);
    pending.clear();
    // A surrogate pair may straddle chunks; carry the high half over.
    if (!isUtf8 && at + n < bodySize && !part.isEmpty() &&
        part.at(part.size() - 1).isHighSurrogate()) {
      pending = part.right(1);
      part.chop(1);
    }
    if (state.invalidChars) {
      error = "This file is not valid " + result.encoding +
              ". Choose a legacy encoding to open it without replacing bytes.";
      if (kind)
        *kind = ReadError::InvalidEncoding;
      return false;
    }
    if (part.contains(QChar(0))) {
      error = "This appears to be a binary file (contains NUL characters).";
      return false;
    }
    if (!isUtf8)
      result.utf8 += part.toUtf8();
  }
  if (state.remainingChars || !pending.isEmpty()) {
    error = "This file is not valid " + result.encoding +
            ". Choose a legacy encoding to open it without replacing bytes.";
    if (kind)
      *kind = ReadError::InvalidEncoding;
    return false;
  }
  if (isUtf8) {
    raw.remove(0, result.bom.size());
    result.utf8 = std::move(raw);
  } else {
    raw.clear();
    raw.squeeze();
  }
  result.stamp();
  out = result;
  if (kind)
    *kind = ReadError::None;
  return true;
}
bool DocumentData::write(const QByteArray &text, const QString &name,
                         QString &error, bool overwriteChanged) {
  const QString absolute = QFileInfo(name).absoluteFilePath();
  QFileInfo info(absolute);
  QString destination = info.isSymLink() ? info.canonicalFilePath() : absolute;
  if (destination.isEmpty()) {
    error = "The symbolic link has no existing target. Choose Save As.";
    return false;
  }
  const QFileInfo target(destination);
  if (target.exists()) {
    if (!target.isFile()) {
      error = "The destination is not a regular file.";
      return false;
    }
#ifndef Q_OS_WIN
    // Hard-link count is only exposed through stat(); Windows saves replace
    // the file without this concern, so the check is POSIX-only.
    struct stat st{};
    if (::stat(QFile::encodeName(destination).constData(), &st) == 0 &&
        st.st_nlink > 1) {
      error = "This file has hard links. Use Save As to avoid breaking their "
              "shared identity.";
      return false;
    }
#endif
    if (!target.isWritable()) {
      error = "The destination is read-only.";
      return false;
    }
  }
  const bool same = absolute == path;
  if (same && exists && !overwriteChanged) {
    QFile old(destination);
    if (!old.open(QIODevice::ReadOnly)) {
      error = "The original file was removed or is inaccessible. Use Save As.";
      return false;
    }
    QByteArray previous = old.readAll();
    if (old.error() != QFileDevice::NoError || hash(previous) != diskHash) {
      error = "The file changed on disk since it was opened. Reload it or use "
              "Save As to keep both versions.";
      return false;
    }
  }
  QString unicode = QString::fromUtf8(text);
  QByteArray bytes;
  if (encoding.startsWith("UTF-32"))
    bytes = encodeUtf32(unicode, encoding == "UTF-32LE");
  else {
    QTextCodec *codec = QTextCodec::codecForName(encoding.toLatin1());
    if (!codec) {
      error = "Unknown encoding: " + encoding;
      return false;
    }
    QTextCodec::ConverterState state(QTextCodec::IgnoreHeader);
    bytes = codec->fromUnicode(unicode.constData(), unicode.size(), &state);
    if (state.invalidChars) {
      error = "Some characters cannot be represented in " + encoding +
              ". Select UTF-8 or UTF-16 before saving.";
      return false;
    }
  }
  bytes.prepend(bom);
  QSaveFile file(destination);
  file.setDirectWriteFallback(false);
  if (!file.open(QIODevice::WriteOnly)) {
    error = file.errorString();
    return false;
  }
  if (file.write(bytes) != bytes.size()) {
    error = file.errorString();
    file.cancelWriting();
    return false;
  }
  if (!file.commit()) {
    error = file.errorString();
    return false;
  }
  path = absolute;
  diskHash = hash(bytes);
  exists = true;
  utf8 = text;
  stamp();
  return true;
}
void DocumentData::stamp() {
  QFileInfo info(path);
  diskTime = info.lastModified();
  diskSize = info.size();
}
QJsonObject DocumentData::json(bool dirty, const QByteArray &text) const {
  QJsonObject o{{"path", path},
                {"encoding", encoding},
                {"bom", QString::fromLatin1(bom.toHex())},
                {"hash", QString::fromLatin1(diskHash.toHex())},
                {"exists", exists},
                {"dirty", dirty}};
  if (needsBackup(dirty))
    o["backup"] = QString::fromLatin1(qCompress(text).toBase64());
  return o;
}
DocumentData DocumentData::fromJson(const QJsonObject &o) {
  DocumentData d;
  d.path = o["path"].toString();
  d.encoding = o["encoding"].toString("UTF-8");
  d.bom = QByteArray::fromHex(o["bom"].toString().toLatin1());
  d.diskHash = QByteArray::fromHex(o["hash"].toString().toLatin1());
  d.exists = o["exists"].toBool();
  // A backup is untrusted input: qUncompress allocates the size declared in
  // its 4-byte big-endian prefix before inflating anything.
  const QByteArray packed =
      QByteArray::fromBase64(o["backup"].toString().toLatin1());
  if (packed.size() >= 4) {
    const auto *u = reinterpret_cast<const uchar *>(packed.constData());
    const quint64 declared = (quint64(u[0]) << 24) | (quint64(u[1]) << 16) |
                             (quint64(u[2]) << 8) | quint64(u[3]);
    // deflate cannot expand more than ~1032:1, so larger claims are bogus.
    if (declared <= (quint64(1) << 30) &&
        declared <= quint64(packed.size()) * 1100 + 1024)
      d.utf8 = qUncompress(packed);
  }
  return d;
}
