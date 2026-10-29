// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Velox Authors
#include "ILexer.h"
#include "Lexilla.h"
#include "SciLexer.h"
#include "window.h"
#include <QtTest>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#include <unistd.h>
#endif

class Integration : public QObject {
  Q_OBJECT
private:
  static void put(const QString &p, const QByteArray &b) {
    QFile f(p);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QCOMPARE(f.write(b), qint64(b.size()));
  }
  static QByteArray get(const QString &p) {
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly))
      return {};
    return f.readAll();
  }
private slots:
  void codecsAndEolRoundTrip() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QList<QByteArray> fixtures{QByteArray("alpha\r\nbeta\nlast\r"),
                               QByteArray::fromHex("efbbbf") +
                                   QString::fromUtf8("héllo 🌍\n").toUtf8(),
                               QByteArray::fromHex("fffe61000d000a003dd800de"),
                               QByteArray::fromHex("feff0061000d000ad83dde00")};
    int n = 0;
    for (const auto &raw : fixtures) {
      QString p = dir.filePath(QString::number(n++));
      put(p, raw);
      DocumentData d;
      QString error;
      QVERIFY2(DocumentData::read(p, d, error), qPrintable(error));
      QVERIFY2(d.write(d.utf8, p, error), qPrintable(error));
      QCOMPARE(get(p), raw);
    }
    QString legacy = dir.filePath("legacy");
    put(legacy, QByteArray::fromHex("436166e90d0a"));
    DocumentData d;
    QString error;
    QVERIFY(!DocumentData::read(legacy, d, error));
    QVERIFY(DocumentData::read(legacy, d, error, "Windows-1252"));
    QVERIFY(d.write(d.utf8, legacy, error));
    QCOMPARE(get(legacy), QByteArray::fromHex("436166e90d0a"));
  }
  void refusesLossyAndChangedSaves() {
    QTemporaryDir dir;
    QString p = dir.filePath("file");
    put(p, "before\n");
    DocumentData d;
    QString error;
    QVERIFY(DocumentData::read(p, d, error));
    put(p, "external\n");
    QVERIFY(!d.write("ours\n", p, error));
    QCOMPARE(get(p), QByteArray("external\n"));
    DocumentData legacy;
    legacy.encoding = "Windows-1252";
    QVERIFY(!legacy.write(QString::fromUtf8("🌍").toUtf8(),
                          dir.filePath("lossy"), error));
    QVERIFY(!QFile::exists(dir.filePath("lossy")));
    QFile::remove(p);
    QVERIFY(!d.write("ours", p, error));
    QVERIFY(!QFile::exists(p));
    QVERIFY(!d.write("ours", dir.filePath("missing/file"), error));
  }
  void preservesSymlinkAndPermissions() {
#ifdef Q_OS_WIN
    QSKIP("needs POSIX symlinks, FIFOs or permission bits");
#else
    QTemporaryDir dir;
    QString target = dir.filePath("target"), link = dir.filePath("link"),
            hard = dir.filePath("hard");
    put(target, "before");
    QVERIFY(::symlink(QFile::encodeName(target), QFile::encodeName(link)) == 0);
    DocumentData d;
    QString error;
    QVERIFY(DocumentData::read(link, d, error));
    QVERIFY2(d.write("after", link, error), qPrintable(error));
    QVERIFY(QFileInfo(link).isSymLink());
    QCOMPARE(get(target), QByteArray("after"));
    QVERIFY(::link(QFile::encodeName(target), QFile::encodeName(hard)) == 0);
    QVERIFY(!d.write("break", link, error));
    QCOMPARE(get(hard), QByteArray("after"));
    QFile::remove(hard);
    QVERIFY(::chmod(QFile::encodeName(target), 0444) == 0);
    QVERIFY(!d.write("readonly", link, error));
    QCOMPARE(get(target), QByteArray("after"));
#endif
  }
  void syntaxAndSharedSplit() {
    Window w(true);
    auto *t = w.newDocument("def hello():\n    return 42\n");
    w.applyLanguage(t, "python");
    QCOMPARE(t->primary->send(SCI_GETLEXER), sptr_t(SCLEX_PYTHON));
    QVERIFY(t->primary->send(SCI_GETSTYLEAT, 0) != 0);
    t->toggleSplit();
    QVERIFY(!t->secondary->isHidden());
    QCOMPARE(t->primary->send(SCI_GETDOCPOINTER),
             t->secondary->send(SCI_GETDOCPOINTER));
    t->secondary->send(SCI_APPENDTEXT, 5, reinterpret_cast<sptr_t>("# end"));
    QVERIFY(t->text().endsWith("# end"));
    QVERIFY(t->dirty());
    t->primary->send(SCI_UNDO);
    QVERIFY(!t->text().endsWith("# end"));
    QVERIFY(GetLexerCount() > 100);
    auto *udl = CreateLexer("user");
    QVERIFY(udl);
    udl->Release();
  }
  void literalSearchReplaceAndUndo() {
    Window w(true);
    auto *t = w.newDocument("one ONE stone one\n");
    t->metadataDirty = false;
    t->primary->send(SCI_SETSAVEPOINT);
    w.query->setText("one");
    w.replacement->setText("two");
    w.matchCase->setChecked(true);
    w.wholeWord->setChecked(true);
    QCOMPARE(w.replaceAll(), 2);
    QCOMPARE(t->text(), QByteArray("two ONE stone two\n"));
    t->primary->send(SCI_UNDO);
    QCOMPARE(t->text(), QByteArray("one ONE stone one\n"));
    t->primary->send(SCI_SETSEL, 0, 0);
    QVERIFY(w.find());
    QCOMPARE(t->primary->send(SCI_GETSELECTIONSTART), sptr_t(0));
    QVERIFY(w.find(true));
    QCOMPARE(t->primary->send(SCI_GETSELECTIONSTART), sptr_t(14));
  }
  void boostRegexCaptureAndZeroWidth() {
    Window w(true);
    auto *t = w.newDocument("item12 item34\n");
    w.regex->setChecked(true);
    w.query->setText("item(\\d+)");
    w.replacement->setText("value\\1");
    QCOMPARE(w.replaceAll(), 2);
    QCOMPARE(t->text(), QByteArray("value12 value34\n"));
    t->setText("a\nb\n");
    w.query->setText("^");
    w.replacement->setText(">");
    QCOMPARE(w.replaceAll(), 3);
    QCOMPARE(t->text(), QByteArray(">a\n>b\n>"));
    t->setText(QString::fromUtf8("café 🌍 café\n").toUtf8());
    w.query->setText("café");
    w.replacement->setText("thé");
    QCOMPARE(w.replaceAll(), 2);
    QCOMPARE(t->text(), QString::fromUtf8("thé 🌍 thé\n").toUtf8());
    w.query->setText("[");
    QCOMPARE(w.replaceAll(), 0);
  }
  void openSaveAndEncodingMetadata() {
    QTemporaryDir dir;
    QString p = dir.filePath("test.py");
    put(p, "print('hello')\r\n");
    Window w(true);
    w.newDocument(); // pristine blank tab is replaced by the opened file
    QVERIFY(w.openFile(p));
    auto *t = w.current();
    QCOMPARE(t->language, QString("python"));
    QCOMPARE(t->primary->send(SCI_GETEOLMODE), sptr_t(SC_EOL_CRLF));
    QVERIFY(!t->dirty());
    QVERIFY(w.openFile(p));
    QCOMPARE(w.tabs->count(), 1);
    w.encodingChoice->setCurrentText("UTF-8 BOM");
    QVERIFY(t->dirty());
    QVERIFY(w.save(t));
    QVERIFY(!t->dirty());
    QVERIFY(get(p).startsWith(QByteArray::fromHex("efbbbf")));
  }
  void recoveryPreservesUnsavedTextAndMetadata() {
    QTemporaryDir dir;
    QString path = dir.filePath("recovery.json");
    {
      Window w(false);
      w.sessionPath = path;
      auto *t = w.newDocument(QString::fromUtf8("unsaved 🌍\n").toUtf8());
      t->data.encoding = "UTF-16BE";
      t->data.bom = QByteArray::fromHex("feff");
      w.applyLanguage(t, "python");
      t->primary->send(SCI_GOTOPOS, 4);
      w.snapshot();
      QVERIFY(QFile::exists(path));
    }
    Window restored(true);
    restored.sessionPath = path;
    restored.restore();
    QCOMPARE(restored.tabs->count(), 1);
    auto *t = restored.current();
    QCOMPARE(t->text(), QString::fromUtf8("unsaved 🌍\n").toUtf8());
    QCOMPARE(t->data.encoding, QString("UTF-16BE"));
    QCOMPARE(t->language, QString("python"));
    QVERIFY(t->dirty());
    QCOMPARE(t->primary->send(SCI_GETCURRENTPOS), sptr_t(4));
  }
  void userDefinedLanguage() {
    QVERIFY(QFile::exists(dataPath("langs.model.xml")));
    QTemporaryDir dir;
    QString p = dir.filePath("notes.md");
    put(p, "# Title\n\nSome text <!-- note -->\n");
    Window w(true);
    QVERIFY(w.openFile(p));
    auto *t = w.current();
    QVERIFY2(t->language.startsWith("Markdown"), qPrintable(t->language));
    QVERIFY(!w.languages[t->language].udl.isNull());
    QCOMPARE(t->primary->send(SCI_GETLEXER), sptr_t(SCLEX_USER));
    QCOMPARE(t->primary->send(SCI_GETSTYLEAT, 0),
             sptr_t(SCE_USER_STYLE_COMMENTLINE));
    QCOMPARE(t->primary->send(SCI_GETSTYLEAT, t->text().indexOf("<!--") + 1),
             sptr_t(SCE_USER_STYLE_COMMENT));
  }
  void markAllHighlightsMatches() {
    Window w(true);
    auto *t = w.newDocument("cat dog cat bird cat\n");
    auto marked = [t](int pos) {
      return t->primary->send(SCI_INDICATORVALUEAT, 31, pos);
    };
    w.query->setText("cat");
    QCOMPARE(w.markAll(), 3);
    QCOMPARE(marked(0), sptr_t(1));
    QCOMPARE(marked(4), sptr_t(0));
    QCOMPARE(marked(17), sptr_t(1));
    w.regex->setChecked(true);
    w.query->setText("^");
    QCOMPARE(w.markAll(), 0);
    QCOMPARE(marked(0), sptr_t(0));
  }
  void findInFilesUsesEditorRegex() {
    QTemporaryDir dir;
    QDir(dir.path()).mkpath("sub");
    QDir(dir.path()).mkpath(".git");
    put(dir.filePath("a.txt"), "alpha 12\nbeta\nalpha 345\n");
    put(dir.filePath("sub/b.cpp"), "int alpha9;\n");
    put(dir.filePath("binary.dat"), QByteArray("alpha 1\0\1", 9));
    put(dir.filePath(".git/c.txt"), "alpha 1\n");
    Window w(true);
    w.regex->setChecked(true);
    w.query->setText("alpha ?(\\d+)");
    {
      const int n = w.findInFiles(dir.path(), "*");
      if (n != 3)
        for (int i = 0; i < w.results->topLevelItemCount(); ++i) {
          auto *it = w.results->topLevelItem(i);
          qWarning() << "DIAG" << it->text(0) << it->childCount();
          for (int j = 0; j < it->childCount(); ++j)
            qWarning() << "DIAG  " << it->child(j)->text(0);
        }
      QCOMPARE(n, 3);
    }
    QCOMPARE(w.results->topLevelItemCount(), 2);
    QCOMPARE(w.findInFiles(dir.path(), "*.cpp *.h"), 1);
    w.openResult(w.results->topLevelItem(0)->child(0));
    auto *e = w.current()->focused;
    QCOMPARE(w.current()->data.path, dir.filePath("sub/b.cpp"));
    QCOMPARE(e->send(SCI_GETSELECTIONSTART), sptr_t(4));
    QCOMPARE(e->send(SCI_GETSELECTIONEND), sptr_t(10));
    w.query->setText("(");
    QCOMPARE(w.findInFiles(dir.path(), "*"), 0);
  }
  void detectsChangesInBackgroundTabs() {
    QTemporaryDir dir;
    QString a = dir.filePath("a.txt"), b = dir.filePath("b.txt");
    put(a, "first\n");
    put(b, "second\n");
    Window w(true);
    QVERIFY(w.openFile(a));
    QVERIFY(w.openFile(b));
    w.checkDisk();
    QVERIFY(w.statusBar()->currentMessage().isEmpty());
    put(a, "changed elsewhere\n");
    w.checkDisk();
    QVERIFY(w.statusBar()->currentMessage().startsWith("a.txt changed"));
  }
  void everyLanguageHasALexer() {
    Window w(true);
    // "nfo" is ASCII art with no matching Lexilla lexer; it stays plain text.
    const QSet<QString> plain{"nfo"};
    for (const auto &l : w.languages) {
      if (!l.udl.isNull() || plain.contains(l.name))
        continue;
      auto *lexer = CreateLexer(l.lexer.toUtf8().constData());
      QVERIFY2(lexer, qPrintable("no Lexilla lexer '" + l.lexer +
                                 "' for language " + l.name));
      lexer->Release();
    }
  }
  void closeCancelKeepsDocument() {
    Window w(true);
    auto *t = w.newDocument("keep me");
    QTimer::singleShot(0, [] {
      for (auto *widget : QApplication::topLevelWidgets())
        if (auto *box = qobject_cast<QMessageBox *>(widget))
          box->button(QMessageBox::Cancel)->click();
    });
    QVERIFY(!w.closeTab(0));
    QCOMPARE(w.tabs->count(), 1);
    QCOMPARE(w.current(), t);
    QCOMPARE(t->text(), QByteArray("keep me"));
  }
};
int runEdgeCases();
int selfTest(QApplication &app) {
  Q_UNUSED(app);
  Integration test;
  int argc = 1;
  char name[] = "integration";
  char *argv[] = {name, nullptr};
  int failed = QTest::qExec(&test, argc, argv);
  return failed + runEdgeCases();
}
#include "integration.moc"
