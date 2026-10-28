// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Velox Authors
#pragma once
#include <QtCore>
enum class ReadError { None, Other, InvalidEncoding };
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
  bool write(const QByteArray &text, const QString &destination, QString &error,
             bool overwriteChanged = false);
  void stamp();
  // `text` is only used (and only needs to be fetched) when needsBackup().
  bool needsBackup(bool dirty) const { return dirty || path.isEmpty(); }
  QJsonObject json(bool dirty, const QByteArray &text = {}) const;
  static DocumentData fromJson(const QJsonObject &o);
};
