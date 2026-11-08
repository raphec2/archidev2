// SPDX-License-Identifier: GPL-2.0-or-later
#include "CodeEditor.h"

#include "Theme.h"

#include <QFileInfo>
#include <QPaintEvent>
#include <QPainter>
#include <QTextBlock>

// --------------------------------------------------------------------------
// Syntax colouring
// --------------------------------------------------------------------------

SyntaxHighlighter::Lang SyntaxHighlighter::langForFile(const QString &path, const QString &firstLine) {
    const QFileInfo fi(path);
    const QString name = fi.fileName().toLower();
    const QString ext = fi.suffix().toLower();
    static const QStringList clike{"c", "h", "cc", "cpp", "cxx", "hh", "hpp", "hxx", "java", "js", "jsx",
                                   "mjs", "cjs", "ts", "tsx", "rs", "go", "cs", "swift", "kt", "kts",
                                   "qml", "css", "scss", "dart", "php"};
    static const QStringList hashConf{"toml", "ini", "conf", "cfg", "yaml", "yml", "cmake", "desktop",
                                      "properties", "env", "gitignore", "dockerignore"};
    if (clike.contains(ext)) return Lang::CLike;
    if (ext == QLatin1String("py") || ext == QLatin1String("pyw")) return Lang::Python;
    if (ext == QLatin1String("sh") || ext == QLatin1String("bash") || ext == QLatin1String("zsh"))
        return Lang::Shell;
    if (ext == QLatin1String("json")) return Lang::Json;
    if (ext == QLatin1String("md") || ext == QLatin1String("markdown")) return Lang::Markdown;
    if (QStringList{"html", "htm", "xml", "svg", "ui", "qrc", "xsd"}.contains(ext)) return Lang::Markup;
    if (hashConf.contains(ext) || name == QLatin1String("cmakelists.txt") || name == QLatin1String("makefile")
        || name == QLatin1String("dockerfile") || name.startsWith(QLatin1String(".archidev2")))
        return Lang::HashConfig;
    if (firstLine.startsWith(QLatin1String("#!"))) {
        if (firstLine.contains(QLatin1String("python"))) return Lang::Python;
        if (firstLine.contains(QLatin1String("sh"))) return Lang::Shell;
    }
    return Lang::None;
}

QString SyntaxHighlighter::langName(Lang l) {
    switch (l) {
    case Lang::CLike:      return QStringLiteral("C-like");
    case Lang::Python:     return QStringLiteral("Python");
    case Lang::Shell:      return QStringLiteral("Shell");
    case Lang::HashConfig: return QStringLiteral("Config");
    case Lang::Json:       return QStringLiteral("JSON");
    case Lang::Markdown:   return QStringLiteral("Markdown");
    case Lang::Markup:     return QStringLiteral("Markup");
    default:               return QStringLiteral("Plain text");
    }
}

SyntaxHighlighter::SyntaxHighlighter(QTextDocument *doc, Lang lang)
    : QSyntaxHighlighter(doc), m_lang(lang) {
    m_kw.setForeground(QColor(QLatin1String(Theme::BlueActive)));
    m_str.setForeground(QColor(0x9c, 0xcc, 0x8a));
    m_com.setForeground(QColor(0x6f, 0x7b, 0x8c));
    m_com.setFontItalic(true);
    m_num.setForeground(QColor(QLatin1String(Theme::Amber)));
    m_head.setForeground(QColor(QLatin1String(Theme::BlueActive)));
    m_head.setFontWeight(QFont::Bold);
    m_code.setForeground(QColor(QLatin1String(Theme::Amber)));

    switch (lang) {
    case Lang::CLike:
        m_keywords = QStringList{
            "auto", "break", "case", "catch", "class", "const", "constexpr", "continue", "default",
            "delete", "do", "else", "enum", "explicit", "export", "extends", "extern", "final", "fn",
            "for", "func", "function", "if", "impl", "implements", "import", "include", "inline",
            "interface", "let", "match", "mod", "mut", "namespace", "new", "override", "package",
            "private", "protected", "pub", "public", "return", "static", "struct", "super", "switch",
            "template", "this", "throw", "trait", "try", "type", "typedef", "typename", "union",
            "use", "using", "var", "virtual", "void", "volatile", "while", "async", "await", "yield",
            "int", "bool", "char", "double", "float", "long", "short", "unsigned", "signed", "string",
            "define", "ifdef", "ifndef", "endif", "pragma", "true", "false", "null", "nullptr",
            "undefined", "self", "Self", "None", "Some"};
        m_lineComment = QStringLiteral("//");
        m_blocks = {{QStringLiteral("/*"), QStringLiteral("*/")}};
        m_quotes = QStringLiteral("\"'`");
        break;
    case Lang::Python:
        m_keywords = QStringList{
            "and", "as", "assert", "async", "await", "break", "class", "continue", "def", "del",
            "elif", "else", "except", "finally", "for", "from", "global", "if", "import", "in", "is",
            "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try", "while", "with",
            "yield", "True", "False", "None", "self"};
        m_lineComment = QStringLiteral("#");
        m_blocks = {{QStringLiteral("\"\"\""), QStringLiteral("\"\"\"")},
                    {QStringLiteral("'''"), QStringLiteral("'''")}};
        m_quotes = QStringLiteral("\"'");
        break;
    case Lang::Shell:
        m_keywords = QStringList{
            "if", "then", "else", "elif", "fi", "for", "while", "until", "do", "done", "case", "esac",
            "in", "function", "return", "local", "export", "readonly", "set", "unset", "shift", "exit",
            "echo", "printf", "read", "source", "true", "false", "cd", "exec", "trap"};
        m_lineComment = QStringLiteral("#");
        m_quotes = QStringLiteral("\"'");
        break;
    case Lang::HashConfig:
        m_keywords = QStringList{"true", "false", "yes", "no", "on", "off", "null"};
        m_lineComment = QStringLiteral("#");
        m_quotes = QStringLiteral("\"'");
        break;
    case Lang::Json:
        m_keywords = QStringList{"true", "false", "null"};
        m_quotes = QStringLiteral("\"");
        break;
    default:
        break;
    }
}

void SyntaxHighlighter::highlightBlock(const QString &text) {
    switch (m_lang) {
    case Lang::None:     return;
    case Lang::Markdown: highlightMarkdown(text); return;
    case Lang::Markup:   highlightMarkup(text); return;
    default:             highlightCode(text); return;
    }
}

// One left-to-right pass, so a "//" inside a string or a quote inside a
// comment is never mistaken for the other. Block state = 1 + index of the
// open multi-line delimiter pair.
void SyntaxHighlighter::highlightCode(const QString &text) {
    const int n = text.size();
    int i = 0;
    setCurrentBlockState(0);
    const int prev = previousBlockState();
    if (prev > 0 && prev <= m_blocks.size()) {
        const QString &end = m_blocks.at(prev - 1).second;
        const QTextCharFormat &f = m_lang == Lang::Python ? m_str : m_com;
        const int e = text.indexOf(end);
        if (e < 0) { setFormat(0, n, f); setCurrentBlockState(prev); return; }
        setFormat(0, e + end.size(), f);
        i = e + end.size();
    }
    while (i < n) {
        bool handled = false;
        for (int b = 0; b < m_blocks.size(); ++b) {
            const auto &pair = m_blocks.at(b);
            if (!text.midRef(i).startsWith(pair.first)) continue;
            const int e = text.indexOf(pair.second, i + pair.first.size());
            // Python triple quotes are strings; C block comments are comments.
            const QTextCharFormat &f = m_lang == Lang::Python ? m_str : m_com;
            if (e < 0) { setFormat(i, n - i, f); setCurrentBlockState(b + 1); return; }
            setFormat(i, e + pair.second.size() - i, f);
            i = e + pair.second.size();
            handled = true;
            break;
        }
        if (handled) continue;
        const QChar c = text.at(i);
        if (!m_lineComment.isEmpty() && text.midRef(i).startsWith(m_lineComment)
            && (m_lineComment != QLatin1String("#") || i == 0 || text.at(i - 1).isSpace())) {
            setFormat(i, n - i, m_com);
            return;
        }
        if (m_quotes.contains(c)) {
            int j = i + 1;
            while (j < n && text.at(j) != c) j += (text.at(j) == QLatin1Char('\\')) ? 2 : 1;
            j = qMin(j + 1, n);
            setFormat(i, j - i, m_str);
            i = j;
            continue;
        }
        if (c.isDigit() && (i == 0 || !(text.at(i - 1).isLetterOrNumber() || text.at(i - 1) == QLatin1Char('_')))) {
            int j = i + 1;
            while (j < n && (text.at(j).isLetterOrNumber() || text.at(j) == QLatin1Char('.')
                             || text.at(j) == QLatin1Char('_'))) ++j;
            setFormat(i, j - i, m_num);
            i = j;
            continue;
        }
        if (c.isLetter() || c == QLatin1Char('_')) {
            int j = i + 1;
            while (j < n && (text.at(j).isLetterOrNumber() || text.at(j) == QLatin1Char('_'))) ++j;
            if (m_keywords.contains(text.mid(i, j - i))) setFormat(i, j - i, m_kw);
            i = j;
            continue;
        }
        ++i;
    }
}

void SyntaxHighlighter::highlightMarkdown(const QString &text) {
    const bool fence = text.trimmed().startsWith(QLatin1String("```"));
    const bool inCode = previousBlockState() == 1;
    if (fence) {
        setFormat(0, text.size(), m_code);
        setCurrentBlockState(inCode ? 0 : 1);
        return;
    }
    setCurrentBlockState(inCode ? 1 : 0);
    if (inCode) { setFormat(0, text.size(), m_code); return; }
    if (text.startsWith(QLatin1Char('#'))) { setFormat(0, text.size(), m_head); return; }
    int i = 0;
    while ((i = text.indexOf(QLatin1Char('`'), i)) >= 0) {
        const int e = text.indexOf(QLatin1Char('`'), i + 1);
        if (e < 0) break;
        setFormat(i, e - i + 1, m_code);
        i = e + 1;
    }
}

void SyntaxHighlighter::highlightMarkup(const QString &text) {
    const int n = text.size();
    int i = 0;
    setCurrentBlockState(0);
    if (previousBlockState() == 1) {
        const int e = text.indexOf(QLatin1String("-->"));
        if (e < 0) { setFormat(0, n, m_com); setCurrentBlockState(1); return; }
        setFormat(0, e + 3, m_com);
        i = e + 3;
    }
    while (i < n) {
        if (text.midRef(i).startsWith(QLatin1String("<!--"))) {
            const int e = text.indexOf(QLatin1String("-->"), i + 4);
            if (e < 0) { setFormat(i, n - i, m_com); setCurrentBlockState(1); return; }
            setFormat(i, e + 3 - i, m_com);
            i = e + 3;
        } else if (text.at(i) == QLatin1Char('<')) {
            int e = text.indexOf(QLatin1Char('>'), i);
            if (e < 0) e = n - 1;
            setFormat(i, e - i + 1, m_kw);
            for (int q = i; q <= e; ++q) {          // attribute values inside the tag
                if (text.at(q) != QLatin1Char('"')) continue;
                const int qe = text.indexOf(QLatin1Char('"'), q + 1);
                if (qe < 0 || qe > e) break;
                setFormat(q, qe - q + 1, m_str);
                q = qe;
            }
            i = e + 1;
        } else {
            ++i;
        }
    }
}

// --------------------------------------------------------------------------
// Editor with line numbers (the gutter follows Qt's Code Editor example)
// --------------------------------------------------------------------------

namespace {
class LineNumberArea : public QWidget {
public:
    explicit LineNumberArea(CodeEditor *e) : QWidget(e), m_editor(e) {}
    QSize sizeHint() const override { return QSize(m_editor->lineNumberWidth(), 0); }
protected:
    void paintEvent(QPaintEvent *e) override { m_editor->paintLineNumbers(e); }
private:
    CodeEditor *m_editor;
};
}

CodeEditor::CodeEditor(QWidget *parent) : QPlainTextEdit(parent) {
    m_area = new LineNumberArea(this);
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] { updateMargins(); });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect &r, int dy) {
        if (dy) m_area->scroll(0, dy);
        else m_area->update(0, r.y(), m_area->width(), r.height());
        if (r.contains(viewport()->rect())) updateMargins();
    });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] { highlightCurrentLine(); });
    updateMargins();
    highlightCurrentLine();
}

void CodeEditor::setLineNumbersVisible(bool on) {
    m_lineNumbers = on;
    m_area->setVisible(on);
    updateMargins();
    highlightCurrentLine();
}

int CodeEditor::lineNumberWidth() const {
    if (!m_lineNumbers) return 0;
    int digits = 1;
    for (int max = qMax(1, blockCount()); max >= 10; max /= 10) ++digits;
    return 12 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * qMax(3, digits);
}

void CodeEditor::updateMargins() {
    setViewportMargins(lineNumberWidth(), 0, 0, 0);
    const QRect cr = contentsRect();
    m_area->setGeometry(QRect(cr.left(), cr.top(), lineNumberWidth(), cr.height()));
}

void CodeEditor::resizeEvent(QResizeEvent *e) {
    QPlainTextEdit::resizeEvent(e);
    updateMargins();
}

void CodeEditor::highlightCurrentLine() {
    QList<QTextEdit::ExtraSelection> extras;
    if (m_lineNumbers) {           // code tabs only; Notes stays plain
        QTextEdit::ExtraSelection sel;
        sel.format.setBackground(QColor(QLatin1String(Theme::Raised)));
        sel.format.setProperty(QTextFormat::FullWidthSelection, true);
        sel.cursor = textCursor();
        sel.cursor.clearSelection();
        extras << sel;
    }
    setExtraSelections(extras);
}

void CodeEditor::paintLineNumbers(QPaintEvent *e) {
    QPainter p(m_area);
    p.fillRect(e->rect(), QColor(QLatin1String(Theme::Window)));
    QTextBlock block = firstVisibleBlock();
    int number = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    const int current = textCursor().blockNumber();
    while (block.isValid() && top <= e->rect().bottom()) {
        if (block.isVisible() && bottom >= e->rect().top()) {
            p.setPen(QColor(QLatin1String(number == current ? Theme::Text : Theme::TextDim)));
            p.drawText(0, top, m_area->width() - 6, fontMetrics().height(), Qt::AlignRight,
                       QString::number(number + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++number;
    }
}
