// SPDX-License-Identifier: GPL-2.0-or-later
//
// A plain-text editor with line numbers and basic syntax colouring. No
// language server, no parsing beyond one line at a time.
#pragma once

#include <QPlainTextEdit>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVector>

// Colours keywords, strings, comments and numbers for a handful of common
// file types, chosen from the file name. Unknown types get no colouring.
class SyntaxHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    enum class Lang { None, CLike, Python, Shell, HashConfig, Json, Markdown, Markup };

    SyntaxHighlighter(QTextDocument *doc, Lang lang);
    static Lang langForFile(const QString &path, const QString &firstLine);
    static QString langName(Lang l);
    Lang lang() const { return m_lang; }

protected:
    void highlightBlock(const QString &text) override;

private:
    void highlightCode(const QString &text);
    void highlightMarkdown(const QString &text);
    void highlightMarkup(const QString &text);

    Lang m_lang;
    QStringList m_keywords;
    QString m_lineComment;
    QVector<QPair<QString, QString>> m_blocks;   // multi-line start/end delimiters
    QString m_quotes;
    QTextCharFormat m_kw, m_str, m_com, m_num, m_head, m_code;
};

class CodeEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit CodeEditor(QWidget *parent = nullptr);

    void setLineNumbersVisible(bool on);
    bool lineNumbersVisible() const { return m_lineNumbers; }
    int lineNumberWidth() const;
    void paintLineNumbers(QPaintEvent *e);

protected:
    void resizeEvent(QResizeEvent *e) override;

private:
    void updateMargins();
    void highlightCurrentLine();

    QWidget *m_area = nullptr;
    bool m_lineNumbers = true;
};
