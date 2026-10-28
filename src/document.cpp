// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Velox Authors
#include "document.h"
#include <sys/stat.h>
static QByteArray hash(const QByteArray &b) {
  return QCryptographicHash::hash(b, QCryptographicHash::Sha256);
}
bool DocumentData::read(const QString &name, DocumentData &out, QString &error,
                        const QString &fallback, ReadError *kind) {
  if (kind)
    *kind = ReadError::Other;
  // Checked before opening: opening a FIFO blocks, and devices never end.
  struct stat st{};
  if (::stat(QFile::encodeName(name).constData(), &st) == 0 &&
      !S_ISREG(st.st_mode)) {
    error = S_ISDIR(st.st_mode) ? "This is a folder, not a file."
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
  result.diskHash = hash(raw);
  if (raw.startsWith(QByteArray::fromHex("fffe0000")) ||
      raw.startsWith(QByteArray::fromHex("0000feff"))) {
    error = "UTF-32 files are not supported yet. Convert a copy to UTF-8 "
            "before opening.";
    return false;
  }
  if (raw.startsWith(QByteArray::fromHex("efbbbf")))
    result.bom = raw.left(3);
  else if (raw.startsWith(QByteArray::fromHex("fffe"))) {
    result.encoding = "UTF-16LE";
    result.bom = raw.left(2);
  } else if (raw.startsWith(QByteArray::fromHex("feff"))) {
    result.encoding = "UTF-16BE";
    result.bom = raw.left(2);
  } else if (!fallback.isEmpty())
    result.encoding = fallback;
  QTextCodec *codec = QTextCodec::codecForName(result.encoding.toLatin1());
  if (!codec) {
    error = "Unknown encoding: " + result.encoding;
    return false;
  }
  QTextCodec::ConverterState state(QTextCodec::IgnoreHeader);
  QByteArray body = raw.mid(result.bom.size());
  QString text = codec->toUnicode(body.constData(), body.size(), &state);
  if (state.invalidChars || state.remainingChars) {
    error = "This file is not valid " + result.encoding +
            ". Choose a legacy encoding to open it without replacing bytes.";
    if (kind)
      *kind = ReadError::InvalidEncoding;
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
bool DocumentData::write(const QByteArray &text, const QString &name,
                         QString &error, bool overwriteChanged) {
  const QString absolute = QFileInfo(name).absoluteFilePath();
  QFileInfo info(absolute);
  QString destination = info.isSymLink() ? info.canonicalFilePath() : absolute;
  if (destination.isEmpty()) {
    error = "The symbolic link has no existing target. Choose Save As.";
    return false;
  }
  struct stat st{};
  const auto native = QFile::encodeName(destination);
  if (::stat(native.constData(), &st) == 0) {
    if (!S_ISREG(st.st_mode)) {
      error = "The destination is not a regular file.";
      return false;
    }
    if (st.st_nlink > 1) {
      error = "This file has hard links. Use Save As to avoid breaking their "
              "shared identity.";
      return false;
    }
    if (!(st.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH))) {
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
  QTextCodec *codec = QTextCodec::codecForName(encoding.toLatin1());
  if (!codec) {
    error = "Unknown encoding: " + encoding;
    return false;
  }
  QString unicode = QString::fromUtf8(text);
  QTextCodec::ConverterState state(QTextCodec::IgnoreHeader);
  QByteArray bytes =
      codec->fromUnicode(unicode.constData(), unicode.size(), &state);
  if (state.invalidChars) {
    error = "Some characters cannot be represented in " + encoding +
            ". Select UTF-8 or UTF-16 before saving.";
    return false;
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
QJsonObject DocumentData::json(const QByteArray &text, bool dirty) const {
  QJsonObject o{{"path", path},
                {"encoding", encoding},
                {"bom", QString::fromLatin1(bom.toHex())},
                {"hash", QString::fromLatin1(diskHash.toHex())},
                {"exists", exists},
                {"dirty", dirty}};
  if (dirty || path.isEmpty())
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
  d.utf8 =
      qUncompress(QByteArray::fromBase64(o["backup"].toString().toLatin1()));
  return d;
}
