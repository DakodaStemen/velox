// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Velox Authors
//
// Scripted tour of the real window for the animation in the README. Built only
// with CONFIG+=demo (DEMO=1 ./build.sh); never part of a release build.
//   QT_QPA_PLATFORM=offscreen build/app/bin/velox --demo <dir>
// renders frame_0001.png, ... at a fixed rate; docs/make-demo.sh turns them
// into docs/demo.gif.
#include "window.h"
#include <cmath>

namespace {
const char *mainCpp = R"(#include <vector>
#include <map>
#include <iostream>
#include <string>

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

const char *const THEMES[] = {"Ruby Blue", "Monokai", "Obsidian"};
constexpr int fps = 12, stripHeight = 44;

class Demo {
public:
  Demo(Window &w, const QString &dir) : w(w), out(dir) {}
  int run();

private:
  Window &w;
  QDir out;
  QDir work{QDir::temp().filePath("wordcount-demo")};
  QString caption, project;
  double now = 0, nextFrame = 0, lastInput = 0;
  int frames = 0;

  ScintillaEditBase *ed() { return w.current()->focused; }
  sptr_t sci(unsigned msg, uptr_t a = 0, sptr_t b = 0) {
    return ed()->send(msg, a, b);
  }
  QString put(const QString &name, const QByteArray &bytes) {
    const QString path = work.filePath(name);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(bytes);
    return path;
  }
  bool trigger(const QString &text) {
    for (auto *a : w.findChildren<QAction *>())
      if (a->text() == text) {
        a->trigger();
        return true;
      }
    qWarning() << "no action" << text;
    return false;
  }
  int posOf(const QByteArray &needle, int from = 0) {
    return w.current()->text().indexOf(needle, from);
  }

  // Writes one frame of the current state.
  void shoot() {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    // The caret is drawn by hand so that it blinks on the video clock and
    // restarts its cycle on every keystroke, like a real one.
    const bool on = std::fmod(now - lastInput, 1000.0) < 560;
    if (w.current())
      for (auto *e : {w.current()->primary, w.current()->secondary}) {
        e->send(SCI_SETCARETPERIOD, 0);
        e->send(SCI_SETCARETSTYLE, on ? CARETSTYLE_LINE : CARETSTYLE_INVISIBLE);
        e->send(SCI_SETCARETWIDTH, 2);
      }
    QImage win = w.grab().toImage().convertToFormat(QImage::Format_RGB32);
    QImage img(win.width(), win.height() + stripHeight, QImage::Format_RGB32);
    img.fill(QColor("#141920"));
    QPainter p(&img);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.drawImage(0, 0, win);
    p.fillRect(0, win.height(), win.width(), 1, QColor("#394352"));
    p.fillRect(18, win.height() + 14, 4, 16, QColor("#4aa8f5"));
    QFont f(QApplication::font());
    f.setPointSize(12);
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    p.setPen(QColor("#dfe6ef"));
    p.drawText(QRect(32, win.height(), win.width() - 50, stripHeight),
               Qt::AlignVCenter | Qt::AlignLeft, caption);
    p.end();
    img.save(out.filePath(QString("frame_%1.png").arg(++frames, 4, 10, QChar('0'))));
  }
  // Moves the video clock forward, emitting a frame at every tick.
  void wait(double ms) {
    const double end = now + ms;
    while (nextFrame <= end) {
      now = nextFrame;
      shoot();
      nextFrame += 1000.0 / fps;
    }
    now = end;
  }
  void say(const QString &text) { caption = text; }
  // Human typing speed: uneven, slower after punctuation, a beat at line ends.
  void type(const QByteArray &text, int base = 24) {
    for (int i = 0; i < text.size(); ++i) {
      const QByteArray ch = text.mid(i, 1);
      sci(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>(ch.constData()));
      sci(SCI_SCROLLCARET);
      lastInput = now;
      double d = base * (0.5 + (qrand01() * 1.1));
      if (ch == "\n")
        d += 70 + qrand01() * 80;
      else if (QByteArray(" (,;{").contains(ch[0]))
        d += 25;
      wait(d);
    }
  }
  void typeField(QLineEdit *field, const QString &text, int base = 55) {
    field->setFocus();
    for (const QChar c : text) {
      field->setText(field->text() + c);
      lastInput = now;
      wait(base * (0.6 + qrand01()));
    }
  }
  void key(int k, const QString &text = {}) {
    QKeyEvent press(QEvent::KeyPress, k, Qt::NoModifier, text);
    QApplication::sendEvent(ed(), &press);
    lastInput = now;
  }
  void keys(const QString &text, int base = 70) {
    for (const QChar c : text) {
      key(c.toUpper().unicode(), QString(c));
      wait(base * (0.7 + qrand01() * 0.7));
    }
  }
  // Eased scroll to a first visible line, so jumps read as motion.
  void scrollTo(int line, double ms = 450) {
    const int from = int(sci(SCI_GETFIRSTVISIBLELINE));
    const int steps = qMax(1, int(ms * fps / 1000));
    for (int i = 1; i <= steps; ++i) {
      const double t = double(i) / steps, e = t * t * (3 - 2 * t);
      sci(SCI_SETFIRSTVISIBLELINE, from + int(std::lround((line - from) * e)));
      wait(1000.0 / fps);
    }
  }
  // Deterministic pseudo-random numbers in [0, 1), so reruns are identical.
  double qrand01() {
    seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
    return double(seed >> 40) / double(1ULL << 24);
  }
  quint64 seed = 12345;
};

int Demo::run() {
  work.removeRecursively();
  project = work.filePath("wordcount");
  put("wordcount/src/main.cpp", mainCpp);
  put("wordcount/src/util.cpp",
      "// TODO: replace with std::filesystem\n#include <string>\n"
      "std::string base(const std::string &p) {\n"
      "  return p.substr(p.rfind('/') + 1);\n}\n");
  put("wordcount/tools/clean.py",
      "import os\n\n# TODO: also remove *.tmp\ndef clean(root):\n"
      "    for name in os.listdir(root):\n        if name.endswith('.o'):\n"
      "            os.remove(os.path.join(root, name))\n");
  put("wordcount/README.md",
      "# Word count\n\nA tiny word counter.\n\n- [ ] TODO write docs\n"
      "- [x] count words\n");
  QApplication::setCursorFlashTime(0);
  QApplication::setFont(QFont("Inter", 10));
  w.prefs.font = "JetBrains Mono";
  w.prefs.fontSize = 12;
  w.resize(1100, 720);
  w.show();
  w.activateWindow();
  w.findChild<QDockWidget *>("workspace")->show();
  w.findChild<QDockWidget *>("outline")->show();
  w.resizeDocks({w.findChild<QDockWidget *>("workspace")}, {190}, Qt::Horizontal);
  w.resizeDocks({w.findChild<QDockWidget *>("outline")}, {190}, Qt::Horizontal);
  QCoreApplication::processEvents();
  if (auto *dock = w.findChild<QDockWidget *>("workspace"))
    if (auto *tree = dock->findChild<QTreeView *>())
      if (auto *model = qobject_cast<QFileSystemModel *>(tree->model())) {
        model->setRootPath(project);
        tree->setRootIndex(model->index(project));
        QEventLoop loop; // the model fills in on a worker thread
        QTimer::singleShot(600, &loop, &QEventLoop::quit);
        loop.exec();
        tree->expandAll();
      }
  w.openFile(project + "/src/main.cpp");
  w.themeChoice->setCurrentText("Obsidian");
  w.applyPreferences(w.current());
  w.applyTheme(w.current());
  w.rebuildOutline();
  sci(SCI_SETFOCUS, 1);
  sci(SCI_GOTOPOS, 0);
  say("Native editor, 95 languages, 22 themes");
  wait(800);

  // 1. Typing with highlighting.
  say("Type with live syntax highlighting");
  const int mainPos = posOf("int main");
  sci(SCI_GOTOPOS, mainPos);
  scrollTo(qMax(0, int(sci(SCI_LINEFROMPOSITION, mainPos)) - 16), 600);
  wait(200);
  type("// Most frequent word.\nstd::string top(const Counter &c) {\n"
       "  std::string best;\n  int hits = 0;\n"
       "  for (const auto &[w, n] : c.counts)\n    if (n > hits)\n"
       "      best = w, hits = n;\n  return best;\n}\n\n", 12);
  w.rebuildOutline();
  wait(700);

  // 2. Regex find.
  say("Regex find, Ctrl+F: matches highlight in place");
  trigger("Find…");
  wait(350);
  w.regex->setChecked(true);
  typeField(w.query, "\\bcount\\w*", 40);
  wait(250);
  w.markAll();
  w.find();
  wait(1100);
  trigger("Clear marks");
  w.query->parentWidget()->parentWidget()->hide();
  sci(SCI_SETFOCUS, 1);
  wait(250);

  // 3. Multiple cursors.
  say("Multiple cursors: rename every `word` at once");
  scrollTo(5, 350);
  {
    const QByteArray text = w.current()->text();
    int found = 0;
    for (int i = 0; (i = text.indexOf("word", i)) >= 0; i += 4) {
      const bool before = i > 0 && (isalnum(uchar(text[i - 1])) || text[i - 1] == '_');
      const bool after = i + 4 < text.size() && (isalnum(uchar(text[i + 4])) || text[i + 4] == '_');
      if (before || after)
        continue;
      sci(found++ ? SCI_ADDSELECTION : SCI_SETSELECTION, i + 4, i);
      wait(200);
    }
  }
  wait(350);
  key(Qt::Key_Backspace);
  wait(250);
  keys("token", 55);
  wait(900);

  // 4. Line commands.
  say("Line tools: sort, comment, move");
  scrollTo(0, 300);
  sci(SCI_SETSELECTION, 0, int(sci(SCI_POSITIONFROMLINE, 4)));
  wait(500);
  trigger("Ascending");
  wait(800);
  const int nl = int(sci(SCI_LINEFROMPOSITION, posOf("std::cout")));
  sci(SCI_GOTOLINE, nl);
  scrollTo(qMax(0, nl - 18), 350);
  wait(200);
  trigger("Toggle comment");
  wait(700);
  trigger("Toggle comment");
  wait(150);

  // 5. Bookmarks.
  say("Bookmarks: toggle and jump (Ctrl+F2, F2)");
  for (const QByteArray &needle : {QByteArray("std::string top"), QByteArray("int main")}) {
    sci(SCI_GOTOPOS, posOf(needle));
    wait(300);
    trigger("Toggle bookmark");
    wait(300);
  }
  sci(SCI_GOTOLINE, 0);
  scrollTo(0, 250);
  trigger("Next bookmark");
  wait(700);

  // 6. Find in files.
  say("Find in files: results open at the matching line");
  w.findChild<QDockWidget *>("workspace")->hide();
  w.findChild<QDockWidget *>("outline")->hide();
  w.regex->setChecked(false);
  w.query->clear();
  w.folder->setText(project);
  w.filters->setText("*.cpp *.py *.md");
  trigger("Find in files…");
  w.query->clear();
  wait(350);
  typeField(w.query, "TODO", 70);
  wait(250);
  w.findInFiles(project, w.filters->text());
  w.findChild<QDockWidget *>("results")->show();
  w.resizeDocks({w.findChild<QDockWidget *>("results")}, {190}, Qt::Vertical);
  w.results->expandAll();
  w.results->setCurrentItem(w.results->topLevelItem(0)
                                ? w.results->topLevelItem(0)->child(0)
                                : nullptr);
  wait(1300);
  w.query->parentWidget()->parentWidget()->hide();
  w.findChild<QDockWidget *>("results")->hide();
  sci(SCI_SETFOCUS, 1);
  wait(200);

  // 7. Split view.
  say("Split view: two windows onto one document");
  wait(250);
  trigger("Split document");
  w.current()->secondary->send(SCI_SETFIRSTVISIBLELINE, 0);
  wait(700);
  w.current()->primary->send(SCI_SETFIRSTVISIBLELINE, 14);
  wait(800);

  // 8. Themes.
  for (const char *theme : THEMES) {
    say(QString("Theme: %1 (22 included)").arg(theme));
    w.themeChoice->setCurrentText(theme);
    wait(900);
  }
  wait(300);
  // Back to the opening layout so the loop restarts without a jump.
  say("Native editor, 95 languages, 22 themes");
  trigger("Split document");
  w.findChild<QDockWidget *>("workspace")->show();
  w.findChild<QDockWidget *>("outline")->show();
  sci(SCI_GOTOPOS, 0);
  sci(SCI_SETFIRSTVISIBLELINE, 0);
  wait(500);
  work.removeRecursively();
  qInfo().noquote() << frames << "frames," << QString::number(now / 1000, 'f', 1) << "s";
  return 0;
}
} // namespace

int runDemo(Window &w, const QString &dir) {
  QDir().mkpath(dir);
  return Demo(w, dir).run();
}
