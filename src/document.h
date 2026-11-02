// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Velox Authors
#pragma once
#include <QtCore>
enum class ReadError { None, Other, InvalidEncoding };
// Largest file DocumentData::read loads. Scintilla's text copies use int
// lengths, so stay well below 2 GiB.
constexpr qint64 maxFileSize = 1024LL * 1024 * 1024;
struct DocumentData {
  QString path;
  QString encoding = "UTF-8";
  QByteArray bom;
  QByteArray diskHash;
  QDateTime diskTime; // cheap change check before re-hashing
  qint64 diskSize = -1;
  QByteArray utf8;
  bool exists = false;
  static bool read(const QString &path, DocumentData &out, QString &error,
                   const QString &fallback = {}, ReadError *kind = nullptr,
                   bool computeHash = true);
  // `conflict` (optional) is set when the only reason for refusing is that
  // the file changed or disappeared on disk since it was loaded; the caller
  // may then ask the user and retry with overwriteChanged.
  bool write(const QByteArray &text, const QString &destination, QString &error,
             bool overwriteChanged = false, bool *conflict = nullptr);
  void stamp();
  // True when `file` (default: path) is still a regular file with the
  // contents loaded or last saved. Never opens FIFOs or devices and reads at
  // most the loaded size (maxFileSize when that is unknown).
  bool unchangedOnDisk(const QString &file = {}) const;
  // `text` is only used (and only needs to be fetched) when needsBackup().
  bool needsBackup(bool dirty) const { return dirty || path.isEmpty(); }
  QJsonObject json(bool dirty, const QByteArray &text = {}) const;
  static DocumentData fromJson(const QJsonObject &o);
};
