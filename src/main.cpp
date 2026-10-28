// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Velox Authors
#include "window.h"
#include <QtNetwork>
#ifdef VELOX_SELFTEST
#include <QtTest>
#endif
#include <memory>
#ifdef Q_OS_UNIX
#include <unistd.h>
#endif
#ifdef VELOX_SELFTEST
int selfTest(QApplication &app);
#endif
// A stable per-user token for the instance name: the numeric uid on POSIX,
// the account name elsewhere (Windows has no uid).
static QString userToken() {
#ifdef Q_OS_UNIX
  return QString::number(qulonglong(getuid()));
#else
  QString name = qEnvironmentVariable("USERNAME");
  if (name.isEmpty())
    name = qEnvironmentVariable("USER", "user");
  name.replace(QRegularExpression("[^A-Za-z0-9_.-]"), "_");
  return name;
#endif
}

#ifdef VELOX_DEMO
int runDemo(Window &window, const QString &dir);
#endif
// Prefer the per-user runtime directory (0700, owned by us) over the shared,
// world-writable /tmp for the socket and lock file. Only XDG systems have one.
static QString runtimeDir() {
#ifdef Q_OS_UNIX
  const QString dir = qEnvironmentVariable("XDG_RUNTIME_DIR");
  QFileInfo info(dir);
  if (dir.isEmpty() || !QDir::isAbsolutePath(dir) || !info.isDir() ||
      !info.isWritable() || info.ownerId() != getuid())
    return {};
  return QDir::cleanPath(dir);
#else
  return {};
#endif
}
static QString instanceName() {
  const QString name = QString("velox-%1").arg(userToken());
  const QString dir = runtimeDir();
  // On Windows QLocalSocket is a named pipe, so a bare name is required.
  return dir.isEmpty() ? name : dir + '/' + name + ".sock";
}
static QString lockPath() {
  const QString dir = runtimeDir();
  return (dir.isEmpty() ? QDir::tempPath() : dir) +
         QString("/velox-%1.lock").arg(userToken());
}
static const qint64 kMaxForwardBytes = 1024 * 1024;
static const int kForwardTimeoutMs = 5000;
// Validates what a client sent: a JSON array of absolute paths, nothing else.
QStringList parseForwardedFiles(const QByteArray &data, bool *ok) {
  QStringList files;
  *ok = false;
  if (data.size() > kMaxForwardBytes)
    return files;
  QJsonParseError err;
  const auto doc = QJsonDocument::fromJson(data, &err);
  if (err.error != QJsonParseError::NoError || !doc.isArray())
    return files;
  for (const auto &v : doc.array()) {
    const QString f = v.toString();
    if (!f.isEmpty() && QDir::isAbsolutePath(f))
      files << f;
  }
  *ok = true;
  return files;
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
  QCoreApplication::setApplicationVersion(QStringLiteral(VELOX_VERSION));
  QGuiApplication::setApplicationDisplayName("Velox");
  QGuiApplication::setDesktopFileName("io.github.DakodaStemen.Velox");
  QApplication::setWindowIcon(
      QIcon::fromTheme("io.github.DakodaStemen.Velox", QIcon(":/velox.svg")));
  QCommandLineParser parser;
  parser.setApplicationDescription(
      "Velox — a fast native text editor built on Scintilla and Lexilla");
  parser.addHelpOption();
  parser.addVersionOption();
#ifdef VELOX_SELFTEST
  parser.addOption(
      {"self-test", "Run integration tests in an isolated profile"});
#endif
  parser.addOption({"fresh", "Start without restoring a previous session"});
  parser.addOption({"new-instance",
                    "Open a separate window instead of reusing a running one"});
#ifdef VELOX_DEMO
  parser.addOption({"demo", "Render the README animation frames", "dir"});
#endif
  parser.addOption({"screenshot", "Write a window preview and exit", "path"});
  parser.addPositionalArgument("files", "Files to open; FILE:LINE[:COLUMN] jumps to a position",
                               "[files…]");
  parser.process(app);
#ifdef VELOX_SELFTEST
  if (parser.isSet("self-test")) {
    QStandardPaths::setTestModeEnabled(true);
    return selfTest(app);
  }
#endif
#ifdef VELOX_DEMO
  const bool demo = parser.isSet("demo");
  if (demo)
    QStandardPaths::setTestModeEnabled(true); // isolated profile
#else
  const bool demo = false;
#endif
  QStringList files;
  for (const auto &path : parser.positionalArguments())
    files << QFileInfo(path).absoluteFilePath();
  bool shot = parser.isSet("screenshot") || demo;
  bool shared = !shot && !parser.isSet("new-instance");
  // The lock makes "is one running? if not, become it" atomic, so several
  // simultaneous launches still end up in a single window.
  QLockFile lock(lockPath());
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
  p.setColor(QPalette::Highlight, QColor("#2b62b8"));
  p.setColor(QPalette::HighlightedText, Qt::white);
  p.setColor(QPalette::ToolTipBase, QColor("#303946"));
  p.setColor(QPalette::ToolTipText, Qt::white);
  app.setPalette(p);
  app.setStyleSheet(
      "QMenuBar{padding:5px} "
      "QToolBar{spacing:7px;padding:7px;border-bottom:1px solid #394352} "
      "QTabBar::tab{padding:10px 18px} QTabBar::tab:selected{border-bottom:2px "
      "solid #4aa8f5} QTabBar::close-button{image:url(:/tab-close.svg)} "
      "QTabBar::close-button:hover{background:#394352;border-radius:3px} "
      "QStatusBar{padding:4px} QComboBox{padding:4px} "
      "QPushButton{padding:6px 12px} QLineEdit{padding:6px} "
      "QDockWidget::title{padding:8px} QSplitter::handle{background:#394352}");
  Window window(shot, shared);
#ifdef VELOX_DEMO
  if (demo)
    return runDemo(window, parser.value("demo"));
#endif
  if (!parser.isSet("fresh") && !shot)
    window.restore();
  for (const auto &path : files)
    window.openSpec(path);
  if (!window.current())
    window.newDocument();
  if (server.isListening()) {
    QObject::connect(&server, &QLocalServer::newConnection, [&] {
      while (auto *socket = server.nextPendingConnection()) {
        // Size-capped and time-limited: a client that never disconnects or
        // streams endlessly is dropped instead of holding the socket.
        auto data = std::make_shared<QByteArray>();
        auto done = std::make_shared<bool>(false);
        auto finish = [&window, socket, data, done](bool accept) {
          if (*done)
            return;
          *done = true;
          socket->disconnect();
          bool ok = false;
          const QStringList files =
              accept ? parseForwardedFiles(*data, &ok) : QStringList();
          if (ok) {
            for (const auto &f : files)
              window.openSpec(f);
            window.setWindowState(window.windowState() &
                                  ~Qt::WindowMinimized);
            window.raise();
            window.activateWindow();
          }
          socket->abort();
          socket->deleteLater();
        };
        QObject::connect(socket, &QLocalSocket::readyRead, socket,
                         [socket, data, finish] {
                           data->append(socket->readAll());
                           if (data->size() > kMaxForwardBytes)
                             finish(false);
                         });
        QObject::connect(socket, &QLocalSocket::disconnected, socket,
                         [socket, data, finish] {
                           data->append(socket->readAll());
                           finish(true);
                         });
        QTimer::singleShot(kForwardTimeoutMs, socket,
                           [finish] { finish(false); });
      }
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
