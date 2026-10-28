// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Velox Authors
#include "palette.h"

int fuzzyScore(const QString &needle, const QString &haystack) {
  if (needle.isEmpty())
    return 0;
  const QString n = needle.toCaseFolded(), h = haystack.toCaseFolded();
  int score = 0, hi = 0, previous = -2;
  for (int ni = 0; ni < n.size(); ++ni) {
    if (n[ni].isSpace())
      continue;
    int found = h.indexOf(n[ni], hi);
    if (found < 0)
      return -1;
    score += 1;
    if (found == previous + 1)
      score += 5; // consecutive
    if (found == 0 || !h[found - 1].isLetterOrNumber())
      score += 4; // start of a word
    if (found == 0)
      score += 2;
    previous = found;
    hi = found + 1;
  }
  // Shorter candidates beat longer ones with the same letters.
  return score * 100 - qMin(haystack.size(), 99);
}

QStringList listProjectFiles(const QString &root, int limit) {
  static const QSet<QString> skip{".git", ".hg",  ".svn",         "node_modules",
                                  "build", "dist", "target",      "__pycache__",
                                  ".cache", ".venv", "venv"};
  QStringList out;
  const QDir base(root);
  std::function<void(const QDir &, int)> walk = [&](const QDir &dir, int depth) {
    if (out.size() >= limit || depth > 12)
      return;
    const auto entries = dir.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks,
        QDir::Name);
    for (const QFileInfo &fi : entries) {
      if (out.size() >= limit)
        return;
      if (fi.isDir()) {
        if (!skip.contains(fi.fileName()))
          walk(QDir(fi.absoluteFilePath()), depth + 1);
      } else {
        out << base.relativeFilePath(fi.absoluteFilePath());
      }
    }
  };
  walk(base, 0);
  out.sort();
  return out;
}

PaletteDialog::PaletteDialog(const QString &placeholder, QWidget *parent)
    : QDialog(parent, Qt::Popup | Qt::FramelessWindowHint) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(6, 6, 6, 6);
  edit = new QLineEdit(this);
  edit->setPlaceholderText(placeholder);
  list = new QListWidget(this);
  list->setFocusPolicy(Qt::NoFocus);
  list->setUniformItemSizes(true);
  layout->addWidget(edit);
  layout->addWidget(list);
  resize(620, 360);
  if (parent) // top-centre of the parent window
    move(parent->mapToGlobal(QPoint((parent->width() - width()) / 2, 40)));
  edit->installEventFilter(this);
  connect(edit, &QLineEdit::textChanged, this, [this] { refilter(); });
  connect(list, &QListWidget::itemActivated, this, [this] { chooseCurrent(); });
  connect(list, &QListWidget::itemClicked, this, [this] { chooseCurrent(); });
  setAttribute(Qt::WA_DeleteOnClose);
}

void PaletteDialog::setItems(const QList<PaletteItem> &items) {
  all = items;
  refilter();
}

void PaletteDialog::setFilter(const QString &text) { edit->setText(text); }

QStringList PaletteDialog::visibleTitles() const {
  QStringList titles;
  for (int i = 0; i < list->count(); ++i)
    titles << list->item(i)->data(Qt::UserRole + 1).toString();
  return titles;
}

void PaletteDialog::refilter() {
  const QString q = edit->text().trimmed();
  QList<QPair<int, int>> scored; // score, index
  for (int i = 0; i < all.size(); ++i) {
    // Match against title and, for files, the detail (directory) as well.
    int s = fuzzyScore(q, all[i].title);
    if (s < 0 && !all[i].detail.isEmpty()) {
      s = fuzzyScore(q, all[i].title + " " + all[i].detail);
      if (s > 0)
        s /= 2;
    }
    if (s >= 0)
      scored.append({s, i});
  }
  std::stable_sort(scored.begin(), scored.end(),
                   [](const auto &a, const auto &b) { return a.first > b.first; });
  list->clear();
  const int maxRows = 200;
  for (int k = 0; k < scored.size() && k < maxRows; ++k) {
    const PaletteItem &it = all[scored[k].second];
    auto *row = new QListWidgetItem(
        it.detail.isEmpty() ? it.title : it.title + "    —  " + it.detail, list);
    row->setData(Qt::UserRole, scored[k].second);
    row->setData(Qt::UserRole + 1, it.title);
    if (!it.enabled)
      row->setFlags(row->flags() & ~Qt::ItemIsEnabled);
  }
  for (int i = 0; i < list->count(); ++i)
    if (list->item(i)->flags() & Qt::ItemIsEnabled) {
      list->setCurrentRow(i);
      break;
    }
}

bool PaletteDialog::chooseCurrent() {
  auto *row = list->currentItem();
  if (!row || !(row->flags() & Qt::ItemIsEnabled))
    return false;
  const QVariant data = all[row->data(Qt::UserRole).toInt()].data;
  close();
  emit chosen(data);
  return true;
}

bool PaletteDialog::eventFilter(QObject *o, QEvent *e) {
  if (o == edit && e->type() == QEvent::KeyPress) {
    auto *k = static_cast<QKeyEvent *>(e);
    const int step = k->key() == Qt::Key_Down ? 1 : k->key() == Qt::Key_Up ? -1 : 0;
    if (step) {
      int r = list->currentRow() + step;
      while (r >= 0 && r < list->count() &&
             !(list->item(r)->flags() & Qt::ItemIsEnabled))
        r += step;
      if (r >= 0 && r < list->count())
        list->setCurrentRow(r);
      return true;
    }
    if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) {
      chooseCurrent();
      return true;
    }
    if (k->key() == Qt::Key_Escape) {
      close();
      return true;
    }
  }
  return QDialog::eventFilter(o, e);
}
