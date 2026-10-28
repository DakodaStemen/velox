cmake_minimum_required(VERSION 3.21)

# Single source of truth for the version is velox.pro (checked by scripts/check-version.sh).
file(STRINGS velox.pro _velox_version_line REGEX "^VERSION *=")
string(REGEX REPLACE "^VERSION *= *([0-9][0-9.]*).*" "\\1" VELOX_VERSION "${_velox_version_line}")

project(velox VERSION ${VELOX_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(VELOX_SELFTEST "Compile the test suite in and enable `velox --self-test`" ON)
option(VELOX_SANITIZE "AddressSanitizer + UndefinedBehaviorSanitizer (GCC/Clang)" OFF)
set(VELOX_QT_MAJOR "" CACHE STRING "Force Qt major version (5 or 6); empty = prefer 6")

if(VELOX_QT_MAJOR)
  find_package(Qt${VELOX_QT_MAJOR} REQUIRED COMPONENTS Core Gui Widgets Xml Network Svg Test)
  set(QT_MAJOR ${VELOX_QT_MAJOR})
else()
  find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
  set(QT_MAJOR ${QT_VERSION_MAJOR})
  find_package(Qt${QT_MAJOR} REQUIRED COMPONENTS Core Gui Widgets Xml Network Svg Test)
endif()
set(QT_LIBS Qt${QT_MAJOR}::Core Qt${QT_MAJOR}::Gui Qt${QT_MAJOR}::Widgets Qt${QT_MAJOR}::Xml Qt${QT_MAJOR}::Network)
if(QT_MAJOR GREATER 5)
  find_package(Qt6 REQUIRED COMPONENTS Core5Compat)
  list(APPEND QT_LIBS Qt6::Core5Compat)
endif()

set(SCI ${CMAKE_CURRENT_SOURCE_DIR}/vendor/scintilla)
set(LEX ${CMAKE_CURRENT_SOURCE_DIR}/vendor/lexilla)
set(BOOSTRE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/boostregex)

# Sanitizers must be applied to every target, so set them globally.
if(VELOX_SANITIZE AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# ---- Lexilla (static) ------------------------------------------------------
file(GLOB LEXER_SOURCES CONFIGURE_DEPENDS ${LEX}/lexers/Lex*.cxx)
set(LEXLIB_SOURCES
  Accessor CharacterCategory CharacterSet DefaultLexer InList LexAccessor
  LexerBase LexerModule LexerSimple PropSetSimple StyleContext WordList)
list(TRANSFORM LEXLIB_SOURCES PREPEND ${LEX}/lexlib/)
list(TRANSFORM LEXLIB_SOURCES APPEND .cxx)
add_library(lexilla STATIC ${LEX}/src/Lexilla.cxx ${LEXLIB_SOURCES} ${LEXER_SOURCES})
target_include_directories(lexilla PUBLIC ${LEX}/include PRIVATE ${SCI}/include ${LEX}/lexlib)
target_compile_definitions(lexilla PRIVATE LEXILLA_NO_EXPORT $<IF:$<CONFIG:Debug>,DEBUG,NDEBUG>)
if(MSVC)
  target_compile_options(lexilla PRIVATE /utf-8 /W3)
else()
  target_compile_options(lexilla PRIVATE -Wall -Wextra -Wno-deprecated-copy)
endif()

# ---- Scintilla Qt edit widget (static) -------------------------------------
set(SCI_CORE
  XPM ViewStyle UniqueString UniConversion UndoHistory Style Selection
  ScintillaBase RunStyles RESearch PositionCache PerLine MarginView LineMarker
  KeyMap Indicator Geometry EditView Editor EditModel Document Decoration DBCS
  ContractionState CharClassify CharacterType CharacterCategoryMap
  ChangeHistory CellBuffer CaseFolder CaseConvert CallTip AutoComplete)
list(TRANSFORM SCI_CORE PREPEND ${SCI}/src/)
list(TRANSFORM SCI_CORE APPEND .cxx)
add_library(scintilla STATIC
  ${SCI}/qt/ScintillaEditBase/PlatQt.cpp
  ${SCI}/qt/ScintillaEditBase/ScintillaQt.cpp
  ${SCI}/qt/ScintillaEditBase/ScintillaEditBase.cpp
  ${SCI_CORE}
  ${BOOSTRE}/BoostRegExSearch.cxx ${BOOSTRE}/UTF8DocumentIterator.cxx)
target_include_directories(scintilla PUBLIC
  ${SCI}/include ${SCI}/src ${SCI}/qt/ScintillaEditBase)
target_include_directories(scintilla PRIVATE ${BOOSTRE})
target_compile_definitions(scintilla PUBLIC SCINTILLA_QT=1)
target_compile_definitions(scintilla PRIVATE MAKING_LIBRARY=1 SCI_OWNREGEX BOOST_REGEX_STANDALONE
  $<$<NOT:$<CONFIG:Debug>>:NDEBUG=1>)
target_link_libraries(scintilla PUBLIC ${QT_LIBS})
if(MSVC)
  target_compile_options(scintilla PRIVATE /utf-8 /bigobj)
endif()

# ---- Velox application -----------------------------------------------------
set(VELOX_SOURCES src/main.cpp src/document.cpp src/window.cpp src/udleditor.cpp
  src/document.h src/window.h src/udleditor.h)
set(VELOX_ICONS)
if(WIN32 AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/velox.rc)
  list(APPEND VELOX_ICONS packaging/windows/velox.rc)
endif()
if(APPLE)
  set(VELOX_MAC_ICON ${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Velox.icns)
  if(EXISTS ${VELOX_MAC_ICON})
    set_source_files_properties(${VELOX_MAC_ICON} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    list(APPEND VELOX_ICONS ${VELOX_MAC_ICON})
  endif()
endif()
add_executable(velox WIN32 MACOSX_BUNDLE ${VELOX_SOURCES} resources.qrc ${VELOX_ICONS})
target_include_directories(velox PRIVATE src)
target_compile_definitions(velox PRIVATE VELOX_VERSION="${VELOX_VERSION}"
  VELOX_SOURCE_SHARE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/share")
target_link_libraries(velox PRIVATE scintilla lexilla ${QT_LIBS})
if(VELOX_SELFTEST)
  target_sources(velox PRIVATE tests/integration.cpp tests/edge_cases.cpp)
  target_compile_definitions(velox PRIVATE VELOX_SELFTEST)
  target_link_libraries(velox PRIVATE Qt${QT_MAJOR}::Test)
endif()
if(MSVC)
  target_compile_options(velox PRIVATE /utf-8 /bigobj)
endif()
set_target_properties(velox PROPERTIES
  MACOSX_BUNDLE_BUNDLE_NAME Velox
  MACOSX_BUNDLE_GUI_IDENTIFIER io.github.DakodaStemen.Velox
  MACOSX_BUNDLE_BUNDLE_VERSION ${VELOX_VERSION}
  MACOSX_BUNDLE_SHORT_VERSION_STRING ${VELOX_VERSION}
  MACOSX_BUNDLE_COPYRIGHT "Copyright (C) 2026 The Velox Authors"
  MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in)

# ---- Tests -----------------------------------------------------------------
include(CTest)
if(VELOX_SELFTEST AND BUILD_TESTING)
  add_test(NAME self-test COMMAND velox --self-test)
  set_tests_properties(self-test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;ASAN_OPTIONS=detect_leaks=0;UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1"
    TIMEOUT 300)
endif()

# ---- Install + packaging ---------------------------------------------------
include(GNUInstallDirs)
set(VELOX_DATA_FILES share/langs.model.xml share/stylers.model.xml)
if(APPLE)
  # Data lives inside the bundle: Velox.app/Contents/Resources/share/velox
  set(VELOX_DATA_DEST Velox.app/Contents/Resources/share/velox)
  install(TARGETS velox BUNDLE DESTINATION . COMPONENT Runtime)
elseif(WIN32)
  # Windows layout: bin/velox.exe + share/velox (the app looks in ../share/velox)
  set(VELOX_DATA_DEST share/velox)
  install(TARGETS velox RUNTIME DESTINATION bin COMPONENT Runtime)
else()
  set(VELOX_DATA_DEST ${CMAKE_INSTALL_DATADIR}/velox)
  install(TARGETS velox RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT Runtime)
  install(FILES packaging/io.github.DakodaStemen.Velox.desktop
          DESTINATION ${CMAKE_INSTALL_DATADIR}/applications COMPONENT Runtime)
  install(FILES packaging/io.github.DakodaStemen.Velox.svg
          DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps COMPONENT Runtime)
  install(FILES packaging/io.github.DakodaStemen.Velox.metainfo.xml
          DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo COMPONENT Runtime)
endif()
install(FILES ${VELOX_DATA_FILES} DESTINATION ${VELOX_DATA_DEST} COMPONENT Runtime)
install(DIRECTORY share/themes share/userDefineLangs DESTINATION ${VELOX_DATA_DEST} COMPONENT Runtime)
install(FILES LICENSE THIRD_PARTY_NOTICES.md DESTINATION ${CMAKE_INSTALL_DOCDIR} COMPONENT Runtime)

# Bundle the Qt runtime for Windows / macOS (needs Qt 6's deploy support).
if((WIN32 OR APPLE) AND QT_MAJOR GREATER 5 AND COMMAND qt_generate_deploy_app_script)
  qt_generate_deploy_app_script(TARGET velox OUTPUT_SCRIPT VELOX_DEPLOY_SCRIPT NO_UNSUPPORTED_PLATFORM_ERROR
                                NO_TRANSLATIONS)
  install(SCRIPT ${VELOX_DEPLOY_SCRIPT} COMPONENT Runtime)
endif()

include(packaging/CPackConfig.cmake)
