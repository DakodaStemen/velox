// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Velox Authors
#pragma once
#include "ScintillaEditBase.h"
#include "document.h"
#include <QtWidgets>
#include <QtXml>
struct Language {
  QString name, lexer;
  QStringList extensions;
  QList<QByteArray> keywords;
  QDomElement udl; // set for user-defined (UDL) languages
};
QString dataPath(const QString &file);
QString configPath(const QString &file = {});
class EditorTab : public QWidget {
public:
  explicit EditorTab(QWidget *parent = nullptr);
  DocumentData data;
  ScintillaEditBase *primary, *secondary, *focused;
  QSplitter *split;
  QString language = "normal";
  int untitled = 0;
  QByteArray text() const;
  void setText(const QByteArray &text, bool dirty = false);
  bool metadataDirty = false;
  bool dirty() const;
  void toggleSplit();
  QString title() const;
};
class Window : public QMainWindow {
public:
  // `shared` is the primary instance: it owns recovery.json and adopts the
  // recovery files of crashed --new-instance windows.
  explicit Window(bool testing = false, bool shared = true);
  EditorTab *current() const;
  EditorTab *newDocument(const QByteArray &text = {});
  bool openFile(const QString &path, const QString &fallback = {});
  bool save(EditorTab *tab, bool saveAs = false);
  bool closeTab(int index);
  void applyLanguage(EditorTab *tab, const QString &language);
  void applyTheme(EditorTab *tab);
  bool find(bool backwards = false);
  int replaceAll();
  int markAll();
  void clearMarks();
  int findInFiles(const QString &folder, const QString &filters);
  void openResult(QTreeWidgetItem *item);
  void checkDisk();
  void snapshot();
  void restore();
  QStringList adoptOrphans(const QString &directory);
  static bool pidAlive(qint64 pid);
  static qint64 recoveryPid(const QString &fileName); // 0 when not ours
  void refresh();
  QTabWidget *tabs;
  QLineEdit *query, *replacement, *folder, *filters;
  QCheckBox *matchCase, *wholeWord, *regex;
  QComboBox *languageChoice, *encodingChoice, *eolChoice, *themeChoice;
  QMap<QString, Language> languages;
  QString sessionPath;
  bool shared = true;
  QStringList *deferredReports = nullptr; // collects report() text
  bool testing;
  bool recording = false;
  bool suppressMacro = false; // editor-internal moves must not be recorded
  QList<QVector<qintptr>> macro;
  QList<QByteArray> macroText;
  QStringList recent;
  QTreeWidget *results;

protected:
  void closeEvent(QCloseEvent *) override;
  void dragEnterEvent(QDragEnterEvent *) override;
  void dropEvent(QDropEvent *) override;

private:
  void buildMenus();
  void loadLanguages();
  void chooseOpen();
  void chooseFolder();
  enum SearchMode { FindMode, ReplaceMode, FilesMode };
  void showSearch(SearchMode mode = FindMode);
  void applyUserLanguage(EditorTab *tab, const QDomElement &udl);
  bool regexFailed(ScintillaEditBase *e);
  void replaceOne();
  void goToLine();
  void bookmark(bool next = false, bool previous = false);
  void rebuildOutline();
  void playMacro();
  bool restoreSession(const QString &path);
  void saveSession(const QString &path);
  bool canClose(EditorTab *tab);
  void report(const QString &message);
  QAction *action(QMenu *menu, const QString &name,
                  const QKeySequence &shortcut, std::function<void()> fn,
                  bool checkable = false);
  void send(unsigned int message, uptr_t w = 0, sptr_t l = 0);
  QStringList completions(const QString &language);
  uptr_t searchFlags(ScintillaEditBase *e, sptr_t from,
                     uptr_t emptyMatch) const;
  QWidget *searchPanel, *replaceRow, *filesRow;
  QLabel *status;
  QDockWidget *filesDock, *outlineDock, *resultsDock;
  ScintillaEditBase *scanner = nullptr;
  QHash<QString, QStringList> completionCache;
  QFileSystemModel *filesModel;
  QTreeView *files;
  QListWidget *outline;
  QTimer recoveryTimer, outlineTimer, diskTimer;
  QSettings settings;
  QSet<EditorTab *> dismissedChanges;
  QMenu *recentMenu;
  int counter = 1;
};
