// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Velox Authors
// Portions adapted from GPL-3.0-or-later code; see THIRD_PARTY_NOTICES.md.
#include "window.h"
#include "palette.h"
#include "udleditor.h"
#include "ILexer.h"
#include "Lexilla.h"
#if defined(Q_OS_WIN)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <sys/types.h>
#endif
#include "SciLexer.h"
#include "BoostRegexSearch.h"
#include <QScopedValueRollback>
#include <QTextCodec> // Qt 6: provided by Core5Compat
#include <algorithm>
// Legacy encodings offered for opening and saving; unavailable codecs are
// dropped so the list never contains a name Qt cannot convert.
static QStringList legacyEncodings() {
  QStringList out;
  for (const char *n : {"Windows-1252", "ISO-8859-1", "ISO-8859-15",
                        "Shift-JIS", "GB18030", "EUC-KR", "Big5", "KOI8-R",
                        "Windows-1251"})
    if (QTextCodec::codecForName(n))
      out << n;
  return out;
}
static int colour(const QColor &c) {
  return c.red() | (c.green() << 8) | (c.blue() << 16);
}
static QDomDocument xml(const QString &path) {
  QFile f(path);
  QDomDocument d;
  if (f.open(QIODevice::ReadOnly))
    d.setContent(&f);
  return d;
}
static constexpr int markIndicator = 31;
// Installed builds keep data in <prefix>/share/velox; development builds run
// from build/app/bin next to the repository's share directory.
QString dataPath(const QString &file) {
  static const QString root = [] {
    const QString app = QCoreApplication::applicationDirPath();
    for (const QString &dir :
         {qEnvironmentVariable("VELOX_DATA_DIR"), app + "/../share/velox",
          app + "/../Resources/share/velox", // macOS app bundle
          app + "/../../../share",           // qmake build/app/bin
#ifdef VELOX_SOURCE_SHARE_DIR
          QStringLiteral(VELOX_SOURCE_SHARE_DIR), // CMake build tree
#endif
         })
      if (!dir.isEmpty() && QFile::exists(dir + "/langs.model.xml"))
        return QDir::cleanPath(dir);
    return QDir::cleanPath(app + "/../share/velox");
  }();
  return root + '/' + file;
}
QString configPath(const QString &file) {
  return QStandardPaths::writableLocation(
             QStandardPaths::GenericConfigLocation) +
         "/velox/" + file;
}
static QDomDocument themeDocument(const QString &theme) {
  static QHash<QString, QDomDocument> cache;
  auto it = cache.find(theme);
  if (it == cache.end())
    it = cache.insert(theme, xml(dataPath(theme == "Light"
                                              ? "stylers.model.xml"
                                              : "themes/" + theme + ".xml")));
  return *it;
}
static QColor themeDefault(const QDomDocument &d, const QString &attribute,
                           const QColor &fallback) {
  auto widgets = d.elementsByTagName("WidgetStyle");
  for (int i = 0; i < widgets.size(); ++i) {
    auto n = widgets.at(i).toElement();
    if (n.attribute("name") == "Default Style" &&
        !n.attribute(attribute).isEmpty())
      return QColor('#' + n.attribute(attribute));
  }
  return fallback;
}
// UDL keyword quoting rules expected by LexUser: quoted phrases
// keep inner spaces, encoded as \v (double quotes) or \b (single quotes).
static QByteArray udlKeywords(const QByteArray &in) {
  QByteArray out;
  bool dq = false, sq = false, nonWs = false;
  auto at = [&in](int j) { return j >= 0 && j < in.size() ? in[j] : '\0'; };
  for (int j = 0; j < in.size(); ++j) {
    const char c = in[j];
    if (!sq && c == '"') {
      dq = !dq;
      continue;
    }
    if (!dq && c == '\'') {
      sq = !sq;
      continue;
    }
    if (c == '\\' && (at(j + 1) == '"' || at(j + 1) == '\'' || at(j + 1) == '\\')) {
      out += in[++j];
      continue;
    }
    if (!dq && !sq)
      out += c;
    else if (uchar(c) > ' ') {
      out += c;
      nonWs = true;
    } else if (nonWs && at(j - 1) != '"' && at(j + 1) != '"' &&
               uchar(at(j + 1)) > ' ')
      out += dq ? '\v' : '\b';
  }
  return out;
}
static ScintillaEditBase *makeEditor(QWidget *parent) {
  auto *e = new ScintillaEditBase(parent);
  e->send(SCI_SETCODEPAGE, SC_CP_UTF8);
  e->send(SCI_SETMULTIPLESELECTION, 1);
  e->send(SCI_SETADDITIONALSELECTIONTYPING, 1);
  e->send(SCI_SETMULTIPASTE, SC_MULTIPASTE_EACH);
  e->send(SCI_SETRECTANGULARSELECTIONMODIFIER, SCMOD_ALT);
  e->send(SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
  e->send(SCI_SETMARGINWIDTHN, 0, 60);
  e->send(SCI_SETMARGINTYPEN, 1, SC_MARGIN_SYMBOL);
  e->send(SCI_SETMARGINWIDTHN, 1, 18);
  e->send(SCI_SETMARGINMASKN, 1, 1);
  e->send(SCI_SETMARGINSENSITIVEN, 1, 1);
  e->send(SCI_MARKERDEFINE, 0, SC_MARK_CIRCLE);
  e->send(SCI_MARKERSETBACK, 0, colour(QColor("#4aa8f5")));
  e->send(SCI_SETMARGINTYPEN, 2, SC_MARGIN_SYMBOL);
  e->send(SCI_SETMARGINWIDTHN, 2, 17);
  e->send(SCI_SETMARGINMASKN, 2, SC_MASK_FOLDERS);
  e->send(SCI_SETMARGINSENSITIVEN, 2, 1);
  for (int n = SC_MARKNUM_FOLDEREND; n <= SC_MARKNUM_FOLDEROPEN; ++n)
    e->send(SCI_MARKERDEFINE, n, SC_MARK_BOXPLUS);
  e->send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPEN, SC_MARK_BOXMINUS);
  e->send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPENMID, SC_MARK_BOXMINUS);
  e->send(SCI_SETAUTOMATICFOLD, SC_AUTOMATICFOLD_SHOW | SC_AUTOMATICFOLD_CLICK |
                                    SC_AUTOMATICFOLD_CHANGE);
  e->sends(SCI_SETPROPERTY, reinterpret_cast<uptr_t>("fold"), "1");
  e->send(SCI_SETTABWIDTH, 4);
  e->send(SCI_SETINDENT, 4);
  e->send(SCI_SETUSETABS, 0);
  e->send(SCI_SETINDENTATIONGUIDES, SC_IV_LOOKBOTH);
  e->send(SCI_SETSCROLLWIDTHTRACKING, 1);
  e->send(SCI_SETSCROLLWIDTH, 1);
  e->send(SCI_SETCARETLINEVISIBLE, 1);
  e->send(SCI_AUTOCSETIGNORECASE, 1);
  e->send(SCI_SETENDATLASTLINE, 0);
  e->send(SCI_SETMOUSEDWELLTIME, 500);
  e->send(SCI_SETEOLMODE, SC_EOL_LF);
  e->send(SCI_INDICSETSTYLE, markIndicator, INDIC_ROUNDBOX);
  e->send(SCI_INDICSETFORE, markIndicator, colour(QColor("#e1ad67")));
  e->send(SCI_INDICSETALPHA, markIndicator, 90);
  e->send(SCI_INDICSETOUTLINEALPHA, markIndicator, 200);
  e->send(SCI_INDICSETUNDER, markIndicator, 1);
  return e;
}
void Preferences::load(const QSettings &s) {
  const Preferences d;
  font = s.value("editor/font", d.font).toString();
  fontSize = qBound(6, s.value("editor/fontSize", d.fontSize).toInt(), 72);
  tabWidth = qBound(1, s.value("editor/tabWidth", d.tabWidth).toInt(), 16);
  spaces = s.value("editor/spaces", d.spaces).toBool();
  lineNumbers = s.value("editor/lineNumbers", d.lineNumbers).toBool();
  caretLine = s.value("editor/caretLine", d.caretLine).toBool();
  wrap = s.value("editor/wrap", d.wrap).toBool();
  whitespace = s.value("editor/whitespace", d.whitespace).toBool();
  autoIndent = s.value("editor/autoIndent", d.autoIndent).toBool();
  completion = s.value("editor/completion", d.completion).toBool();
  trimOnSave = s.value("save/trimTrailingWhitespace", d.trimOnSave).toBool();
  finalNewline = s.value("save/finalNewline", d.finalNewline).toBool();
}
void Preferences::store(QSettings &s) const {
  s.setValue("editor/font", font);
  s.setValue("editor/fontSize", fontSize);
  s.setValue("editor/tabWidth", tabWidth);
  s.setValue("editor/spaces", spaces);
  s.setValue("editor/lineNumbers", lineNumbers);
  s.setValue("editor/caretLine", caretLine);
  s.setValue("editor/wrap", wrap);
  s.setValue("editor/whitespace", whitespace);
  s.setValue("editor/autoIndent", autoIndent);
  s.setValue("editor/completion", completion);
  s.setValue("save/trimTrailingWhitespace", trimOnSave);
  s.setValue("save/finalNewline", finalNewline);
}
EditorTab::EditorTab(QWidget *parent) : QWidget(parent) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  split = new QSplitter(this);
  layout->addWidget(split);
  primary = makeEditor(split);
  secondary = makeEditor(split);
  secondary->send(SCI_SETDOCPOINTER, 0, primary->send(SCI_GETDOCPOINTER));
  secondary->hide();
  focused = primary;
  for (auto *e : {primary, secondary})
    connect(e, &ScintillaEditBase::focusChanged, this, [this, e](bool f) {
      if (f)
        focused = e;
    });
}
QByteArray EditorTab::text() const {
  QByteArray b(int(primary->send(SCI_GETLENGTH)) + 1, '\0');
  primary->send(SCI_GETTEXT, b.size(), reinterpret_cast<sptr_t>(b.data()));
  b.chop(1);
  return b;
}
void EditorTab::setText(const QByteArray &b, bool dirty) {
  primary->send(SCI_CLEARALL);
  primary->send(SCI_ADDTEXT, b.size(), reinterpret_cast<sptr_t>(b.constData()));
  primary->send(SCI_EMPTYUNDOBUFFER);
  primary->send(SCI_SETSAVEPOINT);
  metadataDirty = dirty;
}
bool EditorTab::dirty() const {
  return metadataDirty || primary->send(SCI_GETMODIFY);
}
void EditorTab::toggleSplit() {
  secondary->setVisible(secondary->isHidden());
  split->setSizes({500, 500});
}
QString EditorTab::title() const {
  return (data.path.isEmpty() ? QString("new %1").arg(untitled)
                              : QFileInfo(data.path).fileName()) +
         (dirty() ? " •" : "");
}

bool Window::pidAlive(qint64 pid) {
  if (pid <= 0)
    return false;
#if defined(Q_OS_WIN)
  HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
  if (!h)
    return GetLastError() == ERROR_ACCESS_DENIED; // exists, just not ours
  DWORD code = 0;
  const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
  CloseHandle(h);
  return alive;
#else
  // Signal 0 only checks existence; EPERM means the process exists.
  return ::kill(pid_t(pid), 0) == 0 || errno == EPERM;
#endif
}
qint64 Window::recoveryPid(const QString &fileName) {
  static const QRegularExpression expr("^recovery-(\\d+)\\.json$");
  auto m = expr.match(fileName);
  return m.hasMatch() ? m.captured(1).toLongLong() : 0;
}

Window::Window(bool test, bool sharedInstance)
    : testing(test), shared(sharedInstance), settings(configPath("settings.ini"), QSettings::IniFormat) {
  setWindowTitle("Velox");
  resize(1300, 850);
  setAcceptDrops(true);
  // Each --new-instance window snapshots into its own file so windows cannot
  // overwrite one another's unsaved text.
  sessionPath =
      QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
      (shared ? QString("/velox/recovery.json")
              : QString("/velox/recovery-%1.json")
                    .arg(QCoreApplication::applicationPid()));
  QDir().mkpath(QFileInfo(sessionPath).absolutePath());
  loadLanguages();
  auto *center = new QWidget;
  auto *layout = new QVBoxLayout(center);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  tabs = new QTabWidget;
  tabs->setDocumentMode(true);
  tabs->setMovable(true);
  tabs->setTabsClosable(true);
  layout->addWidget(tabs, 1);
  setCentralWidget(center);
  searchPanel = new QWidget;
  auto *search = new QVBoxLayout(searchPanel);
  search->setContentsMargins(12, 9, 12, 9);
  query = new QLineEdit;
  query->setPlaceholderText("Find…");
  replacement = new QLineEdit;
  replacement->setPlaceholderText("Replace with…");
  folder = new QLineEdit(QDir::homePath());
  folder->setPlaceholderText("Folder to search");
  filters = new QLineEdit("*");
  filters->setPlaceholderText("Filters, e.g. *.cpp *.h");
  filters->setMaximumWidth(220);
  filesEncoding = new QComboBox;
  filesEncoding->addItem("UTF-8 / UTF-16 / UTF-32");
  filesEncoding->addItems(legacyEncodings());
  filesEncoding->setToolTip(
      "Encoding assumed for files without a byte order mark");
  matchCase = new QCheckBox("Match case");
  wholeWord = new QCheckBox("Whole word");
  regex = new QCheckBox("Regular expression");
  auto button = [this](QString text, std::function<void()> fn) {
    auto *b = new QPushButton(text);
    connect(b, &QPushButton::clicked, this, fn);
    return b;
  };
  auto row = [search](const QString &label, const QList<QWidget *> &widgets) {
    auto *w = new QWidget;
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    auto *name = new QLabel(label);
    name->setMinimumWidth(70);
    l->addWidget(name);
    for (auto *x : widgets)
      l->addWidget(x, qobject_cast<QLineEdit *>(x) ? 1 : 0);
    search->addWidget(w);
    return w;
  };
  auto hideSearch = [this] {
    searchPanel->hide();
    if (current())
      current()->focused->setFocus();
  };
  row("Find", {query, button("Previous", [this] { find(true); }),
               button("Next", [this] { find(); }),
               button("Mark all", [this] { markAll(); }),
               button("×", hideSearch)});
  replaceRow = row("Replace", {replacement,
                               button("Replace", [this] { replaceOne(); }),
                               button("Replace all", [this] { replaceAll(); })});
  filesRow = row("In folder", {folder, button("…",
                                              [this] {
                                                auto p = QFileDialog::getExistingDirectory(
                                                    this, "Search folder",
                                                    folder->text());
                                                if (!p.isEmpty())
                                                  folder->setText(p);
                                              }),
                               filters, filesEncoding,
                               findFilesButton = button("Find in files", [this] {
                                 findInFiles(folder->text(), filters->text());
                               }),
                               stopButton = button("Stop", [this] { stopSearch(); })});
  stopButton->setEnabled(false);
  auto *options = new QHBoxLayout;
  options->addSpacing(76);
  options->addWidget(matchCase);
  options->addWidget(wholeWord);
  options->addWidget(regex);
  options->addStretch();
  search->addLayout(options);
  layout->addWidget(searchPanel);
  searchPanel->hide();
  auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), searchPanel);
  escape->setContext(Qt::WidgetWithChildrenShortcut);
  connect(escape, &QShortcut::activated, this, [this, hideSearch] {
    if (searching)
      stopSearch();
    else
      hideSearch();
  });
  connect(query, &QLineEdit::returnPressed, this, [this] {
    if (filesRow->isVisible())
      findInFiles(folder->text(), filters->text());
    else
      find();
  });
  connect(replacement, &QLineEdit::returnPressed, this,
          [this] { replaceOne(); });
  filesDock = new QDockWidget("Folder workspace", this);
  filesDock->setObjectName("workspace");
  files = new QTreeView;
  filesModel = new QFileSystemModel(this);
  filesModel->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot);
  filesModel->setRootPath(QDir::homePath());
  files->setModel(filesModel);
  files->setRootIndex(filesModel->index(QDir::homePath()));
  for (int i = 1; i < 4; ++i)
    files->hideColumn(i);
  files->setHeaderHidden(true);
  filesDock->setWidget(files);
  addDockWidget(Qt::LeftDockWidgetArea, filesDock);
  filesDock->hide();
  connect(files, &QTreeView::doubleClicked, this, [this](const QModelIndex &i) {
    if (!filesModel->isDir(i))
      openFile(filesModel->filePath(i));
  });
  outlineDock = new QDockWidget("Document outline", this);
  outlineDock->setObjectName("outline");
  outline = new QListWidget;
  outlineDock->setWidget(outline);
  addDockWidget(Qt::RightDockWidgetArea, outlineDock);
  outlineDock->hide();
  auto showOutline = [this](bool visible) {
    if (visible && outlineStale)
      rebuildOutline();
  };
  connect(outlineDock, &QDockWidget::visibilityChanged, this, showOutline);
  connect(outlineDock->toggleViewAction(), &QAction::toggled, this, showOutline);
  connect(outline, &QListWidget::itemActivated, this,
          [this](QListWidgetItem *i) {
            send(SCI_GOTOLINE, i->data(Qt::UserRole).toInt());
          });
  resultsDock = new QDockWidget("Search results", this);
  resultsDock->setObjectName("results");
  results = new QTreeWidget;
  results->setHeaderHidden(true);
  results->setUniformRowHeights(true);
  resultsDock->setWidget(results);
  addDockWidget(Qt::BottomDockWidgetArea, resultsDock);
  resultsDock->hide();
  connect(results, &QTreeWidget::itemActivated, this, &Window::openResult);
  status = new QLabel;
  statusBar()->addWidget(status, 1);
  languageChoice = new QComboBox;
  languageChoice->setToolTip("Syntax language");
  languageChoice->addItems(languages.keys());
  statusBar()->addPermanentWidget(languageChoice);
  encodingChoice = new QComboBox;
  encodingChoice->addItems({"UTF-8", "UTF-8 BOM", "UTF-16LE", "UTF-16BE",
                            "UTF-32LE", "UTF-32BE"});
  encodingChoice->addItems(legacyEncodings());
  encodingChoice->setToolTip("Encoding used when saving");
  statusBar()->addPermanentWidget(encodingChoice);
  eolChoice = new QComboBox;
  eolChoice->addItems({"LF", "CRLF", "CR"});
  eolChoice->setToolTip("Convert line endings");
  statusBar()->addPermanentWidget(eolChoice);
  themeChoice = new QComboBox;
  themeChoice->addItem("Light");
  QDir themes(dataPath("themes"));
  for (const auto &f : themes.entryList({"*.xml"}, QDir::Files))
    themeChoice->addItem(QFileInfo(f).completeBaseName());
  themeChoice->setCurrentText(
      settings.value("theme", "DarkModeDefault").toString());
  statusBar()->addPermanentWidget(themeChoice);
  connect(languageChoice, &QComboBox::currentTextChanged, this,
          [this](const QString &l) {
            if (current())
              applyLanguage(current(), l);
          });
  connect(themeChoice, &QComboBox::currentTextChanged, this, [this] {
    for (int i = 0; i < tabs->count(); ++i)
      applyTheme(static_cast<EditorTab *>(tabs->widget(i)));
  });
  connect(encodingChoice, &QComboBox::currentTextChanged, this,
          [this](const QString &name) {
            auto *t = current();
            if (!t)
              return;
            t->data.encoding = name == "UTF-8 BOM" ? "UTF-8" : name;
            t->data.bom = name == "UTF-8 BOM"  ? QByteArray::fromHex("efbbbf")
                          : name == "UTF-16LE" ? QByteArray::fromHex("fffe")
                          : name == "UTF-16BE" ? QByteArray::fromHex("feff")
                          : name == "UTF-32LE" ? QByteArray::fromHex("fffe0000")
                          : name == "UTF-32BE" ? QByteArray::fromHex("0000feff")
                                               : QByteArray();
            t->metadataDirty = true;
            refresh();
          });
  connect(eolChoice, qOverload<int>(&QComboBox::currentIndexChanged), this,
          [this](int i) {
            if (!current())
              return;
            int mode = i == 0 ? SC_EOL_LF : i == 1 ? SC_EOL_CRLF : SC_EOL_CR;
            send(SCI_CONVERTEOLS, mode);
            for (auto *e : {current()->primary, current()->secondary})
              e->send(SCI_SETEOLMODE, mode);
          });
  connect(tabs, &QTabWidget::tabCloseRequested, this,
          [this](int i) { closeTab(i); });
  connect(tabs, &QTabWidget::currentChanged, this, [this] {
    refresh();
    rebuildOutline();
  });
  connect(&recoveryTimer, &QTimer::timeout, this, &Window::snapshot);
  if (!testing)
    recoveryTimer.start(5000);
  outlineTimer.setSingleShot(true);
  connect(&outlineTimer, &QTimer::timeout, this, &Window::rebuildOutline);
  connect(&diskTimer, &QTimer::timeout, this, &Window::checkDisk);
  if (!testing)
    diskTimer.start(3000);
  recent = settings.value("recent").toStringList();
  prefs.load(settings);
  buildMenus();
  if (!testing)
    restoreGeometry(settings.value("geometry").toByteArray());
}
EditorTab *Window::current() const {
  return static_cast<EditorTab *>(tabs->currentWidget());
}
void Window::send(unsigned int msg, uptr_t w, sptr_t l) {
  if (current())
    current()->focused->send(msg, w, l);
}
// The Boost regex engine rejects empty matches (e.g. "^")
// unless an EMPTYMATCH mode is requested, and never inside a CRLF pair.
uptr_t Window::searchFlags(ScintillaEditBase *e, sptr_t from,
                           uptr_t emptyMatch) const {
  uptr_t flags = (matchCase->isChecked() ? SCFIND_MATCHCASE : 0) |
                 (wholeWord->isChecked() ? SCFIND_WHOLEWORD : 0);
  if (!regex->isChecked())
    return flags;
  flags |= SCFIND_REGEXP | SCFIND_REGEXP_SKIPCRLFASONE;
  if (from > 0 && e->send(SCI_GETCHARAT, from - 1) == '\r' &&
      e->send(SCI_GETCHARAT, from) == '\n')
    return flags;
  return flags | emptyMatch;
}
QAction *Window::action(QMenu *m, const QString &name, const QKeySequence &key,
                        std::function<void()> fn, bool check) {
  auto *a = m->addAction(name);
  a->setShortcut(key);
  a->setCheckable(check);
  connect(a, &QAction::triggered, this, fn);
  menuActions.append({a, m->title()});
  return a;
}
void Window::buildMenus() {
  auto *f = menuBar()->addMenu("&File");
  action(f, "&New", QKeySequence::New, [this] { newDocument(); });
  action(f, "&Open…", QKeySequence::Open, [this] { chooseOpen(); });
  action(f, "Open with encoding…", {}, [this] {
    QString p = QFileDialog::getOpenFileName(this, "Open with encoding");
    if (p.isEmpty())
      return;
    bool ok;
    QString enc = QInputDialog::getItem(
        this, "File encoding", "Decode as",
        legacyEncodings(), 0, false, &ok);
    if (ok)
      openFile(p, enc);
  });
  action(f, "Open folder…", QKeySequence("Ctrl+Shift+O"),
         [this] { chooseFolder(); });
  recentMenu = f->addMenu("Recent files");
  connect(recentMenu, &QMenu::aboutToShow, this, [this] {
    recentMenu->clear();
    for (const QString &p : recent)
      recentMenu->addAction(p, [this, p] { openFile(p); });
    if (recent.isEmpty())
      recentMenu->addAction("No recent files")->setEnabled(false);
  });
  f->addSeparator();
  action(f, "&Save", QKeySequence::Save, [this] { save(current()); });
  action(f, "Save &as…", QKeySequence::SaveAs,
         [this] { save(current(), true); });
  action(f, "Save all", QKeySequence("Ctrl+Alt+Shift+S"), [this] {
    for (int i = 0; i < tabs->count(); ++i) {
      auto *t = static_cast<EditorTab *>(tabs->widget(i));
      if (t->dirty() && !save(t))
        break;
    }
  });
  action(f, "Reload from disk", {}, [this] {
    auto *t = current();
    if (!t || t->data.path.isEmpty())
      return;
    if (t->dirty() &&
        QMessageBox::question(this, "Reload",
                              "Discard unsaved edits and reload?") !=
            QMessageBox::Yes)
      return;
    DocumentData d;
    QString error;
    if (DocumentData::read(t->data.path, d, error, t->data.encoding)) {
      QScopedValueRollback<bool> quiet(suppressMacro, true);
      t->data = d;
      t->large = d.utf8.size() > largeFileSize;
      t->setText(d.utf8);
      dismissedChanges.remove(t);
      applyLanguage(t, t->language);
      refresh();
    } else
      report(error);
  });
  f->addSeparator();
  action(f, "Save session…", {}, [this] {
    auto p = QFileDialog::getSaveFileName(this, "Save session", {},
                                          "Velox session (*.json)");
    if (!p.isEmpty())
      saveSession(p);
  });
  action(f, "Load session…", {}, [this] {
    auto p = QFileDialog::getOpenFileName(this, "Load session", {},
                                          "Velox session (*.json)");
    if (!p.isEmpty())
      restoreSession(p);
  });
  f->addSeparator();
  action(f, "Close tab", QKeySequence::Close,
         [this] { closeTab(tabs->currentIndex()); });
  action(f, "Quit", QKeySequence::Quit, [this] { close(); });
  auto *e = menuBar()->addMenu("&Edit");
  action(e, "Undo", QKeySequence::Undo, [this] { send(SCI_UNDO); });
  action(e, "Redo", QKeySequence::Redo, [this] { send(SCI_REDO); });
  e->addSeparator();
  action(e, "Cut", QKeySequence::Cut, [this] { send(SCI_CUT); });
  action(e, "Copy", QKeySequence::Copy, [this] { send(SCI_COPY); });
  action(e, "Paste", QKeySequence::Paste, [this] { send(SCI_PASTE); });
  action(e, "Select all", QKeySequence::SelectAll,
         [this] { send(SCI_SELECTALL); });
  e->addSeparator();
  action(e, "Duplicate line / selection", QKeySequence("Ctrl+D"),
         [this] { send(SCI_SELECTIONDUPLICATE); });
  action(e, "Delete line", QKeySequence("Ctrl+Shift+L"),
         [this] { send(SCI_LINEDELETE); });
  action(e, "Uppercase", QKeySequence("Ctrl+Shift+U"),
         [this] { send(SCI_UPPERCASE); });
  action(e, "Lowercase", QKeySequence("Ctrl+U"),
         [this] { send(SCI_LOWERCASE); });
  action(e, "Trim trailing whitespace", {},
         [this] { trimTrailingWhitespace(current()); });
  e->addSeparator();
  action(e, "Preferences…", QKeySequence("Ctrl+,"), [this] { showPreferences(); });
  e->addSeparator();
  action(e, "Toggle comment", QKeySequence("Ctrl+/"), [this] { toggleComment(); });
  action(e, "Move line up", QKeySequence("Alt+Up"),
         [this] { send(SCI_MOVESELECTEDLINESUP); });
  action(e, "Move line down", QKeySequence("Alt+Down"),
         [this] { send(SCI_MOVESELECTEDLINESDOWN); });
  action(e, "Join lines", QKeySequence("Ctrl+J"), [this] { joinLines(); });
  auto *lines = e->addMenu("Sort lines");
  action(lines, "Ascending", {}, [this] { sortLines(Ascending); });
  action(lines, "Descending", {}, [this] { sortLines(Descending); });
  action(lines, "Ascending, ignore case", {}, [this] { sortLines(IgnoreCase); });
  action(e, "Remove duplicate lines", {}, [this] { removeDuplicateLines(); });
  auto *s = menuBar()->addMenu("&Search");
  action(s, "Find…", QKeySequence::Find, [this] { showSearch(); });
  action(s, "Replace…", QKeySequence("Ctrl+H"),
         [this] { showSearch(ReplaceMode); });
  action(s, "Find in files…", QKeySequence("Ctrl+Shift+F"),
         [this] { showSearch(FilesMode); });
  action(s, "Find next", QKeySequence("F3"), [this] { find(); });
  action(s, "Find previous", QKeySequence("Shift+F3"), [this] { find(true); });
  action(s, "Go to line…", QKeySequence("Ctrl+G"), [this] { goToLine(); });
  s->addSeparator();
  action(s, "Mark all occurrences", {}, [this] {
    if (query->text().isEmpty())
      showSearch();
    else
      markAll();
  });
  action(s, "Clear marks", {}, [this] { clearMarks(); });
  s->addSeparator();
  action(s, "Toggle bookmark", QKeySequence("Ctrl+F2"), [this] { bookmark(); });
  action(s, "Next bookmark", QKeySequence("F2"), [this] { bookmark(true); });
  action(s, "Previous bookmark", QKeySequence("Shift+F2"),
         [this] { bookmark(false, true); });
  auto *v = menuBar()->addMenu("&View");
  action(v, "Split document", QKeySequence("Ctrl+Alt+S"), [this] {
    if (current())
      current()->toggleSplit();
  });
  action(
      v, "Word wrap", QKeySequence("Alt+Z"),
      [this] {
        auto *t = current();
        if (t) {
          int mode = t->focused->send(SCI_GETWRAPMODE) == SC_WRAP_NONE
                         ? SC_WRAP_WORD
                         : SC_WRAP_NONE;
          for (auto *ed : {t->primary, t->secondary})
            ed->send(SCI_SETWRAPMODE, mode);
        }
      },
      true)
      ->setChecked(prefs.wrap);
  action(
      v, "Show whitespace", {},
      [this] {
        auto *t = current();
        if (t) {
          int mode = t->focused->send(SCI_GETVIEWWS) == SCWS_INVISIBLE
                         ? SCWS_VISIBLEALWAYS
                         : SCWS_INVISIBLE;
          for (auto *ed : {t->primary, t->secondary})
            ed->send(SCI_SETVIEWWS, mode);
        }
      },
      true)
      ->setChecked(prefs.whitespace);
  action(v, "Zoom in", QKeySequence::ZoomIn, [this] { send(SCI_ZOOMIN); });
  action(v, "Zoom out", QKeySequence::ZoomOut, [this] { send(SCI_ZOOMOUT); });
  action(v, "Reset zoom", QKeySequence("Ctrl+0"),
         [this] { send(SCI_SETZOOM, 0); });
  v->addAction(filesDock->toggleViewAction());
  v->addAction(outlineDock->toggleViewAction());
  v->addAction(resultsDock->toggleViewAction());
  action(v, "Fullscreen", QKeySequence("F11"),
         [this] { isFullScreen() ? showNormal() : showFullScreen(); });
  auto *lang = menuBar()->addMenu("&Language");
  action(lang, "Define custom language…", {}, [this] { editUserLanguage(); });
  auto *editLanguage = lang->addMenu("Edit custom language");
  connect(editLanguage, &QMenu::aboutToShow, this, [this, editLanguage] {
    editLanguage->clear();
    for (const QString &name : UdlSpec::saved().keys())
      editLanguage->addAction(name, [this, name] { editUserLanguage(name); });
    if (editLanguage->isEmpty())
      editLanguage->addAction("No custom languages yet")->setEnabled(false);
  });
  auto *m = menuBar()->addMenu("&Macro");
  auto *rec = action(
      m, "Start / stop recording", QKeySequence("Ctrl+Shift+R"),
      [this] {
        recording = !recording;
        if (recording) {
          macro.clear();
          macroText.clear();
        }
        statusBar()->showMessage(recording ? "Recording editor actions…"
                                           : "Recording stopped",
                                 3000);
      },
      true);
  Q_UNUSED(rec);
  action(m, "Playback", QKeySequence("Ctrl+Alt+P"), [this] { playMacro(); });
  action(e, "Command palette…", QKeySequence("Ctrl+Shift+P"),
         [this] { showCommandPalette(); });
  action(f, "Quick open…", QKeySequence("Ctrl+P"), [this] { showQuickOpen(); });
  auto *h = menuBar()->addMenu("&Help");
  action(h, "Open configuration folder", {}, [] {
    QDir().mkpath(configPath("userDefineLangs"));
    QDesktopServices::openUrl(QUrl::fromLocalFile(configPath()));
  });
  action(h, "About Velox", {}, [this] {
    QMessageBox::about(
        this, "About Velox",
        QString("<h3>Velox %1</h3>"
                "<p>A fast native text editor for Linux.</p>"
                "<p>Add your own user-defined languages (UDL 2.x XML) to "
                "<code>%2</code>.</p>"
                "<p>Free software under the GNU General Public License, "
                "version 3 or later. Velox includes third-party components; "
                "see THIRD_PARTY_NOTICES.md for their copyrights and "
                "licenses.</p>")
            .arg(QCoreApplication::applicationVersion(),
                 configPath("userDefineLangs").toHtmlEscaped()));
  });
  auto *toolbar = addToolBar("Main");
  toolbar->setObjectName("main");
  toolbar->setMovable(false);
  for (int i : {0, 1, 5, 6})
    if (i < f->actions().size())
      toolbar->addAction(f->actions()[i]);
  toolbar->addSeparator();
  toolbar->addAction(s->actions()[0]);
  toolbar->addAction(v->actions()[0]);
}
void Window::loadLanguages() {
  QDomDocument d = xml(dataPath("langs.model.xml"));
  auto nodes = d.elementsByTagName("Language");
  for (int i = 0; i < nodes.size(); ++i) {
    auto n = nodes.at(i).toElement();
    Language l;
    l.name = n.attribute("name");
    l.extensions = n.attribute("ext").split(' ', Qt::SkipEmptyParts);
    l.lexer = l.name;
    QMap<QString, QString> aliases{{"c", "cpp"},
                                   {"java", "cpp"},
                                   {"javascript", "cpp"},
                                   {"typescript", "cpp"},
                                   {"cs", "cpp"},
                                   {"objective-c", "cpp"},
                                   {"html", "hypertext"},
                                   {"xml", "xml"},
                                   {"normal", "null"},
                                   {"ini", "props"},
                                   {"makefile", "makefile"},
                                   {"shell", "bash"},
                                   {"caml", "caml"},
                                   {"json", "json"},
                                   // Languages without a Lexilla lexer of
                                   // their own; "nfo" stays plain text.
                                   {"actionscript", "cpp"},
                                   {"go", "cpp"},
                                   {"swift", "cpp"},
                                   {"rc", "cpp"},
                                   {"javascript.js", "cpp"},
                                   {"php", "hypertext"},
                                   {"asp", "hypertext"},
                                   {"jsp", "hypertext"},
                                   {"scheme", "lisp"},
                                   {"postscript", "ps"},
                                   {"autoit", "au3"},
                                   {"fortran77", "f77"},
                                   {"baanc", "baan"},
                                   {"cobol", "COBOL"},
                                   {"json5", "json"},
                                   {"nfo", "null"}};
    if (aliases.contains(l.name))
      l.lexer = aliases[l.name];
    auto keys = n.elementsByTagName("Keywords");
    for (int j = 0; j < keys.size(); ++j)
      l.keywords << keys.at(j).toElement().text().simplified().toUtf8();
    languages[l.name] = l;
  }
  if (!languages.contains("normal"))
    languages["normal"] = {"normal", "null", {"txt"}, {}, {}};
  for (const QString &dir :
       {dataPath("userDefineLangs"), configPath("userDefineLangs")})
    for (const auto &file :
         QDir(dir).entryInfoList({"*.xml"}, QDir::Files, QDir::Name)) {
      auto langs = xml(file.filePath()).elementsByTagName("UserLang");
      for (int i = 0; i < langs.size(); ++i) {
        auto n = langs.at(i).toElement();
        if (n.attribute("name").isEmpty())
          continue;
        languages[n.attribute("name")] = {
            n.attribute("name"), "user",
            n.attribute("ext").split(' ', Qt::SkipEmptyParts), {}, n};
      }
    }
}
QStringList Window::completions(const QString &language) {
  auto it = completionCache.find(language);
  if (it != completionCache.end())
    return *it;
  QStringList words;
  for (const auto &k : languages.value(language).keywords)
    words << QString::fromUtf8(k).split(' ', Qt::SkipEmptyParts);
  words.removeDuplicates();
  std::sort(words.begin(), words.end(), [](const QString &a, const QString &b) {
    return a.compare(b, Qt::CaseInsensitive) < 0;
  });
  return *completionCache.insert(language, words);
}
EditorTab *Window::newDocument(const QByteArray &text) {
  // Loading a document is not a user edit; keep it out of a running macro.
  QScopedValueRollback<bool> quiet(suppressMacro, true);
  auto *t = new EditorTab;
  t->untitled = counter++;
  int i = tabs->addTab(t, t->title());
  tabs->setCurrentIndex(i);
  for (auto *ed : {t->primary, t->secondary}) {
    connect(ed, &ScintillaEditBase::savePointChanged, this, [this, t] {
      tabs->setTabText(tabs->indexOf(t), t->title());
      refresh();
    });
    connect(ed, &ScintillaEditBase::updateUi, this, [this] { refresh(); });
    connect(ed, &ScintillaEditBase::notifyChange, this, [this, t] {
      dismissedChanges.remove(t);
      outlineTimer.start(700);
    });
    connect(ed, &ScintillaEditBase::marginClicked, this,
            [ed](Scintilla::Position pos, Scintilla::KeyMod, int margin) {
              if (margin != 1)
                return;
              int line = ed->send(SCI_LINEFROMPOSITION, pos);
              ed->send(ed->send(SCI_MARKERGET, line) & 1 ? SCI_MARKERDELETE
                                                         : SCI_MARKERADD,
                       line, 0);
            });
    connect(ed, &ScintillaEditBase::uriDropped, this,
            [this](const QString &uri) {
              for (auto line : uri.split('\n', Qt::SkipEmptyParts)) {
                QUrl u(line.trimmed());
                if (u.isLocalFile())
                  openFile(u.toLocalFile());
              }
            });
    connect(ed, &ScintillaEditBase::charAdded, this, [this, t, ed](int ch) {
      if (ch == '\n' ||
          (ch == '\r' && ed->send(SCI_GETEOLMODE) == SC_EOL_CR)) {
        // '\r' only counts in CR mode; in CRLF mode the '\n' that follows
        // does the indenting, so handling both would indent twice.
        if (!prefs.autoIndent)
          return;
        QScopedValueRollback<bool> quiet(suppressMacro, true);
        int line = ed->send(SCI_LINEFROMPOSITION, ed->send(SCI_GETCURRENTPOS));
        if (line > 0) {
          int indent = ed->send(SCI_GETLINEINDENTATION, line - 1);
          ed->send(SCI_SETLINEINDENTATION, line, indent);
          ed->send(SCI_GOTOPOS, ed->send(SCI_GETLINEINDENTPOSITION, line));
        }
      } else if (prefs.completion && ch >= 32 && ch != 127 &&
                 (ch == '_' || QChar::isLetterOrNumber(uint(ch)))) {
        // In UTF-8 mode Scintilla reports the decoded code point, so non-ASCII
        // letters compare correctly via QChar; control chars stay excluded.
        auto pos = ed->send(SCI_GETCURRENTPOS);
        auto start = ed->send(SCI_WORDSTARTPOSITION, pos, 1);
        if (pos - start < 2)
          return;
        const QStringList words = completions(t->language);
        if (words.isEmpty())
          return;
        QByteArray word(int(pos - start) + 1, '\0');
        Sci_TextRangeFull range{{start, pos}, word.data()};
        ed->send(SCI_GETTEXTRANGEFULL, 0, reinterpret_cast<sptr_t>(&range));
        word.chop(1);
        const QString prefix = QString::fromUtf8(word);
        QStringList matches;
        for (const auto &w : words)
          if (w.startsWith(prefix, Qt::CaseInsensitive) && w != prefix)
            matches << w;
        if (matches.isEmpty())
          return;
        QByteArray list = matches.join(' ').toUtf8();
        ed->sends(SCI_AUTOCSHOW, pos - start, list.constData());
      }
    });
    connect(ed, &ScintillaEditBase::macroRecord, this,
            [this](Scintilla::Message msg, Scintilla::uptr_t w,
                   Scintilla::sptr_t l) {
              if (!recording || suppressMacro)
                return;
              unsigned int m = static_cast<unsigned int>(msg);
              QByteArray b;
              // lParam points at caller memory that is gone by playback
              // time, so copy the text now and never store the pointer.
              if (m == SCI_REPLACESEL || m == SCI_ADDTEXT ||
                  m == SCI_INSERTTEXT || m == SCI_APPENDTEXT ||
                  m == SCI_SEARCHNEXT || m == SCI_SEARCHPREV) {
                if (l)
                  b = (m == SCI_ADDTEXT || m == SCI_APPENDTEXT)
                          ? QByteArray(reinterpret_cast<const char *>(l),
                                       int(w))
                          : QByteArray(reinterpret_cast<const char *>(l));
                l = 0;
              }
              macro.append({qintptr(m), qintptr(w), qintptr(l)});
              macroText << b;
            });
    ed->send(SCI_STARTRECORD);
  }
  t->setText(text, !text.isEmpty());
  applyPreferences(t);
  applyLanguage(t, "normal");
  t->primary->setFocus();
  refresh();
  return t;
}
bool Window::openFile(const QString &path, const QString &fallback) {
  QScopedValueRollback<bool> quiet(suppressMacro, true);
  const QString absolute = QFileInfo(path).absoluteFilePath();
  const QString canonical = QFileInfo(absolute).canonicalFilePath();
  for (int i = 0; i < tabs->count(); ++i) {
    auto *t = static_cast<EditorTab *>(tabs->widget(i));
    if (!t->data.path.isEmpty() && !canonical.isEmpty() &&
        QFileInfo(t->data.path).canonicalFilePath() == canonical) {
      tabs->setCurrentIndex(i);
      return true;
    }
  }
  if (QFileInfo(path).size() > maxFileSize) {
    report("Files over 1 GiB are not supported.");
    return false;
  }
  DocumentData d;
  QString error;
  ReadError kind = ReadError::None;
  if (!DocumentData::read(path, d, error, fallback, &kind)) {
    // Offer a legacy encoding instead of a dead end. Skipped when a fallback
    // was already given (no loops) and in testing mode (never block).
    if (kind == ReadError::InvalidEncoding && fallback.isEmpty() && !testing) {
      const QStringList choices = legacyEncodings();
      if (!choices.isEmpty()) {
        bool ok = false;
        const QString enc = QInputDialog::getItem(
            this, "Unsupported encoding",
            error + "\n\nOpen it using this encoding instead?", choices, 0,
            false, &ok);
        if (ok)
          return openFile(path, enc);
        return false;
      }
    }
    report(error);
    return false;
  }
  // An untouched blank "new" tab gives way to the opened file.
  auto *blank = current();
  if (tabs->count() != 1 || !blank->data.path.isEmpty() || blank->dirty() ||
      blank->primary->send(SCI_GETLENGTH))
    blank = nullptr;
  auto *t = newDocument();
  t->data = d;
  t->large = d.utf8.size() > largeFileSize;
  applyPreferences(t);
  t->setText(d.utf8);
  tabs->setTabToolTip(tabs->indexOf(t), d.path);
  if (blank) {
    tabs->removeTab(tabs->indexOf(blank));
    delete blank;
  }
  QString ext = QFileInfo(path).suffix();
  QString lang = "normal";
  const bool dark = themeDefault(themeDocument(themeChoice->currentText()),
                                 "bgColor", Qt::white)
                        .lightness() < 128;
  int best = 0;
  for (const auto &l : languages)
    if (l.extensions.contains(ext, Qt::CaseInsensitive)) {
      int score = l.udl.isNull() ? 1
                  : l.name.contains("dark", Qt::CaseInsensitive) == dark ? 3
                                                                         : 2;
      if (score > best) {
        best = score;
        lang = l.name;
      }
    }
  if (QFileInfo(path).fileName().compare("Makefile", Qt::CaseInsensitive) == 0)
    lang = "makefile";
  applyLanguage(t, lang);
  int crlf = d.utf8.count("\r\n"), lf = d.utf8.count('\n') - crlf,
      cr = d.utf8.count('\r') - crlf;
  int mode = crlf > lf && crlf >= cr ? SC_EOL_CRLF
             : cr > lf               ? SC_EOL_CR
                                     : SC_EOL_LF;
  for (auto *ed : {t->primary, t->secondary})
    ed->send(SCI_SETEOLMODE, mode);
  recent.removeAll(absolute);
  recent.prepend(absolute);
  while (recent.size() > 15)
    recent.removeLast();
  refresh();
  return true;
}
void Window::chooseOpen() {
  for (const QString &p : QFileDialog::getOpenFileNames(this, "Open files"))
    openFile(p);
}
void Window::chooseFolder() {
  auto path = QFileDialog::getExistingDirectory(this, "Open folder");
  if (path.isEmpty())
    return;
  files->setRootIndex(filesModel->setRootPath(path));
  workspaceRoot = path;
  filesDock->show();
}
QString Window::quickOpenRoot() const {
  if (!workspaceRoot.isEmpty())
    return workspaceRoot;
  if (auto *t = current(); t && !t->data.path.isEmpty())
    return QFileInfo(t->data.path).absolutePath();
  return QDir::currentPath();
}
void Window::showCommandPalette() {
  auto *dialog = new PaletteDialog("Type a command…", this);
  QList<PaletteItem> items;
  for (const auto &[a, menu] : menuActions) {
    QString title = a->text();
    title.remove('&');
    if (title.isEmpty() || a->menu())
      continue;
    QString detail = menu;
    detail.remove('&');
    if (!a->shortcut().isEmpty())
      detail += "  [" + a->shortcut().toString(QKeySequence::NativeText) + "]";
    items.append({title, detail, QVariant::fromValue(static_cast<void *>(a)),
                  a->isEnabled()});
  }
  dialog->setItems(items);
  connect(dialog, &PaletteDialog::chosen, this, [](const QVariant &v) {
    // Run after the popup has closed so focus is back in the editor.
    auto *a = static_cast<QAction *>(v.value<void *>());
    QTimer::singleShot(0, a, [a] { a->trigger(); });
  });
  dialog->show();
  dialog->input()->setFocus();
}
void Window::showQuickOpen() {
  const QString root = quickOpenRoot();
  auto *dialog = new PaletteDialog("Open file in " + root + "…", this);
  QList<PaletteItem> items;
  QSet<QString> seen;
  auto fresh = [&seen](const QString &p) {
    if (seen.contains(p))
      return false;
    seen.insert(p);
    return true;
  };
  for (int i = 0; i < tabs->count(); ++i) { // open tabs first
    auto *t = static_cast<EditorTab *>(tabs->widget(i));
    if (t->data.path.isEmpty() || !fresh(t->data.path))
      continue;
    items.append({QFileInfo(t->data.path).fileName(), "open tab: " + t->data.path,
                  t->data.path});
  }
  for (const QString &p : recent)
    if (fresh(p))
      items.append({QFileInfo(p).fileName(), "recent: " + p, p});
  for (const QString &rel : listProjectFiles(root)) {
    const QString abs = QDir(root).absoluteFilePath(rel);
    if (fresh(abs))
      items.append({rel, {}, abs});
  }
  dialog->setItems(items);
  connect(dialog, &PaletteDialog::chosen, this,
          [this](const QVariant &v) { openFile(v.toString()); });
  dialog->show();
  dialog->input()->setFocus();
}
bool Window::save(EditorTab *t, bool saveAs) {
  if (!t)
    return false;
  QString path = t->data.path;
  if (saveAs || path.isEmpty()) {
    path = QFileDialog::getSaveFileName(this, "Save file", path);
    if (path.isEmpty())
      return false;
    for (int i = 0; i < tabs->count(); ++i) {
      auto *other = static_cast<EditorTab *>(tabs->widget(i));
      if (other != t && QFileInfo(other->data.path).absoluteFilePath() ==
                            QFileInfo(path).absoluteFilePath()) {
        report("That file is already open in another tab.");
        return false;
      }
    }
  }
  if (prefs.trimOnSave)
    trimTrailingWhitespace(t);
  if (prefs.finalNewline)
    ensureFinalNewline(t);
  QString error;
  if (!t->data.write(t->text(), path, error)) {
    report(error);
    return false;
  }
  t->metadataDirty = false;
  t->primary->send(SCI_SETSAVEPOINT);
  dismissedChanges.remove(t);
  tabs->setTabToolTip(tabs->indexOf(t), t->data.path);
  refresh();
  snapshot();
  return true;
}
bool Window::canClose(EditorTab *t) {
  if (!t->dirty())
    return true;
  auto choice = QMessageBox::warning(
      this, "Unsaved changes", "Save changes to " + t->title() + "?",
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
      QMessageBox::Save);
  if (choice == QMessageBox::Cancel)
    return false;
  if (choice == QMessageBox::Save)
    return save(t);
  return true;
}
bool Window::closeTab(int index) {
  if (index < 0)
    return true;
  auto *t = static_cast<EditorTab *>(tabs->widget(index));
  if (!canClose(t))
    return false;
  dismissedChanges.remove(t);
  tabs->removeTab(index);
  delete t;
  if (!tabs->count())
    newDocument();
  snapshot();
  refresh();
  return true;
}
void Window::applyLanguage(EditorTab *t, const QString &name) {
  if (!t)
    return;
  t->language = languages.contains(name) && !t->large ? name : "normal";
  if (t->large && name != "normal")
    statusBar()->showMessage(
        "Syntax highlighting is off for files over 32 MiB", 5000);
  for (auto *ed : {t->primary, t->secondary}) {
    ed->send(SCI_SETAUTOMATICFOLD, t->large ? 0 : SC_AUTOMATICFOLD_SHOW |
                                                      SC_AUTOMATICFOLD_CLICK |
                                                      SC_AUTOMATICFOLD_CHANGE);
    ed->send(SCI_SETMARGINWIDTHN, 2, t->large ? 0 : 17);
  }
  const auto l = languages.value(t->language);
  auto *lexer = CreateLexer(l.lexer.toUtf8().constData());
  if (!lexer)
    lexer = CreateLexer("null");
  t->primary->send(SCI_SETILEXER, 0, reinterpret_cast<sptr_t>(lexer));
  if (!l.udl.isNull())
    applyUserLanguage(t, l.udl);
  else
    for (int i = 0; i < 9; ++i)
      t->primary->sends(SCI_SETKEYWORDS, i,
                        i < l.keywords.size() ? l.keywords[i].constData()
                                              : "");
  if (t->language == "json" || t->language == "json5")
    t->primary->sends(SCI_SETPROPERTY,
                      reinterpret_cast<uptr_t>("lexer.json.allow.comments"),
                      "1");
  applyTheme(t);
  t->primary->send(SCI_COLOURISE, 0, -1);
  refresh();
}
// Configures the LexUser lexer from a UDL 2.x <UserLang> definition.
void Window::applyUserLanguage(EditorTab *t, const QDomElement &udl) {
  auto *e = t->primary;
  auto prop = [e](const QByteArray &key, const QByteArray &value) {
    e->sends(SCI_SETPROPERTY, reinterpret_cast<uptr_t>(key.constData()),
             value.constData());
  };
  auto flag = [](const QDomElement &n, const QString &attribute) {
    return QByteArray(n.attribute(attribute) == "yes" ? "1" : "0");
  };
  auto settings = udl.firstChildElement("Settings");
  auto global = settings.firstChildElement("Global"),
       prefix = settings.firstChildElement("Prefix");
  prop("userDefine.isCaseIgnored", flag(global, "caseIgnored"));
  prop("userDefine.allowFoldOfComments", flag(global, "allowFoldOfComments"));
  prop("userDefine.foldCompact", flag(global, "foldCompact"));
  prop("userDefine.forcePureLC", global.attribute("forcePureLC", "0").toUtf8());
  prop("userDefine.decimalSeparator",
       global.attribute("decimalSeparator", "0").toUtf8());
  prop("userDefine.currentBufferID", QByteArray::number(t->untitled));
  for (int i = 1; i <= SCE_USER_TOTAL_KEYWORD_GROUPS; ++i)
    prop("userDefine.prefixKeywords" + QByteArray::number(i),
         flag(prefix, QString("Keywords%1").arg(i)));
  static const QHash<QString, int> lists = [] {
    QHash<QString, int> m{
        {"Comments", SCE_USER_KWLIST_COMMENTS},
        {"Numbers, prefix1", SCE_USER_KWLIST_NUMBER_PREFIX1},
        {"Numbers, prefix2", SCE_USER_KWLIST_NUMBER_PREFIX2},
        {"Numbers, prefixes", SCE_USER_KWLIST_NUMBER_PREFIX2},
        {"Numbers, extras1", SCE_USER_KWLIST_NUMBER_EXTRAS1},
        {"Numbers, extras2", SCE_USER_KWLIST_NUMBER_EXTRAS2},
        {"Numbers, extras with prefixes", SCE_USER_KWLIST_NUMBER_EXTRAS2},
        {"Numbers, suffix1", SCE_USER_KWLIST_NUMBER_SUFFIX1},
        {"Numbers, suffix2", SCE_USER_KWLIST_NUMBER_SUFFIX2},
        {"Numbers, suffixes", SCE_USER_KWLIST_NUMBER_SUFFIX2},
        {"Numbers, range", SCE_USER_KWLIST_NUMBER_RANGE},
        {"Numbers, additional", SCE_USER_KWLIST_NUMBER_RANGE},
        {"Operators1", SCE_USER_KWLIST_OPERATORS1},
        {"Operators2", SCE_USER_KWLIST_OPERATORS2},
        {"Delimiters", SCE_USER_KWLIST_DELIMITERS}};
    const char *folders[] = {"code1", "code2", "comment"};
    const char *parts[] = {"open", "middle", "close"};
    for (int f = 0; f < 3; ++f)
      for (int p = 0; p < 3; ++p)
        m[QString("Folders in %1, %2").arg(folders[f], parts[p])] =
            SCE_USER_KWLIST_FOLDERS_IN_CODE1_OPEN + f * 3 + p;
    for (int i = 0; i < 8; ++i)
      m[QString("Keywords%1").arg(i + 1)] = SCE_USER_KWLIST_KEYWORDS1 + i;
    return m;
  }();
  // Lists the lexer reads as properties; the rest go to SCI_SETKEYWORDS in order.
  static const QHash<int, QByteArray> properties{
      {SCE_USER_KWLIST_COMMENTS, "comments"},
      {SCE_USER_KWLIST_DELIMITERS, "delimiters"},
      {SCE_USER_KWLIST_OPERATORS1, "operators1"},
      {SCE_USER_KWLIST_NUMBER_PREFIX1, "numberPrefix1"},
      {SCE_USER_KWLIST_NUMBER_PREFIX2, "numberPrefix2"},
      {SCE_USER_KWLIST_NUMBER_EXTRAS1, "numberExtras1"},
      {SCE_USER_KWLIST_NUMBER_EXTRAS2, "numberExtras2"},
      {SCE_USER_KWLIST_NUMBER_SUFFIX1, "numberSuffix1"},
      {SCE_USER_KWLIST_NUMBER_SUFFIX2, "numberSuffix2"},
      {SCE_USER_KWLIST_NUMBER_RANGE, "numberRange"},
      {SCE_USER_KWLIST_FOLDERS_IN_CODE1_OPEN, "foldersInCode1Open"},
      {SCE_USER_KWLIST_FOLDERS_IN_CODE1_MIDDLE, "foldersInCode1Middle"},
      {SCE_USER_KWLIST_FOLDERS_IN_CODE1_CLOSE, "foldersInCode1Close"}};
  QVector<QByteArray> words(SCE_USER_KWLIST_TOTAL);
  for (auto k = udl.firstChildElement("KeywordLists").firstChildElement("Keywords");
       !k.isNull(); k = k.nextSiblingElement("Keywords"))
    if (lists.contains(k.attribute("name")))
      words[lists[k.attribute("name")]] = k.text().toUtf8();
  int keywordSet = 0;
  for (int i = 0; i < words.size(); ++i) {
    if (properties.contains(i))
      prop("userDefine." + properties[i], words[i]);
    else
      e->sends(SCI_SETKEYWORDS, keywordSet++, udlKeywords(words[i]).constData());
  }
  static const QHash<QString, int> nesting = [] {
    QHash<QString, int> m{{"COMMENTS", SCE_USER_STYLE_COMMENT},
                          {"LINE COMMENTS", SCE_USER_STYLE_COMMENTLINE}};
    for (int i = 0; i < 8; ++i)
      m[QString("DELIMITERS%1").arg(i + 1)] = SCE_USER_STYLE_DELIMITER1 + i;
    return m;
  }();
  for (auto n = udl.firstChildElement("Styles").firstChildElement("WordsStyle");
       !n.isNull(); n = n.nextSiblingElement("WordsStyle"))
    if (nesting.contains(n.attribute("name")))
      prop(QString("userDefine.nesting.%1")
               .arg(nesting[n.attribute("name")], 2, 10, QChar('0'))
               .toUtf8(),
           n.attribute("nesting", "0").toUtf8());
}
void Window::applyTheme(EditorTab *t) {
  bool light = themeChoice->currentText() == "Light";
  const QDomDocument d = themeDocument(themeChoice->currentText());
  const QColor bg = themeDefault(d, "bgColor", light ? "#ffffff" : "#1e222b"),
               fg = themeDefault(d, "fgColor", light ? "#242a35" : "#dbe1ee");
  const QDomElement udl = languages.value(t->language).udl;
  for (auto *e : {t->primary, t->secondary}) {
    e->send(SCI_STYLESETFORE, STYLE_DEFAULT, colour(fg));
    e->send(SCI_STYLESETBACK, STYLE_DEFAULT, colour(bg));
    e->sends(SCI_STYLESETFONT, STYLE_DEFAULT, prefs.font.toUtf8().constData());
    e->send(SCI_STYLESETSIZE, STYLE_DEFAULT, prefs.fontSize);
    e->send(SCI_STYLECLEARALL);
    e->send(SCI_SETCARETFORE, colour(fg));
    e->send(SCI_SETCARETLINEBACK, colour(bg.lighter(light ? 96 : 120)));
    e->send(SCI_SETSELBACK, 1,
            colour(light ? QColor("#c5dafa") : QColor("#3d506e")));
    e->send(SCI_STYLESETFORE, STYLE_LINENUMBER,
            colour(light ? QColor("#737d8d") : QColor("#7d8798")));
    e->send(SCI_STYLESETBACK, STYLE_LINENUMBER, colour(bg));
    e->send(SCI_SETFOLDMARGINCOLOUR, 1, colour(bg));
    e->send(SCI_SETFOLDMARGINHICOLOUR, 1, colour(bg));
    if (!udl.isNull()) {
      // colorStyle bit 1 = use fgColor, bit 2 = use bgColor; otherwise the
      // theme default shows through, which keeps light UDLs readable on dark
      // themes.
      static const QHash<QString, int> ids = [] {
        QHash<QString, int> m{{"DEFAULT", SCE_USER_STYLE_DEFAULT},
                              {"COMMENTS", SCE_USER_STYLE_COMMENT},
                              {"LINE COMMENTS", SCE_USER_STYLE_COMMENTLINE},
                              {"NUMBERS", SCE_USER_STYLE_NUMBER},
                              {"OPERATORS", SCE_USER_STYLE_OPERATOR},
                              {"FOLDER IN CODE1", SCE_USER_STYLE_FOLDER_IN_CODE1},
                              {"FOLDER IN CODE2", SCE_USER_STYLE_FOLDER_IN_CODE2},
                              {"FOLDER IN COMMENT",
                               SCE_USER_STYLE_FOLDER_IN_COMMENT}};
        for (int i = 0; i < 8; ++i) {
          m[QString("KEYWORDS%1").arg(i + 1)] = SCE_USER_STYLE_KEYWORD1 + i;
          m[QString("DELIMITERS%1").arg(i + 1)] = SCE_USER_STYLE_DELIMITER1 + i;
        }
        return m;
      }();
      for (auto node = udl.firstChildElement("Styles").firstChildElement(
               "WordsStyle");
           !node.isNull(); node = node.nextSiblingElement("WordsStyle")) {
        if (!ids.contains(node.attribute("name")))
          continue;
        int id = ids[node.attribute("name")];
        int colourStyle = node.attribute("colorStyle", "3").toInt();
        if (colourStyle & 1 && !node.attribute("fgColor").isEmpty())
          e->send(SCI_STYLESETFORE, id,
                  colour(QColor('#' + node.attribute("fgColor"))));
        if (colourStyle & 2 && !node.attribute("bgColor").isEmpty())
          e->send(SCI_STYLESETBACK, id,
                  colour(QColor('#' + node.attribute("bgColor"))));
        int fs = node.attribute("fontStyle").toInt();
        e->send(SCI_STYLESETBOLD, id, fs & 1);
        e->send(SCI_STYLESETITALIC, id, fs & 2);
        e->send(SCI_STYLESETUNDERLINE, id, fs & 4);
      }
    }
    auto lexers = d.elementsByTagName("LexerType");
    QString styleName = t->language;
    if (styleName == "c")
      styleName = "cpp";
    // Languages the theme has no LexerType for borrow their base language's.
    static const QHash<QString, QString> styleFallback{{"jsp", "html"},
                                                       {"json5", "json"}};
    if (styleFallback.contains(styleName)) {
      bool found = false;
      for (int i = 0; i < lexers.size() && !found; ++i)
        found = lexers.at(i).toElement().attribute("name") == styleName;
      if (!found)
        styleName = styleFallback[styleName];
    }
    for (int i = 0; i < lexers.size(); ++i) {
      auto lexer = lexers.at(i).toElement();
      if (lexer.attribute("name") != styleName)
        continue;
      for (auto node = lexer.firstChildElement("WordsStyle"); !node.isNull();
           node = node.nextSiblingElement("WordsStyle")) {
        int id = node.attribute("styleID").toInt();
        auto fc = node.attribute("fgColor"), bc = node.attribute("bgColor");
        if (!fc.isEmpty())
          e->send(SCI_STYLESETFORE, id, colour(QColor('#' + fc)));
        if (!bc.isEmpty())
          e->send(SCI_STYLESETBACK, id, colour(QColor('#' + bc)));
        int fs = node.attribute("fontStyle").toInt();
        e->send(SCI_STYLESETBOLD, id, fs & 1);
        e->send(SCI_STYLESETITALIC, id, fs & 2);
        e->send(SCI_STYLESETUNDERLINE, id, fs & 4);
      }
    }
    e->send(SCI_COLOURISE, 0, -1);
  }
}
// Re-reads every language definition after one was added, changed or removed.
void Window::reloadLanguages() {
  languages.clear();
  completionCache.clear();
  loadLanguages();
  {
    QSignalBlocker blocker(languageChoice);
    languageChoice->clear();
    languageChoice->addItems(languages.keys());
  }
  for (int i = 0; i < tabs->count(); ++i) {
    auto *t = static_cast<EditorTab *>(tabs->widget(i));
    applyLanguage(t, t->language);
  }
  refresh();
}
void Window::editUserLanguage(const QString &existingName) {
  UdlSpec spec;
  QString oldPath;
  if (!existingName.isEmpty()) {
    oldPath = UdlSpec::saved().value(existingName);
    const auto langs = xml(oldPath).elementsByTagName("UserLang");
    for (int i = 0; i < langs.size(); ++i)
      if (langs.at(i).toElement().attribute("name") == existingName)
        UdlSpec::fromElement(langs.at(i).toElement(), spec);
  }
  const bool existing = !oldPath.isEmpty();
  UdlEditor dialog(spec, existing, this);
  while (true) {
    if (dialog.exec() != QDialog::Accepted)
      return;
    if (dialog.removed) {
      QFile::remove(oldPath);
      break;
    }
    spec = dialog.spec();
    QString error = spec.validate(languages, existingName);
    if (error.isEmpty())
      spec.save(error); // leaves `error` set when writing fails
    if (!error.isEmpty()) {
      QMessageBox::warning(this, "Custom language", error);
      continue;
    }
    if (existing && QFileInfo(oldPath).fileName() != spec.fileName())
      QFile::remove(oldPath);
    break;
  }
  reloadLanguages();
  if (!dialog.removed && current())
    applyLanguage(current(), spec.name);
}
void Window::applyPreferences(EditorTab *t) {
  if (!t)
    return;
  for (auto *e : {t->primary, t->secondary}) {
    e->send(SCI_SETTABWIDTH, prefs.tabWidth);
    e->send(SCI_SETINDENT, prefs.tabWidth);
    e->send(SCI_SETUSETABS, !prefs.spaces);
    e->send(SCI_SETMARGINWIDTHN, 0, prefs.lineNumbers ? 60 : 0);
    e->send(SCI_SETCARETLINEVISIBLE, prefs.caretLine);
    // Wrapping a huge document makes every edit and scroll slow.
    e->send(SCI_SETWRAPMODE, prefs.wrap && !t->large ? SC_WRAP_WORD : SC_WRAP_NONE);
    e->send(SCI_SETVIEWWS, prefs.whitespace ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE);
  }
}
void Window::showPreferences() {
  QDialog dialog(this);
  dialog.setWindowTitle("Preferences");
  auto *form = new QFormLayout(&dialog);
  auto *font = new QFontComboBox;
  font->setFontFilters(QFontComboBox::MonospacedFonts);
  font->setCurrentFont(QFont(prefs.font));
  auto *size = new QSpinBox;
  size->setRange(6, 72);
  size->setValue(prefs.fontSize);
  auto *tab = new QSpinBox;
  tab->setRange(1, 16);
  tab->setValue(prefs.tabWidth);
  auto check = [&form](const QString &text, bool on) {
    auto *c = new QCheckBox(text);
    c->setChecked(on);
    form->addRow(c);
    return c;
  };
  form->addRow("Font", font);
  form->addRow("Font size", size);
  form->addRow("Tab width", tab);
  auto *spaces = check("Insert spaces instead of tabs", prefs.spaces);
  auto *autoIndent = check("Keep the previous line's indentation", prefs.autoIndent);
  auto *completion = check("Suggest keywords while typing", prefs.completion);
  auto *numbers = check("Show line numbers", prefs.lineNumbers);
  auto *caret = check("Highlight the current line", prefs.caretLine);
  auto *wrap = check("Wrap long lines", prefs.wrap);
  auto *whitespace = check("Show whitespace", prefs.whitespace);
  auto *trim = check("Trim trailing whitespace when saving", prefs.trimOnSave);
  auto *newline = check("End files with a newline when saving", prefs.finalNewline);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                       QDialogButtonBox::Cancel);
  form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted)
    return;
  prefs.font = font->currentFont().family();
  prefs.fontSize = size->value();
  prefs.tabWidth = tab->value();
  prefs.spaces = spaces->isChecked();
  prefs.autoIndent = autoIndent->isChecked();
  prefs.completion = completion->isChecked();
  prefs.lineNumbers = numbers->isChecked();
  prefs.caretLine = caret->isChecked();
  prefs.wrap = wrap->isChecked();
  prefs.whitespace = whitespace->isChecked();
  prefs.trimOnSave = trim->isChecked();
  prefs.finalNewline = newline->isChecked();
  prefs.store(settings);
  settings.sync();
  for (int i = 0; i < tabs->count(); ++i) {
    auto *t = static_cast<EditorTab *>(tabs->widget(i));
    applyPreferences(t);
    applyTheme(t);
  }
}
void Window::refresh() {
  auto *t = current();
  if (!t)
    return;
  // setTabText relayouts the whole tab bar, so only touch changed titles.
  for (int i = 0; i < tabs->count(); ++i) {
    const QString title = static_cast<EditorTab *>(tabs->widget(i))->title();
    if (tabs->tabText(i) != title)
      tabs->setTabText(i, title);
  }
  auto *e = t->focused;
  auto pos = e->send(SCI_GETCURRENTPOS);
  int line = e->send(SCI_LINEFROMPOSITION, pos),
      col = e->send(SCI_GETCOLUMN, pos);
  status->setText(QString("  Ln %1, Col %2   |   %3 bytes   |   %4 lines   %5")
                      .arg(line + 1)
                      .arg(col + 1)
                      .arg(e->send(SCI_GETLENGTH))
                      .arg(e->send(SCI_GETLINECOUNT))
                      .arg(recording ? "● REC" : ""));
  QSignalBlocker b1(languageChoice), b2(encodingChoice), b3(eolChoice);
  languageChoice->setCurrentText(t->language);
  encodingChoice->setCurrentText(
      t->data.encoding == "UTF-8" && !t->data.bom.isEmpty() ? "UTF-8 BOM"
                                                            : t->data.encoding);
  int mode = e->send(SCI_GETEOLMODE);
  eolChoice->setCurrentIndex(mode == SC_EOL_LF     ? 0
                             : mode == SC_EOL_CRLF ? 1
                                                   : 2);
  setWindowTitle(t->title() + " — Velox");
}
void Window::showSearch(SearchMode mode) {
  replaceRow->setVisible(mode == ReplaceMode);
  filesRow->setVisible(mode == FilesMode);
  searchPanel->show();
  auto *t = current();
  if (mode == FilesMode && t && !t->data.path.isEmpty() &&
      (folder->text().isEmpty() || folder->text() == QDir::homePath()))
    folder->setText(QFileInfo(t->data.path).absolutePath());
  if (t) {
    auto *e = t->focused;
    auto len = e->send(SCI_GETSELECTIONEND) - e->send(SCI_GETSELECTIONSTART);
    if (len > 0 && len < 300) {
      QByteArray b(len + 1, '\0');
      e->send(SCI_GETSELTEXT, 0, reinterpret_cast<sptr_t>(b.data()));
      query->setText(QString::fromUtf8(b.constData()));
    }
  }
  query->setFocus();
  query->selectAll();
}
bool Window::find(bool backwards) {
  auto *t = current();
  if (!t || query->text().isEmpty())
    return false;
  auto *e = t->focused;
  QByteArray q = query->text().toUtf8();
  auto length = e->send(SCI_GETLENGTH),
       start = e->send(backwards ? SCI_GETSELECTIONSTART : SCI_GETSELECTIONEND);
  for (int round = 0; round < 2; ++round) {
    auto from = round ? (backwards ? length : 0) : start;
    e->send(SCI_SETSEARCHFLAGS,
            searchFlags(e, from, SCFIND_REGEXP_EMPTYMATCH_ALL));
    e->send(SCI_SETTARGETSTART, from);
    e->send(SCI_SETTARGETEND, round ? start : (backwards ? 0 : length));
    auto pos = e->sends(SCI_SEARCHINTARGET, q.size(), q.constData());
    if (pos >= 0) {
      e->send(SCI_SETSEL, e->send(SCI_GETTARGETSTART),
              e->send(SCI_GETTARGETEND));
      e->send(SCI_SCROLLCARET);
      statusBar()->showMessage(round ? "Search wrapped" : "Match found", 2000);
      return true;
    }
    if (regexFailed(e))
      return false;
  }
  statusBar()->showMessage("No matches", 3000);
  return false;
}
void Window::replaceOne() {
  auto *t = current();
  if (!t || query->text().isEmpty())
    return;
  auto *e = t->focused;
  auto a = e->send(SCI_GETSELECTIONSTART), b = e->send(SCI_GETSELECTIONEND);
  if (a == b) {
    find();
    return;
  }
  e->send(SCI_SETTARGETSTART, a);
  e->send(SCI_SETTARGETEND, b);
  e->send(SCI_SETSEARCHFLAGS,
          searchFlags(e, a,
                      SCFIND_REGEXP_EMPTYMATCH_ALL |
                          SCFIND_REGEXP_EMPTYMATCH_ALLOWATSTART));
  auto q = query->text().toUtf8();
  auto found = e->sends(SCI_SEARCHINTARGET, q.size(), q.constData());
  if (found != a || e->send(SCI_GETTARGETEND) != b) {
    find();
    return;
  }
  auto r = replacement->text().toUtf8();
  auto added =
      e->sends(regex->isChecked() ? SCI_REPLACETARGETRE : SCI_REPLACETARGET,
               r.size(), r.constData());
  e->send(SCI_SETSEL, a + added, a + added);
  find();
}
int Window::replaceAll() {
  auto *t = current();
  if (!t || query->text().isEmpty())
    return 0;
  auto *e = t->focused;
  auto q = query->text().toUtf8(), r = replacement->text().toUtf8();
  e->send(SCI_SETSEARCHFLAGS,
          searchFlags(e, 0, SCFIND_REGEXP_EMPTYMATCH_NOTAFTERMATCH));
  e->send(SCI_BEGINUNDOACTION);
  sptr_t start = 0;
  int count = 0;
  while (start <= e->send(SCI_GETLENGTH)) {
    e->send(SCI_SETTARGETSTART, start);
    e->send(SCI_SETTARGETEND, e->send(SCI_GETLENGTH));
    auto pos = e->sends(SCI_SEARCHINTARGET, q.size(), q.constData());
    if (pos < 0)
      break;
    auto end = e->send(SCI_GETTARGETEND);
    bool empty = end == pos;
    auto added =
        e->sends(regex->isChecked() ? SCI_REPLACETARGETRE : SCI_REPLACETARGET,
                 r.size(), r.constData());
    if (added < 0)
      break;
    ++count;
    start = pos + added;
    if (empty) {
      auto next = e->send(SCI_POSITIONAFTER, start);
      if (next <= start)
        break;
      start = next;
    }
  }
  e->send(SCI_ENDUNDOACTION);
  if (!regexFailed(e))
    statusBar()->showMessage(QString("Replaced %1 matches").arg(count), 4000);
  return count;
}
bool Window::regexFailed(ScintillaEditBase *e) {
  if (e->send(SCI_GETSTATUS) != SC_STATUS_WARN_REGEX)
    return false;
  statusBar()->showMessage("Invalid regular expression", 4000);
  e->send(SCI_SETSTATUS, SC_STATUS_OK);
  return true;
}
int Window::markAll() {
  auto *t = current();
  if (!t)
    return 0;
  clearMarks();
  if (query->text().isEmpty())
    return 0;
  auto *e = t->focused;
  auto q = query->text().toUtf8();
  e->send(SCI_SETSEARCHFLAGS,
          searchFlags(e, 0, SCFIND_REGEXP_EMPTYMATCH_NONE));
  e->send(SCI_SETINDICATORCURRENT, markIndicator);
  int count = 0;
  const sptr_t length = e->send(SCI_GETLENGTH);
  for (sptr_t start = 0; start < length;) {
    e->send(SCI_SETTARGETRANGE, start, length);
    auto pos = e->sends(SCI_SEARCHINTARGET, q.size(), q.constData());
    if (pos < 0)
      break;
    auto end = e->send(SCI_GETTARGETEND);
    if (end > pos) {
      e->send(SCI_INDICATORFILLRANGE, pos, end - pos);
      ++count;
    }
    start = end > pos ? end : e->send(SCI_POSITIONAFTER, pos);
    if (start <= pos)
      break;
  }
  if (!regexFailed(e))
    statusBar()->showMessage(QString("Marked %1 matches").arg(count), 4000);
  return count;
}
void Window::clearMarks() {
  if (auto *t = current()) {
    t->primary->send(SCI_SETINDICATORCURRENT, markIndicator);
    t->primary->send(SCI_INDICATORCLEARRANGE, 0,
                     t->primary->send(SCI_GETLENGTH));
  }
}
// Scans files through a hidden Scintilla view so results use exactly the same
// Boost regex semantics as in-document search. Hidden files/directories and
// files that are not valid UTF-8/UTF-16 text are skipped.
void Window::stopSearch() {
  if (searching)
    cancelSearch = true;
}
int Window::findInFiles(const QString &root, const QString &patterns) {
  if (searching) // re-entered from the event loop below
    return 0;
  struct Running {
    Window *w;
    explicit Running(Window *window) : w(window) {
      w->searching = true;
      w->cancelSearch = false;
      w->findFilesButton->setEnabled(false);
      w->stopButton->setEnabled(true);
    }
    ~Running() {
      w->searching = false;
      w->findFilesButton->setEnabled(true);
      w->stopButton->setEnabled(false);
    }
  } running(this);
  results->clear();
  auto q = query->text().toUtf8();
  if (q.isEmpty() || !QFileInfo(root).isDir()) {
    statusBar()->showMessage("Enter search text and an existing folder", 4000);
    return 0;
  }
  if (!scanner) {
    scanner = makeEditor(this);
    scanner->hide();
    scanner->send(SCI_SETUNDOCOLLECTION, 0);
  }
  auto names = patterns.split(QRegularExpression("[\\s;,]+"),
                              Qt::SkipEmptyParts);
  QDirIterator it(root, names.isEmpty() ? QStringList{"*"} : names,
                  QDir::Files, QDirIterator::Subdirectories);
  int hits = 0, matchedFiles = 0, scanned = 0, slow = 0;
  const int limit = 20000;
  const qint64 slowMs = 5000; // per-file budget for pathological patterns
  bool cancelled = false;
  int skipped = 0;
  const QString fallback =
      filesEncoding->currentIndex() > 0 ? filesEncoding->currentText() : QString();
  while (it.hasNext() && hits < limit) {
    const QString path = it.next();
    // QDir::Files only hides dot-files on Unix filesystems with the hidden
    // attribute semantics of the platform (macOS uses a flag, not the name),
    // so skip dot-directories like .git explicitly everywhere.
    if (QDir(root).relativeFilePath(path).contains(QRegularExpression("(^|/)\\.")))
      continue;
    if (++scanned % 64 == 0) {
      statusBar()->showMessage(QString("Searching… %1 files").arg(scanned));
      // User input stays enabled so Stop / Escape can reach us; the
      // `searching` guard makes re-entry into findInFiles a no-op.
      QCoreApplication::processEvents();
    }
    if (cancelSearch) {
      cancelled = true;
      break;
    }
    if (it.fileInfo().size() > 64 * 1024 * 1024)
      continue;
    DocumentData d;
    QString error;
    if (!DocumentData::read(path, d, error, fallback, nullptr, false)) {
      ++skipped;
      continue;
    }
    QElapsedTimer fileTimer;
    fileTimer.start();
    scanner->send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(d.utf8.constData()));
    scanner->send(SCI_SETSEARCHFLAGS,
                  searchFlags(scanner, 0, SCFIND_REGEXP_EMPTYMATCH_NOTAFTERMATCH));
    QTreeWidgetItem *fileItem = nullptr;
    int fileHits = 0;
    const sptr_t length = scanner->send(SCI_GETLENGTH);
    for (sptr_t start = 0; start <= length && hits < limit;) {
      scanner->send(SCI_SETTARGETRANGE, start, length);
      auto pos = scanner->sends(SCI_SEARCHINTARGET, q.size(), q.constData());
      if (pos < 0)
        break;
      if (fileTimer.elapsed() > slowMs) {
        ++slow;
        break;
      }
      auto end = scanner->send(SCI_GETTARGETEND);
      int line = scanner->send(SCI_LINEFROMPOSITION, pos);
      auto lineStart = scanner->send(SCI_POSITIONFROMLINE, line),
           lineEnd = scanner->send(SCI_GETLINEENDPOSITION, line);
      sptr_t snipLen = qMin<sptr_t>(lineEnd - lineStart, 300);
      // Never cut inside a UTF-8 sequence: back off over continuation bytes.
      while (snipLen > 0 && snipLen < lineEnd - lineStart &&
             (uchar(d.utf8.at(int(lineStart + snipLen))) & 0xC0) == 0x80)
        --snipLen;
      auto text = QString::fromUtf8(d.utf8.mid(lineStart, snipLen));
      if (!fileItem)
        fileItem = new QTreeWidgetItem(results);
      auto *hit = new QTreeWidgetItem(
          fileItem, {QString("%1:  %2").arg(line + 1).arg(text.trimmed())});
      hit->setData(0, Qt::UserRole, path);
      hit->setData(0, Qt::UserRole + 1, qlonglong(pos));
      hit->setData(0, Qt::UserRole + 2, qlonglong(end));
      hit->setData(0, Qt::UserRole + 3, fallback);
      ++fileHits;
      ++hits;
      start = end > pos ? end : scanner->send(SCI_POSITIONAFTER, pos);
      if (start <= pos)
        break;
    }
    if (regexFailed(scanner)) {
      results->clear();
      return 0;
    }
    if (fileTimer.elapsed() > slowMs && !slow) // single search call was slow
      ++slow;
    if (cancelSearch) {
      cancelled = true;
      if (!fileItem)
        break;
    }
    if (fileItem) {
      fileItem->setText(0, QString("%1  (%2)")
                               .arg(QDir(root).relativeFilePath(path))
                               .arg(fileHits));
      fileItem->setData(0, Qt::UserRole, path);
      fileItem->setData(0, Qt::UserRole + 3, fallback);
      ++matchedFiles;
    }
  }
  scanner->send(SCI_CLEARALL);
  if (matchedFiles <= 30)
    results->expandAll();
  QString summary = QString("%1 matches in %2 of %3 files%4")
                        .arg(hits)
                        .arg(matchedFiles)
                        .arg(scanned)
                        .arg(cancelled           ? " (stopped)"
                             : hits >= limit     ? " (stopped at limit)"
                                                 : "");
  if (skipped)
    summary += QString(", %1 skipped (binary or not valid text in the chosen encoding)")
                   .arg(skipped);
  if (slow)
    summary += QString("; %1 file(s) skipped: too slow").arg(slow);
  resultsDock->setWindowTitle("Search results — " + summary);
  resultsDock->show();
  statusBar()->showMessage(summary, 6000);
  return hits;
}
void Window::openResult(QTreeWidgetItem *item) {
  const QString path = item->data(0, Qt::UserRole).toString();
  if (path.isEmpty() ||
      !openFile(path, item->data(0, Qt::UserRole + 3).toString()) ||
      !item->parent())
    return;
  auto *e = current()->focused;
  e->send(SCI_SETSEL, item->data(0, Qt::UserRole + 1).toLongLong(),
          item->data(0, Qt::UserRole + 2).toLongLong());
  e->send(SCI_SCROLLCARET);
  e->setFocus();
}
void Window::goToLine() {
  auto *t = current();
  if (!t)
    return;
  bool ok;
  int line = QInputDialog::getInt(this, "Go to line", "Line number", 1, 1,
                                  t->focused->send(SCI_GETLINECOUNT), 1, &ok);
  if (ok)
    send(SCI_GOTOLINE, line - 1);
}
void Window::bookmark(bool next, bool previous) {
  auto *t = current();
  if (!t)
    return;
  auto *e = t->focused;
  int line = e->send(SCI_LINEFROMPOSITION, e->send(SCI_GETCURRENTPOS));
  if (!next && !previous)
    e->send(e->send(SCI_MARKERGET, line) & 1 ? SCI_MARKERDELETE : SCI_MARKERADD,
            line, 0);
  else {
    auto found = e->send(previous ? SCI_MARKERPREVIOUS : SCI_MARKERNEXT,
                         line + (previous ? -1 : 1), 1);
    if (found < 0)
      found = e->send(previous ? SCI_MARKERPREVIOUS : SCI_MARKERNEXT,
                      previous ? e->send(SCI_GETLINECOUNT) - 1 : 0, 1);
    if (found >= 0)
      e->send(SCI_GOTOLINE, found);
  }
}
// Skipped while the dock is hidden (rebuilt when it is shown again) and
// bounded for huge documents: lines are read one at a time from Scintilla
// instead of copying the whole buffer into a QString.
void Window::rebuildOutline() {
  if (!outlineDock->isVisibleTo(this)) {
    outlineStale = true;
    return;
  }
  outlineStale = false;
  ++outlineBuilds;
  outline->clear();
  auto *t = current();
  if (!t || t->large)
    return;
  QRegularExpression expr(
      t->language == "python"
          ? "^\\s*(?:async\\s+)?(?:def|class)\\s+([A-Za-z_]\\w*)"
      : t->language == "markdown"
          ? "^#{1,6}\\s+(.+)"
          : "^\\s*(?:(?:export|public|private|static|async|const|let|function|"
            "class|struct|void|int|auto|double|float|bool)\\s+)+([\\w:]+)\\s*(?"
            ":\\(|[{:])");
  const int maxLines = 50000, maxLineBytes = 1000;
  const int lines = qMin<int>(t->primary->send(SCI_GETLINECOUNT), maxLines);
  QByteArray buffer;
  for (int line = 0; line < lines; ++line) {
    const int length = int(t->primary->send(SCI_LINELENGTH, line));
    if (length <= 0 || length > maxLineBytes)
      continue; // blank, or minified/generated: not a declaration
    buffer.resize(length);
    t->primary->send(SCI_GETLINE, line, reinterpret_cast<sptr_t>(buffer.data()));
    auto m = expr.match(QString::fromUtf8(buffer.constData(), length));
    if (m.hasMatch()) {
      auto *i = new QListWidgetItem(m.captured(1).trimmed(), outline);
      i->setData(Qt::UserRole, line);
    }
  }
  if (t->primary->send(SCI_GETLINECOUNT) > maxLines)
    outline->addItem("… outline limited to the first 50000 lines");
}
void Window::checkDisk() {
  for (int i = 0; i < tabs->count(); ++i) {
    auto *t = static_cast<EditorTab *>(tabs->widget(i));
    if (!t->data.exists || dismissedChanges.contains(t))
      continue;
    QFileInfo info(t->data.path);
    if (info.exists() && info.lastModified() == t->data.diskTime &&
        info.size() == t->data.diskSize)
      continue;
    QFile f(t->data.path);
    QByteArray raw;
    if (f.open(QIODevice::ReadOnly))
      raw = f.readAll();
    if (f.isOpen() && QCryptographicHash::hash(raw, QCryptographicHash::Sha256) ==
                          t->data.diskHash) {
      t->data.stamp(); // touched, content identical
      continue;
    }
    dismissedChanges.insert(t);
    statusBar()->showMessage(
        info.fileName() + " changed or disappeared on disk. Use File → "
                          "Reload, or Save As to keep your edits.",
        15000);
  }
}
void Window::playMacro() {
  auto *t = current();
  if (!t || recording)
    return;
  auto *e = t->focused;
  e->send(SCI_BEGINUNDOACTION);
  for (int i = 0; i < macro.size(); ++i) {
    auto a = macro[i];
    sptr_t l = macroText[i].isNull()
                   ? a[2]
                   : reinterpret_cast<sptr_t>(macroText[i].constData());
    e->send(a[0], a[1], l);
  }
  e->send(SCI_ENDUNDOACTION);
}
void Window::saveSession(const QString &path, bool onlyIfChanged) {
  QJsonArray docs;
  for (int i = 0; i < tabs->count(); ++i) {
    auto *t = static_cast<EditorTab *>(tabs->widget(i));
    // Only dirty/untitled tabs need their text (and its compression).
    const bool dirty = t->dirty();
    auto o = t->data.json(dirty, t->data.needsBackup(dirty) ? t->text()
                                                              : QByteArray());
    o["language"] = t->language;
    o["cursor"] = int(t->focused->send(SCI_GETCURRENTPOS));
    o["firstLine"] = int(t->focused->send(SCI_GETFIRSTVISIBLELINE));
    o["split"] = !t->secondary->isHidden();
    o["eol"] = int(t->primary->send(SCI_GETEOLMODE));
    docs << o;
  }
  const auto b = QJsonDocument(QJsonObject{{"version", 1},
                                           {"documents", docs},
                                           {"active", tabs->currentIndex()}})
                     .toJson();
  const QByteArray digest = QCryptographicHash::hash(b, QCryptographicHash::Sha1);
  if (onlyIfChanged && digest == lastSessionHash && QFile::exists(path))
    return; // nothing changed since the last snapshot
  QSaveFile f(path);
  if (f.open(QIODevice::WriteOnly)) {
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (f.write(b) == b.size() && f.commit()) {
      lastSessionHash = digest;
      ++sessionWrites;
    }
  }
}
void Window::snapshot() {
  if (!testing)
    saveSession(sessionPath, true);
}
bool Window::restoreSession(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly))
    return false;
  QJsonParseError err;
  auto doc = QJsonDocument::fromJson(f.readAll(), &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject() ||
      doc.object()["version"].toInt() != 1) {
    report("Invalid or unsupported session file.");
    return false;
  }
  QScopedValueRollback<bool> quiet(suppressMacro, true);
  // One summary instead of one modal dialog per unrestorable file.
  QStringList problems;
  deferredReports = &problems;
  auto obj = doc.object();
  int first = tabs->count();
  for (const auto &value : obj["documents"].toArray()) {
    auto o = value.toObject();
    EditorTab *t = nullptr;
    if (o.contains("backup")) {
      auto d = DocumentData::fromJson(o);
      t = newDocument();
      t->data = d;
      t->setText(d.utf8, o["dirty"].toBool());
    } else {
      const QString p = o["path"].toString();
      const int before = problems.size();
      if (openFile(p, o["encoding"].toString()))
        t = current();
      else if (problems.size() > before)
        problems[before] = p + ": " + problems[before];
      else
        problems << p + ": could not be opened";
    }
    if (!t)
      continue;
    applyLanguage(t, o["language"].toString("normal"));
    t->primary->send(SCI_SETEOLMODE, o["eol"].toInt(SC_EOL_LF));
    t->primary->send(SCI_GOTOPOS, o["cursor"].toInt());
    t->primary->send(SCI_SETFIRSTVISIBLELINE, o["firstLine"].toInt());
    if (o["split"].toBool())
      t->toggleSplit();
  }
  if (first + obj["active"].toInt() < tabs->count())
    tabs->setCurrentIndex(first + obj["active"].toInt());
  refresh();
  deferredReports = nullptr;
  if (!problems.isEmpty()) {
    const int shown = qMin(problems.size(), 10);
    QString text = QString("%1 item(s) could not be restored:\n\n")
                       .arg(problems.size()) +
                   problems.mid(0, shown).join('\n');
    if (problems.size() > shown)
      text += QString("\n…and %1 more.").arg(problems.size() - shown);
    report(text);
  }
  return true;
}
// Recovery files left by --new-instance windows that died without a clean
// exit. Only files whose PID is gone are taken; each is deleted after its
// tabs are loaded and re-snapshotted into this window's own file.
QStringList Window::adoptOrphans(const QString &directory) {
  QStringList adopted;
  const qint64 self = QCoreApplication::applicationPid();
  const auto entries = QDir(directory).entryInfoList(
      {"recovery-*.json"}, QDir::Files | QDir::NoSymLinks, QDir::Name);
  for (const auto &info : entries) {
    const qint64 pid = recoveryPid(info.fileName());
    if (!pid || pid == self || pidAlive(pid))
      continue;
    if (restoreSession(info.absoluteFilePath()))
      adopted << info.absoluteFilePath();
    else
      QFile::rename(info.absoluteFilePath(),
                    info.absoluteFilePath() + ".corrupt");
  }
  if (!adopted.isEmpty()) {
    saveSession(sessionPath); // the adopted tabs are now safe in our file
    for (const auto &p : adopted)
      QFile::remove(p);
  }
  return adopted;
}
void Window::restore() {
  if (QFileInfo::exists(sessionPath))
    restoreSession(sessionPath);
  if (shared)
    adoptOrphans(QFileInfo(sessionPath).absolutePath());
  if (!tabs->count())
    newDocument();
}
void Window::closeEvent(QCloseEvent *event) {
  if (testing) {
    event->accept();
    return;
  }
  QSet<EditorTab *> discarded;
  for (int i = 0; i < tabs->count(); ++i) {
    auto *t = static_cast<EditorTab *>(tabs->widget(i));
    if (!t->dirty())
      continue;
    auto choice = QMessageBox::warning(
        this, "Unsaved changes", "Save changes to " + t->title() + "?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (choice == QMessageBox::Cancel ||
        (choice == QMessageBox::Save && !save(t))) {
      event->ignore();
      return;
    }
    if (choice == QMessageBox::Discard)
      discarded.insert(t);
  }
  recoveryTimer.stop();
  for (auto *t : discarded) {
    t->metadataDirty = false;
    t->primary->send(SCI_SETSAVEPOINT);
  }
  if (shared)
    saveSession(sessionPath); // becomes the next launch's session
  else
    QFile::remove(sessionPath); // clean exit: nothing left to recover
  settings.setValue("geometry", saveGeometry());
  settings.setValue("recent", recent);
  settings.setValue("theme", themeChoice->currentText());
  event->accept();
}
void Window::trimTrailingWhitespace(EditorTab *t) {
  if (!t)
    return;
  auto *ed = t->focused;
  ed->send(SCI_BEGINUNDOACTION);
  for (int line = int(ed->send(SCI_GETLINECOUNT)) - 1; line >= 0; --line) {
    auto start = ed->send(SCI_POSITIONFROMLINE, line),
         end = ed->send(SCI_GETLINEENDPOSITION, line), trim = end;
    while (trim > start) {
      int c = ed->send(SCI_GETCHARAT, trim - 1);
      if (c != ' ' && c != '\t')
        break;
      --trim;
    }
    if (trim < end) {
      ed->send(SCI_SETTARGETSTART, trim);
      ed->send(SCI_SETTARGETEND, end);
      ed->sends(SCI_REPLACETARGET, 0, "");
    }
  }
  ed->send(SCI_ENDUNDOACTION);
}
void Window::ensureFinalNewline(EditorTab *t) {
  if (!t)
    return;
  auto *ed = t->focused;
  auto length = ed->send(SCI_GETLENGTH);
  if (!length)
    return;
  int last = ed->send(SCI_GETCHARAT, length - 1);
  if (last == '\n' || last == '\r')
    return;
  int mode = ed->send(SCI_GETEOLMODE);
  ed->send(SCI_APPENDTEXT, mode == SC_EOL_CRLF ? 2 : 1,
           reinterpret_cast<sptr_t>(mode == SC_EOL_CRLF ? "\r\n"
                                    : mode == SC_EOL_CR ? "\r"
                                                        : "\n"));
}
// Whole lines touched by the selection; a selection that ends at the start of a
// line does not include that line. With no selection, `whole` picks the
// document and otherwise only the caret's line.
static QPair<int, int> selectedLines(ScintillaEditBase *e, bool whole = false) {
  auto a = e->send(SCI_GETSELECTIONSTART), b = e->send(SCI_GETSELECTIONEND);
  if (a == b && whole)
    return {0, int(e->send(SCI_GETLINECOUNT)) - 1};
  int first = e->send(SCI_LINEFROMPOSITION, a),
      last = e->send(SCI_LINEFROMPOSITION, b);
  if (b > a && e->send(SCI_POSITIONFROMLINE, last) == b)
    --last;
  return {first, qMax(first, last)};
}
// Comment syntax per language: a line comment, or an open/close pair.
static QPair<QString, QString> commentSyntax(const QString &language) {
  static const QHash<QString, QPair<QString, QString>> table = [] {
    QHash<QString, QPair<QString, QString>> m;
    auto add = [&m](const QString &open, const QString &close,
                    const QStringList &names) {
      for (const auto &n : names)
        m[n] = {open, close};
    };
    add("//", {}, {"actionscript", "c", "cpp", "cs", "d", "go", "java",
                   "javascript", "javascript.js", "json5", "jsp", "objc",
                   "php", "rust", "swift", "typescript", "verilog", "escript",
                   "oscript"});
    add("#", {}, {"bash", "cmake", "coffeescript", "makefile", "nim",
                  "nncrontab", "perl", "powershell", "python", "gdscript", "r",
                  "raku", "ruby", "tcl", "toml", "yaml", "props", "avs"});
    add(";", {}, {"asm", "ini", "lisp", "scheme", "inno", "nsis", "autoit",
                  "kix", "rebol", "hollywood", "csound", "purebasic"});
    add("--", {}, {"ada", "haskell", "mssql", "sql", "vhdl", "asn1"});
    add("'", {}, {"vb", "freebasic", "blitzbasic", "asp"});
    add("%", {}, {"latex", "tex", "matlab", "erlang", "postscript",
                  "txt2tags", "visualprolog"});
    add("REM ", {}, {"batch"});
    add("!", {}, {"fortran", "fortran77"});
    add("<!--", "-->", {"html", "xml"});
    add("/*", "*/", {"css"});
    add("(*", "*)", {"pascal", "caml"});
    return m;
  }();
  return table.value(language);
}
bool Window::toggleComment() {
  auto *t = current();
  if (!t)
    return false;
  const auto syntax = commentSyntax(t->language);
  if (syntax.first.isEmpty()) {
    statusBar()->showMessage("No comment syntax known for this language", 3000);
    return false;
  }
  auto *e = t->focused;
  const QByteArray open = syntax.first.toUtf8(), close = syntax.second.toUtf8();
  const auto range = selectedLines(e);
  auto lineText = [e](int line) {
    auto start = e->send(SCI_POSITIONFROMLINE, line),
         end = e->send(SCI_GETLINEENDPOSITION, line);
    QByteArray b(int(end - start) + 1, '\0');
    Sci_TextRangeFull r{{start, end}, b.data()};
    e->send(SCI_GETTEXTRANGEFULL, 0, reinterpret_cast<sptr_t>(&r));
    b.chop(1);
    return b;
  };
  bool allCommented = true, anyText = false;
  int indent = INT_MAX;
  for (int line = range.first; line <= range.second; ++line) {
    const QByteArray text = lineText(line).trimmed();
    if (text.isEmpty())
      continue;
    anyText = true;
    indent = qMin(indent, int(e->send(SCI_GETLINEINDENTATION, line)));
    if (!text.startsWith(open) || (!close.isEmpty() && !text.endsWith(close)))
      allCommented = false;
  }
  if (!anyText)
    return false;
  e->send(SCI_BEGINUNDOACTION);
  for (int line = range.first; line <= range.second; ++line) {
    if (lineText(line).trimmed().isEmpty())
      continue;
    auto at = e->send(SCI_GETLINEINDENTPOSITION, line),
         end = e->send(SCI_GETLINEENDPOSITION, line);
    if (allCommented) {
      // Remove the closing marker first so the opening position stays valid.
      if (!close.isEmpty()) {
        auto stop = end;
        while (stop > at && (e->send(SCI_GETCHARAT, stop - 1) == ' ' ||
                             e->send(SCI_GETCHARAT, stop - 1) == '\t'))
          --stop;
        auto from = stop - close.size();
        if (from > at && e->send(SCI_GETCHARAT, from - 1) == ' ')
          --from;
        e->send(SCI_SETTARGETRANGE, from, stop);
        e->sends(SCI_REPLACETARGET, 0, "");
      }
      auto len = open.size();
      if (e->send(SCI_GETCHARAT, at + len) == ' ')
        ++len;
      e->send(SCI_SETTARGETRANGE, at, at + len);
      e->sends(SCI_REPLACETARGET, 0, "");
    } else {
      const auto column = e->send(SCI_FINDCOLUMN, line, indent);
      if (!close.isEmpty())
        e->sends(SCI_INSERTTEXT, end, (" " + close).constData());
      e->sends(SCI_INSERTTEXT, column, (open + " ").constData());
    }
  }
  // Keep the lines selected so the command can be repeated to undo itself.
  if (range.first != range.second)
    e->send(SCI_SETSEL, e->send(SCI_POSITIONFROMLINE, range.first),
            e->send(SCI_GETLINEENDPOSITION, range.second));
  e->send(SCI_ENDUNDOACTION);
  return true;
}
// Replaces whole lines [first, last] with `lines`, keeping the document's
// line-ending style and a trailing line break only if there was one.
static void replaceLines(ScintillaEditBase *e, int first, int last,
                         const QList<QByteArray> &lines) {
  const int mode = e->send(SCI_GETEOLMODE);
  const QByteArray eol = mode == SC_EOL_CRLF ? "\r\n" : mode == SC_EOL_CR ? "\r" : "\n";
  auto start = e->send(SCI_POSITIONFROMLINE, first),
       end = e->send(SCI_GETLINEENDPOSITION, last);
  e->send(SCI_SETTARGETRANGE, start, end);
  QByteArray joined;
  for (int i = 0; i < lines.size(); ++i)
    joined += (i ? eol : QByteArray()) + lines[i];
  e->send(SCI_REPLACETARGET, joined.size(),
          reinterpret_cast<sptr_t>(joined.constData()));
}
static QList<QByteArray> readLines(ScintillaEditBase *e, int first, int last) {
  QList<QByteArray> lines;
  for (int line = first; line <= last; ++line) {
    auto start = e->send(SCI_POSITIONFROMLINE, line),
         end = e->send(SCI_GETLINEENDPOSITION, line);
    QByteArray b(int(end - start) + 1, '\0');
    Sci_TextRangeFull r{{start, end}, b.data()};
    e->send(SCI_GETTEXTRANGEFULL, 0, reinterpret_cast<sptr_t>(&r));
    b.chop(1);
    lines << b;
  }
  return lines;
}
int Window::sortLines(SortOrder order) {
  auto *t = current();
  if (!t)
    return 0;
  auto *e = t->focused;
  const auto range = selectedLines(e, true);
  auto lines = readLines(e, range.first, range.second);
  // A final empty line is the document's trailing newline, not content.
  const bool trailing = range.second == e->send(SCI_GETLINECOUNT) - 1 &&
                        !lines.isEmpty() && lines.last().isEmpty() &&
                        lines.size() > 1;
  if (trailing)
    lines.removeLast();
  std::stable_sort(lines.begin(), lines.end(),
                   [order](const QByteArray &a, const QByteArray &b) {
                     if (order == IgnoreCase) {
                       const int c = QString::fromUtf8(a).compare(
                           QString::fromUtf8(b), Qt::CaseInsensitive);
                       return c < 0;
                     }
                     return order == Ascending ? a < b : b < a;
                   });
  if (trailing)
    lines << QByteArray();
  e->send(SCI_BEGINUNDOACTION);
  replaceLines(e, range.first, range.second, lines);
  e->send(SCI_ENDUNDOACTION);
  return lines.size() - trailing;
}
int Window::removeDuplicateLines() {
  auto *t = current();
  if (!t)
    return 0;
  auto *e = t->focused;
  const auto range = selectedLines(e, true);
  auto lines = readLines(e, range.first, range.second);
  const bool trailing = range.second == e->send(SCI_GETLINECOUNT) - 1 &&
                        lines.size() > 1 && lines.last().isEmpty();
  if (trailing)
    lines.removeLast();
  QSet<QByteArray> seen;
  QList<QByteArray> kept;
  for (const auto &line : lines)
    if (!seen.contains(line)) {
      seen.insert(line);
      kept << line;
    }
  const int removed = lines.size() - kept.size();
  if (removed) {
    if (trailing)
      kept << QByteArray();
    e->send(SCI_BEGINUNDOACTION);
    replaceLines(e, range.first, range.second, kept);
    e->send(SCI_ENDUNDOACTION);
  }
  statusBar()->showMessage(QString("Removed %1 duplicate lines").arg(removed),
                           3000);
  return removed;
}
void Window::joinLines() {
  auto *t = current();
  if (!t)
    return;
  auto *e = t->focused;
  auto range = selectedLines(e);
  // A lone caret joins its line with the next one.
  if (range.first == range.second &&
      range.second < e->send(SCI_GETLINECOUNT) - 1)
    ++range.second;
  if (range.first == range.second)
    return;
  e->send(SCI_SETTARGETRANGE, e->send(SCI_POSITIONFROMLINE, range.first),
          e->send(SCI_GETLINEENDPOSITION, range.second));
  e->send(SCI_LINESJOIN);
}
bool Window::openSpec(const QString &spec) {
  if (QFileInfo::exists(spec))
    return openFile(spec);
  static const QRegularExpression suffix(R"(^(.+?):(\d+)(?::(\d+))?$)");
  const auto m = suffix.match(spec);
  if (!m.hasMatch() || !QFileInfo::exists(m.captured(1)))
    return openFile(spec);
  if (!openFile(m.captured(1)))
    return false;
  auto *e = current()->focused;
  const int line = qMax(1, m.captured(2).toInt()) - 1;
  const int column = qMax(1, m.captured(3).isEmpty() ? 1 : m.captured(3).toInt()) - 1;
  e->send(SCI_GOTOPOS, e->send(SCI_FINDCOLUMN, line, column));
  e->send(SCI_SCROLLCARET);
  return true;
}
void Window::dragEnterEvent(QDragEnterEvent *e) {
  if (e->mimeData()->hasUrls())
    e->acceptProposedAction();
}
void Window::dropEvent(QDropEvent *e) {
  for (auto u : e->mimeData()->urls())
    if (u.isLocalFile())
      openFile(u.toLocalFile());
  e->acceptProposedAction();
}
void Window::report(const QString &message) {
  if (deferredReports) {
    *deferredReports << message;
    return;
  }
  if (testing)
    qWarning().noquote() << message;
  else
    QMessageBox::warning(this, "Velox", message);
}
