QT += core gui widgets xml network testlib
greaterThan(QT_MAJOR_VERSION, 5): QT += core5compat
CONFIG += c++17 release
TEMPLATE = app
TARGET = velox
DESTDIR = bin
SOURCES += src/main.cpp src/document.cpp src/window.cpp src/udleditor.cpp tests/integration.cpp tests/edge_cases.cpp tests/demo.cpp
HEADERS += src/document.h src/window.h src/udleditor.h
RESOURCES += resources.qrc
INCLUDEPATH += src vendor/scintilla/include vendor/scintilla/src vendor/scintilla/qt/ScintillaEditBase vendor/lexilla/include
# Both engines are linked statically so the installed binary is self-contained.
LIBS += $$PWD/vendor/scintilla/bin/libScintillaEditBase.a $$PWD/vendor/lexilla/bin/liblexilla.a
PRE_TARGETDEPS += $$PWD/vendor/scintilla/bin/libScintillaEditBase.a $$PWD/vendor/lexilla/bin/liblexilla.a

isEmpty(PREFIX): PREFIX = /usr
target.path = $$PREFIX/bin
data.path = $$PREFIX/share/velox
data.files = share/langs.model.xml share/stylers.model.xml share/themes share/userDefineLangs
desktop.path = $$PREFIX/share/applications
desktop.files = packaging/io.github.DakodaStemen.Velox.desktop
icon.path = $$PREFIX/share/icons/hicolor/scalable/apps
icon.files = packaging/io.github.DakodaStemen.Velox.svg
metainfo.path = $$PREFIX/share/metainfo
metainfo.files = packaging/io.github.DakodaStemen.Velox.metainfo.xml
INSTALLS += target data desktop icon metainfo
