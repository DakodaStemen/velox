// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Velox Authors
#pragma once
#include <QtWidgets>

// Fuzzy subsequence score of `needle` within `haystack` (case-insensitive).
// Returns -1 when `needle` is not a subsequence; higher is a better match.
// Consecutive characters, word starts and a match at the very beginning score
// extra; an empty needle matches everything with score 0.
int fuzzyScore(const QString &needle, const QString &haystack);

// Files under `root` for quick open (relative paths, sorted), skipping VCS,
// dependency and build directories. Stops after `limit` files.
QStringList listProjectFiles(const QString &root, int limit = 50000);

struct PaletteItem {
  QString title;  // searched and shown
  QString detail; // right-aligned hint (menu path, shortcut, directory)
  QVariant data;  // returned to the caller
  bool enabled = true;
};

// A small modal-less popup: type to filter, Up/Down to move, Enter to choose,
// Escape to dismiss.
class PaletteDialog : public QDialog {
  Q_OBJECT
public:
  PaletteDialog(const QString &placeholder, QWidget *parent = nullptr);
  void setItems(const QList<PaletteItem> &items);
  // Rows currently visible, best match first.
  QStringList visibleTitles() const;
  void setFilter(const QString &text);
  bool chooseCurrent(); // emits chosen() for the highlighted row
  QLineEdit *input() const { return edit; }
signals:
  void chosen(const QVariant &data);

protected:
  bool eventFilter(QObject *o, QEvent *e) override;

private:
  void refilter();
  QLineEdit *edit;
  QListWidget *list;
  QList<PaletteItem> all;
};
