// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Velox Authors
#include "window.h"
#include <QtNetwork>
#include <QtTest>
#include <unistd.h>
int selfTest(QApplication &app);
static QString instanceName() {
  return QString("velox-%1").arg(getuid());
}
// Hands files to an already running Velox; true when one accepted them.
static bool forwardToRunning(const QStringList &files) {
  QLocalSocket socket;
  socket.connectToServer(instanceName());
  if (!socket.waitForConnected(500))
    return false;
  socket.write(QJsonDocument(QJsonArray::fromStringList(files))
                   .toJson(QJsonDocument::Compact));
  if (!socket.waitForBytesWritten(1000))
    return false;
  socket.disconnectFromServer();
  if (socket.state() != QLocalSocket::UnconnectedState)
    socket.waitForDisconnected(1000);
  return true;
}
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  QCoreApplication::setApplicationName("velox");
  QCoreApplication::setApplicationVersion("0.1.0");
  QGuiApplication::setApplicationDisplayName("Velox");
  QGuiApplication::setDesktopFileName("io.github.DakodaStemen.Velox");
  QApplication::setWindowIcon(
      QIcon::fromTheme("io.github.DakodaStemen.Velox", QIcon(":/velox.svg")));
  QCommandLineParser parser;
  parser.setApplicationDescription(
      "Velox — a fast native text editor built on Scintilla and Lexilla");
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addOption(
      {"self-test", "Run integration tests in an isolated profile"});
  parser.addOption({"fresh", "Start without restoring a previous session"});
  parser.addOption({"new-instance",
                    "Open a separate window instead of reusing a running one"});
  parser.addOption({"screenshot", "Write a window preview and exit", "path"});
  parser.addPositionalArgument("files", "Files to open", "[files…]");
  parser.process(app);
  if (parser.isSet("self-test")) {
    QStandardPaths::setTestModeEnabled(true);
    return selfTest(app);
  }
  QStringList files;
  for (const auto &path : parser.positionalArguments())
    files << QFileInfo(path).absoluteFilePath();
  bool shot = parser.isSet("screenshot");
  bool shared = !shot && !parser.isSet("new-instance");
  // The lock makes "is one running? if not, become it" atomic, so several
  // simultaneous launches still end up in a single window.
  QLockFile lock(QDir::tempPath() + '/' + instanceName() + ".lock");
  QLocalServer server;
  if (shared) {
    lock.tryLock(5000);
    if (forwardToRunning(files))
      return 0;
    QLocalServer::removeServer(instanceName()); // stale socket after a crash
    server.setSocketOptions(QLocalServer::UserAccessOption);
    server.listen(instanceName());
    lock.unlock();
  }
  app.setStyle("Fusion");
  QPalette p;
  p.setColor(QPalette::Window, QColor("#202630"));
  p.setColor(QPalette::WindowText, QColor("#e0e6ee"));
  p.setColor(QPalette::Base, QColor("#191e26"));
  p.setColor(QPalette::AlternateBase, QColor("#252c37"));
  p.setColor(QPalette::Text, QColor("#e0e6ee"));
  p.setColor(QPalette::Button, QColor("#303946"));
  p.setColor(QPalette::ButtonText, QColor("#e0e6ee"));
  p.setColor(QPalette::Highlight, QColor("#346a95"));
  p.setColor(QPalette::HighlightedText, Qt::white);
  p.setColor(QPalette::ToolTipBase, QColor("#303946"));
  p.setColor(QPalette::ToolTipText, Qt::white);
  app.setPalette(p);
  app.setStyleSheet(
      "QMenuBar{padding:5px} "
      "QToolBar{spacing:7px;padding:7px;border-bottom:1px solid #394352} "
      "QTabBar::tab{padding:10px 18px} QTabBar::tab:selected{border-bottom:2px "
      "solid #e0874a} QTabBar::close-button{image:url(:/tab-close.svg)} "
      "QTabBar::close-button:hover{background:#394352;border-radius:3px} "
      "QStatusBar{padding:4px} QComboBox{padding:4px} "
      "QPushButton{padding:6px 12px} QLineEdit{padding:6px} "
      "QDockWidget::title{padding:8px} QSplitter::handle{background:#394352}");
  Window window(shot);
  if (!parser.isSet("fresh") && !shot)
    window.restore();
  for (const auto &path : files)
    window.openFile(path);
  if (!window.current())
    window.newDocument();
  if (server.isListening()) {
    QObject::connect(&server, &QLocalServer::newConnection, [&] {
      while (auto *socket = server.nextPendingConnection())
        QObject::connect(
            socket, &QLocalSocket::disconnected, socket, [&window, socket] {
              for (const auto &f :
                   QJsonDocument::fromJson(socket->readAll()).array())
                window.openFile(f.toString());
              window.setWindowState(window.windowState() & ~Qt::WindowMinimized);
              window.raise();
              window.activateWindow();
              socket->deleteLater();
            });
    });
  }
  window.show();
  if (shot)
    QTimer::singleShot(700, &app, [&] {
      window.grab().save(parser.value("screenshot"));
      app.quit();
    });
  return app.exec();
}
