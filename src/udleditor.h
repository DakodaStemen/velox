// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Velox Authors
#pragma once
#include "window.h"
// A small, editable description of a user-defined language. It covers the common
// cases (keywords, comments, strings, numbers, operators); Velox writes it out
// as a UDL 2.x file in the configuration folder, where the generic user
// lexer picks it up like any hand-written definition.
struct UdlSpec {
  static constexpr int keywordGroups = 4;
  QString name, extensions, lineComment, blockOpen, blockClose, operators;
  bool ignoreCase = false, doubleQuotes = true, singleQuotes = false;
  QString keywords[keywordGroups];
  bool keywordBold[keywordGroups] = {false, false, false, false};
  QColor keywordColor[keywordGroups] = {"#3b82c4", "#b04a8f", "#2a9d8f",
                                        "#a0762a"};
  QColor commentColor = "#5a9a4f", numberColor = "#c08a1c",
         stringColor = "#c0622b", operatorColor = "#7a8494";
  // Empty when the spec can be saved; otherwise the reason it cannot.
  // `original` is the name being edited, which may keep its own name.
  QString validate(const QMap<QString, Language> &languages,
                   const QString &original = {}) const;
  QString normalizedExtensions() const;
  QString fileName() const; // within configPath("userDefineLangs")
  QByteArray toXml() const;
  bool save(QString &error) const;
  static bool fromElement(const QDomElement &userLang, UdlSpec &out);
  // Definitions made with this editor, by language name (value: file path).
  static QMap<QString, QString> saved();
};
class UdlEditor : public QDialog {
public:
  UdlEditor(const UdlSpec &spec, bool existing, QWidget *parent = nullptr);
  UdlSpec spec() const;
  bool removed = false; // set when the user chose Delete

private:
  QLineEdit *name, *extensions, *lineComment, *blockOpen, *blockClose,
      *operators, *keywords[UdlSpec::keywordGroups];
  QCheckBox *ignoreCase, *doubleQuotes, *singleQuotes,
      *bold[UdlSpec::keywordGroups];
  QPushButton *keywordColor[UdlSpec::keywordGroups], *commentColor,
      *numberColor, *stringColor, *operatorColor;
  QColor colors[UdlSpec::keywordGroups + 4];
  QPushButton *colorButton(int slot, const QColor &initial);
};
