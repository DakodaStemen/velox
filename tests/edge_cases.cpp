// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Velox Authors
#include "ILexer.h"
#include "Lexilla.h"
#include "SciLexer.h"
#include "palette.h"
#include "udleditor.h"
#include "window.h"
#include <QTextCodec>
#include <QtTest>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#include <unistd.h>
#endif

static bool isRoot() {
#ifdef Q_OS_UNIX
  return geteuid() == 0;
#else
  return false;
#endif
}
QStringList parseForwardedFiles(const QByteArray &data, bool *ok);

class EdgeCases : public QObject {
  Q_OBJECT
  static void put(const QString &p, const QByteArray &b) {
    QFile f(p);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QCOMPARE(f.write(b), qint64(b.size()));
  }
  static QByteArray get(const QString &p) {
    QFile f(p);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
  }
  static bool trigger(Window &w, const QString &text) {
    for (auto *a : w.findChildren<QAction *>())
      if (a->text() == text) {
        a->trigger();
        return true;
      }
    return false;
  }
  static sptr_t at(EditorTab *t, unsigned int msg, uptr_t w = 0, sptr_t l = 0) {
    return t->primary->send(msg, w, l);
  }

private slots:
  // ---- file reading and writing -------------------------------------------
  void emptyAndBomOnlyFiles() {
    QTemporaryDir dir;
    for (const auto &raw : {QByteArray(), QByteArray::fromHex("efbbbf"),
                            QByteArray::fromHex("fffe"),
                            QByteArray::fromHex("feff")}) {
      QString p = dir.filePath("f");
      put(p, raw);
      DocumentData d;
      QString error;
      QVERIFY2(DocumentData::read(p, d, error), qPrintable(error));
      QVERIFY(d.utf8.isEmpty());
      QVERIFY2(d.write(d.utf8, p, error), qPrintable(error));
      QCOMPARE(get(p), raw);
    }
  }
  void rejectsMalformedAndBinaryInput() {
#ifdef Q_OS_WIN
    QSKIP("needs POSIX symlinks, FIFOs or permission bits");
#else
    QTemporaryDir dir;
    QString p = dir.filePath("bad");
    DocumentData d;
    QString error;
    for (const auto &raw :
         {QByteArray::fromHex("fffe6100"
                              "62"),          // UTF-16LE with odd byte count
          QByteArray::fromHex("fffe0000610000"), // UTF-32LE
          QByteArray::fromHex("c328"),           // invalid UTF-8
          QByteArray::fromHex("ed a0 80"),       // UTF-8 encoded surrogate
          QByteArray("abc\0def", 7)}) {          // NUL byte
      put(p, raw);
      error.clear();
      QVERIFY2(!DocumentData::read(p, d, error), raw.toHex().constData());
      QVERIFY(!error.isEmpty());
    }
    QVERIFY(!DocumentData::read(dir.filePath("missing"), d, error));
    QVERIFY(!DocumentData::read(dir.path(), d, error));
    QString fifo = dir.filePath("fifo");
    QVERIFY(::mkfifo(QFile::encodeName(fifo), 0600) == 0);
    QVERIFY(!DocumentData::read(fifo, d, error)); // must not block
    QVERIFY(!DocumentData::read("/dev/zero", d, error)); // must not exhaust memory
    QVERIFY(!DocumentData::read("/dev/null", d, error));
#endif
  }
  void unicodePathsAndLongLines() {
    QTemporaryDir dir;
    QString p = dir.filePath(QString::fromUtf8("Ünïcødé 文件 #1 (copy).txt"));
    QByteArray line(5 * 1024 * 1024, 'x');
    line += QString::fromUtf8(" 🌍 end").toUtf8();
    put(p, line);
    Window w(true);
    QVERIFY(w.openFile(p));
    QCOMPARE(w.current()->text(), line);
    QCOMPARE(at(w.current(), SCI_GETLINECOUNT), sptr_t(1));
    QVERIFY(w.save(w.current()));
    QCOMPARE(get(p), line);
  }
  void mixedAndLoneCarriageReturns() {
    QTemporaryDir dir;
    QString p = dir.filePath("cr.txt");
    put(p, "a\rb\rc\r\nd");
    Window w(true);
    QVERIFY(w.openFile(p));
    QCOMPARE(at(w.current(), SCI_GETEOLMODE), sptr_t(SC_EOL_CR));
    QCOMPARE(at(w.current(), SCI_GETLINECOUNT), sptr_t(4));
    QVERIFY(w.save(w.current()));
    QCOMPARE(get(p), QByteArray("a\rb\rc\r\nd")); // untouched EOLs stay
    w.eolChoice->setCurrentIndex(0);              // convert to LF
    QVERIFY(w.save(w.current()));
    QCOMPARE(get(p), QByteArray("a\nb\nc\nd"));
  }
  void failedSavesNeverDamageTheOriginal() {
#ifdef Q_OS_WIN
    QSKIP("needs POSIX symlinks, FIFOs or permission bits");
#else
    QTemporaryDir dir;
    QString sub = dir.filePath("locked"), p = sub + "/file.txt";
    QVERIFY(QDir().mkpath(sub));
    put(p, "original\n");
    Window w(true);
    QVERIFY(w.openFile(p));
    auto *t = w.current();
    t->primary->send(SCI_APPENDTEXT, 4, reinterpret_cast<sptr_t>("more"));
    QVERIFY(::chmod(QFile::encodeName(sub), 0555) == 0);
    bool saved = w.save(t);
    ::chmod(QFile::encodeName(sub), 0755);
    if (!isRoot()) { // root ignores directory permissions
      QVERIFY(!saved);
      QCOMPARE(get(p), QByteArray("original\n"));
      QVERIFY(t->dirty());
    }
    // Lossy encoding change is refused and leaves the file alone.
    t->setText(QString::fromUtf8("emoji 🌍\n").toUtf8(), true);
    w.encodingChoice->setCurrentText("Windows-1252");
    QVERIFY(!w.save(t));
    QVERIFY(get(p).startsWith("original"));
    // Dangling symlink and directory destinations are refused.
    QString link = dir.filePath("dangling");
    QVERIFY(::symlink("/nonexistent/target", QFile::encodeName(link)) == 0);
    DocumentData d;
    QString error;
    QVERIFY(!d.write("x", link, error));
    QVERIFY(!d.write("x", dir.path(), error));
#endif
  }
  void openRejectsDirectoriesMissingAndHugeFiles() {
    QTemporaryDir dir;
    Window w(true);
    QVERIFY(!w.openFile(dir.filePath("nope.txt")));
    QVERIFY(!w.openFile(dir.path()));
    QString huge = dir.filePath("huge.log");
    QFile f(huge);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QVERIFY(f.resize(qint64(129) * 1024 * 1024)); // sparse, no real disk use
    f.close();
    QVERIFY(!w.openFile(huge));
    QCOMPARE(w.tabs->count(), 0);
  }
  void sameFileThroughSymlinkOpensOnce() {
#ifdef Q_OS_WIN
    QSKIP("needs POSIX symlinks, FIFOs or permission bits");
#else
    QTemporaryDir dir;
    QString real = dir.filePath("real.txt"), link = dir.filePath("link.txt");
    put(real, "x");
    QVERIFY(::symlink(QFile::encodeName(real), QFile::encodeName(link)) == 0);
    Window w(true);
    QVERIFY(w.openFile(real));
    QVERIFY(w.openFile(link));
    QVERIFY(w.openFile(dir.path() + "/./real.txt"));
    QCOMPARE(w.tabs->count(), 1);
#endif
  }

  // ---- search and replace --------------------------------------------------
  void searchEdgeCases() {
    Window w(true);
    auto *t = w.newDocument("");
    w.query->setText("x");
    QVERIFY(!w.find());
    QCOMPARE(w.replaceAll(), 0);
    QCOMPARE(w.markAll(), 0);
    w.query->clear();
    t->setText("abc");
    QVERIFY(!w.find());
    QCOMPARE(w.replaceAll(), 0);
    // Replacement containing the match must not loop forever.
    w.query->setText("a");
    w.replacement->setText("aa");
    t->setText("a a a");
    QCOMPARE(w.replaceAll(), 3);
    QCOMPARE(t->text(), QByteArray("aa aa aa"));
    // Replace with nothing.
    w.replacement->clear();
    QCOMPARE(w.replaceAll(), 6);
    QCOMPARE(t->text(), QByteArray("  "));
    // Wrap-around in both directions.
    t->setText("one two one");
    w.query->setText("one");
    t->primary->send(SCI_GOTOPOS, 10);
    QVERIFY(w.find());
    QCOMPARE(at(t, SCI_GETSELECTIONSTART), sptr_t(0));
    t->primary->send(SCI_GOTOPOS, 1);
    QVERIFY(w.find(true));
    QCOMPARE(at(t, SCI_GETSELECTIONSTART), sptr_t(8));
  }
  void regexEdgeCases() {
    Window w(true);
    auto *t = w.newDocument("");
    w.regex->setChecked(true);
    auto replace = [&](const QByteArray &text, const QString &q,
                       const QString &r, int count, const QByteArray &result) {
      t->setText(text);
      w.query->setText(q);
      w.replacement->setText(r);
      QCOMPARE(w.replaceAll(), count);
      QCOMPARE(t->text(), result);
    };
    replace("a\r\nb\r\n", "$", ";", 3, "a;\r\nb;\r\n;");
    replace("a\r\n\r\nb", "^$", "-", 1, "a\r\n-\r\nb");
    replace("x\r\ny", "\\r?\\n", "|", 1, "x|y");
    replace("foo1 bar2", "(\\w+?)(\\d)", "\\2\\1", 2, "1foo 2bar");
    replace("price 10 cost 20", "\\d+(?= )", "N", 1, "price N cost 20");
#ifndef Q_OS_WIN
    // The vendored Boost.Regex wrapper works on wchar_t; on Windows that is
    // UTF-16, so "." matches half of an astral code point there (known gap).
    replace(QString::fromUtf8("🌍é").toUtf8(), ".", "#", 2, "##");
#endif
    replace("aaa", "a*", "X", 1, "X");
    replace("Hello", "(?i)hello", "bye", 1, "bye");
    replace("tab\there", "\\t", "\\\\t", 1, "tab\\there");
    w.query->setText("(unclosed");
    QCOMPARE(w.replaceAll(), 0);
    QVERIFY(!w.find());
    QCOMPARE(w.markAll(), 0);
    // A valid search afterwards still works (status was reset).
    t->setText("ok");
    w.query->setText("o.");
    QVERIFY(w.find());
  }
  // ---- catastrophic regular expressions -----------------------------------
  // `(a+)+$` against a run of 'a' ending in 'b' backtracks exponentially.
  // Boost.Regex must give up (error_complexity) quickly and the editor must
  // report it instead of freezing the UI thread.
  void catastrophicRegexIsBounded() {
    const QByteArray text = QByteArray(4000, 'a') + "b\n";
    const QString pattern = "(a+)+$";
    Window w(true);
    auto *t = w.newDocument(text);
    w.regex->setChecked(true);
    w.query->setText(pattern);
    QElapsedTimer timer;
    timer.start();
    QVERIFY(!w.find());
    QVERIFY2(timer.elapsed() < 5000,
             qPrintable(QString("find took %1 ms").arg(timer.elapsed())));
    QVERIFY2(w.statusBar()->currentMessage().contains("too complex"),
             qPrintable(w.statusBar()->currentMessage()));
    timer.restart();
    QCOMPARE(w.markAll(), 0);
    QCOMPARE(w.replaceAll(), 0);
    QCOMPARE(t->text(), text); // nothing was replaced
    QVERIFY2(timer.elapsed() < 10000,
             qPrintable(QString("mark/replace took %1 ms").arg(timer.elapsed())));
    // Find in files stops on the first pathological file instead of
    // spending the budget again on every other file.
    QTemporaryDir dir;
    for (int i = 0; i < 20; ++i)
      put(dir.filePath(QString("f%1.txt").arg(i)), text);
    timer.restart();
    QCOMPARE(w.findInFiles(dir.path(), "*.txt"), 0);
    QVERIFY2(timer.elapsed() < 10000,
             qPrintable(QString("find in files took %1 ms").arg(timer.elapsed())));
    QVERIFY2(w.statusBar()->currentMessage().contains("too complex"),
             qPrintable(w.statusBar()->currentMessage()));
    QVERIFY(!w.searching);
    // The editor recovers: an ordinary search afterwards works.
    w.query->setText("a+b");
    QVERIFY(w.find());
    QCOMPARE(w.findInFiles(dir.path(), "*.txt"), 20);
    // Invalid patterns are now reported as such (previously the error code
    // from the regex wrapper never reached the status bar).
    w.query->setText("(unclosed");
    QVERIFY(!w.find());
    QCOMPARE(w.statusBar()->currentMessage(), QString("Invalid regular expression"));
    // The budget scales with the text: a linear-time pattern that has to try
    // every position of a large document must not be cut off.
    QByteArray big;
    for (int i = 0; big.size() < 8 * 1024 * 1024; ++i)
      big += "lorem ipsum dolor sit amet " + QByteArray::number(i) + "\n";
    t->setText(big + "needle42\n");
    t->primary->send(SCI_GOTOPOS, 0);
    w.query->setText("\\w+\\d{2}\\n(?!.)");
    QVERIFY2(w.find(), qPrintable(w.statusBar()->currentMessage()));
    QCOMPARE(t->primary->send(SCI_GETSELECTIONSTART), sptr_t(big.size()));
  }
  void wholeWordAndCaseOptions() {
    Window w(true);
    auto *t = w.newDocument("Cat cat concat CAT_x cat.");
    w.query->setText("cat");
    QCOMPARE(w.markAll(), 5); // includes "concat" and "CAT_x"
    w.matchCase->setChecked(true);
    QCOMPARE(w.markAll(), 3);
    w.wholeWord->setChecked(true);
    QCOMPARE(w.markAll(), 2);
    Q_UNUSED(t);
  }
  void replaceIsSingleUndoStep() {
    Window w(true);
    auto *t = w.newDocument("a b a b a");
    w.query->setText("a");
    w.replacement->setText("c");
    QCOMPARE(w.replaceAll(), 3);
    t->primary->send(SCI_UNDO);
    QCOMPARE(t->text(), QByteArray("a b a b a"));
  }

  // ---- find in files -------------------------------------------------------
  void invalidEncodingIsClassified() {
    QTemporaryDir dir;
    QString p = dir.filePath("latin1.txt");
    put(p, QByteArray("caf\xe9\n"));
    DocumentData d;
    QString error;
    ReadError kind = ReadError::None;
    QVERIFY(!DocumentData::read(p, d, error, {}, &kind));
    QVERIFY(kind == ReadError::InvalidEncoding);
    QVERIFY(DocumentData::read(p, d, error, "ISO-8859-1", &kind));
    QVERIFY(kind == ReadError::None);
    QCOMPARE(d.utf8, QString::fromUtf8("caf\xc3\xa9\n").toUtf8());
    put(p, QByteArray("a\0b", 3));
    QVERIFY(!DocumentData::read(p, d, error, {}, &kind));
    QVERIFY(kind == ReadError::Other);
    // Testing-mode windows report instead of prompting, so this cannot block.
    put(p, QByteArray("caf\xe9\n"));
    Window w(true);
    QVERIFY(!w.openFile(p));
    QVERIFY(w.openFile(p, "Windows-1252"));
    QVERIFY(w.encodingChoice->findText("Windows-1251") >= 0 ||
            !QTextCodec::codecForName("Windows-1251"));
  }
  void findInFilesSnippetKeepsUtf8Intact() {
    QTemporaryDir dir;
    // 299 ASCII bytes then 2-byte characters: the 300-byte cut would fall
    // inside the first one.
    QByteArray line = "needle" + QByteArray(293, 'x') + QString::fromUtf8("éééé").toUtf8();
    put(dir.filePath("u.txt"), line + "\n");
    Window w(true);
    w.regex->setChecked(false);
    w.query->setText("needle");
    QCOMPARE(w.findInFiles(dir.path(), "*.txt"), 1);
    const QString shown = w.results->topLevelItem(0)->child(0)->text(0);
    QVERIFY(!shown.contains(QChar::ReplacementCharacter));
  }
  void findInFilesEdgeCases() {
#ifdef Q_OS_WIN
    QSKIP("needs POSIX symlinks, FIFOs or permission bits");
#else
    QTemporaryDir dir;
    Window w(true);
    w.query->setText("needle");
    QCOMPARE(w.findInFiles(dir.path(), "*"), 0);              // empty folder
    QCOMPARE(w.findInFiles(dir.filePath("missing"), "*"), 0); // missing folder
    QDir(dir.path()).mkpath("a/b");
    put(dir.filePath("a/b/deep.txt"), "x needle\n");
    put(dir.filePath("utf16.txt"),
        QByteArray::fromHex("fffe") +
            QByteArray::fromRawData(reinterpret_cast<const char *>(
                                        QString("hay needle").utf16()),
                                    20));
    put(dir.filePath("latin1.txt"), QByteArray("caf\xe9 needle"));
    put(dir.filePath("locked.txt"), "needle");
    ::chmod(QFile::encodeName(dir.filePath("locked.txt")), 0);
    QVERIFY(::symlink(QFile::encodeName(dir.path()),
                      QFile::encodeName(dir.filePath("a/loop"))) == 0);
    int hits = w.findInFiles(dir.path(), "*.txt, *.md; *.cpp");
    ::chmod(QFile::encodeName(dir.filePath("locked.txt")), 0644);
    // deep + utf16 (+ locked when running as root); latin1 is skipped.
    QCOMPARE(hits, isRoot() ? 3 : 2);
    // Opening a UTF-16 result selects the right characters.
    for (int i = 0; i < w.results->topLevelItemCount(); ++i)
      if (w.results->topLevelItem(i)->text(0).startsWith("utf16"))
        w.openResult(w.results->topLevelItem(i)->child(0));
    auto *t = w.current();
    QVERIFY(t && t->data.encoding == "UTF-16LE");
    QByteArray sel(at(t, SCI_GETSELTEXT, 0, 0) + 1, '\0');
    t->primary->send(SCI_GETSELTEXT, 0, reinterpret_cast<sptr_t>(sel.data()));
    QCOMPARE(QByteArray(sel.constData()), QByteArray("needle"));
#endif
  }
  void findInFilesHandlesManyFiles() {
    QTemporaryDir dir;
    for (int i = 0; i < 2000; ++i)
      put(dir.filePath(QString("f%1.txt").arg(i)), "line\nneedle here\n");
    Window w(true);
    w.query->setText("needle");
    QElapsedTimer timer;
    timer.start();
    QCOMPARE(w.findInFiles(dir.path(), "*.txt"), 2000);
    QVERIFY2(timer.elapsed() < 10000, qPrintable(QString::number(timer.elapsed())));
  }

  void findInFilesCancelAndReentrancy() {
    QTemporaryDir dir;
    for (int i = 0; i < 300; ++i)
      put(dir.filePath(QString("f%1.txt").arg(i)), "needle\n");
    Window w(true);
    w.query->setText("needle");
    QVERIFY(w.findFilesButton->isEnabled());
    QVERIFY(!w.stopButton->isEnabled());
    // Stop is a no-op while idle.
    w.stopSearch();
    QVERIFY(!w.cancelSearch);
    // Cancel from inside the event loop once the search is running.
    QTimer::singleShot(0, &w, [&w] {
      QVERIFY(w.searching);
      QVERIFY(!w.findFilesButton->isEnabled());
      QVERIFY(w.stopButton->isEnabled());
      w.stopSearch();
    });
    int hits = w.findInFiles(dir.path(), "*.txt");
    QVERIFY(hits < 300);
    QVERIFY(!w.searching);
    QVERIFY(w.findFilesButton->isEnabled());
    QVERIFY(!w.stopButton->isEnabled());
    // A fresh search runs to completion after a cancelled one.
    QCOMPARE(w.findInFiles(dir.path(), "*.txt"), 300);
    // Re-entrant calls are refused.
    w.searching = true;
    QCOMPARE(w.findInFiles(dir.path(), "*.txt"), 0);
    w.searching = false;
  }
  void searchOnlyReadSkipsHash() {
    QTemporaryDir dir;
    QString p = dir.filePath("a.txt");
    put(p, "h\xc3\xa9llo");
    DocumentData full, quick;
    QString error;
    QVERIFY(DocumentData::read(p, full, error));
    QVERIFY(DocumentData::read(p, quick, error, {}, nullptr, false));
    QVERIFY(!full.diskHash.isEmpty());
    QVERIFY(quick.diskHash.isEmpty());
    QCOMPARE(quick.utf8, full.utf8);
  }
  void largeAndChunkedDecodingMatches() {
    QTemporaryDir dir;
    // Multi-byte characters straddle the 1 MiB decode chunk boundary.
    QString unit = QString::fromUtf8("a\xc3\xa9\xe2\x82\xac\xf0\x9f\x8c\x8d\n");
    QString text;
    while (text.toUtf8().size() < 3 * 1024 * 1024 + 7)
      text += unit.repeated(4096);
    QString p8 = dir.filePath("u8.txt"), p16 = dir.filePath("u16.txt");
    put(p8, text.toUtf8());
    QByteArray u16 = QByteArray::fromHex("fffe");
    u16 += QByteArray(reinterpret_cast<const char *>(text.utf16()),
                      int(text.size() * 2));
    put(p16, u16);
    DocumentData a, b;
    QString error;
    QVERIFY2(DocumentData::read(p8, a, error), qPrintable(error));
    QVERIFY2(DocumentData::read(p16, b, error), qPrintable(error));
    QCOMPARE(a.utf8, text.toUtf8());
    QCOMPARE(b.utf8, text.toUtf8());
  }

  // ---- outline ---------------------------------------------------------------
  void outlineSkippedWhileHiddenAndRebuiltOnShow() {
    Window w(true);
    auto *t = w.newDocument("def alpha():\n    pass\nclass Beta:\n    pass\n");
    w.applyLanguage(t, "python");
    w.show();
    QVERIFY(w.outlineDock->isHidden());
    int before = w.outlineBuilds;
    w.rebuildOutline();
    w.rebuildOutline();
    QCOMPARE(w.outlineBuilds, before); // hidden: no work
    w.outlineDock->show(); // visibilityChanged -> rebuild
    QTest::qWait(50);
    QVERIFY(w.outlineBuilds > before);
    QStringList names;
    for (int i = 0; i < w.outline->count(); ++i)
      names << w.outline->item(i)->text();
    QCOMPARE(names, QStringList({"alpha", "Beta"}));
  }
  void outlineIsBoundedOnHugeDocuments() {
    Window w(true);
    QByteArray text;
    for (int i = 0; i < 60000; ++i)
      text += "def f" + QByteArray::number(i) + "():\n";
    auto *t = w.newDocument(text);
    w.applyLanguage(t, "python");
    w.show();
    w.outlineDock->show();
    QTest::qWait(50);
    w.rebuildOutline();
    QVERIFY(w.outline->count() <= 50001);
    QVERIFY(w.outline->count() >= 50000);
  }

  // ---- sessions and recovery ----------------------------------------------
  void sessionSkipsUnchangedWritesAndStaysCompatible() {
    QTemporaryDir dir;
    QString path = dir.filePath("recovery.json");
    QString file = dir.filePath("clean.txt");
    put(file, "clean\n");
    Window w(false);
    w.sessionPath = path;
    QVERIFY(w.openFile(file));
    auto *dirty = w.newDocument("unsaved\n");
    Q_UNUSED(dirty);
    w.snapshot();
    QCOMPARE(w.sessionWrites, 1);
    w.snapshot();
    QCOMPARE(w.sessionWrites, 1); // unchanged: nothing written
    auto doc = QJsonDocument::fromJson(get(path)).object();
    QCOMPARE(doc["version"].toInt(), 1);
    int backups = 0;
    for (const auto &v : doc["documents"].toArray()) {
      auto o = v.toObject();
      QVERIFY(o.contains("path") && o.contains("hash") && o.contains("dirty"));
      backups += o.contains("backup");
    }
    QCOMPARE(backups, 1); // only the untitled tab carries text
    dirty->primary->send(SCI_APPENDTEXT, 1, reinterpret_cast<sptr_t>("x"));
    w.snapshot();
    QCOMPARE(w.sessionWrites, 2);
    Window restored(true);
    restored.sessionPath = path;
    restored.restore();
    QVERIFY(restored.tabs->count() >= 2);
  }
  void corruptOrForeignSessionFiles() {
    QTemporaryDir dir;
    for (const auto &raw :
         {QByteArray("{not json"), QByteArray("[]"),
          QByteArray("{\"version\":99,\"documents\":[]}"),
          QByteArray("{\"version\":1,\"documents\":[{\"path\":\"/no/such/file\"}]}"),
          QByteArray("{\"version\":1,\"documents\":\"oops\",\"active\":42}")}) {
      QString p = dir.filePath("session.json");
      put(p, raw);
      Window w(true);
      w.sessionPath = p;
      w.restore();
      QCOMPARE(w.tabs->count(), 1); // falls back to one blank tab
    }
  }
  void closingLastTabLeavesBlankDocument() {
    Window w(true);
    w.newDocument();
    QVERIFY(w.closeTab(0));
    QCOMPARE(w.tabs->count(), 1);
    QVERIFY(w.current()->data.path.isEmpty());
    QVERIFY(w.closeTab(-1));
  }
  void externalDeletionAndTouchDetection() {
    QTemporaryDir dir;
    QString p = dir.filePath("t.txt");
    put(p, "same\n");
    Window w(true);
    QVERIFY(w.openFile(p));
    QTest::qWait(20);
    put(p, "same\n"); // rewritten with identical content: not a change
    QFile(p).setFileTime(QDateTime::currentDateTime().addSecs(5),
                         QFileDevice::FileModificationTime);
    w.checkDisk();
    QVERIFY(w.statusBar()->currentMessage().isEmpty());
    QFile::remove(p);
    w.checkDisk();
    QVERIFY(w.statusBar()->currentMessage().startsWith("t.txt changed"));
    QVERIFY(!w.save(w.current())); // refuses to recreate silently
  }

  // ---- languages, themes, UI actions ---------------------------------------
  void languageDetection() {
    QTemporaryDir dir;
    Window w(true);
    QMap<QString, QString> cases{{"SCRIPT.PY", "python"},
                                 {"Makefile", "makefile"},
                                 {"noext", "normal"},
                                 {"x.unknownext", "normal"},
                                 {".bashrc", "bash"},
                                 {"a.json", "json"},
                                 {"a.html", "html"}};
    for (auto it = cases.begin(); it != cases.end(); ++it) {
      put(dir.filePath(it.key()), "x\n");
      QVERIFY(w.openFile(dir.filePath(it.key())));
      QCOMPARE(w.current()->language, it.value());
    }
    w.applyLanguage(w.current(), "no-such-language");
    QCOMPARE(w.current()->language, QString("normal"));
    w.applyLanguage(nullptr, "python");
  }
  void everyLanguageWithEveryTheme() {
    Window w(true);
    auto *t = w.newDocument("int main() { return 0; } // x\n<a href='x'>y</a>\n");
    QVERIFY(w.themeChoice->count() >= 20);
    for (int i = 0; i < w.themeChoice->count(); ++i) {
      w.themeChoice->setCurrentIndex(i);
      for (const auto &name : w.languages.keys()) {
        w.applyLanguage(t, name);
        QCOMPARE(t->language, name);
      }
    }
  }
  void userLanguageFromConfigFolder() {
    QDir().mkpath(configPath("userDefineLangs"));
    QString p = configPath("userDefineLangs/test.udl.xml");
    put(p, "<Velox><UserLang name=\"Tiny\" ext=\"tiny\"><Settings><Global "
           "caseIgnored=\"yes\"/></Settings><KeywordLists><Keywords "
           "name=\"Keywords1\">begin end \"two words\"</Keywords><Keywords "
           "name=\"Comments\">00; 01 02</Keywords></KeywordLists><Styles>"
           "<WordsStyle name=\"KEYWORDS1\" fgColor=\"FF0000\" colorStyle=\"1\" "
           "fontStyle=\"1\"/></Styles></UserLang></Velox>");
    put(configPath("userDefineLangs/broken.xml"), "<Velox><UserLang");
    QTemporaryDir dir;
    put(dir.filePath("a.tiny"), "BEGIN x two words end ; note\n");
    Window w(true);
    QVERIFY(w.openFile(dir.filePath("a.tiny")));
    auto *t = w.current();
    QCOMPARE(t->language, QString("Tiny"));
    QCOMPARE(at(t, SCI_GETSTYLEAT, 0), sptr_t(SCE_USER_STYLE_KEYWORD1));
    QCOMPARE(at(t, SCI_GETSTYLEAT, 8), sptr_t(SCE_USER_STYLE_KEYWORD1));
    QCOMPARE(at(t, SCI_GETSTYLEAT, 24), sptr_t(SCE_USER_STYLE_COMMENTLINE));
    QVERIFY(at(t, SCI_STYLEGETBOLD, SCE_USER_STYLE_KEYWORD1));
    QFile::remove(p);
    QFile::remove(configPath("userDefineLangs/broken.xml"));
  }
  void menuActionsOnEmptyAndFullDocuments() {
    Window w(true);
    auto *t = w.newDocument("  a  \n\tb\t\n\n c");
    QVERIFY(trigger(w, "Trim trailing whitespace"));
    QCOMPARE(t->text(), QByteArray("  a\n\tb\n\n c"));
    t->primary->send(SCI_GOTOLINE, 1);
    QVERIFY(trigger(w, "Toggle bookmark"));
    t->primary->send(SCI_GOTOLINE, 3);
    QVERIFY(trigger(w, "Next bookmark")); // wraps to line 1
    QCOMPARE(at(t, SCI_LINEFROMPOSITION, at(t, SCI_GETCURRENTPOS)), sptr_t(1));
    QVERIFY(trigger(w, "Duplicate line / selection"));
    QVERIFY(trigger(w, "Uppercase"));
    QVERIFY(trigger(w, "Split document"));
    QVERIFY(trigger(w, "Word wrap"));
    QVERIFY(trigger(w, "Show whitespace"));
    QVERIFY(trigger(w, "Mark all occurrences"));
    QVERIFY(trigger(w, "Clear marks"));
    // Every action must survive a window with an empty document.
    auto *empty = w.newDocument();
    Q_UNUSED(empty);
    const QStringList skip{"Quit", "Open…", "Open with encoding…",
                           "Open folder…", "Save as…", "Save session…",
                           "Load session…", "Go to line…", "Fullscreen",
                           "Open configuration folder", "About Velox",
                           "Save", "Save all", "Close tab", "Preferences…",
                           "Define custom language…"};
    for (auto *a : w.findChildren<QAction *>())
      if (!a->text().isEmpty() && !skip.contains(QString(a->text()).remove('&')) &&
          !a->menu())
        a->trigger();
    QVERIFY(w.current());
  }
  void macroRecordAndPlayback() {
    Window w(true);
    auto *t = w.newDocument("");
    QVERIFY(trigger(w, "Start / stop recording"));
    t->primary->send(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>("ab"));
    t->primary->send(SCI_NEWLINE);
    QVERIFY(trigger(w, "Start / stop recording"));
    QVERIFY(trigger(w, "Playback"));
    QVERIFY(trigger(w, "Playback"));
    QCOMPARE(t->text(), QByteArray("ab\nab\nab\n"));
  }
  // ---- UTF-32, large files and encodings in search -----------------------
  void utf32RoundTripsAndRejectsTruncation() {
    QTemporaryDir dir;
    const QString text = QString::fromUtf8("a\n🌍");
    for (bool le : {true, false}) {
      QByteArray raw = QByteArray::fromHex(le ? "fffe0000" : "0000feff");
      for (char32_t c : {U'a', U'\n', U'\U0001F30D'}) {
        for (int i = 0; i < 4; ++i)
          raw += char(le ? (c >> (8 * i)) & 0xff : (c >> (8 * (3 - i))) & 0xff);
      }
      QString p = dir.filePath(le ? "le" : "be");
      put(p, raw);
      DocumentData d;
      QString error;
      QVERIFY2(DocumentData::read(p, d, error), qPrintable(error));
      QCOMPARE(d.encoding, QString(le ? "UTF-32LE" : "UTF-32BE"));
      QCOMPARE(QString::fromUtf8(d.utf8), text);
      QVERIFY2(d.write(d.utf8, p, error), qPrintable(error));
      QCOMPARE(get(p), raw);
      put(p, raw.chopped(1));
      QVERIFY(!DocumentData::read(p, d, error));
    }
  }
  void largeFilesOpenWithoutHighlighting() {
    QTemporaryDir dir;
    QString p = dir.filePath("big.cpp");
    QFile f(p);
    QVERIFY(f.open(QIODevice::WriteOnly));
    const QByteArray row("int x = 1; // filler\n");
    const QByteArray block = row.repeated(64 * 1024);
    while (f.size() <= largeFileSize)
      QCOMPARE(f.write(block), qint64(block.size()));
    f.close();
    Window w(true);
    QVERIFY(w.openFile(p));
    auto *t = w.current();
    QVERIFY(t->large);
    QCOMPARE(t->language, QString("normal"));
    QCOMPARE(at(t, SCI_GETMARGINWIDTHN, 2), sptr_t(0));
    w.applyLanguage(t, "cpp");
    QCOMPARE(t->language, QString("normal"));
    w.prefs.wrap = true;
    w.applyPreferences(t);
    QCOMPARE(at(t, SCI_GETWRAPMODE), sptr_t(SC_WRAP_NONE));
  }
  void findInFilesHonoursChosenEncoding() {
    QTemporaryDir dir;
    put(dir.filePath("latin.txt"), QByteArray("caf\xe9 au lait\n"));
    Window w(true);
    w.query->setText("lait");
    QCOMPARE(w.findInFiles(dir.path(), "*"), 0);
    w.filesEncoding->setCurrentText("Windows-1252");
    QCOMPARE(w.findInFiles(dir.path(), "*"), 1);
    auto *file = w.results->topLevelItem(0);
    QVERIFY(file);
    w.openResult(file->child(0));
    QCOMPARE(QString::fromUtf8(w.current()->text()),
             QString::fromUtf8("café au lait\n"));
  }
  // ---- editing commands ---------------------------------------------------
  void toggleCommentRoundTrips() {
    Window w(true);
    auto *t = w.newDocument("int a;\n  int b;\n\nint c;\n");
    w.applyLanguage(t, "cpp");
    at(t, SCI_SELECTALL);
    QVERIFY(w.toggleComment());
    QCOMPARE(t->text(), QByteArray("// int a;\n//   int b;\n\n// int c;\n"));
    QVERIFY(w.toggleComment());
    QCOMPARE(t->text(), QByteArray("int a;\n  int b;\n\nint c;\n"));
    // A caret comments only its own line, even when other lines are commented.
    w.applyLanguage(t, "python");
    at(t, SCI_GOTOPOS, 0);
    QVERIFY(w.toggleComment());
    QCOMPARE(t->text(), QByteArray("# int a;\n  int b;\n\nint c;\n"));
    w.applyLanguage(t, "html");
    at(t, SCI_SETTEXT, 0, reinterpret_cast<sptr_t>("<p>x</p>"));
    QVERIFY(w.toggleComment());
    QCOMPARE(t->text(), QByteArray("<!-- <p>x</p> -->"));
    QVERIFY(w.toggleComment());
    QCOMPARE(t->text(), QByteArray("<p>x</p>"));
    w.applyLanguage(t, "json");
    QVERIFY(!w.toggleComment());
  }
  void sortAndDeduplicateLines() {
    Window w(true);
    auto *t = w.newDocument("b\nB\na\nA\n");
    w.sortLines(Window::Ascending);
    QCOMPARE(t->text(), QByteArray("A\nB\na\nb\n"));
    w.sortLines(Window::Descending);
    QCOMPARE(t->text(), QByteArray("b\na\nB\nA\n"));
    w.sortLines(Window::IgnoreCase);
    QCOMPARE(t->text(), QByteArray("a\nA\nb\nB\n"));
    auto *d = w.newDocument("a\nb\na\nc\nb\n");
    QCOMPARE(w.removeDuplicateLines(), 2);
    QCOMPARE(d->text(), QByteArray("a\nb\nc\n"));
    auto *crlf = w.newDocument("z\r\ny\r\n");
    at(crlf, SCI_SETEOLMODE, SC_EOL_CRLF);
    w.sortLines(Window::Ascending);
    QCOMPARE(crlf->text(), QByteArray("y\r\nz\r\n"));
  }
  void joinLinesAndOpenAtPosition() {
    QTemporaryDir dir;
    QString p = dir.filePath("five.txt");
    put(p, "12345\nabcde\nfghij\n");
    Window w(true);
    QVERIFY(w.openSpec(p + ":2:3"));
    QCOMPARE(at(w.current(), SCI_GETCURRENTPOS), sptr_t(8));
    QVERIFY(w.openSpec(p + ":99"));   // past the end clamps to the last line
    QVERIFY(w.current()->data.path == p);
    at(w.current(), SCI_GOTOPOS, 0);
    w.joinLines();
    QCOMPARE(w.current()->text(), QByteArray("12345 abcde\nfghij\n"));
    QVERIFY(!w.openSpec(dir.filePath("nope.txt:3")));
  }
  // ---- preferences --------------------------------------------------------
  void preferencesApplyAndPersist() {
    QTemporaryDir dir;
    QSettings store(dir.filePath("s.ini"), QSettings::IniFormat);
    Preferences p;
    p.fontSize = 17;
    p.tabWidth = 2;
    p.spaces = false;
    p.trimOnSave = true;
    p.store(store);
    Preferences q;
    q.load(store);
    QCOMPARE(q.fontSize, 17);
    QCOMPARE(q.tabWidth, 2);
    QVERIFY(!q.spaces);
    QVERIFY(q.trimOnSave);
    QVERIFY(!q.finalNewline);
    Window w(true);
    w.prefs = q;
    w.prefs.lineNumbers = false;
    w.prefs.finalNewline = true;
    auto *t = w.newDocument();
    w.applyPreferences(t);
    w.applyTheme(t);
    QCOMPARE(at(t, SCI_GETTABWIDTH), sptr_t(2));
    QCOMPARE(at(t, SCI_GETUSETABS), sptr_t(1));
    QCOMPARE(at(t, SCI_GETMARGINWIDTHN, 0), sptr_t(0));
    QCOMPARE(at(t, SCI_STYLEGETSIZE, STYLE_DEFAULT), sptr_t(17));
    QString f = dir.filePath("trim.txt");
    put(f, "x  \ny\t");
    QVERIFY(w.openFile(f));
    w.prefs = q;
    w.prefs.finalNewline = true;
    QVERIFY(w.save(w.current()));
    QCOMPARE(get(f), QByteArray("x\ny\n"));
  }
  // ---- custom language editor ---------------------------------------------
  void customLanguageRoundTripsAndHighlights() {
    Window w(true);
    UdlSpec s;
    s.name = "Todo List";
    s.extensions = ".TODO, todo";
    s.lineComment = "#";
    s.blockOpen = "/*";
    s.blockClose = "*/";
    s.singleQuotes = true;
    s.keywords[0] = "TODO  FIXME";
    s.keywordBold[0] = true;
    s.operators = "+ -";
    QVERIFY2(s.validate(w.languages).isEmpty(),
             qPrintable(s.validate(w.languages)));
    QCOMPARE(s.normalizedExtensions(), QString("todo"));
    QCOMPARE(s.fileName(), QString("todo-list.xml"));
    QString error;
    QVERIFY2(s.save(error), qPrintable(error));
    QMap<QString, QString> saved = UdlSpec::saved();
    QVERIFY(saved.contains("Todo List"));
    QDomDocument d;
    QFile f(saved["Todo List"]);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QVERIFY(d.setContent(&f));
    f.close(); // Windows cannot delete a file that is still open
    UdlSpec back;
    QVERIFY(UdlSpec::fromElement(d.elementsByTagName("UserLang").at(0).toElement(), back));
    QCOMPARE(back.name, s.name);
    QCOMPARE(back.lineComment, QString("#"));
    QCOMPARE(back.blockClose, QString("*/"));
    QVERIFY(back.singleQuotes && back.doubleQuotes);
    QVERIFY(back.keywordBold[0] && !back.keywordBold[1]);
    QCOMPARE(back.keywords[0].simplified(), QString("TODO FIXME"));
    w.reloadLanguages();
    QVERIFY(w.languages.contains("Todo List"));
    QVERIFY(w.languageChoice->findText("Todo List") >= 0);
    auto *t = w.newDocument("TODO fix # note\n");
    w.applyLanguage(t, "Todo List");
    QCOMPARE(t->language, QString("Todo List"));
    // "TODO" must be styled as keyword 1, the comment as a line comment.
    QCOMPARE(at(t, SCI_GETSTYLEAT, 0), sptr_t(SCE_USER_STYLE_KEYWORD1));
    QCOMPARE(at(t, SCI_GETSTYLEAT, 10), sptr_t(SCE_USER_STYLE_COMMENTLINE));
    // Rejected: clashes with a built-in name, bad markers, half a block comment.
    UdlSpec bad = s;
    bad.name = "python";
    QVERIFY(!bad.validate(w.languages).isEmpty());
    bad = s;
    bad.lineComment = "# #";
    QVERIFY(!bad.validate(w.languages, "Todo List").isEmpty());
    bad = s;
    bad.blockClose.clear();
    QVERIFY(!bad.validate(w.languages, "Todo List").isEmpty());
    QVERIFY(s.validate(w.languages, "Todo List").isEmpty()); // may keep its own name
    // Deleting the file and reloading turns open tabs back into plain text.
    QVERIFY(QFile::remove(saved["Todo List"]));
    w.reloadLanguages();
    QVERIFY(!w.languages.contains("Todo List"));
    QCOMPARE(t->language, QString("normal"));
  }
  void splitViewSurvivesTabClose() {
    Window w(true);
    auto *t = w.newDocument("x");
    t->toggleSplit();
    t->secondary->setFocus();
    t->metadataDirty = false;
    t->primary->send(SCI_SETSAVEPOINT);
    QVERIFY(w.closeTab(w.tabs->indexOf(t)));
    QCOMPARE(w.tabs->count(), 1);
  }
  void macroIgnoresProgrammaticLoads() {
    QTemporaryDir dir;
    QString p = dir.filePath("loaded.txt");
    put(p, "from disk\n");
    Window w(true);
    w.newDocument("");
    QVERIFY(trigger(w, "Start / stop recording"));
    QVERIFY(w.openFile(p));
    QVERIFY(trigger(w, "Reload from disk"));
    QVERIFY(w.macro.isEmpty());
    w.newDocument("seed");
    QVERIFY(w.macro.isEmpty());
    QVERIFY(!w.suppressMacro);
    w.current()->primary->send(SCI_REPLACESEL, 0,
                               reinterpret_cast<sptr_t>("typed"));
    QVERIFY(!w.macro.isEmpty()); // real user edits still record
    QVERIFY(trigger(w, "Start / stop recording"));
  }
  void fuzzyScoreRanksSubsequences() {
    QVERIFY(fuzzyScore("sv", "Save") >= 0);
    QCOMPARE(fuzzyScore("zz", "Save"), -1);
    QCOMPARE(fuzzyScore("", "anything"), 0);
    QVERIFY(fuzzyScore("sa", "Save") > fuzzyScore("sa", "Select all"));
    QVERIFY(fuzzyScore("spl", "Split view") > fuzzyScore("spl", "Save plain text"));
    QVERIFY(fuzzyScore("SAVE", "save") >= 0); // case-insensitive
  }
  void paletteListsEveryMenuActionAndFilters() {
    Window w(true);
    QVERIFY(w.menuActions.size() > 40);
    QStringList names;
    for (const auto &entry : w.menuActions)
      names << entry.first->text().remove('&');
    QVERIFY(names.contains("Command palette…"));
    QVERIFY(names.contains("Quick open…"));
    QVERIFY(names.contains("Playback"));
    QList<PaletteItem> items;
    for (const auto &entry : w.menuActions)
      items.append({entry.first->text().remove('&'), entry.second, {}});
    auto *dialog = new PaletteDialog("x", &w);
    dialog->setItems(items);
    dialog->setFilter("playb");
    QVERIFY(dialog->visibleTitles().contains("Playback"));
    QCOMPARE(dialog->visibleTitles().first(), QString("Playback"));
    dialog->setFilter("qqqzzz");
    QVERIFY2(dialog->visibleTitles().isEmpty(), qPrintable(dialog->visibleTitles().join("|")));
    delete dialog;
  }
  void paletteRunsTheChosenAction() {
    Window w(true);
    int fired = 0;
    auto *a = new QAction("Palette probe", &w);
    connect(a, &QAction::triggered, &w, [&] { ++fired; });
    auto *dialog = new PaletteDialog("x", &w);
    dialog->setItems({{"Palette probe", "test", QVariant::fromValue(static_cast<void *>(a))}});
    QVariant got;
    connect(dialog, &PaletteDialog::chosen, &w, [&](const QVariant &v) { got = v; });
    dialog->setFilter("probe");
    QVERIFY(dialog->chooseCurrent());
    static_cast<QAction *>(got.value<void *>())->trigger();
    QCOMPARE(fired, 1);
  }
  void shortcutsDoNotConflict() {
    Window w(true);
    QMultiHash<QString, QString> byKey;
    for (const auto &entry : w.menuActions)
      if (!entry.first->shortcut().isEmpty())
        byKey.insert(entry.first->shortcut().toString(), entry.first->text().remove('&'));
    for (const QString &key : byKey.uniqueKeys())
      QVERIFY2(byKey.count(key) == 1,
               qPrintable(key + " is bound to " + byKey.values(key).join(" and ")));
    for (const auto &entry : w.menuActions)
      if (entry.first->text().remove('&') == "Playback")
        QCOMPARE(entry.first->shortcut().toString(), QString("Ctrl+Alt+P"));
  }
  void quickOpenListsProjectFilesAndSkipsBuildDirs() {
    QTemporaryDir dir;
    QDir d(dir.path());
    QVERIFY(d.mkpath("src/sub"));
    QVERIFY(d.mkpath(".git/objects"));
    QVERIFY(d.mkpath("node_modules/x"));
    for (const QString &f : {"a.txt", "src/b.cpp", "src/sub/c.h", ".git/objects/z",
                             "node_modules/x/y.js"}) {
      QFile file(d.filePath(f));
      QVERIFY(file.open(QIODevice::WriteOnly));
    }
    QCOMPARE(listProjectFiles(dir.path()),
             (QStringList{"a.txt", "src/b.cpp", "src/sub/c.h"}));
    QCOMPARE(listProjectFiles(dir.path(), 2).size(), 2);
    Window w(true);
    w.workspaceRoot = dir.path();
    QCOMPARE(w.quickOpenRoot(), dir.path());
  }
  void macroCopiesPointerText() {
    Window w(true);
    auto *t = w.newDocument("");
    w.recording = true;
    w.macro.clear();
    w.macroText.clear();
    QByteArray a("appended"), s("needle");
    t->primary->send(SCI_APPENDTEXT, a.size(),
                     reinterpret_cast<sptr_t>(a.constData()));
    t->primary->send(SCI_SETTARGETRANGE, 0, 0);
    t->primary->send(SCI_SEARCHNEXT, 0, reinterpret_cast<sptr_t>(s.constData()));
    a.fill('z');
    s.fill('z');
    bool appended = false, searched = false;
    for (int i = 0; i < w.macro.size(); ++i) {
      if (w.macro[i][0] == SCI_APPENDTEXT) {
        appended = true;
        QCOMPARE(w.macroText[i], QByteArray("appended"));
        QCOMPARE(w.macro[i][2], qintptr(0));
      }
      if (w.macro[i][0] == SCI_SEARCHNEXT) {
        searched = true;
        QCOMPARE(w.macroText[i], QByteArray("needle"));
        QCOMPARE(w.macro[i][2], qintptr(0));
      }
    }
    QVERIFY(appended);
    QVERIFY(searched);
    w.recording = false;
  }
  void backupSizeCapRejectsAbsurdLengths() {
    DocumentData d;
    d.path = "/tmp/x";
    auto good = d.json(true, "hello\n");
    QCOMPARE(DocumentData::fromJson(good).utf8, QByteArray("hello\n"));
    for (quint32 declared : {0x7fffffffu, 0x40000001u, 0xffffffffu}) {
      QByteArray packed(8, '\0');
      packed[0] = char(declared >> 24);
      packed[1] = char(declared >> 16);
      packed[2] = char(declared >> 8);
      packed[3] = char(declared);
      auto bad = good;
      bad["backup"] = QString::fromLatin1(packed.toBase64());
      QVERIFY(DocumentData::fromJson(bad).utf8.isEmpty());
    }
    auto tiny = good;
    tiny["backup"] = QString::fromLatin1(QByteArray("ab").toBase64());
    QVERIFY(DocumentData::fromJson(tiny).utf8.isEmpty());
  }
  void recoveryFileNamingAndAdoption() {
    QCOMPARE(Window::recoveryPid("recovery-123.json"), qint64(123));
    QCOMPARE(Window::recoveryPid("recovery.json"), qint64(0));
    QCOMPARE(Window::recoveryPid("recovery-1a.json"), qint64(0));
    QVERIFY(Window::pidAlive(QCoreApplication::applicationPid()));
    QVERIFY(!Window::pidAlive(999999999));
    {
      Window own(true, false);
      QVERIFY(own.sessionPath.endsWith(
          QString("recovery-%1.json").arg(QCoreApplication::applicationPid())));
      Window primary(true, true);
      QVERIFY(primary.sessionPath.endsWith("/recovery.json"));
    }
    QTemporaryDir dir;
    QString dead = dir.filePath("recovery-999999999.json");
#ifdef Q_OS_UNIX
    const qint64 otherLivePid = getppid();
#else
    const qint64 otherLivePid = QCoreApplication::applicationPid();
#endif
    QString live = dir.filePath(QString("recovery-%1.json").arg(otherLivePid));
    for (const auto &p : {dead, live}) {
      Window crashed(false, false);
      crashed.sessionPath = p;
      crashed.newDocument(QByteArray("unsaved in ") + p.toUtf8());
      crashed.snapshot();
      QVERIFY(QFile::exists(p));
    }
    Window primary(true, true);
    primary.sessionPath = dir.filePath("recovery.json");
    primary.restore();
    QCOMPARE(primary.tabs->count(), 1);
    QCOMPARE(primary.current()->text(), QByteArray("unsaved in ") + dead.toUtf8());
    QVERIFY(!QFile::exists(dead));    // adopted, then removed
    QVERIFY(QFile::exists(live));     // owner still running: left alone
    QVERIFY(QFile::exists(primary.sessionPath)); // re-snapshotted first
    // A --new-instance window never restores or adopts anything.
    Window other(true, false);
    other.sessionPath = dir.filePath("recovery-4242.json");
    other.restore();
    QCOMPARE(other.tabs->count(), 1);
    QVERIFY(other.current()->text().isEmpty());
  }
  void forwardedDataIsValidated() {
    bool ok = false;
    QCOMPARE(parseForwardedFiles("[\"/a/b\",\"rel\",\"\",3]", &ok),
             QStringList{"/a/b"});
    QVERIFY(ok);
    parseForwardedFiles("{\"x\":1}", &ok);
    QVERIFY(!ok);
    parseForwardedFiles("not json", &ok);
    QVERIFY(!ok);
    QByteArray big = "[\"/" + QByteArray(1024 * 1024 + 1, 'a') + "\"]";
    parseForwardedFiles(big, &ok);
    QVERIFY(!ok);
  }
  void missingSessionFilesReportOnce() {
    QTemporaryDir dir;
    QString p = dir.filePath("session.json");
    put(p, "{\"version\":1,\"documents\":[{\"path\":\"/no/a\"},"
           "{\"path\":\"/no/b\"},{\"path\":\"/no/c\"}]}");
    Window w(true);
    w.sessionPath = p;
    QTest::ignoreMessage(QtWarningMsg,
                         QRegularExpression("3 item\\(s\\) could not be restored"));
    w.restore();
    QCOMPARE(w.tabs->count(), 1);
    QVERIFY(!w.deferredReports);
  }
};

int runEdgeCases() {
  EdgeCases test;
  // VELOX_TEST_ONLY="fnA fnB" runs just those edge-case functions (for
  // iterating on one test); unset runs everything.
  QStringList args{"edge-cases"};
  args += qEnvironmentVariable("VELOX_TEST_ONLY").split(' ', Qt::SkipEmptyParts);
  return QTest::qExec(&test, args);
}
#include "edge_cases.moc"
