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
// Files above this size open without highlighting, folding or the outline.
constexpr qint64 largeFileSize = 32 * 1024 * 1024;
// Scintilla's text copies here use int lengths, so stay well below 2 GiB.
constexpr qint64 maxFileSize = 1024LL * 1024 * 1024;
QString dataPath(const QString &file);
QString configPath(const QString &file = {});
// User-adjustable options, stored in settings.ini and edited in the
// Preferences dialog.
struct Preferences {
  QString font = "DejaVu Sans Mono";
  int fontSize = 11, tabWidth = 4;
  bool spaces = true, lineNumbers = true, caretLine = true, wrap = false,
       whitespace = false, autoIndent = true, completion = true,
       trimOnSave = false, finalNewline = false;
  void load(const QSettings &s);
  void store(QSettings &s) const;
};
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
  bool large = false; // no highlighting, folding or outline; see largeFileSize
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
  void reloadLanguages();
  void editUserLanguage(const QString &name = {});
  void applyPreferences(EditorTab *tab);
  void showPreferences();
  Preferences prefs;
  bool openSpec(const QString &spec); // "path", "path:line" or "path:line:col"
  void trimTrailingWhitespace(EditorTab *tab);
  void ensureFinalNewline(EditorTab *tab);
  bool toggleComment();
  enum SortOrder { Ascending, Descending, IgnoreCase };
  int sortLines(SortOrder order);
  int removeDuplicateLines();
  void joinLines();
  void showCommandPalette();
  void showQuickOpen();
  QList<QPair<QAction *, QString>> menuActions; // every menu QAction + menu path
  QString workspaceRoot;                         // folder chosen with Open folder
  QString quickOpenRoot() const;
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
  void rebuildOutline();
  void stopSearch();
  QListWidget *outline;
  QDockWidget *outlineDock;
  QPushButton *findFilesButton = nullptr, *stopButton = nullptr;
  int outlineBuilds = 0;   // how many times the outline was really rebuilt
  int sessionWrites = 0;   // how many times recovery data hit the disk
  bool searching = false;  // Find in files is running (re-entrancy guard)
  bool cancelSearch = false;
  QTabWidget *tabs;
  QLineEdit *query, *replacement, *folder, *filters;
  QCheckBox *matchCase, *wholeWord, *regex;
  QComboBox *filesEncoding, *languageChoice, *encodingChoice, *eolChoice, *themeChoice;
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
  void playMacro();
  bool restoreSession(const QString &path);
  void saveSession(const QString &path, bool onlyIfChanged = false);
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
  QDockWidget *filesDock, *resultsDock;
  bool outlineStale = false;
  QByteArray lastSessionHash;
  ScintillaEditBase *scanner = nullptr;
  QHash<QString, QStringList> completionCache;
  QFileSystemModel *filesModel;
  QTreeView *files;
  QTimer recoveryTimer, outlineTimer, diskTimer;
  QSettings settings;
  QSet<EditorTab *> dismissedChanges;
  QMenu *recentMenu;
  int counter = 1;
};
