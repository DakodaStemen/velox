// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Velox Authors
//
// Drives a real window through the editor's features and saves a captioned
// frame after each step, for the animation in the README. Run with
//   QT_QPA_PLATFORM=offscreen build/app/bin/velox --demo <dir>
// then docs/make-demo.sh turns the frames into docs/demo.gif.
#include "ILexer.h"
#include "SciLexer.h"
#include "udleditor.h"
#include "window.h"
#include <functional>

namespace {
const char *cppSample = R"(#include <iostream>
#include <map>
#include <string>
#include <vector>

// Counts how often each word appears.
struct Counter {
  std::map<std::string, int> counts;
  void add(const std::string &word) { ++counts[word]; }
  int total() const {
    int count = 0;
    for (const auto &entry : counts)
      count += entry.second;
    return count;
  }
};

int main() {
  Counter counter;
  std::vector<std::string> words{"fast", "native", "fast", "editor"};
  for (const auto &word : words)
    counter.add(word);
  int count = counter.total(); // TODO: handle empty input
  std::cout << "words: " << count << "\n";
  return 0;
}
)";

class Demo {
public:
  Demo(QApplication &app, const QString &dir) : app(app), out(dir), w(true) {}
  int run();

private:
  QApplication &app;
  QDir out;
  Window w;
  QTemporaryDir work;
  QStringList manifest;
  int frames = 0;

  static constexpr int canvasWidth = 1000, windowHeight = 720, bar = 64;

  void settle(int ms = 80) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
  }
  void shot(const QString &caption, double seconds, QWidget *dialog = nullptr) {
    settle();
    QImage win = w.grab().toImage().convertToFormat(QImage::Format_RGB32);
    if (dialog) {
      QImage d = dialog->grab().toImage();
      QPainter p(&win);
      p.fillRect(win.rect(), QColor(0, 0, 0, 120));
      p.drawImage((win.width() - d.width()) / 2, (win.height() - d.height()) / 2,
                  d);
    }
    compose(win, caption, seconds);
  }
  void compose(const QImage &win, const QString &caption, double seconds) {
    QImage canvas(win.width(), win.height() + bar, QImage::Format_RGB32);
    canvas.fill(QColor("#10141a"));
    QPainter p(&canvas);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.drawImage(0, 0, win);
    p.fillRect(0, win.height(), 6, bar, QColor("#4aa8f5"));
    QFont f("DejaVu Sans", 14);
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor("#e8eef7"));
    p.drawText(QRect(24, win.height(), win.width() - 48, bar),
               Qt::AlignVCenter | Qt::AlignLeft, caption);
    p.end();
    const QString name = QString("frame_%1.png").arg(++frames, 4, 10, QChar('0'));
    canvas.save(out.filePath(name));
    qInfo().noquote() << name << caption;
    manifest << "file '" + name + "'" << QString("duration %1").arg(seconds);
  }
  void card(const QString &title, const QStringList &lines, double seconds) {
    QImage img(canvasWidth, windowHeight, QImage::Format_RGB32);
    QLinearGradient g(0, 0, 0, windowHeight);
    g.setColorAt(0, QColor("#1b2230"));
    g.setColorAt(1, QColor("#10141a"));
    QPainter p(&img);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.fillRect(img.rect(), g);
    QFont t("DejaVu Sans", 40);
    t.setBold(true);
    p.setFont(t);
    p.setPen(QColor("#e8eef7"));
    int y = 150;
    p.drawText(QRect(0, y, img.width(), 80), Qt::AlignCenter, title);
    QFont s("DejaVu Sans", 17);
    p.setFont(s);
    p.setPen(QColor("#9fb0c7"));
    y += 100;
    for (const QString &line : lines) {
      p.drawText(QRect(0, y, img.width(), 40), Qt::AlignCenter, line);
      y += 46;
    }
    p.fillRect(QRect(img.width() / 2 - 40, 140, 80, 4), QColor("#4aa8f5"));
    p.end();
    compose(img, "", seconds);
  }
  bool trigger(const QString &text) {
    for (auto *a : w.findChildren<QAction *>())
      if (a->text() == text) {
        a->trigger();
        return true;
      }
    return false;
  }
  QString put(const QString &name, const QByteArray &bytes) {
    const QString path = work.filePath(name);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(bytes);
    return path;
  }
  sptr_t sci(unsigned int msg, uptr_t a = 0, sptr_t b = 0) {
    return w.current()->focused->send(msg, a, b);
  }
  void gotoLine(int line) { sci(SCI_GOTOLINE, line - 1); }
  int posOf(const char *needle, int from = 0) {
    const QByteArray text = w.current()->text();
    return text.indexOf(needle, from);
  }
  void type(const QByteArray &text, int chunk, const QString &caption,
            double each) {
    for (int i = 0; i < text.size(); i += chunk) {
      sci(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>(text.mid(i, chunk).constData()));
      shot(caption, each);
    }
  }
  // Types like a user at the keyboard, so every selection receives the text.
  void keys(const QString &text) {
    auto *ed = w.current()->focused;
    for (QChar c : text) {
      QKeyEvent press(QEvent::KeyPress, c.toUpper().unicode(), Qt::NoModifier,
                      QString(c));
      QApplication::sendEvent(ed, &press);
    }
  }
  int selectWholeWord(const QByteArray &word, const QString &caption) {
    const QByteArray text = w.current()->text();
    auto isWord = [&text](int p) {
      return p >= 0 && p < text.size() &&
             (isalnum(uchar(text[p])) || text[p] == '_');
    };
    int found = 0;
    for (int i = 0; (i = text.indexOf(word, i)) >= 0; i += word.size()) {
      if (isWord(i - 1) || isWord(i + word.size()))
        continue;
      sci(found ? SCI_ADDSELECTION : SCI_SETSELECTION, i + word.size(), i);
      sci(SCI_SETFIRSTVISIBLELINE, 6); // keep all five occurrences in view
      shot(caption.arg(++found), 0.5);
    }
    return found;
  }
  // Runs `script` inside the modal dialog that `open` shows.
  void modal(const std::function<void(QDialog *)> &script,
             const std::function<void()> &open) {
    QTimer::singleShot(250, &w, [this, script] {
      auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
      if (!d)
        return qWarning("no modal dialog to drive");
      script(d);
    });
    open();
  }
  // Closes the current tab without the "save changes?" prompt.
  void discard() {
    auto *t = w.current();
    t->metadataDirty = false;
    t->primary->send(SCI_SETSAVEPOINT);
    w.closeTab(w.tabs->currentIndex());
  }
  void hideSearch() { w.query->parentWidget()->parentWidget()->hide(); }
  static QByteArray utf16le(const QString &text) {
    QByteArray out;
    for (QChar c : text) {
      out += char(c.unicode() & 0xff);
      out += char(c.unicode() >> 8);
    }
    return out;
  }
  QCheckBox *checkBox(QDialog *d, const QString &text) {
    for (auto *c : d->findChildren<QCheckBox *>())
      if (c->text() == text)
        return c;
    return nullptr;
  }
  QLineEdit *lineEdit(QDialog *d, const QString &placeholder) {
    for (auto *e : d->findChildren<QLineEdit *>())
      if (e->placeholderText() == placeholder)
        return e;
    return nullptr;
  }

  void sceneHighlighting();
  void sceneThemes();
  void sceneMultiCursor();
  void sceneSearch();
  void sceneFindInFiles();
  void sceneSplitAndOutline();
  void sceneLineCommands();
  void sceneBookmarksAndView();
  void sceneEncodings();
  void sceneMacro();
  void sceneExternalChange();
  void sceneCustomLanguage();
  void sceneLargeFile();
  QString projectDir;
};

void Demo::sceneHighlighting() {
  projectDir = work.filePath("project");
  put("project/src/main.cpp", cppSample);
  put("project/src/util.cpp",
      "// TODO: replace with std::filesystem\n#include <string>\nstd::string "
      "base(const std::string &p) { return p.substr(p.rfind('/') + 1); }\n");
  put("project/tools/clean.py",
      "import os\n\n# TODO: also remove *.tmp\ndef clean(root):\n    for name "
      "in os.listdir(root):\n        if name.endswith('.o'):\n            "
      "os.remove(os.path.join(root, name))\n");
  put("project/README.md",
      "# Demo project\n\nA tiny word counter.\n\n- [ ] TODO write docs\n- [x] "
      "count words\n\n## Build\n\n```sh\nmake\n```\n");
  const QString main = put("project/src/main.cpp", cppSample);
  auto *model = w.findChild<QFileSystemModel *>();
  auto *view = w.findChild<QTreeView *>();
  if (model && view)
    view->setRootIndex(model->setRootPath(projectDir));
  w.findChild<QDockWidget *>("workspace")->show();
  w.openFile(main);
  w.applyLanguage(w.current(), "cpp");
  gotoLine(1);
  shot("Native editor: tabs, folder sidebar and syntax highlighting for ~95 languages", 2.4);
  w.openFile(work.filePath("project/tools/clean.py"));
  shot("Open as many files as you like, each in its own tab", 1.2);
  w.openFile(work.filePath("project/README.md"));
  shot("Python, Markdown, C++ …  the language is detected from the file", 1.4);
  w.openFile(main);
  w.findChild<QDockWidget *>("workspace")->hide();
}
void Demo::sceneThemes() {
  for (const QString &theme : {"Light", "Solarized", "Zenburn", "Monokai",
                               "DarkModeDefault"}) {
    w.themeChoice->setCurrentText(theme);
    shot("22 colour themes — " + theme, theme == "DarkModeDefault" ? 1.0 : 0.9);
  }
}
void Demo::sceneMultiCursor() {
  const int n = selectWholeWord("count", "Multiple cursors: %1 occurrences of \"count\" selected");
  Q_UNUSED(n);
  keys("sum");
  sci(SCI_SETFIRSTVISIBLELINE, 6);
  shot("…and one edit changes them all", 1.8);
  sci(SCI_UNDO);
  sci(SCI_UNDO);
  sci(SCI_UNDO);
  sci(SCI_SETEMPTYSELECTION, 0);
  // A zero-width rectangular selection puts a cursor on every line of the block.
  sci(SCI_SETRECTANGULARSELECTIONANCHOR, 0);
  sci(SCI_SETRECTANGULARSELECTIONCARET, sci(SCI_POSITIONFROMLINE, 3));
  sci(SCI_SETFIRSTVISIBLELINE, 0);
  shot("Rectangular selection (Alt+drag): one cursor per line", 1.3);
  keys("// ");
  sci(SCI_SETFIRSTVISIBLELINE, 0);
  shot("Column edits — here commenting out four lines at once", 1.8);
  for (int i = 0; i < 3; ++i)
    sci(SCI_UNDO);
  sci(SCI_SETEMPTYSELECTION, 0);
}
void Demo::sceneSearch() {
  trigger("Replace…");
  w.query->setText("std::\\w+");
  w.regex->setChecked(true);
  shot("Regular-expression find with captures and lookarounds", 1.3);
  w.markAll();
  shot("Mark all: every match highlighted at once", 1.8);
  w.clearMarks();
  w.query->setText("int (\\w+) =");
  w.replacement->setText("long \\1 =");
  shot("Replace with capture groups", 1.2);
  w.replaceAll();
  shot("Replace all — one undo step", 1.8);
  sci(SCI_UNDO);
  w.regex->setChecked(false);
  w.query->clear();
  w.replacement->clear();
  hideSearch();
}
void Demo::sceneFindInFiles() {
  trigger("Find in files…");
  w.folder->setText(projectDir);
  w.filters->setText("*.cpp *.py *.md");
  w.query->setText("TODO");
  w.regex->setChecked(false);
  shot("Find in files", 0.9);
  w.findInFiles(projectDir, "*.cpp *.py *.md");
  shot("…with a clickable results panel — every TODO in the project", 2.2);
  if (auto *dock = w.findChild<QDockWidget *>("results"))
    dock->hide();
  hideSearch();
}
void Demo::sceneSplitAndOutline() {
  trigger("Split document");
  w.findChild<QDockWidget *>("outline")->show();
  settle(900); // the outline refreshes on a timer
  w.current()->secondary->send(SCI_SETFIRSTVISIBLELINE, 14);
  shot("Split view of one document, plus a live document outline", 2.2);
  trigger("Split document");
  w.findChild<QDockWidget *>("outline")->hide();
}
void Demo::sceneLineCommands() {
  w.newDocument("pear\napple\nbanana\napple\ncherry\n");
  w.applyLanguage(w.current(), "normal");
  shot("Line tools — start with an unsorted list", 1.1);
  w.sortLines(Window::Ascending);
  shot("Sort lines (ascending, descending or ignoring case)", 1.3);
  sci(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>("apple\napple\nbanana\ncherry\ncherry\n"));
  w.removeDuplicateLines();
  shot("Remove duplicate lines", 1.3);
  w.statusBar()->clearMessage();
  sci(SCI_SETTEXT, 0,
      reinterpret_cast<sptr_t>("import os\nimport sys\n\ndef main():\n    print(sys.argv)\n"));
  w.applyLanguage(w.current(), "python");
  sci(SCI_SELECTALL);
  shot("Toggle comment (Ctrl+/) — here for Python", 1.0);
  w.toggleComment();
  shot("Comments added to the selected lines; press again to remove", 1.6);
  w.toggleComment();
  sci(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>("1. wake up\n3. ship it\n2. write code\n"));
  w.applyLanguage(w.current(), "normal");
  gotoLine(3);
  shot("Move lines with Alt+Up / Alt+Down…", 1.1);
  trigger("Move line up");
  shot("…to put a line where it belongs", 1.6);
  discard();
}
void Demo::sceneBookmarksAndView() {
  w.openFile(work.filePath("project/src/main.cpp"));
  gotoLine(7);
  trigger("Toggle bookmark");
  gotoLine(10);
  trigger("Toggle bookmark");
  gotoLine(22);
  trigger("Toggle bookmark");
  shot("Bookmarks in the margin; F2 jumps to the next one", 1.8);
  trigger("Show whitespace");
  shot("Show whitespace and indentation guides", 1.6);
  trigger("Show whitespace");
  gotoLine(1);
}
void Demo::sceneEncodings() {
  const QString utf16 = put("encodings/utf16.txt",
                            QByteArray::fromHex("fffe") +
                                utf16le(QString::fromUtf8(
                                    "Grüße aus Köln — 你好 🌍\r\nUTF-16 with a BOM\r\n")));
  w.openFile(utf16);
  shot("Encodings are detected: UTF-16 LE with a BOM, CRLF line endings", 2.0);
  w.encodingChoice->setCurrentText("UTF-32LE");
  shot("Save as UTF-8, UTF-16 or UTF-32 — or any legacy code page", 1.6);
  w.eolChoice->setCurrentIndex(0);
  shot("Convert line endings (LF, CRLF, CR) in one click", 1.4);
  const QString latin = put("encodings/latin1.txt", QByteArray("caf\xe9 cr\xe8me br\xfbl\xe9""e\n"));
  w.openFile(latin, "Windows-1252");
  shot("Open legacy files with an explicit encoding (here Windows-1252)", 1.8);
  discard();
  discard();
}
void Demo::sceneMacro() {
  w.newDocument("alpha\nbeta\ngamma\n");
  w.applyLanguage(w.current(), "normal");
  gotoLine(1);
  trigger("Start / stop recording");
  sci(SCI_HOME);
  sci(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>("- "));
  sci(SCI_LINEDOWN);
  shot("Macros: record a few edits (● REC in the status bar)…", 1.6);
  trigger("Start / stop recording");
  trigger("Playback");
  shot("…and play them back", 1.0);
  trigger("Playback");
  shot("…as often as you like", 1.6);
  discard();
}
void Demo::sceneExternalChange() {
  const QString path = put("notes.txt", "shopping list\n- milk\n- bread\n");
  w.openFile(path);
  shot("Careful with your files: edits from other programs are detected…", 1.5);
  QFile f(path);
  f.open(QIODevice::Append);
  f.write("- eggs (added by another program)\n");
  f.close();
  w.checkDisk();
  shot("…and Velox never overwrites them. Saves are atomic, with crash recovery", 3.0);
  discard();
}
void Demo::sceneCustomLanguage() {
  w.newDocument("# shopping list\nTODO milk\nDONE bread\nTODO \"eggs\" /* free range */\n");
  w.applyLanguage(w.current(), "normal");
  shot("Define your own language: plain text to start with", 1.3);
  modal(
      [this](QDialog *d) {
        for (auto *e : d->findChildren<QLineEdit *>())
          if (e->placeholderText().isEmpty() && e->text().isEmpty()) {
            e->setText("Todo List"); // the name field is the only one without a placeholder
            break;
          }
        lineEdit(d, "e.g. log txt")->setText("todo");
        lineEdit(d, "e.g. #")->setText("#");
        lineEdit(d, "/*")->setText("/*");
        lineEdit(d, "*/")->setText("*/");
        QStringList keywordGroups{"TODO", "DONE"};
        for (auto *e : d->findChildren<QLineEdit *>())
          if (e->placeholderText() == "words separated by spaces" &&
              !keywordGroups.isEmpty())
            e->setText(keywordGroups.takeFirst());
        shot("Keywords, comments, strings and colours — no XML needed", 2.4, d);
        d->accept();
      },
      [this] { w.editUserLanguage(); });
  shot("Highlighted immediately; saved as a language you can edit later", 2.4);
  discard();
}
void Demo::sceneLargeFile() {
  QFile f(work.filePath("big.cpp"));
  f.open(QIODevice::WriteOnly);
  const QByteArray block = QByteArray("int value = compute(1, 2); // filler\n").repeated(64 * 1024);
  while (f.size() <= largeFileSize)
    f.write(block);
  f.close();
  w.openFile(f.fileName());
  shot("Large files (here 33 MiB) open instantly; highlighting switches off to stay fast", 2.6);
  discard();
}

int Demo::run() {
  // The test profile persists between runs; a leftover language would make the
  // "define a language" step fail validation and raise a dialog.
  QFile::remove(configPath("userDefineLangs/todo-list.xml"));
  w.reloadLanguages();
  out.mkpath(".");
  for (const auto &old : out.entryList({"frame_*.png"}, QDir::Files))
    out.remove(old);
  w.resize(canvasWidth, windowHeight);
  w.show();
  settle(300);
  card("Velox", {"A fast, lightweight native text and code editor for Linux", "",
                 "Built on Scintilla and Lexilla · GPL-3.0-or-later"},
       2.2);
  for (void (Demo::*scene)() :
       {&Demo::sceneHighlighting, &Demo::sceneThemes, &Demo::sceneMultiCursor,
        &Demo::sceneSearch, &Demo::sceneFindInFiles, &Demo::sceneSplitAndOutline,
        &Demo::sceneLineCommands, &Demo::sceneBookmarksAndView,
        &Demo::sceneEncodings, &Demo::sceneMacro, &Demo::sceneExternalChange,
        &Demo::sceneCustomLanguage, &Demo::sceneLargeFile}) {
    w.statusBar()->clearMessage(); // each scene starts without the last one's message
    (this->*scene)();
  }
  w.statusBar()->clearMessage();
  // Preferences last of the dialogs: it changes the font for everything after.
  w.openFile(work.filePath("project/src/main.cpp"));
  w.applyLanguage(w.current(), "cpp");
  w.statusBar()->clearMessage();
  modal(
      [this](QDialog *d) {
        shot("Preferences: font, tabs, wrapping, line numbers, save behaviour", 2.0, d);
        auto spins = d->findChildren<QSpinBox *>();
        spins[0]->setValue(14);
        spins[1]->setValue(2);
        checkBox(d, "Show whitespace")->setChecked(true);
        checkBox(d, "Trim trailing whitespace when saving")->setChecked(true);
        shot("Preferences: bigger font, 2-space tabs, whitespace shown", 1.6, d);
        d->accept();
      },
      [this] { w.showPreferences(); });
  w.statusBar()->clearMessage();
  shot("Applied to every open document straight away", 1.8);
  w.statusBar()->clearMessage();
  w.openSpec(work.filePath("project/src/main.cpp") + ":22:9");
  shot("velox main.cpp:22:9 — open straight at a line and column", 2.0);
  card("Velox",
       {"Atomic saves · crash recovery · one window for all your files",
        "UTF-8/16/32 and legacy encodings · 95 languages · 22 themes",
        "Qt 5 and Qt 6 · free software (GPL-3.0-or-later)"},
       2.6);
  QFile::remove(configPath("userDefineLangs/todo-list.xml"));
  QFile list(out.filePath("frames.txt"));
  if (!list.open(QIODevice::WriteOnly))
    return 1;
  // The concat demuxer ignores the duration of the last entry unless it is repeated.
  const QString last = manifest.at(manifest.size() - 2);
  list.write((manifest.join('\n') + "\n" + last + "\n").toUtf8());
  qInfo().noquote() << frames << "frames written to" << out.absolutePath();
  return 0;
}
} // namespace

int runDemo(QApplication &app, const QString &outputDir) {
  Demo demo(app, outputDir);
  return demo.run();
}
