// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Velox Authors
#include "udleditor.h"
static const char *editorMark = "veloxEditor";
QString UdlSpec::normalizedExtensions() const {
  QStringList out;
  for (QString e : extensions.split(QRegularExpression("[\\s,;]+"),
                                    Qt::SkipEmptyParts)) {
    while (e.startsWith('.'))
      e.remove(0, 1);
    e = e.toLower();
    if (!e.isEmpty() && !out.contains(e))
      out << e;
  }
  return out.join(' ');
}
QString UdlSpec::fileName() const {
  QString slug = name.toLower();
  slug.replace(QRegularExpression("[^a-z0-9]+"), "-");
  slug = slug.mid(slug.startsWith('-') ? 1 : 0);
  if (slug.endsWith('-'))
    slug.chop(1);
  return (slug.isEmpty() ? QString("language") : slug) + ".xml";
}
QString UdlSpec::validate(const QMap<QString, Language> &languages,
                          const QString &original) const {
  const QString n = name.trimmed();
  if (n.isEmpty())
    return "Give the language a name.";
  for (const auto &l : languages)
    if (l.name.compare(n, Qt::CaseInsensitive) == 0 &&
        l.name.compare(original, Qt::CaseInsensitive) != 0)
      return l.udl.isNull()
                 ? "\"" + l.name + "\" is a built-in language. Choose another name."
                 : "A language called \"" + l.name + "\" already exists.";
  static const QRegularExpression space("\\s");
  for (const QString &marker : {lineComment, blockOpen, blockClose})
    if (marker.contains(space))
      return "Comment markers cannot contain spaces.";
  if (blockOpen.isEmpty() != blockClose.isEmpty())
    return "A block comment needs both an opening and a closing marker.";
  return {};
}
// UDL comment settings are numbered slots: 00 line start, 01 continuation,
// 02 line end, 03 block start, 04 block end. Delimiters use three slots per
// pair: open, escape, close.
QByteArray UdlSpec::toXml() const {
  QDomDocument doc;
  doc.appendChild(doc.createProcessingInstruction(
      "xml", "version=\"1.0\" encoding=\"UTF-8\""));
  auto root = doc.createElement("Velox");
  doc.appendChild(root);
  auto lang = doc.createElement("UserLang");
  lang.setAttribute("name", name.trimmed());
  lang.setAttribute("ext", normalizedExtensions());
  lang.setAttribute("udlVersion", "2.1");
  lang.setAttribute(editorMark, "1");
  root.appendChild(lang);
  auto settings = doc.createElement("Settings");
  lang.appendChild(settings);
  auto global = doc.createElement("Global");
  global.setAttribute("caseIgnored", ignoreCase ? "yes" : "no");
  global.setAttribute("allowFoldOfComments", "no");
  global.setAttribute("foldCompact", "no");
  global.setAttribute("forcePureLC", "0");
  global.setAttribute("decimalSeparator", "0");
  settings.appendChild(global);
  auto prefix = doc.createElement("Prefix");
  for (int i = 1; i <= 8; ++i)
    prefix.setAttribute(QString("Keywords%1").arg(i), "no");
  settings.appendChild(prefix);
  QStringList comments, delimiters;
  if (!lineComment.isEmpty())
    comments << "00" + lineComment << "01" << "02((EOL))";
  if (!blockOpen.isEmpty())
    comments << "03" + blockOpen << "04" + blockClose;
  if (doubleQuotes)
    delimiters << "00\"" << "01\\" << "02\"";
  if (singleQuotes)
    delimiters << "03'" << "04\\" << "05'";
  auto lists = doc.createElement("KeywordLists");
  lang.appendChild(lists);
  auto add = [&](const QString &listName, const QString &text) {
    auto k = doc.createElement("Keywords");
    k.setAttribute("name", listName);
    k.appendChild(doc.createTextNode(text));
    lists.appendChild(k);
  };
  add("Comments", comments.join(' '));
  for (const char *n : {"Numbers, prefix1", "Numbers, prefix2",
                        "Numbers, extras1", "Numbers, extras2",
                        "Numbers, suffix1", "Numbers, suffix2",
                        "Numbers, range"})
    add(n, {});
  add("Operators1", operators.simplified());
  add("Operators2", {});
  for (const char *f : {"code1", "code2", "comment"})
    for (const char *p : {"open", "middle", "close"})
      add(QString("Folders in %1, %2").arg(f, p), {});
  for (int i = 0; i < 8; ++i)
    add(QString("Keywords%1").arg(i + 1),
        i < keywordGroups ? keywords[i].simplified() : QString());
  add("Delimiters", delimiters.join(' '));
  auto styles = doc.createElement("Styles");
  lang.appendChild(styles);
  auto style = [&](const QString &styleName, const QColor &fg, int fontStyle) {
    auto s = doc.createElement("WordsStyle");
    s.setAttribute("name", styleName);
    s.setAttribute("fgColor", fg.isValid() ? fg.name().mid(1).toUpper() : "");
    s.setAttribute("bgColor", "FFFFFF");
    // 0 keeps the theme's colours; 1 applies only this foreground colour.
    s.setAttribute("colorStyle", fg.isValid() ? "1" : "0");
    s.setAttribute("fontName", "");
    s.setAttribute("fontStyle", QString::number(fontStyle));
    s.setAttribute("nesting", "0");
    styles.appendChild(s);
  };
  style("DEFAULT", {}, 0);
  style("COMMENTS", commentColor, 2);
  style("LINE COMMENTS", commentColor, 2);
  style("NUMBERS", numberColor, 0);
  for (int i = 0; i < keywordGroups; ++i)
    style(QString("KEYWORDS%1").arg(i + 1), keywordColor[i],
          keywordBold[i] ? 1 : 0);
  style("OPERATORS", operatorColor, 0);
  style("DELIMITERS1", stringColor, 0);
  style("DELIMITERS2", stringColor, 0);
  return doc.toByteArray(2);
}
bool UdlSpec::save(QString &error) const {
  QDir().mkpath(configPath("userDefineLangs"));
  QSaveFile f(configPath("userDefineLangs/" + fileName()));
  if (!f.open(QIODevice::WriteOnly) || f.write(toXml()) < 0 || !f.commit()) {
    error = f.errorString();
    return false;
  }
  return true;
}
bool UdlSpec::fromElement(const QDomElement &lang, UdlSpec &out) {
  if (lang.attribute(editorMark) != "1")
    return false;
  UdlSpec s;
  s.name = lang.attribute("name");
  s.extensions = lang.attribute("ext");
  s.ignoreCase = lang.firstChildElement("Settings")
                     .firstChildElement("Global")
                     .attribute("caseIgnored") == "yes";
  QHash<QString, QString> words;
  for (auto k = lang.firstChildElement("KeywordLists").firstChildElement("Keywords");
       !k.isNull(); k = k.nextSiblingElement("Keywords"))
    words[k.attribute("name")] = k.text();
  for (int i = 0; i < keywordGroups; ++i)
    s.keywords[i] = words.value(QString("Keywords%1").arg(i + 1));
  s.operators = words.value("Operators1");
  for (const QString &token :
       words.value("Comments").split(' ', Qt::SkipEmptyParts)) {
    const QString slot = token.left(2), text = token.mid(2);
    if (slot == "00")
      s.lineComment = text;
    else if (slot == "03")
      s.blockOpen = text;
    else if (slot == "04")
      s.blockClose = text;
  }
  const QString delimiters = words.value("Delimiters");
  s.doubleQuotes = delimiters.contains("00\"");
  s.singleQuotes = delimiters.contains("03'");
  QHash<QString, QDomElement> styles;
  for (auto n = lang.firstChildElement("Styles").firstChildElement("WordsStyle");
       !n.isNull(); n = n.nextSiblingElement("WordsStyle"))
    styles[n.attribute("name")] = n;
  auto colorOf = [&styles](const QString &styleName, const QColor &fallback) {
    const QString hex = styles.value(styleName).attribute("fgColor");
    return hex.isEmpty() ? fallback : QColor('#' + hex);
  };
  s.commentColor = colorOf("COMMENTS", s.commentColor);
  s.numberColor = colorOf("NUMBERS", s.numberColor);
  s.stringColor = colorOf("DELIMITERS1", s.stringColor);
  s.operatorColor = colorOf("OPERATORS", s.operatorColor);
  for (int i = 0; i < keywordGroups; ++i) {
    const QString key = QString("KEYWORDS%1").arg(i + 1);
    s.keywordColor[i] = colorOf(key, s.keywordColor[i]);
    s.keywordBold[i] = styles.value(key).attribute("fontStyle").toInt() & 1;
  }
  out = s;
  return true;
}
QMap<QString, QString> UdlSpec::saved() {
  QMap<QString, QString> found;
  for (const auto &file : QDir(configPath("userDefineLangs"))
                              .entryInfoList({"*.xml"}, QDir::Files, QDir::Name)) {
    QFile f(file.filePath());
    QDomDocument d;
    if (!f.open(QIODevice::ReadOnly) || !d.setContent(&f))
      continue;
    auto langs = d.elementsByTagName("UserLang");
    for (int i = 0; i < langs.size(); ++i) {
      UdlSpec spec;
      if (fromElement(langs.at(i).toElement(), spec))
        found[spec.name] = file.filePath();
    }
  }
  return found;
}
QPushButton *UdlEditor::colorButton(int slot, const QColor &initial) {
  colors[slot] = initial;
  auto *b = new QPushButton;
  b->setFixedWidth(44);
  auto paint = [b, this, slot] {
    b->setStyleSheet(QString("background:%1;border:1px solid #888")
                         .arg(colors[slot].name()));
  };
  paint();
  connect(b, &QPushButton::clicked, this, [this, slot, paint] {
    const QColor c = QColorDialog::getColor(colors[slot], this);
    if (c.isValid()) {
      colors[slot] = c;
      paint();
    }
  });
  return b;
}
UdlEditor::UdlEditor(const UdlSpec &s, bool existing, QWidget *parent)
    : QDialog(parent) {
  setWindowTitle(existing ? "Edit custom language" : "Define custom language");
  setMinimumWidth(720);
  auto *form = new QFormLayout(this);
  name = new QLineEdit(s.name);
  extensions = new QLineEdit(s.extensions);
  extensions->setPlaceholderText("e.g. log txt");
  ignoreCase = new QCheckBox("Ignore case in keywords");
  ignoreCase->setChecked(s.ignoreCase);
  lineComment = new QLineEdit(s.lineComment);
  lineComment->setPlaceholderText("e.g. #");
  blockOpen = new QLineEdit(s.blockOpen);
  blockClose = new QLineEdit(s.blockClose);
  blockOpen->setPlaceholderText("/*");
  blockClose->setPlaceholderText("*/");
  doubleQuotes = new QCheckBox("Double-quoted strings");
  doubleQuotes->setChecked(s.doubleQuotes);
  singleQuotes = new QCheckBox("Single-quoted strings");
  singleQuotes->setChecked(s.singleQuotes);
  operators = new QLineEdit(s.operators);
  operators->setPlaceholderText("e.g. + - * / = < >");
  form->addRow("Name", name);
  form->addRow("File extensions", extensions);
  form->addRow(ignoreCase);
  form->addRow("Line comment", lineComment);
  auto *block = new QHBoxLayout;
  block->addWidget(blockOpen);
  block->addWidget(new QLabel("…"));
  block->addWidget(blockClose);
  form->addRow("Block comment", block);
  auto *strings = new QHBoxLayout;
  strings->addWidget(doubleQuotes);
  strings->addWidget(singleQuotes);
  form->addRow("Strings", strings);
  form->addRow("Operators", operators);
  for (int i = 0; i < UdlSpec::keywordGroups; ++i) {
    keywords[i] = new QLineEdit(s.keywords[i]);
    keywords[i]->setPlaceholderText("words separated by spaces");
    keywordColor[i] = colorButton(i, s.keywordColor[i]);
    bold[i] = new QCheckBox("Bold");
    bold[i]->setChecked(s.keywordBold[i]);
    auto *row = new QHBoxLayout;
    row->addWidget(keywords[i], 1);
    row->addWidget(keywordColor[i]);
    row->addWidget(bold[i]);
    form->addRow(QString("Keywords %1").arg(i + 1), row);
  }
  const int base = UdlSpec::keywordGroups;
  commentColor = colorButton(base, s.commentColor);
  numberColor = colorButton(base + 1, s.numberColor);
  stringColor = colorButton(base + 2, s.stringColor);
  operatorColor = colorButton(base + 3, s.operatorColor);
  auto *colorRow = new QHBoxLayout;
  const QList<QPair<QString, QPushButton *>> swatches{{"Comments", commentColor},
                                                      {"Numbers", numberColor},
                                                      {"Strings", stringColor},
                                                      {"Operators", operatorColor}};
  for (const auto &swatch : swatches) {
    colorRow->addWidget(new QLabel(swatch.first));
    colorRow->addWidget(swatch.second);
  }
  colorRow->addStretch();
  form->addRow("Colours", colorRow);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save |
                                       QDialogButtonBox::Cancel);
  if (existing) {
    auto *del = buttons->addButton("Delete", QDialogButtonBox::DestructiveRole);
    connect(del, &QPushButton::clicked, this, [this] {
      if (QMessageBox::question(this, "Delete language",
                                "Delete this custom language definition?") ==
          QMessageBox::Yes) {
        removed = true;
        accept();
      }
    });
  }
  form->addRow(buttons);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}
UdlSpec UdlEditor::spec() const {
  UdlSpec s;
  s.name = name->text().trimmed();
  s.extensions = extensions->text();
  s.ignoreCase = ignoreCase->isChecked();
  s.lineComment = lineComment->text().trimmed();
  s.blockOpen = blockOpen->text().trimmed();
  s.blockClose = blockClose->text().trimmed();
  s.doubleQuotes = doubleQuotes->isChecked();
  s.singleQuotes = singleQuotes->isChecked();
  s.operators = operators->text();
  for (int i = 0; i < UdlSpec::keywordGroups; ++i) {
    s.keywords[i] = keywords[i]->text();
    s.keywordBold[i] = bold[i]->isChecked();
    s.keywordColor[i] = colors[i];
  }
  const int base = UdlSpec::keywordGroups;
  s.commentColor = colors[base];
  s.numberColor = colors[base + 1];
  s.stringColor = colors[base + 2];
  s.operatorColor = colors[base + 3];
  return s;
}
