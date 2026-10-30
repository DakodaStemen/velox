VERSION = 0.1.0
QT += core gui widgets xml network
greaterThan(QT_MAJOR_VERSION, 5): QT += core5compat
CONFIG += c++17 release
DEFINES += VELOX_VERSION=\\\"$$VERSION\\\"
TEMPLATE = app
TARGET = velox
DESTDIR = bin
SOURCES += src/main.cpp src/document.cpp src/window.cpp src/udleditor.cpp src/palette.cpp

# Build options (pass to qmake as CONFIG+=...; build.sh sets selftest by default):
#   selftest  - compile the test suite in and enable `velox --self-test`
#   sanitizer - AddressSanitizer + UndefinedBehaviorSanitizer
selftest {
    QT += testlib
    DEFINES += VELOX_SELFTEST
    SOURCES += tests/integration.cpp tests/edge_cases.cpp
}
sanitizer {
    QMAKE_CXXFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
    QMAKE_LFLAGS += -fsanitize=address,undefined
}
# Opt out with `qmake velox.pro VELOX_HARDENING=OFF`.
# This project builds the application; separately built vendor archives must be
# given the same compiler flags by their caller (CMake handles all targets).
isEmpty(VELOX_HARDENING): VELOX_HARDENING = ON
!equals(VELOX_HARDENING, OFF):!equals(VELOX_HARDENING, 0) {
    win32-msvc* {
        QMAKE_CXXFLAGS += /guard:cf
        QMAKE_LFLAGS += /guard:cf /DYNAMICBASE /NXCOMPAT
        contains(QT_ARCH, x86_64|arm64): QMAKE_LFLAGS += /HIGHENTROPYVA
    } else {
        QMAKE_CXXFLAGS += -fstack-protector-strong
        QMAKE_CXXFLAGS_RELEASE += -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3
        QMAKE_CXXFLAGS_RELEASE_WITH_DEBUGINFO += -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3
        # Probe optional flags with the selected compiler, not its version.
        # /dev/null is only used on Unix; MSVC and MinGW never run these probes.
        unix {
            system($$QMAKE_CXX $$QMAKE_CXXFLAGS -Werror -fstack-clash-protection -x c++ -c /dev/null -o /dev/null 2>/dev/null): QMAKE_CXXFLAGS += -fstack-clash-protection
            equals(QT_ARCH, x86_64) {
                system($$QMAKE_CXX $$QMAKE_CXXFLAGS -Werror -fcf-protection -x c++ -c /dev/null -o /dev/null 2>/dev/null): QMAKE_CXXFLAGS += -fcf-protection
            }
        }
        linux {
            QMAKE_CXXFLAGS += -fPIE
            QMAKE_LFLAGS += -pie -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack
        }
    }
}
HEADERS += src/document.h src/window.h src/udleditor.h src/palette.h
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
