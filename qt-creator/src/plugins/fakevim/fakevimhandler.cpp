/****************************************************************************
**
** Copyright (C) 2012 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
****************************************************************************/

//
// ATTENTION:
//
// 1 Please do not add any direct dependencies to other Qt Creator code here.
//   Instead emit signals and let the FakeVimPlugin channel the information to
//   Qt Creator. The idea is to keep this file here in a "clean" state that
//   allows easy reuse with any QTextEdit or QPlainTextEdit derived class.
//
// 2 There are a few auto tests located in ../../../tests/auto/fakevim.
//   Commands that are covered there are marked as "// tested" below.
//
// 3 Some conventions:
//
//   Use 1 based line numbers and 0 based column numbers. Even though
//   the 1 based line are not nice it matches vim's and QTextEdit's 'line'
//   concepts.
//
//   Do not pass QTextCursor etc around unless really needed. Convert
//   early to  line/column.
//
//   A QTextCursor is always between characters, whereas vi's cursor is always
//   over a character. FakeVim interprets the QTextCursor to be over the character
//   to the right of the QTextCursor's position().
//
//   A current "region of interest"
//   spans between anchor(), (i.e. the character below anchor()), and
//   position(). The character below position() is not included
//   if the last movement command was exclusive (MoveExclusive).
//

#include "fakevimhandler.h"

#include <utils/hostosinfo.h>
#include <utils/qtcassert.h>

#include <QDebug>
#include <QFile>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QRegExp>
#include <QTextStream>
#include <QTimer>
#include <QtAlgorithms>
#include <QStack>

#include <QApplication>
#include <QClipboard>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocumentFragment>
#include <QTextEdit>
#include <QMimeData>

#include <algorithm>
#include <climits>
#include <ctype.h>

//#define DEBUG_KEY  1
#if DEBUG_KEY
#   define KEY_DEBUG(s) qDebug() << s
#else
#   define KEY_DEBUG(s)
#endif

//#define DEBUG_UNDO  1
#if DEBUG_UNDO
#   define UNDO_DEBUG(s) qDebug() << << revision() << s
#else
#   define UNDO_DEBUG(s)
#endif

using namespace Utils;

namespace FakeVim {
namespace Internal {

///////////////////////////////////////////////////////////////////////
//
// FakeVimHandler
//
///////////////////////////////////////////////////////////////////////

#define StartOfLine     QTextCursor::StartOfLine
#define EndOfLine       QTextCursor::EndOfLine
#define MoveAnchor      QTextCursor::MoveAnchor
#define KeepAnchor      QTextCursor::KeepAnchor
#define Up              QTextCursor::Up
#define Down            QTextCursor::Down
#define Right           QTextCursor::Right
#define Left            QTextCursor::Left
#define EndOfDocument   QTextCursor::End
#define StartOfDocument QTextCursor::Start

#define ParagraphSeparator QChar::ParagraphSeparator

#define EDITOR(s) (m_textedit ? m_textedit->s : m_plaintextedit->s)

#define MetaModifier     // Use HostOsInfo::controlModifier() instead
#define ControlModifier  // Use HostOsInfo::controlModifier() instead

typedef QLatin1String _;

/* Clipboard MIME types used by Vim. */
static const QString vimMimeText = "_VIM_TEXT";
static const QString vimMimeTextEncoded = "_VIMENC_TEXT";

using namespace Qt;

/*! A \e Mode represents one of the basic modes of operation of FakeVim.
*/

enum Mode
{
    InsertMode,
    ReplaceMode,
    CommandMode,
    ExMode
};

/*! A \e SubMode is used for things that require one more data item
    and are 'nested' behind a \l Mode.
*/
enum SubMode
{
    NoSubMode,
    ChangeSubMode,       // Used for c
    DeleteSubMode,       // Used for d
    FilterSubMode,       // Used for !
    IndentSubMode,       // Used for =
    RegisterSubMode,     // Used for "
    ShiftLeftSubMode,    // Used for <
    ShiftRightSubMode,   // Used for >
    InvertCaseSubMode,   // Used for g~
    DownCaseSubMode,     // Used for gu
    UpCaseSubMode,       // Used for gU
    WindowSubMode,       // Used for Ctrl-w
    YankSubMode,         // Used for y
    ZSubMode,            // Used for z
    CapitalZSubMode,     // Used for Z
    ReplaceSubMode,      // Used for r
    OpenSquareSubMode,   // Used for [
    CloseSquareSubMode   // Used for ]
};

/*! A \e SubSubMode is used for things that require one more data item
    and are 'nested' behind a \l SubMode.
*/
enum SubSubMode
{
    NoSubSubMode,
    FtSubSubMode,         // Used for f, F, t, T.
    MarkSubSubMode,       // Used for m.
    BackTickSubSubMode,   // Used for `.
    TickSubSubMode,       // Used for '.
    TextObjectSubSubMode, // Used for thing like iw, aW, as etc.
    SearchSubSubMode
};

enum VisualMode
{
    NoVisualMode,
    VisualCharMode,
    VisualLineMode,
    VisualBlockMode
};

enum MoveType
{
    MoveExclusive,
    MoveInclusive,
    MoveLineWise
};

/*!
    \enum RangeMode

    The \e RangeMode serves as a means to define how the "Range" between
    the \l cursor and the \l anchor position is to be interpreted.

    \value RangeCharMode   Entered by pressing \key v. The range includes
                           all characters between cursor and anchor.
    \value RangeLineMode   Entered by pressing \key V. The range includes
                           all lines between the line of the cursor and
                           the line of the anchor.
    \value RangeLineModeExclusice Like \l RangeLineMode, but keeps one
                           newline when deleting.
    \value RangeBlockMode  Entered by pressing \key Ctrl-v. The range includes
                           all characters with line and column coordinates
                           between line and columns coordinates of cursor and
                           anchor.
    \value RangeBlockAndTailMode Like \l RangeBlockMode, but also includes
                           all characters in the affected lines up to the end
                           of these lines.
*/

enum EventResult
{
    EventHandled,
    EventUnhandled,
    EventCancelled, // Event is handled but a sub mode was cancelled.
    EventPassedToCore
};

struct CursorPosition
{
    CursorPosition() : line(-1), column(-1) {}
    CursorPosition(int block, int column) : line(block), column(column) {}
    explicit CursorPosition(const QTextCursor &tc)
        : line(tc.block().blockNumber()), column(tc.positionInBlock()) {}
    CursorPosition(const QTextDocument *document, int position)
    {
        QTextBlock block = document->findBlock(position);
        line = block.blockNumber();
        column = position - block.position();
    }
    bool isValid() const { return line >= 0 && column >= 0; }
    bool operator==(const CursorPosition &other) const
        { return line == other.line && column == other.column; }
    bool operator!=(const CursorPosition &other) const { return !operator==(other); }

    int line; // Line in document (from 0, folded lines included).
    int column; // Position on line.
};

struct Mark
{
    Mark(const CursorPosition &pos = CursorPosition(), const QString &fileName = QString())
        : position(pos), fileName(fileName) {}

    bool isValid() const { return position.isValid(); }

    bool isLocal(const QString &localFileName) const
    {
        return fileName.isEmpty() || fileName == localFileName;
    }

    CursorPosition position;
    QString fileName;
};
typedef QHash<QChar, Mark> Marks;
typedef QHashIterator<QChar, Mark> MarksIterator;

struct State
{
    State() : revision(-1), position(), marks(), lastVisualMode(NoVisualMode) {}
    State(int revision, const CursorPosition &position, const Marks &marks,
        VisualMode lastVisualMode) : revision(revision), position(position), marks(marks),
        lastVisualMode(lastVisualMode) {}

    int revision;
    CursorPosition position;
    Marks marks;
    VisualMode lastVisualMode;
};

struct Column
{
    Column(int p, int l) : physical(p), logical(l) {}
    int physical; // Number of characters in the data.
    int logical; // Column on screen.
};

QDebug operator<<(QDebug ts, const Column &col)
{
    return ts << "(p: " << col.physical << ", l: " << col.logical << ")";
}

struct Register
{
    Register() : rangemode(RangeCharMode) {}
    Register(const QString &c) : contents(c), rangemode(RangeCharMode) {}
    Register(const QString &c, RangeMode m) : contents(c), rangemode(m) {}
    QString contents;
    RangeMode rangemode;
};

QDebug operator<<(QDebug ts, const Register &reg)
{
    return ts << reg.contents;
}

struct SearchData
{
    SearchData()
    {
        forward = true;
        highlightMatches = true;
    }

    QString needle;
    bool forward;
    bool highlightMatches;
};

// If string begins with given prefix remove it with trailing spaces and return true.
static bool eatString(const QString &prefix, QString *str)
{
    if (!str->startsWith(prefix))
        return false;
    *str = str->mid(prefix.size()).trimmed();
    return true;
}

static QRegExp vimPatternToQtPattern(QString needle, bool smartcase)
{
    /* Transformations (Vim regexp -> QRegExp):
     *   \a -> [A-Za-z]
     *   \A -> [^A-Za-z]
     *   \h -> [A-Za-z_]
     *   \H -> [^A-Za-z_]
     *   \l -> [a-z]
     *   \L -> [^a-z]
     *   \o -> [0-7]
     *   \O -> [^0-7]
     *   \u -> [A-Z]
     *   \U -> [^A-Z]
     *   \x -> [0-9A-Fa-f]
     *   \X -> [^0-9A-Fa-f]
     *
     *   \< -> \b
     *   \> -> \b
     *   [] -> \[\]
     *   \= -> ?
     *
     *   (...)  <-> \(...\)
     *   {...}  <-> \{...\}
     *   |      <-> \|
     *   ?      <-> \?
     *   +      <-> \+
     *   \{...} -> {...}
     *
     *   \c - set ignorecase for rest
     *   \C - set noignorecase for rest
     */
    bool ignorecase = smartcase && !needle.contains(QRegExp("[A-Z]"));
    QString pattern;
    pattern.reserve(2 * needle.size());

    bool escape = false;
    bool brace = false;
    bool embraced = false;
    bool range = false;
    bool curly = false;
    foreach (const QChar &c, needle) {
        if (brace) {
            brace = false;
            if (c == ']') {
                pattern.append(_("\\[\\]"));
                continue;
            }
            pattern.append('[');
            escape = true;
            embraced = true;
        }
        if (embraced) {
            if (range) {
                QChar c2 = pattern[pattern.size() - 2];
                pattern.remove(pattern.size() - 2, 2);
                pattern.append(c2.toUpper() + '-' + c.toUpper());
                pattern.append(c2.toLower() + '-' + c.toLower());
                range = false;
            } else if (escape) {
                escape = false;
                pattern.append(c);
            } else if (c == '\\') {
                escape = true;
            } else if (c == ']') {
                pattern.append(']');
                embraced = false;
            } else if (c == '-') {
                range = ignorecase && pattern[pattern.size() - 1].isLetter();
                pattern.append('-');
            } else if (c.isLetter() && ignorecase) {
                pattern.append(c.toLower()).append(c.toUpper());
            } else {
                pattern.append(c);
            }
        } else if (QString("(){}+|?").indexOf(c) != -1) {
            if (c == '{') {
                curly = escape;
            } else if (c == '}' && curly) {
                curly = false;
                escape = true;
            }

            if (escape)
                escape = false;
            else
                pattern.append('\\');
            pattern.append(c);
        } else if (escape) {
            // escape expression
            escape = false;
            if (c == '<' || c == '>')
                pattern.append(_("\\b"));
            else if (c == 'a')
                pattern.append(_("[a-zA-Z]"));
            else if (c == 'A')
                pattern.append(_("[^a-zA-Z]"));
            else if (c == 'h')
                pattern.append(_("[A-Za-z_]"));
            else if (c == 'H')
                pattern.append(_("[^A-Za-z_]"));
            else if (c == 'c' || c == 'C')
                ignorecase = (c == 'c');
            else if (c == 'l')
                pattern.append(_("[a-z]"));
            else if (c == 'L')
                pattern.append(_("[^a-z]"));
            else if (c == 'o')
                pattern.append(_("[0-7]"));
            else if (c == 'O')
                pattern.append(_("[^0-7]"));
            else if (c == 'u')
                pattern.append(_("[A-Z]"));
            else if (c == 'U')
                pattern.append(_("[^A-Z]"));
            else if (c == 'x')
                pattern.append(_("[0-9A-Fa-f]"));
            else if (c == 'X')
                pattern.append(_("[^0-9A-Fa-f]"));
            else if (c == '=')
                pattern.append(_("?"));
            else
                pattern.append('\\' + c);
        } else {
            // unescaped expression
            if (c == '\\')
                escape = true;
            else if (c == '[')
                brace = true;
            else if (c.isLetter() && ignorecase)
                pattern.append('[' + c.toLower() + c.toUpper() + ']');
            else
                pattern.append(c);
        }
    }
    if (escape)
        pattern.append('\\');
    else if (brace)
        pattern.append('[');

    return QRegExp(pattern);
}

static bool afterEndOfLine(const QTextDocument *doc, int position)
{
    return doc->characterAt(position) == ParagraphSeparator
        && doc->findBlock(position).length() > 1;
}

static void searchForward(QTextCursor *tc, QRegExp &needleExp, int *repeat)
{
    const QTextDocument *doc = tc->document();
    const int startPos = tc->position();

    // Search from beginning of line so that matched text is the same.
    tc->movePosition(StartOfLine);

    // forward to current position
    *tc = doc->find(needleExp, *tc);
    while (!tc->isNull() && tc->anchor() < startPos) {
        if (!tc->hasSelection())
            tc->movePosition(Right);
        if (tc->atBlockEnd())
            tc->movePosition(Right);
        *tc = doc->find(needleExp, *tc);
    }

    if (tc->isNull())
        return;

    --*repeat;

    while (*repeat > 0) {
        if (!tc->hasSelection())
            tc->movePosition(Right);
        if (tc->atBlockEnd())
            tc->movePosition(Right);
        *tc = doc->find(needleExp, *tc);
        if (tc->isNull())
            return;
        --*repeat;
    }

    if (!tc->isNull() && afterEndOfLine(doc, tc->anchor()))
        tc->movePosition(Left);
}

static void searchBackward(QTextCursor *tc, QRegExp &needleExp, int *repeat)
{
    // Search from beginning of line so that matched text is the same.
    QTextBlock block = tc->block();
    QString line = block.text();

    int i = line.indexOf(needleExp, 0);
    while (i != -1 && i < tc->positionInBlock()) {
        --*repeat;
        i = line.indexOf(needleExp, i + qMax(1, needleExp.matchedLength()));
        if (i == line.size())
            i = -1;
    }

    if (i == tc->positionInBlock())
        --*repeat;

    while (*repeat > 0) {
        block = block.previous();
        if (!block.isValid())
            break;
        line = block.text();
        i = line.indexOf(needleExp, 0);
        while (i != -1) {
            --*repeat;
            i = line.indexOf(needleExp, i + qMax(1, needleExp.matchedLength()));
            if (i == line.size())
                i = -1;
        }
    }

    if (!block.isValid()) {
        *tc = QTextCursor();
        return;
    }

    i = line.indexOf(needleExp, 0);
    while (*repeat < 0) {
        i = line.indexOf(needleExp, i + qMax(1, needleExp.matchedLength()));
        ++*repeat;
    }
    tc->setPosition(block.position() + i);
}

static bool substituteText(QString *text, QRegExp &pattern, const QString &replacement,
    bool global)
{
    bool substituted = false;
    int pos = 0;
    while (true) {
        pos = pattern.indexIn(*text, pos, QRegExp::CaretAtZero);
        if (pos == -1)
            break;
        substituted = true;
        QString matched = text->mid(pos, pattern.cap(0).size());
        QString repl;
        bool escape = false;
        // insert captured texts
        for (int i = 0; i < replacement.size(); ++i) {
            const QChar &c = replacement[i];
            if (escape) {
                escape = false;
                if (c.isDigit()) {
                    if (c.digitValue() <= pattern.captureCount())
                        repl += pattern.cap(c.digitValue());
                } else {
                    repl += c;
                }
            } else {
                if (c == '\\')
                    escape = true;
                else if (c == '&')
                    repl += pattern.cap(0);
                else
                    repl += c;
            }
        }
        text->replace(pos, matched.size(), repl);
        pos += qMax(1, repl.size());

        if (pos >= text->size() || !global)
            break;
    }

    return substituted;
}

static int findUnescaped(QChar c, const QString &line, int from)
{
    for (int i = from; i < line.size(); ++i) {
        if (line.at(i) == c && (i == 0 || line.at(i - 1) != '\\'))
            return i;
    }
    return -1;
}

static void setClipboardData(const QString &content, RangeMode mode,
    QClipboard::Mode clipboardMode)
{
    QClipboard *clipboard = QApplication::clipboard();
    char vimRangeMode = mode;

    QByteArray bytes1;
    bytes1.append(vimRangeMode);
    bytes1.append(content.toUtf8());

    QByteArray bytes2;
    bytes2.append(vimRangeMode);
    bytes2.append("utf-8");
    bytes2.append('\0');
    bytes2.append(content.toUtf8());

    QMimeData *data = new QMimeData;
    data->setText(content);
    data->setData(vimMimeText, bytes1);
    data->setData(vimMimeTextEncoded, bytes2);
    clipboard->setMimeData(data, clipboardMode);
}

static const QMap<QString, int> &vimKeyNames()
{
    static QMap<QString, int> k;
    if (!k.isEmpty())
        return k;

    // FIXME: Should be value of mapleader.
    k.insert("LEADER", Key_Backslash);

    k.insert("SPACE", Key_Space);
    k.insert("TAB", Key_Tab);
    k.insert("NL", Key_Return);
    k.insert("NEWLINE", Key_Return);
    k.insert("LINEFEED", Key_Return);
    k.insert("LF", Key_Return);
    k.insert("CR", Key_Return);
    k.insert("RETURN", Key_Return);
    k.insert("ENTER", Key_Return);
    k.insert("BS", Key_Backspace);
    k.insert("BACKSPACE", Key_Backspace);
    k.insert("ESC", Key_Escape);
    k.insert("BAR", Key_Bar);
    k.insert("BSLASH", Key_Backslash);
    k.insert("DEL", Key_Delete);
    k.insert("DELETE", Key_Delete);
    k.insert("KDEL", Key_Delete);
    k.insert("UP", Key_Up);
    k.insert("DOWN", Key_Down);
    k.insert("LEFT", Key_Left);
    k.insert("RIGHT", Key_Right);

    k.insert("F1", Key_F1);
    k.insert("F2", Key_F2);
    k.insert("F3", Key_F3);
    k.insert("F4", Key_F4);
    k.insert("F5", Key_F5);
    k.insert("F6", Key_F6);
    k.insert("F7", Key_F7);
    k.insert("F8", Key_F8);
    k.insert("F9", Key_F9);
    k.insert("F10", Key_F10);

    k.insert("F11", Key_F11);
    k.insert("F12", Key_F12);
    k.insert("F13", Key_F13);
    k.insert("F14", Key_F14);
    k.insert("F15", Key_F15);
    k.insert("F16", Key_F16);
    k.insert("F17", Key_F17);
    k.insert("F18", Key_F18);
    k.insert("F19", Key_F19);
    k.insert("F20", Key_F20);

    k.insert("F21", Key_F21);
    k.insert("F22", Key_F22);
    k.insert("F23", Key_F23);
    k.insert("F24", Key_F24);
    k.insert("F25", Key_F25);
    k.insert("F26", Key_F26);
    k.insert("F27", Key_F27);
    k.insert("F28", Key_F28);
    k.insert("F29", Key_F29);
    k.insert("F30", Key_F30);

    k.insert("F31", Key_F31);
    k.insert("F32", Key_F32);
    k.insert("F33", Key_F33);
    k.insert("F34", Key_F34);
    k.insert("F35", Key_F35);

    k.insert("INSERT", Key_Insert);
    k.insert("INS", Key_Insert);
    k.insert("KINSERT", Key_Insert);
    k.insert("HOME", Key_Home);
    k.insert("END", Key_End);
    k.insert("PAGEUP", Key_PageUp);
    k.insert("PAGEDOWN", Key_PageDown);

    k.insert("KPLUS", Key_Plus);
    k.insert("KMINUS", Key_Minus);
    k.insert("KDIVIDE", Key_Slash);
    k.insert("KMULTIPLY", Key_Asterisk);
    k.insert("KENTER", Key_Enter);
    k.insert("KPOINT", Key_Period);

    return k;
}


Range::Range()
    : beginPos(-1), endPos(-1), rangemode(RangeCharMode)
{}

Range::Range(int b, int e, RangeMode m)
    : beginPos(qMin(b, e)), endPos(qMax(b, e)), rangemode(m)
{}

QString Range::toString() const
{
    return QString("%1-%2 (mode: %3)").arg(beginPos).arg(endPos)
        .arg(rangemode);
}

QDebug operator<<(QDebug ts, const Range &range)
{
    return ts << '[' << range.beginPos << ',' << range.endPos << ']';
}


ExCommand::ExCommand(const QString &c, const QString &a, const Range &r)
    : cmd(c), hasBang(false), args(a), range(r), count(1)
{}

bool ExCommand::matches(const QString &min, const QString &full) const
{
    return cmd.startsWith(min) && full.startsWith(cmd);
}

QDebug operator<<(QDebug ts, const ExCommand &cmd)
{
    return ts << cmd.cmd << ' ' << cmd.args << ' ' << cmd.range;
}

QDebug operator<<(QDebug ts, const QList<QTextEdit::ExtraSelection> &sels)
{
    foreach (const QTextEdit::ExtraSelection &sel, sels)
        ts << "SEL: " << sel.cursor.anchor() << sel.cursor.position();
    return ts;
}

QString quoteUnprintable(const QString &ba)
{
    QString res;
    for (int i = 0, n = ba.size(); i != n; ++i) {
        const QChar c = ba.at(i);
        const int cc = c.unicode();
        if (c.isPrint())
            res += c;
        else if (cc == '\n')
            res += _("<CR>");
        else
            res += QString("\\x%1").arg(c.unicode(), 2, 16, QLatin1Char('0'));
    }
    return res;
}

static bool startsWithWhitespace(const QString &str, int col)
{
    QTC_ASSERT(str.size() >= col, return false);
    for (int i = 0; i < col; ++i) {
        uint u = str.at(i).unicode();
        if (u != ' ' && u != '\t')
            return false;
    }
    return true;
}

inline QString msgMarkNotSet(const QString &text)
{
    return FakeVimHandler::tr("Mark '%1' not set").arg(text);
}

class Input
{
public:
    // Remove some extra "information" on Mac.
    static int cleanModifier(int m)  { return m & ~Qt::KeypadModifier; }

    Input()
        : m_key(0), m_xkey(0), m_modifiers(0) {}

    explicit Input(QChar x)
        : m_key(x.unicode()), m_xkey(x.unicode()), m_modifiers(0), m_text(x)
    {
        if (x.isUpper())
            m_modifiers = Qt::ShiftModifier;
        else if (x.isLower())
            m_key = x.toUpper().unicode();
    }

    Input(int k, int m, const QString &t = QString())
        : m_key(k), m_modifiers(cleanModifier(m)), m_text(t)
    {
        // On Mac, QKeyEvent::text() returns non-empty strings for
        // cursor keys. This breaks some of the logic later on
        // relying on text() being empty for "special" keys.
        // FIXME: Check the real conditions.
        if (m_text.size() == 1 && m_text.at(0).unicode() < ' ')
            m_text.clear();

        // Set text only if input is ascii key without control modifier.
        if (m_text.isEmpty() && k <= 0x7f && (m & (HostOsInfo::controlModifier())) == 0) {
            QChar c = QChar::fromAscii(k);
            m_text = QString((m & ShiftModifier) != 0 ? c.toUpper() : c.toLower());
        }

        // m_xkey is only a cache.
        m_xkey = (m_text.size() == 1 ? m_text.at(0).unicode() : m_key);
    }

    bool isValid() const
    {
        return m_key != 0 || !m_text.isNull();
    }

    bool isDigit() const
    {
        return m_xkey >= '0' && m_xkey <= '9';
    }

    bool isKey(int c) const
    {
        return !m_modifiers && m_key == c;
    }

    bool isBackspace() const
    {
        return m_key == Key_Backspace || isControl('h');
    }

    bool isReturn() const
    {
        return m_key == '\n' || m_key == Key_Return || m_key == Key_Enter;
    }

    bool isEscape() const
    {
        return isKey(Key_Escape) || isKey(27) || isControl('c')
            || isControl(Key_BracketLeft);
    }

    bool is(int c) const
    {
        return m_xkey == c && m_modifiers != int(HostOsInfo::controlModifier());
    }

    bool isControl(int c) const
    {
        return m_modifiers == int(HostOsInfo::controlModifier())
            && (m_xkey == c || m_xkey + 32 == c || m_xkey + 64 == c || m_xkey + 96 == c);
    }

    bool isShift(int c) const
    {
        return m_modifiers == Qt::ShiftModifier && m_xkey == c;
    }

    bool operator<(const Input &a) const
    {
        if (m_key != a.m_key)
            return m_key < a.m_key;
        // Text for some mapped key cannot be determined (e.g. <C-J>) so if text is not set for
        // one of compared keys ignore it.
        if (!m_text.isEmpty() && !a.m_text.isEmpty())
            return m_text < a.m_text;
        return m_modifiers < a.m_modifiers;
    }

    bool operator==(const Input &a) const
    {
        return !(*this < a || a < *this);
    }

    bool operator!=(const Input &a) const { return !operator==(a); }

    QString text() const { return m_text; }

    QChar asChar() const
    {
        return (m_text.size() == 1 ? m_text.at(0) : QChar());
    }

    int key() const { return m_key; }

    QChar raw() const
    {
        if (m_key == Key_Tab)
            return '\t';
        if (m_key == Key_Return)
            return '\n';
        return m_key;
    }

    QString toString() const
    {
        bool hasCtrl = m_modifiers & int(HostOsInfo::controlModifier());
        QString key = vimKeyNames().key(m_key);
        if (key.isEmpty())
            key = QChar(m_xkey);
        else
            key = '<' + key + '>';
        return (hasCtrl ? QString("^") : QString()) + key;
    }

    QDebug dump(QDebug ts) const
    {
        return ts << m_key << '-' << m_modifiers << '-'
            << quoteUnprintable(m_text);
    }
private:
    int m_key;
    int m_xkey;
    int m_modifiers;
    QString m_text;
};

// mapping to <Nop> (do nothing)
static const Input Nop(-1, -1, QString());

QDebug operator<<(QDebug ts, const Input &input) { return input.dump(ts); }

class Inputs : public QVector<Input>
{
public:
    Inputs() : m_noremap(true), m_silent(false) {}

    explicit Inputs(const QString &str, bool noremap = true, bool silent = false)
        : m_noremap(noremap), m_silent(silent)
    {
        parseFrom(str);
    }

    bool noremap() const { return m_noremap; }

    bool silent() const { return m_silent; }

private:
    void parseFrom(const QString &str);

    bool m_noremap;
    bool m_silent;
};

static Input parseVimKeyName(const QString &keyName)
{
    if (keyName.length() == 1)
        return Input(keyName.at(0));

    const QStringList keys = keyName.split('-');
    const int len = keys.length();

    if (len == 1 && keys.at(0) == _("nop"))
        return Nop;

    int mods = NoModifier;
    for (int i = 0; i < len - 1; ++i) {
        const QString &key = keys[i].toUpper();
        if (key == "S")
            mods |= Qt::ShiftModifier;
        else if (key == "C")
            mods |= HostOsInfo::controlModifier();
        else
            return Input();
    }

    if (!keys.isEmpty()) {
        const QString key = keys.last();
        if (key.length() == 1) {
            // simple character
            QChar c = key.at(0).toUpper();
            return Input(c.unicode(), mods);
        }

        // find key name
        QMap<QString, int>::ConstIterator it = vimKeyNames().constFind(key.toUpper());
        if (it != vimKeyNames().end())
            return Input(*it, mods);
    }

    return Input();
}

void Inputs::parseFrom(const QString &str)
{
    const int n = str.size();
    for (int i = 0; i < n; ++i) {
        uint c = str.at(i).unicode();
        if (c == '<') {
            int j = str.indexOf('>', i);
            Input input;
            if (j != -1) {
                const QString key = str.mid(i+1, j - i - 1);
                if (!key.contains('<'))
                    input = parseVimKeyName(key);
            }
            if (input.isValid()) {
                append(input);
                i = j;
            } else {
                append(Input(QLatin1Char(c)));
            }
        } else {
            append(Input(QLatin1Char(c)));
        }
    }
}

class History
{
public:
    History() : m_items(QString()), m_index(0) {}
    void append(const QString &item);
    const QString &move(const QStringRef &prefix, int skip);
    const QString &current() const { return m_items[m_index]; }
    const QStringList &items() const { return m_items; }
    void restart() { m_index = m_items.size() - 1; }

private:
    // Last item is always empty or current search prefix.
    QStringList m_items;
    int m_index;
};

void History::append(const QString &item)
{
    if (item.isEmpty())
        return;
    m_items.pop_back();
    m_items.removeAll(item);
    m_items << item << QString();
    restart();
}

const QString &History::move(const QStringRef &prefix, int skip)
{
    if (!current().startsWith(prefix))
        restart();

    if (m_items.last() != prefix)
        m_items[m_items.size() - 1] = prefix.toString();

    int i = m_index + skip;
    if (!prefix.isEmpty())
        for (; i >= 0 && i < m_items.size() && !m_items[i].startsWith(prefix); i += skip);
    if (i >= 0 && i < m_items.size())
        m_index = i;

    return current();
}

// Command line buffer with prompt (i.e. :, / or ? characters), text contents and cursor position.
class CommandBuffer
{
public:
    CommandBuffer() : m_pos(0), m_userPos(0), m_historyAutoSave(true) {}

    void setPrompt(const QChar &prompt) { m_prompt = prompt; }
    void setContents(const QString &s) { m_buffer = s; m_pos = s.size(); }

    void setContents(const QString &s, int pos) { m_buffer = s; m_pos = m_userPos = pos; }

    QStringRef userContents() const { return m_buffer.leftRef(m_userPos); }
    const QChar &prompt() const { return m_prompt; }
    const QString &contents() const { return m_buffer; }
    bool isEmpty() const { return m_buffer.isEmpty(); }
    int cursorPos() const { return m_pos; }

    void insertChar(QChar c) { m_buffer.insert(m_pos++, c); m_userPos = m_pos; }
    void insertText(const QString &s)
    {
        m_buffer.insert(m_pos, s); m_userPos = m_pos = m_pos + s.size();
    }
    void deleteChar() { if (m_pos) m_buffer.remove(--m_pos, 1); m_userPos = m_pos; }

    void moveLeft() { if (m_pos) m_userPos = --m_pos; }
    void moveRight() { if (m_pos < m_buffer.size()) m_userPos = ++m_pos; }
    void moveStart() { m_userPos = m_pos = 0; }
    void moveEnd() { m_userPos = m_pos = m_buffer.size(); }

    void setHistoryAutoSave(bool autoSave) { m_historyAutoSave = autoSave; }
    void historyDown() { setContents(m_history.move(userContents(), 1)); }
    void historyUp() { setContents(m_history.move(userContents(), -1)); }
    const QStringList &historyItems() const { return m_history.items(); }
    void historyPush(const QString &item = QString())
    {
        m_history.append(item.isNull() ? contents() : item);
    }

    void clear()
    {
        if (m_historyAutoSave)
            historyPush();
        m_buffer.clear();
        m_userPos = m_pos = 0;
    }

    QString display() const
    {
        QString msg(m_prompt);
        for (int i = 0; i != m_buffer.size(); ++i) {
            const QChar c = m_buffer.at(i);
            if (c.unicode() < 32) {
                msg += '^';
                msg += QLatin1Char(c.unicode() + 64);
            } else {
                msg += c;
            }
        }
        return msg;
    }

    bool handleInput(const Input &input)
    {
        if (input.isKey(Key_Left)) {
            moveLeft();
        } else if (input.isKey(Key_Right)) {
            moveRight();
        } else if (input.isKey(Key_Home)) {
            moveStart();
        } else if (input.isKey(Key_End)) {
            moveEnd();
        } else if (input.isKey(Key_Delete)) {
            if (m_pos < m_buffer.size())
                m_buffer.remove(m_pos, 1);
            else
                deleteChar();
        } else if (!input.text().isEmpty()) {
            insertText(input.text());
        } else {
            return false;
        }
        return true;
    }

private:
    QString m_buffer;
    QChar m_prompt;
    History m_history;
    int m_pos;
    int m_userPos; // last position of inserted text (for retrieving history items)
    bool m_historyAutoSave; // store items to history on clear()?
};

// Mappings for a specific mode (trie structure)
class ModeMapping : public QMap<Input, ModeMapping>
{
public:
    const Inputs &value() const { return m_value; }
    void setValue(const Inputs &value) { m_value = value; }
private:
    Inputs m_value;
};

// Mappings for all modes
typedef QHash<char, ModeMapping> Mappings;

// Iterator for mappings
class MappingsIterator : public QVector<ModeMapping::Iterator>
{
public:
    MappingsIterator(Mappings *mappings, char mode = -1, const Inputs &inputs = Inputs())
        : m_parent(mappings)
    {
        reset(mode);
        walk(inputs);
    }

    // Reset iterator state. Keep previous mode if 0.
    void reset(char mode = 0)
    {
        clear();
        m_lastValid = -1;
        m_invalidInputCount = 0;
        if (mode != 0) {
            m_mode = mode;
            if (mode != -1)
                m_modeMapping = m_parent->find(mode);
        }
    }

    bool isValid() const { return !empty(); }

    // Return true if mapping can be extended.
    bool canExtend() const { return isValid() && !last()->empty(); }

    // Return true if this mapping can be used.
    bool isComplete() const { return m_lastValid != -1; }

    // Return size of current map.
    int mapLength() const { return m_lastValid + 1; }

    int invalidInputCount() const { return m_invalidInputCount; }

    bool walk(const Input &input)
    {
        if (m_modeMapping == m_parent->end())
            return false;

        if (!input.isValid()) {
            m_invalidInputCount += 1;
            return true;
        }

        ModeMapping::Iterator it;
        if (isValid()) {
            it = last()->find(input);
            if (it == last()->end())
                return false;
        } else {
            it = m_modeMapping->find(input);
            if (it == m_modeMapping->end())
                return false;
        }

        if (!it->value().isEmpty())
            m_lastValid = size();
        append(it);

        return true;
    }

    bool walk(const Inputs &inputs)
    {
        foreach (const Input &input, inputs) {
            if (!walk(input))
                return false;
        }
        return true;
    }

    // Return current mapped value. Iterator must be valid.
    const Inputs &inputs() const
    {
        return at(m_lastValid)->value();
    }

    void remove()
    {
        if (isValid()) {
            if (canExtend()) {
                last()->setValue(Inputs());
            } else {
                if (size() > 1) {
                    while (last()->empty()) {
                        at(size() - 2)->erase(last());
                        pop_back();
                        if (size() == 1 || !last()->value().isEmpty())
                            break;
                    }
                    if (last()->empty() && last()->value().isEmpty())
                        m_modeMapping->erase(last());
                } else if (last()->empty() && !last()->value().isEmpty()) {
                    m_modeMapping->erase(last());
                }
            }
        }
    }

    void setInputs(const Inputs &key, const Inputs &inputs, bool unique = false)
    {
        ModeMapping *current = &(*m_parent)[m_mode];
        foreach (const Input &input, key)
            current = &(*current)[input];
        if (!unique || current->value().isEmpty())
            current->setValue(inputs);
    }

private:
    Mappings *m_parent;
    Mappings::Iterator m_modeMapping;
    int m_lastValid;
    int m_invalidInputCount;
    char m_mode;
};

// state of current mapping
struct MappingState {
    MappingState()
        : maxMapDepth(1000), noremap(false), silent(false) {}
    MappingState(int depth, bool noremap, bool silent)
        : maxMapDepth(depth), noremap(noremap), silent(silent) {}
    int maxMapDepth;
    bool noremap;
    bool silent;
};

class FakeVimHandler::Private : public QObject
{
    Q_OBJECT

public:
    Private(FakeVimHandler *parent, QWidget *widget);

    EventResult handleEvent(QKeyEvent *ev);
    bool wantsOverride(QKeyEvent *ev);
    bool parseExCommmand(QString *line, ExCommand *cmd);
    bool parseLineRange(QString *line, ExCommand *cmd);
    int parseLineAddress(QString *cmd);
    void parseRangeCount(const QString &line, Range *range) const;
    void handleCommand(const QString &cmd); // Sets m_tc + handleExCommand
    void handleExCommand(const QString &cmd);

    void installEventFilter();
    void passShortcuts(bool enable);
    void setupWidget();
    void restoreWidget(int tabSize);

    friend class FakeVimHandler;

    void init();
    void focus();

    EventResult handleKey(const Input &input);
    EventResult handleDefaultKey(const Input &input);
    void handleMappedKeys();
    void unhandleMappedKeys();
    EventResult handleInsertMode(const Input &);
    EventResult handleReplaceMode(const Input &);

    EventResult handleCommandMode(const Input &);

    // return true only if input in current mode and sub-mode was correctly handled
    bool handleEscape();
    bool handleNoSubMode(const Input &);
    bool handleChangeDeleteSubModes(const Input &);
    bool handleReplaceSubMode(const Input &);
    bool handleFilterSubMode(const Input &);
    bool handleRegisterSubMode(const Input &);
    bool handleShiftSubMode(const Input &);
    bool handleChangeCaseSubMode(const Input &);
    bool handleWindowSubMode(const Input &);
    bool handleYankSubMode(const Input &);
    bool handleZSubMode(const Input &);
    bool handleCapitalZSubMode(const Input &);
    bool handleOpenSquareSubMode(const Input &);
    bool handleCloseSquareSubMode(const Input &);

    bool handleMovement(const Input &);

    EventResult handleExMode(const Input &);
    EventResult handleSearchSubSubMode(const Input &);
    bool handleCommandSubSubMode(const Input &);
    void fixSelection(); // Fix selection according to current range, move and command modes.
    void finishMovement(const QString &dotCommandMovement = QString());
    void finishMovement(const QString &dotCommandMovement, int count);
    void resetCommandMode();
    QTextCursor search(const SearchData &sd, int startPos, int count, bool showMessages);
    void search(const SearchData &sd, bool showMessages = true);
    void searchNext(bool forward = true);
    void searchBalanced(bool forward, QChar needle, QChar other);
    void highlightMatches(const QString &needle);
    void stopIncrementalFind();
    void updateFind(bool isComplete);

    int mvCount() const { return m_mvcount.isEmpty() ? 1 : m_mvcount.toInt(); }
    int opCount() const { return m_opcount.isEmpty() ? 1 : m_opcount.toInt(); }
    int count() const { return mvCount() * opCount(); }
    QTextBlock block() const { return cursor().block(); }
    int leftDist() const { return position() - block().position(); }
    int rightDist() const { return block().length() - leftDist() - 1; }
    bool atBlockStart() const { return cursor().atBlockStart(); }
    bool atBlockEnd() const { return cursor().atBlockEnd(); }
    bool atEndOfLine() const { return atBlockEnd() && block().length() > 1; }
    bool atDocumentEnd() const { return position() >= lastPositionInDocument(); }
    bool atDocumentStart() const { return cursor().atStart(); }

    bool atEmptyLine(const QTextCursor &tc = QTextCursor()) const;
    bool atBoundary(bool end, bool simple, bool onlyWords = false,
        const QTextCursor &tc = QTextCursor()) const;
    bool atWordBoundary(bool end, bool simple, const QTextCursor &tc = QTextCursor()) const;
    bool atWordStart(bool simple, const QTextCursor &tc = QTextCursor()) const;
    bool atWordEnd(bool simple, const QTextCursor &tc = QTextCursor()) const;
    bool isFirstNonBlankOnLine(int pos);

    int lastPositionInDocument(bool ignoreMode = false) const; // Returns last valid position in doc.
    int firstPositionInLine(int line, bool onlyVisibleLines = true) const; // 1 based line, 0 based pos
    int lastPositionInLine(int line, bool onlyVisibleLines = true) const; // 1 based line, 0 based pos
    int lineForPosition(int pos) const;  // 1 based line, 0 based pos
    QString lineContents(int line) const; // 1 based line
    void setLineContents(int line, const QString &contents); // 1 based line
    int blockBoundary(const QString &left, const QString &right,
        bool end, int count) const; // end or start position of current code block
    int lineNumber(const QTextBlock &block) const;

    int linesOnScreen() const;
    int columnsOnScreen() const;
    int linesInDocument() const;

    // The following use all zero-based counting.
    int cursorLineOnScreen() const;
    int cursorLine() const;
    int cursorBlockNumber() const; // "." address
    int physicalCursorColumn() const; // as stored in the data
    int logicalCursorColumn() const; // as visible on screen
    int physicalToLogicalColumn(int physical, const QString &text) const;
    int logicalToPhysicalColumn(int logical, const QString &text) const;
    Column cursorColumn() const; // as visible on screen
    int firstVisibleLine() const;
    void scrollToLine(int line);
    void scrollUp(int count);
    void scrollDown(int count) { scrollUp(-count); }
    void alignViewportToCursor(Qt::AlignmentFlag align, int line = -1,
        bool moveToNonBlank = false);

    void setCursorPosition(const CursorPosition &p);
    void setCursorPosition(QTextCursor *tc, const CursorPosition &p);

    // Helper functions for indenting/
    bool isElectricCharacter(QChar c) const;
    void indentSelectedText(QChar lastTyped = QChar());
    void indentText(const Range &range, QChar lastTyped = QChar());
    void shiftRegionLeft(int repeat = 1);
    void shiftRegionRight(int repeat = 1);

    void moveToFirstNonBlankOnLine();
    void moveToFirstNonBlankOnLine(QTextCursor *tc);
    void moveToTargetColumn();
    void setTargetColumn() {
        m_targetColumn = logicalCursorColumn();
        m_visualTargetColumn = m_targetColumn;
        //qDebug() << "TARGET: " << m_targetColumn;
    }
    void moveToMatchingParanthesis();
    void moveToBoundary(bool simple, bool forward = true);
    void moveToNextBoundary(bool end, int count, bool simple, bool forward);
    void moveToNextBoundaryStart(int count, bool simple, bool forward = true);
    void moveToNextBoundaryEnd(int count, bool simple, bool forward = true);
    void moveToBoundaryStart(int count, bool simple, bool forward = true);
    void moveToBoundaryEnd(int count, bool simple, bool forward = true);
    void moveToNextWord(bool end, int count, bool simple, bool forward, bool emptyLines);
    void moveToNextWordStart(int count, bool simple, bool forward = true, bool emptyLines = true);
    void moveToNextWordEnd(int count, bool simple, bool forward = true, bool emptyLines = true);
    void moveToWordStart(int count, bool simple, bool forward = true, bool emptyLines = true);
    void moveToWordEnd(int count, bool simple, bool forward = true, bool emptyLines = true);

    // Convenience wrappers to reduce line noise.
    void moveToStartOfLine();
    void moveToEndOfLine();
    void moveBehindEndOfLine();
    void moveUp(int n = 1) { moveDown(-n); }
    void moveDown(int n = 1);
    void dump(const char *msg) const {
        qDebug() << msg << "POS: " << anchor() << position()
            << "EXT: " << m_oldExternalAnchor << m_oldExternalPosition
            << "INT: " << m_oldInternalAnchor << m_oldInternalPosition
            << "VISUAL: " << m_visualMode;
    }
    void moveRight(int n = 1) {
        //dump("RIGHT 1");
        QTextCursor tc = cursor();
        tc.movePosition(Right, KeepAnchor, n);
        setCursor(tc);
        if (atEndOfLine())
            emit q->fold(1, false);
        //dump("RIGHT 2");
    }
    void moveLeft(int n = 1) {
        QTextCursor tc = cursor();
        tc.movePosition(Left, KeepAnchor, n);
        setCursor(tc);
    }
    void setAnchor() {
        QTextCursor tc = cursor();
        tc.setPosition(tc.position(), MoveAnchor);
        setCursor(tc);
    }
    void setAnchor(int position) {
        QTextCursor tc = cursor();
        tc.setPosition(tc.anchor(), MoveAnchor);
        tc.setPosition(position, KeepAnchor);
        setCursor(tc);
    }
    void setPosition(int position) {
        QTextCursor tc = cursor();
        tc.setPosition(position, KeepAnchor);
        setCursor(tc);
    }
    void setAnchorAndPosition(int anchor, int position) {
        QTextCursor tc = cursor();
        tc.setPosition(anchor, MoveAnchor);
        tc.setPosition(position, KeepAnchor);
        setCursor(tc);
    }

    // Workaround for occational crash when setting text cursor while in edit block.
    QTextCursor m_cursor;

    QTextCursor cursor() const {
        if (m_editBlockLevel > 0)
            return m_cursor;
        return EDITOR(textCursor());
    }

    void setCursor(const QTextCursor &tc) {
        m_cursor = tc;
        if (m_editBlockLevel == 0)
            EDITOR(setTextCursor(tc));
    }

    bool handleFfTt(QString key);

    void enterInsertMode();
    void enterReplaceMode();
    void enterCommandMode(Mode returnToMode = CommandMode);
    void enterExMode(const QString &contents = QString());
    void showMessage(MessageLevel level, const QString &msg);
    void clearMessage() { showMessage(MessageInfo, QString()); }
    void notImplementedYet();
    void updateMiniBuffer();
    void updateSelection();
    void updateHighlights();
    void updateCursorShape();
    QWidget *editor() const;
    QTextDocument *document() const { return EDITOR(document()); }
    QChar characterAtCursor() const
        { return document()->characterAt(position()); }

    int m_editBlockLevel; // current level of edit blocks
    void joinPreviousEditBlock();
    void beginEditBlock(bool rememberPosition = true);
    void beginLargeEditBlock() { beginEditBlock(false); }
    void endEditBlock();
    void breakEditBlock() { m_breakEditBlock = true; }

    bool isVisualMode() const { return m_visualMode != NoVisualMode; }
    bool isNoVisualMode() const { return m_visualMode == NoVisualMode; }
    bool isVisualCharMode() const { return m_visualMode == VisualCharMode; }
    bool isVisualLineMode() const { return m_visualMode == VisualLineMode; }
    bool isVisualBlockMode() const { return m_visualMode == VisualBlockMode; }
    char currentModeCode() const;
    void updateEditor();

    void selectTextObject(bool simple, bool inner);
    void selectWordTextObject(bool inner);
    void selectWORDTextObject(bool inner);
    void selectSentenceTextObject(bool inner);
    void selectParagraphTextObject(bool inner);
    void changeNumberTextObject(int count);
    // return true only if cursor is in a block delimited with correct characters
    bool selectBlockTextObject(bool inner, char left, char right);
    bool selectQuotedStringTextObject(bool inner, const QString &quote);

    Q_SLOT void importSelection();
    void exportSelection();
    void ensureCursorVisible();
    void insertInInsertMode(const QString &text);

public:
    QTextEdit *m_textedit;
    QPlainTextEdit *m_plaintextedit;
    bool m_wasReadOnly; // saves read-only state of document

    FakeVimHandler *q;
    Mode m_mode;
    bool m_passing; // let the core see the next event
    bool m_firstKeyPending;
    SubMode m_submode;
    SubSubMode m_subsubmode;
    Input m_subsubdata;
    int m_oldExternalPosition; // copy from last event to check for external changes
    int m_oldExternalAnchor;
    int m_oldInternalPosition; // copy from last event to check for external changes
    int m_oldInternalAnchor;
    int m_oldPosition; // FIXME: Merge with above.
    int m_register;
    QString m_mvcount;
    QString m_opcount;
    MoveType m_movetype;
    RangeMode m_rangemode;
    int m_visualInsertCount;

    bool m_fakeEnd;
    bool m_anchorPastEnd;
    bool m_positionPastEnd; // '$' & 'l' in visual mode can move past eol

    int m_gflag;  // whether current command started with 'g'

    QString m_currentFileName;

    int m_findStartPosition;
    QString m_lastInsertion;
    QString m_lastDeletion;

    bool m_breakEditBlock;

    int anchor() const { return cursor().anchor(); }
    int position() const { return cursor().position(); }

    struct TransformationData
    {
        TransformationData(const QString &s, const QVariant &d)
            : from(s), extraData(d) {}
        QString from;
        QString to;
        QVariant extraData;
    };
    typedef void (Private::*Transformation)(TransformationData *td);
    void transformText(const Range &range, Transformation transformation,
        const QVariant &extraData = QVariant());

    void insertText(const Register &reg);
    void removeText(const Range &range);
    void removeTransform(TransformationData *td);

    void invertCase(const Range &range);
    void invertCaseTransform(TransformationData *td);

    void upCase(const Range &range);
    void upCaseTransform(TransformationData *td);

    void downCase(const Range &range);
    void downCaseTransform(TransformationData *td);

    void replaceText(const Range &range, const QString &str);
    void replaceByStringTransform(TransformationData *td);
    void replaceByCharTransform(TransformationData *td);

    QString selectText(const Range &range) const;
    void setCurrentRange(const Range &range);
    Range currentRange() const { return Range(position(), anchor(), m_rangemode); }

    void yankText(const Range &range, int toregister = '"');

    void pasteText(bool afterCursor);

    void joinLines(int count, bool preserveSpace = false);

    // undo handling
    int revision() const { return document()->availableUndoSteps(); }
    void undoRedo(bool undo);
    void undo();
    void redo();
    void setUndoPosition(bool overwrite = true);
    // revision -> state
    QStack<State> m_undo;
    QStack<State> m_redo;

    // extra data for '.'
    void replay(const QString &text);
    void setDotCommand(const QString &cmd) { g.dotCommand = cmd; }
    void setDotCommand(const QString &cmd, int n) { g.dotCommand = cmd.arg(n); }
    QString visualDotCommand() const;

    // extra data for ';'
    QString m_semicolonCount;
    Input m_semicolonType;  // 'f', 'F', 't', 'T'
    QString m_semicolonKey;

    // visual modes
    void toggleVisualMode(VisualMode visualMode);
    void leaveVisualMode();
    VisualMode m_visualMode;
    VisualMode m_lastVisualMode;

    // marks
    Mark mark(QChar code) const;
    void setMark(QChar code, CursorPosition position);
    // jump to valid mark return true if mark is valid and local
    bool jumpToMark(QChar mark, bool backTickMode);
    Marks m_marks; // local marks

    // vi style configuration
    QVariant config(int code) const { return theFakeVimSetting(code)->value(); }
    bool hasConfig(int code) const { return config(code).toBool(); }
    bool hasConfig(int code, const char *value) const // FIXME
        { return config(code).toString().contains(value); }

    int m_targetColumn; // -1 if past end of line
    int m_visualTargetColumn; // 'l' can move past eol in visual mode only

    // auto-indent
    QString tabExpand(int len) const;
    Column indentation(const QString &line) const;
    void insertAutomaticIndentation(bool goingDown);
    bool removeAutomaticIndentation(); // true if something removed
    // number of autoindented characters
    int m_justAutoIndented;
    void handleStartOfLine();

    // register handling
    QString registerContents(int reg) const;
    void setRegister(int reg, const QString &contents, RangeMode mode);
    RangeMode registerRangeMode(int reg) const;
    void getRegisterType(int reg, bool *isClipboard, bool *isSelection) const;

    void recordJump();
    void jump(int distance);
    QStack<CursorPosition> m_jumpListUndo;
    QStack<CursorPosition> m_jumpListRedo;
    CursorPosition m_lastChangePosition;

    QList<QTextEdit::ExtraSelection> m_extraSelections;
    QTextCursor m_searchCursor;
    int m_searchStartPosition;
    int m_searchFromScreenLine;
    QString m_oldNeedle;

    bool handleExCommandHelper(ExCommand &cmd); // Returns success.
    bool handleExPluginCommand(const ExCommand &cmd); // Handled by plugin?
    bool handleExBangCommand(const ExCommand &cmd);
    bool handleExYankDeleteCommand(const ExCommand &cmd);
    bool handleExChangeCommand(const ExCommand &cmd);
    bool handleExMoveCommand(const ExCommand &cmd);
    bool handleExJoinCommand(const ExCommand &cmd);
    bool handleExGotoCommand(const ExCommand &cmd);
    bool handleExHistoryCommand(const ExCommand &cmd);
    bool handleExRegisterCommand(const ExCommand &cmd);
    bool handleExMapCommand(const ExCommand &cmd);
    bool handleExNohlsearchCommand(const ExCommand &cmd);
    bool handleExNormalCommand(const ExCommand &cmd);
    bool handleExReadCommand(const ExCommand &cmd);
    bool handleExUndoRedoCommand(const ExCommand &cmd);
    bool handleExSetCommand(const ExCommand &cmd);
    bool handleExShiftCommand(const ExCommand &cmd);
    bool handleExSourceCommand(const ExCommand &cmd);
    bool handleExSubstituteCommand(const ExCommand &cmd);
    bool handleExWriteCommand(const ExCommand &cmd);
    bool handleExEchoCommand(const ExCommand &cmd);

    void timerEvent(QTimerEvent *ev);

    void setupCharClass();
    int charClass(QChar c, bool simple) const;
    signed char m_charClass[256];
    bool m_ctrlVActive;

    void miniBufferTextEdited(const QString &text, int cursorPos);

    static struct GlobalData
    {
        GlobalData()
            : mappings(), currentMap(&mappings), inputTimer(-1), currentMessageLevel(MessageInfo),
              lastSearchForward(false), findPending(false), returnToMode(CommandMode)
        {
            // default mapping state - shouldn't be removed
            mapStates << MappingState();
            commandBuffer.setPrompt(':');
        }

        // Repetition.
        QString dotCommand;

        QHash<int, Register> registers;

        // All mappings.
        Mappings mappings;

        // Input.
        Inputs pendingInput;
        MappingsIterator currentMap;
        int inputTimer;
        QStack<MappingState> mapStates;

        // Command line buffers.
        CommandBuffer commandBuffer;
        CommandBuffer searchBuffer;

        // Current mini buffer message.
        QString currentMessage;
        MessageLevel currentMessageLevel;
        QString currentCommand;

        // Search state.
        QString lastSearch;
        bool lastSearchForward;
        bool findPending;

        // Last substitution command.
        QString lastSubstituteFlags;
        QString lastSubstitutePattern;
        QString lastSubstituteReplacement;

        // Global marks.
        Marks marks;

        // Return to insert/replace mode after single command (<C-O>).
        Mode returnToMode;
    } g;
};

FakeVimHandler::Private::GlobalData FakeVimHandler::Private::g;

FakeVimHandler::Private::Private(FakeVimHandler *parent, QWidget *widget)
{
    //static PythonHighlighterRules pythonRules;
    q = parent;
    m_textedit = qobject_cast<QTextEdit *>(widget);
    m_plaintextedit = qobject_cast<QPlainTextEdit *>(widget);
    //new Highlighter(document(), &pythonRules);
    init();
}

void FakeVimHandler::Private::init()
{
    m_mode = CommandMode;
    m_submode = NoSubMode;
    m_subsubmode = NoSubSubMode;
    m_passing = false;
    m_firstKeyPending = false;
    g.findPending = false;
    m_findStartPosition = -1;
    m_fakeEnd = false;
    m_positionPastEnd = false;
    m_anchorPastEnd = false;
    g.lastSearchForward = true;
    m_register = '"';
    m_gflag = false;
    m_visualMode = NoVisualMode;
    m_lastVisualMode = NoVisualMode;
    m_targetColumn = 0;
    m_visualTargetColumn = 0;
    m_movetype = MoveInclusive;
    m_justAutoIndented = 0;
    m_rangemode = RangeCharMode;
    m_ctrlVActive = false;
    m_oldInternalAnchor = -1;
    m_oldInternalPosition = -1;
    m_oldExternalAnchor = -1;
    m_oldExternalPosition = -1;
    m_oldPosition = -1;
    m_breakEditBlock = false;
    m_searchStartPosition = 0;
    m_searchFromScreenLine = 0;
    m_editBlockLevel = 0;

    setupCharClass();
}

void FakeVimHandler::Private::focus()
{
    stopIncrementalFind();
    if (g.returnToMode != CommandMode && g.currentCommand.isEmpty() && m_mode != ExMode) {
        // Return to insert mode.
        resetCommandMode();
        updateMiniBuffer();
        updateCursorShape();
    }
}

bool FakeVimHandler::Private::wantsOverride(QKeyEvent *ev)
{
    const int key = ev->key();
    const int mods = ev->modifiers();
    KEY_DEBUG("SHORTCUT OVERRIDE" << key << "  PASSING: " << m_passing);

    if (key == Key_Escape) {
        if (m_subsubmode == SearchSubSubMode)
            return true;
        // Not sure this feels good. People often hit Esc several times.
        if (isNoVisualMode()
                && m_mode == CommandMode
                && m_submode == NoSubMode
                && g.currentCommand.isEmpty()
                && g.returnToMode == CommandMode)
            return false;
        return true;
    }

    // We are interested in overriding most Ctrl key combinations.
    if (mods == int(HostOsInfo::controlModifier())
            && !config(ConfigPassControlKey).toBool()
            && ((key >= Key_A && key <= Key_Z && key != Key_K)
                || key == Key_BracketLeft || key == Key_BracketRight)) {
        // Ctrl-K is special as it is the Core's default notion of Locator
        if (m_passing) {
            KEY_DEBUG(" PASSING CTRL KEY");
            // We get called twice on the same key
            //m_passing = false;
            return false;
        }
        KEY_DEBUG(" NOT PASSING CTRL KEY");
        //updateMiniBuffer();
        return true;
    }

    // Let other shortcuts trigger.
    return false;
}

EventResult FakeVimHandler::Private::handleEvent(QKeyEvent *ev)
{
    const int key = ev->key();
    const int mods = ev->modifiers();

    if (key == Key_Shift || key == Key_Alt || key == Key_Control
            || key == Key_Alt || key == Key_AltGr || key == Key_Meta)
    {
        KEY_DEBUG("PLAIN MODIFIER");
        return EventUnhandled;
    }

    if (m_passing) {
        passShortcuts(false);
        KEY_DEBUG("PASSING PLAIN KEY..." << ev->key() << ev->text());
        //if (input.is(',')) { // use ',,' to leave, too.
        //    qDebug() << "FINISHED...";
        //    return EventHandled;
        //}
        m_passing = false;
        updateMiniBuffer();
        KEY_DEBUG("   PASS TO CORE");
        return EventPassedToCore;
    }

    bool inSnippetMode = false;
    QMetaObject::invokeMethod(editor(),
        "inSnippetMode", Q_ARG(bool *, &inSnippetMode));

    if (inSnippetMode)
        return EventPassedToCore;

    // Fake "End of line"
    //m_tc = cursor();

    //bool hasBlock = false;
    //emit q->requestHasBlockSelection(&hasBlock);
    //qDebug() << "IMPORT BLOCK 2:" << hasBlock;

    //if (0 && hasBlock) {
    //    (pos > anc) ? --pos : --anc;

    importSelection();

    // Position changed externally, e.g. by code completion.
    if (position() != m_oldPosition) {
        setTargetColumn();
        //qDebug() << "POSITION CHANGED EXTERNALLY";
        if (m_mode == InsertMode) {
            int dist = position() - m_oldPosition;
            // Try to compensate for code completion
            if (dist > 0 && dist <= physicalCursorColumn()) {
                Range range(m_oldPosition, position());
                m_lastInsertion.append(selectText(range));
            }
        } else if (!isVisualMode()) {
            if (atEndOfLine())
                moveLeft();
        }
    }

    QTextCursor tc = cursor();
    tc.setVisualNavigation(true);
    if (m_firstKeyPending) {
        m_firstKeyPending = false;
        recordJump();
    }
    setCursor(tc);

    if (m_fakeEnd)
        moveRight();

    //if ((mods & RealControlModifier) != 0) {
    //    if (key >= Key_A && key <= Key_Z)
    //        key = shift(key); // make it lower case
    //    key = control(key);
    //} else if (key >= Key_A && key <= Key_Z && (mods & Qt::ShiftModifier) == 0) {
    //    key = shift(key);
    //}

    //QTC_ASSERT(m_mode == InsertMode || m_mode == ReplaceMode
    //        || !atBlockEnd() || block().length() <= 1,
    //    qDebug() << "Cursor at EOL before key handler");

    EventResult result = handleKey(Input(key, mods, ev->text()));

    // The command might have destroyed the editor.
    if (m_textedit || m_plaintextedit) {
        // We fake vi-style end-of-line behaviour
        m_fakeEnd = atEndOfLine() && m_mode == CommandMode && !isVisualBlockMode();

        //QTC_ASSERT(m_mode == InsertMode || m_mode == ReplaceMode
        //        || !atBlockEnd() || block().length() <= 1,
        //    qDebug() << "Cursor at EOL after key handler");
        if (m_fakeEnd)
            moveLeft();

        m_oldPosition = position();
        if (hasConfig(ConfigShowMarks))
            updateSelection();

        exportSelection();
        updateCursorShape();
    }

    return result;
}

void FakeVimHandler::Private::installEventFilter()
{
    EDITOR(viewport()->installEventFilter(q));
    EDITOR(installEventFilter(q));
}

void FakeVimHandler::Private::setupWidget()
{
    resetCommandMode();
    if (m_textedit) {
        m_textedit->setLineWrapMode(QTextEdit::NoWrap);
    } else if (m_plaintextedit) {
        m_plaintextedit->setLineWrapMode(QPlainTextEdit::NoWrap);
    }
    m_wasReadOnly = EDITOR(isReadOnly());
    m_firstKeyPending = true;

    updateEditor();
    importSelection();
    updateMiniBuffer();
    updateCursorShape();
}

void FakeVimHandler::Private::exportSelection()
{
    int pos = position();
    int anc = isVisualMode() ? anchor() : position();

    m_oldInternalPosition = pos;
    m_oldInternalAnchor = anc;

    if (isVisualMode()) {
        if (pos >= anc)
            setAnchorAndPosition(anc, pos + 1);
        else
            setAnchorAndPosition(anc + 1, pos);

        if (m_visualMode == VisualBlockMode) {
            emit q->requestSetBlockSelection(false);
            emit q->requestSetBlockSelection(true);
        } else if (m_visualMode == VisualLineMode) {
            const int posLine = lineForPosition(pos);
            const int ancLine = lineForPosition(anc);
            if (anc < pos) {
                pos = lastPositionInLine(posLine);
                anc = firstPositionInLine(ancLine);
            } else {
                pos = firstPositionInLine(posLine);
                anc = lastPositionInLine(ancLine);
            }
            // putting cursor on folded line will unfold the line, so move the cursor a bit
            if (!document()->findBlock(pos).isVisible())
                ++pos;
            setAnchorAndPosition(anc, pos);
        } else if (m_visualMode == VisualCharMode) {
            /* Nothing */
        } else {
            QTC_CHECK(false);
        }

        setMark('<', mark('<').position);
        setMark('>', mark('>').position);
    } else {
        if (m_subsubmode == SearchSubSubMode && !m_searchCursor.isNull())
            setCursor(m_searchCursor);
        else
            setAnchorAndPosition(pos, pos);
    }
    m_oldExternalPosition = position();
    m_oldExternalAnchor = anchor();
}

void FakeVimHandler::Private::ensureCursorVisible()
{
    int pos = position();
    int anc = isVisualMode() ? anchor() : position();

    // fix selection so it is outside folded block
    int start = qMin(pos, anc);
    int end = qMax(pos, anc) + 1;
    QTextBlock block = document()->findBlock(start);
    QTextBlock block2 = document()->findBlock(end);
    if (!block.isVisible() || !block2.isVisible()) {
        // FIXME: Moving cursor left/right or unfolding block immediately after block is folded
        //        should restore cursor position inside block.
        // Changing cursor position after folding is not Vim behavior so at least record the jump.
        if (block.isValid() && !block.isVisible())
            recordJump();

        pos = start;
        while (block.isValid() && !block.isVisible())
            block = block.previous();
        if (block.isValid())
            pos = block.position() + qMin(m_targetColumn, block.length() - 2);

        if (isVisualMode()) {
            anc = end;
            while (block2.isValid() && !block2.isVisible()) {
                anc = block2.position() + block2.length() - 2;
                block2 = block2.next();
            }
        }

        setAnchorAndPosition(anc, pos);
    }
}

void FakeVimHandler::Private::importSelection()
{
    bool hasBlock = false;
    emit q->requestHasBlockSelection(&hasBlock);

    if (position() == m_oldExternalPosition
            && anchor() == m_oldExternalAnchor) {
        // Undo drawing correction.
        setAnchorAndPosition(m_oldInternalAnchor, m_oldInternalPosition);
    } else {
        // Import new selection.
        Qt::KeyboardModifiers mods = QApplication::keyboardModifiers();
        if (cursor().hasSelection()) {
            if (mods & HostOsInfo::controlModifier())
                m_visualMode = VisualBlockMode;
            else if (mods & Qt::AltModifier)
                m_visualMode = VisualBlockMode;
            else if (mods & Qt::ShiftModifier)
                m_visualMode = VisualLineMode;
            else
                m_visualMode = VisualCharMode;
            m_lastVisualMode = m_visualMode;
        } else {
            m_visualMode = NoVisualMode;
        }
    }
}

void FakeVimHandler::Private::updateEditor()
{
    const int charWidth = QFontMetrics(EDITOR(font())).width(QChar(' '));
    EDITOR(setTabStopWidth(charWidth * config(ConfigTabStop).toInt()));
    setupCharClass();
}

void FakeVimHandler::Private::restoreWidget(int tabSize)
{
    //clearMessage();
    //updateMiniBuffer();
    //EDITOR(removeEventFilter(q));
    //EDITOR(setReadOnly(m_wasReadOnly));
    const int charWidth = QFontMetrics(EDITOR(font())).width(QChar(' '));
    EDITOR(setTabStopWidth(charWidth * tabSize));
    m_visualMode = NoVisualMode;
    // Force "ordinary" cursor.
    m_mode = InsertMode;
    m_submode = NoSubMode;
    m_subsubmode = NoSubSubMode;
    updateCursorShape();
    updateSelection();
    updateHighlights();
}

EventResult FakeVimHandler::Private::handleKey(const Input &input)
{
    KEY_DEBUG("HANDLE INPUT: " << input << " MODE: " << mode);

    bool handleMapped = true;
    bool hasInput = input.isValid();

    if (hasInput)
        g.pendingInput.append(input);

    // Waiting on input to complete mapping?
    if (g.inputTimer != -1) {
        killTimer(g.inputTimer);
        g.inputTimer = -1;
        // If there is a new input add it to incomplete input or
        // if the mapped input can be completed complete.
        if (hasInput && g.currentMap.walk(input)) {
            if (g.currentMap.canExtend()) {
                g.currentCommand.append(input.toString());
                updateMiniBuffer();
                g.inputTimer = startTimer(1000);
                return EventHandled;
            } else {
                hasInput = false;
                handleMappedKeys();
            }
        } else if (g.currentMap.isComplete()) {
            handleMappedKeys();
        } else {
            g.currentMap.reset();
            handleMapped = false;
        }
        g.currentCommand.clear();
        updateMiniBuffer();
    }

    EventResult r = EventUnhandled;
    while (!g.pendingInput.isEmpty()) {
        const Input &in = g.pendingInput.front();

        // invalid input is used to pop mapping state
        if (!in.isValid()) {
            unhandleMappedKeys();
        } else {
            if (handleMapped && !g.mapStates.last().noremap && m_subsubmode != SearchSubSubMode) {
                if (!g.currentMap.isValid()) {
                    g.currentMap.reset(currentModeCode());
                    if (!g.currentMap.walk(g.pendingInput) && g.currentMap.isComplete()) {
                        handleMappedKeys();
                        continue;
                    }
                }

                // handle user mapping
                if (g.currentMap.canExtend()) {
                    g.currentCommand.append(in.toString());
                    updateMiniBuffer();
                    // wait for user to press any key or trigger complete mapping after interval
                    g.inputTimer = startTimer(1000);
                    return EventHandled;
                } else if (g.currentMap.isComplete()) {
                    handleMappedKeys();
                    continue;
                }
            }

            r = handleDefaultKey(in);
            if (r != EventHandled) {
                // clear bad mapping
                const int index = g.pendingInput.lastIndexOf(Input());
                if (index != -1)
                    g.pendingInput.remove(0, index - 1);
            }
        }
        handleMapped = true;
        g.pendingInput.pop_front();
    }

    return r;
}

EventResult FakeVimHandler::Private::handleDefaultKey(const Input &input)
{
    if (input == Nop)
        return EventHandled;
    else if (m_subsubmode == SearchSubSubMode)
        return handleSearchSubSubMode(input);
    else if (m_mode == CommandMode)
        return handleCommandMode(input);
    else if (m_mode == InsertMode)
        return handleInsertMode(input);
    else if (m_mode == ReplaceMode)
        return handleReplaceMode(input);
    else if (m_mode == ExMode)
        return handleExMode(input);
    return EventUnhandled;
}

void FakeVimHandler::Private::handleMappedKeys()
{
    int maxMapDepth = g.mapStates.last().maxMapDepth - 1;

    int invalidCount = g.currentMap.invalidInputCount();
    if (invalidCount > 0) {
        g.mapStates.remove(g.mapStates.size() - invalidCount, invalidCount);
        QTC_CHECK(!g.mapStates.empty());
        for (int i = 0; i < invalidCount; ++i)
            endEditBlock();
    }

    if (maxMapDepth <= 0) {
        showMessage(MessageError, "recursive mapping");
        g.pendingInput.remove(0, g.currentMap.mapLength() + invalidCount);
    } else {
        const Inputs &inputs = g.currentMap.inputs();
        QVector<Input> rest = g.pendingInput.mid(g.currentMap.mapLength() + invalidCount);
        g.pendingInput.clear();
        g.pendingInput << inputs << Input() << rest;
        g.mapStates << MappingState(maxMapDepth, inputs.noremap(), inputs.silent());
        g.commandBuffer.setHistoryAutoSave(false);
        beginLargeEditBlock();
    }
    g.currentMap.reset();
}

void FakeVimHandler::Private::unhandleMappedKeys()
{
    if (g.mapStates.size() == 1)
        return;
    g.mapStates.pop_back();
    endEditBlock();
    if (g.mapStates.size() == 1)
        g.commandBuffer.setHistoryAutoSave(true);
    if (m_mode == ExMode || m_subsubmode == SearchSubSubMode)
        updateMiniBuffer(); // update cursor position on command line
}

void FakeVimHandler::Private::timerEvent(QTimerEvent *ev)
{
    if (ev->timerId() == g.inputTimer) {
        if (g.currentMap.isComplete())
            handleMappedKeys();
        handleKey(Input());
    }
}

void FakeVimHandler::Private::stopIncrementalFind()
{
    if (g.findPending) {
        g.findPending = false;
        QTextCursor tc = cursor();
        setAnchorAndPosition(m_findStartPosition, tc.selectionStart());
        finishMovement();
        setAnchor();
    }
}

void FakeVimHandler::Private::updateFind(bool isComplete)
{
    if (!isComplete && !hasConfig(ConfigIncSearch))
        return;

    g.currentMessage.clear();

    const QString &needle = g.searchBuffer.contents();
    SearchData sd;
    sd.needle = needle;
    sd.forward = g.lastSearchForward;
    sd.highlightMatches = isComplete;
    if (isComplete) {
        setPosition(m_searchStartPosition);
        recordJump();
    }
    search(sd, isComplete);
}

bool FakeVimHandler::Private::atEmptyLine(const QTextCursor &tc) const
{
    if (tc.isNull())
        return atEmptyLine(cursor());
    return tc.block().length() == 1;
}

bool FakeVimHandler::Private::atBoundary(bool end, bool simple, bool onlyWords,
    const QTextCursor &tc) const
{
    if (tc.isNull())
        return atBoundary(end, simple, onlyWords, cursor());
    if (atEmptyLine(tc))
        return true;
    int pos = tc.position();
    QChar c1 = document()->characterAt(pos);
    QChar c2 = document()->characterAt(pos + (end ? 1 : -1));
    int thisClass = charClass(c1, simple);
    return (!onlyWords || thisClass != 0)
        && (c2.isNull() || c2 == ParagraphSeparator || thisClass != charClass(c2, simple));
}

bool FakeVimHandler::Private::atWordBoundary(bool end, bool simple, const QTextCursor &tc) const
{
    return atBoundary(end, simple, true, tc);
}

bool FakeVimHandler::Private::atWordStart(bool simple, const QTextCursor &tc) const
{
    return atWordBoundary(false, simple, tc);
}

bool FakeVimHandler::Private::atWordEnd(bool simple, const QTextCursor &tc) const
{
    return atWordBoundary(true, simple, tc);
}

bool FakeVimHandler::Private::isFirstNonBlankOnLine(int pos)
{
    for (int i = document()->findBlock(pos).position(); i < pos; ++i) {
        if (!document()->characterAt(i).isSpace())
            return false;
    }
    return true;
}

void FakeVimHandler::Private::setUndoPosition(bool overwrite)
{
    const int rev = revision();
    if (!overwrite && !m_undo.empty() && m_undo.top().revision >= rev)
        return;

    int pos = position();
    if (m_mode != InsertMode && m_mode != ReplaceMode) {
        if (isVisualMode() || m_submode == DeleteSubMode) {
            pos = qMin(pos, anchor());
            if (isVisualLineMode())
                pos = firstPositionInLine(lineForPosition(pos));
        } else if (m_movetype == MoveLineWise && hasConfig(ConfigStartOfLine)) {
            QTextCursor tc = cursor();
            if (m_submode == ShiftLeftSubMode || m_submode == ShiftRightSubMode
                || m_submode == IndentSubMode) {
                pos = qMin(pos, anchor());
            }
            tc.setPosition(pos);
            moveToFirstNonBlankOnLine(&tc);
            pos = qMin(pos, tc.position());
        }
    }

    m_redo.clear();
    while (!m_undo.empty() && m_undo.top().revision >= rev)
        m_undo.pop();
    m_lastChangePosition = CursorPosition(document(), pos);
    if (isVisualMode()) {
        setMark('<', mark('<').position);
        setMark('>', mark('>').position);
    }
    m_undo.push(State(rev, m_lastChangePosition, m_marks, m_lastVisualMode));
}

void FakeVimHandler::Private::moveDown(int n)
{
    QTextBlock block = cursor().block();
    const int col = position() - block.position();
    const int targetLine = qMax(0, lineNumber(block) + n);

    block = document()->findBlockByLineNumber(qMax(0, targetLine - 1));
    if (!block.isValid())
        block = document()->lastBlock();
    setPosition(block.position() + qMax(0, qMin(block.length() - 2, col)));
    moveToTargetColumn();
}

void FakeVimHandler::Private::moveToEndOfLine()
{
    // Additionally select (in visual mode) or apply current command on hidden lines following
    // the current line.
    bool onlyVisibleLines = isVisualMode() || m_submode != NoSubMode;
    const int id = onlyVisibleLines ? lineNumber(block()) : block().blockNumber() + 1;
    setPosition(lastPositionInLine(id, onlyVisibleLines));
}

void FakeVimHandler::Private::moveBehindEndOfLine()
{
    emit q->fold(1, false);
    int pos = qMin(block().position() + block().length() - 1,
        lastPositionInDocument() + 1);
    setPosition(pos);
}

void FakeVimHandler::Private::moveToStartOfLine()
{
#if 0
    // does not work for "hidden" documents like in the autotests
    tc.movePosition(StartOfLine, MoveAnchor);
#else
    setPosition(block().position());
#endif
}

void FakeVimHandler::Private::fixSelection()
{
    if (m_rangemode == RangeBlockMode)
         return;

    if (m_movetype == MoveExclusive) {
        if (anchor() < position() && atBlockStart()) {
            // Exlusive motion ending at the beginning of line
            // becomes inclusive and end is moved to end of previous line.
            m_movetype = MoveInclusive;
            moveToStartOfLine();
            moveLeft();

            // Exclusive motion ending at the beginning of line and
            // starting at or before first non-blank on a line becomes linewise.
            if (anchor() < block().position() && isFirstNonBlankOnLine(anchor())) {
                m_movetype = MoveLineWise;
            }
        }
    }

    if (m_movetype == MoveLineWise)
        m_rangemode = (m_submode == ChangeSubMode)
            ? RangeLineModeExclusive
            : RangeLineMode;

    if (m_movetype == MoveInclusive) {
        if (anchor() <= position()) {
            if (!atBlockEnd())
                setPosition(position() + 1); // correction

            // Omit first character in selection if it's line break on non-empty line.
            int start = anchor();
            int end = position();
            if (afterEndOfLine(document(), start) && start > 0) {
                start = qMin(start + 1, end);
                if (m_submode == DeleteSubMode && !atDocumentEnd())
                    setAnchorAndPosition(start, end + 1);
                else
                    setAnchorAndPosition(start, end);
            }

            // If more than one line is selected and all are selected completely
            // movement becomes linewise.
            if (start < block().position() && isFirstNonBlankOnLine(start) && atBlockEnd()) {
                if (m_submode != ChangeSubMode) {
                    moveRight();
                    if (atEmptyLine())
                        moveRight();
                }
                m_movetype = MoveLineWise;
            }
        } else {
            setAnchorAndPosition(anchor() + 1, position());
        }
    }

    if (m_positionPastEnd) {
        const int anc = anchor();
        moveBehindEndOfLine();
        moveRight();
        setAnchorAndPosition(anc, position());
    }

    if (m_anchorPastEnd) {
        setAnchorAndPosition(anchor() + 1, position());
    }
}

void FakeVimHandler::Private::finishMovement(const QString &dotCommandMovement, int count)
{
    finishMovement(dotCommandMovement.arg(count));
}

void FakeVimHandler::Private::finishMovement(const QString &dotCommandMovement)
{
    //dump("FINISH MOVEMENT");
    if (m_submode == FilterSubMode) {
        int beginLine = lineForPosition(anchor());
        int endLine = lineForPosition(position());
        setPosition(qMin(anchor(), position()));
        enterExMode(QString(".,+%1!").arg(qAbs(endLine - beginLine)));
        return;
    }

    if (m_submode == ChangeSubMode
        || m_submode == DeleteSubMode
        || m_submode == YankSubMode
        || m_submode == InvertCaseSubMode
        || m_submode == DownCaseSubMode
        || m_submode == UpCaseSubMode) {
        if (m_submode != YankSubMode)
            beginEditBlock();

        fixSelection();

        if (m_submode != InvertCaseSubMode
            && m_submode != DownCaseSubMode
            && m_submode != UpCaseSubMode) {
            yankText(currentRange(), m_register);
            if (m_movetype == MoveLineWise)
                setRegister(m_register, registerContents(m_register), RangeLineMode);
        }

        m_positionPastEnd = m_anchorPastEnd = false;
    }

    QString dotCommand;
    if (m_submode == ChangeSubMode) {
        if (m_rangemode != RangeLineModeExclusive)
            setUndoPosition();
        removeText(currentRange());
        dotCommand = QString('c');
        if (m_movetype == MoveLineWise)
            insertAutomaticIndentation(true);
        endEditBlock();
        setTargetColumn();
        g.returnToMode = InsertMode;
    } else if (m_submode == DeleteSubMode) {
        setUndoPosition();
        removeText(currentRange());
        dotCommand = QString('d');
        if (m_movetype == MoveLineWise)
            handleStartOfLine();
        if (atEndOfLine())
            moveLeft();
        else
            setTargetColumn();
        endEditBlock();
    } else if (m_submode == YankSubMode) {
        setPosition(qMin(position(), anchor()));
        leaveVisualMode();
        if (anchor() <= position())
            setPosition(anchor());
    } else if (m_submode == InvertCaseSubMode
        || m_submode == UpCaseSubMode
        || m_submode == DownCaseSubMode) {
        if (m_submode == InvertCaseSubMode) {
            invertCase(currentRange());
            dotCommand = QString("g~");
        } else if (m_submode == DownCaseSubMode) {
            downCase(currentRange());
            dotCommand = QString("gu");
        } else if (m_submode == UpCaseSubMode) {
            upCase(currentRange());
            dotCommand = QString("gU");
        }
        if (m_movetype == MoveLineWise)
            handleStartOfLine();
        endEditBlock();
    } else if (m_submode == IndentSubMode
        || m_submode == ShiftRightSubMode
        || m_submode == ShiftLeftSubMode) {
        recordJump();
        setUndoPosition();
        if (m_submode == IndentSubMode) {
            indentSelectedText();
            dotCommand = QString('=');
        } else if (m_submode == ShiftRightSubMode) {
            shiftRegionRight(1);
            dotCommand = QString('>');
        } else if (m_submode == ShiftLeftSubMode) {
            shiftRegionLeft(1);
            dotCommand = QString('<');
        }
    }

    if (!dotCommand.isEmpty() && !dotCommandMovement.isEmpty())
        setDotCommand(dotCommand + dotCommandMovement);

    resetCommandMode();
}

void FakeVimHandler::Private::resetCommandMode()
{
    m_subsubmode = NoSubSubMode;
    m_submode = NoSubMode;
    m_movetype = MoveInclusive;
    m_mvcount.clear();
    m_opcount.clear();
    m_gflag = false;
    m_register = '"';
    //m_tc.clearSelection();
    m_rangemode = RangeCharMode;
    if (isNoVisualMode())
        setAnchor();
    g.currentCommand.clear();
    if (g.returnToMode != CommandMode) {
        if (g.returnToMode == InsertMode)
            enterInsertMode();
        else
            enterReplaceMode();
        moveToTargetColumn();
    }
}

void FakeVimHandler::Private::updateSelection()
{
    QList<QTextEdit::ExtraSelection> selections = m_extraSelections;
    if (hasConfig(ConfigShowMarks)) {
        for (MarksIterator it(m_marks); it.hasNext(); ) {
            it.next();
            QTextEdit::ExtraSelection sel;
            sel.cursor = cursor();
            setCursorPosition(&sel.cursor, it.value().position);
            sel.cursor.setPosition(sel.cursor.position(), MoveAnchor);
            sel.cursor.movePosition(Right, KeepAnchor);
            sel.format = cursor().blockCharFormat();
            sel.format.setForeground(Qt::blue);
            sel.format.setBackground(Qt::green);
            selections.append(sel);
        }
    }
    //qDebug() << "SELECTION: " << selections;
    emit q->selectionChanged(selections);
}

void FakeVimHandler::Private::updateHighlights()
{
    if (!hasConfig(ConfigUseCoreSearch))
        emit q->highlightMatches(m_oldNeedle);
}

void FakeVimHandler::Private::updateMiniBuffer()
{
    if (!m_textedit && !m_plaintextedit)
        return;

    QString msg;
    int cursorPos = -1;
    MessageLevel messageLevel = MessageMode;

    if (g.mapStates.last().silent && g.currentMessageLevel < MessageInfo)
        g.currentMessage.clear();

    if (m_passing) {
        msg = "PASSING";
    } else if (m_subsubmode == SearchSubSubMode) {
        msg = g.searchBuffer.display();
        if (g.mapStates.size() == 1)
            cursorPos = g.searchBuffer.cursorPos() + 1;
    } else if (m_mode == ExMode) {
        msg = g.commandBuffer.display();
        if (g.mapStates.size() == 1)
            cursorPos = g.commandBuffer.cursorPos() + 1;
    } else if (!g.currentMessage.isEmpty()) {
        msg = g.currentMessage;
        g.currentMessage.clear();
        messageLevel = g.currentMessageLevel;
    } else if (g.mapStates.size() > 1 && !g.mapStates.last().silent) {
        // Do not reset previous message when after running a mapped command.
        return;
    } else if (m_mode == CommandMode && !g.currentCommand.isEmpty() && hasConfig(ConfigShowCmd)) {
        msg = g.currentCommand;
        messageLevel = MessageShowCmd;
    } else if (m_mode == CommandMode && isVisualMode()) {
        if (isVisualCharMode()) {
            msg = "VISUAL";
        } else if (isVisualLineMode()) {
            msg = "VISUAL LINE";
        } else if (isVisualBlockMode()) {
            msg = "VISUAL BLOCK";
        }
    } else if (m_mode == InsertMode) {
        msg = "INSERT";
    } else if (m_mode == ReplaceMode) {
        msg = "REPLACE";
    } else {
        QTC_CHECK(m_mode == CommandMode && m_subsubmode != SearchSubSubMode);
        if (g.returnToMode == CommandMode)
            msg = "COMMAND";
        else if (g.returnToMode == InsertMode)
            msg = "(insert)";
        else
            msg = "(replace)";
    }

    emit q->commandBufferChanged(msg, cursorPos, messageLevel, q);

    int linesInDoc = linesInDocument();
    int l = cursorLine();
    QString status;
    const QString pos = QString::fromLatin1("%1,%2")
        .arg(l + 1).arg(physicalCursorColumn() + 1);
    // FIXME: physical "-" logical
    if (linesInDoc != 0) {
        status = FakeVimHandler::tr("%1%2%").arg(pos, -10).arg(l * 100 / linesInDoc, 4);
    } else {
        status = FakeVimHandler::tr("%1All").arg(pos, -10);
    }
    emit q->statusDataChanged(status);
}

void FakeVimHandler::Private::showMessage(MessageLevel level, const QString &msg)
{
    //qDebug() << "MSG: " << msg;
    g.currentMessage = msg;
    g.currentMessageLevel = level;
}

void FakeVimHandler::Private::notImplementedYet()
{
    qDebug() << "Not implemented in FakeVim";
    showMessage(MessageError, FakeVimHandler::tr("Not implemented in FakeVim"));
}

void FakeVimHandler::Private::passShortcuts(bool enable)
{
    m_passing = enable;
    updateMiniBuffer();
    if (enable)
        QCoreApplication::instance()->installEventFilter(q);
    else
        QCoreApplication::instance()->removeEventFilter(q);
}

bool FakeVimHandler::Private::handleCommandSubSubMode(const Input &input)
{
    //const int key = input.key;
    bool handled = true;
    if (m_subsubmode == FtSubSubMode) {
        m_semicolonType = m_subsubdata;
        m_semicolonKey = input.text();
        bool valid = handleFfTt(m_semicolonKey);
        m_subsubmode = NoSubSubMode;
        if (!valid) {
            m_submode = NoSubMode;
            resetCommandMode();
            handled = false;
        } else {
            finishMovement(QString("%1%2%3")
                           .arg(count())
                           .arg(m_semicolonType.text())
                           .arg(m_semicolonKey));
        }
    } else if (m_subsubmode == TextObjectSubSubMode) {
        bool ok = true;
        if (input.is('w'))
            selectWordTextObject(m_subsubdata.is('i'));
        else if (input.is('W'))
            selectWORDTextObject(m_subsubdata.is('i'));
        else if (input.is('s'))
            selectSentenceTextObject(m_subsubdata.is('i'));
        else if (input.is('p'))
            selectParagraphTextObject(m_subsubdata.is('i'));
        else if (input.is('[') || input.is(']'))
            ok = selectBlockTextObject(m_subsubdata.is('i'), '[', ']');
        else if (input.is('(') || input.is(')') || input.is('b'))
            ok = selectBlockTextObject(m_subsubdata.is('i'), '(', ')');
        else if (input.is('<') || input.is('>'))
            ok = selectBlockTextObject(m_subsubdata.is('i'), '<', '>');
        else if (input.is('{') || input.is('}') || input.is('B'))
            ok = selectBlockTextObject(m_subsubdata.is('i'), '{', '}');
        else if (input.is('"') || input.is('\'') || input.is('`'))
            ok = selectQuotedStringTextObject(m_subsubdata.is('i'), input.asChar());
        else
            ok = false;
        m_subsubmode = NoSubSubMode;
        if (ok) {
            finishMovement(QString("%1%2%3")
                           .arg(count())
                           .arg(m_subsubdata.text())
                           .arg(input.text()));
        } else {
            resetCommandMode();
            handled = false;
        }
    } else if (m_subsubmode == MarkSubSubMode) {
        setMark(input.asChar(), CursorPosition(cursor()));
        m_subsubmode = NoSubSubMode;
    } else if (m_subsubmode == BackTickSubSubMode
            || m_subsubmode == TickSubSubMode) {
        if (jumpToMark(input.asChar(), m_subsubmode == BackTickSubSubMode)) {
            finishMovement();
        } else {
            resetCommandMode();
            handled = false;
        }
        m_subsubmode = NoSubSubMode;
    } else {
        handled = false;
    }
    return handled;
}

bool FakeVimHandler::Private::handleOpenSquareSubMode(const Input &input)
{
    bool handled = true;
    m_submode = NoSubMode;
    if (input.is('{')) {
        searchBalanced(false, '{', '}');
    } else if (input.is('(')) {
        searchBalanced(false, '(', ')');
    } else {
        handled = false;
    }
    return handled;
}

bool FakeVimHandler::Private::handleCloseSquareSubMode(const Input &input)
{
    bool handled = true;
    m_submode = NoSubMode;
    if (input.is('}')) {
        searchBalanced(true, '}', '{');
    } else if (input.is(')')) {
        searchBalanced(true, ')', '(');
    } else {
        handled = false;
    }
    return handled;
}

bool FakeVimHandler::Private::handleMovement(const Input &input)
{
    bool handled = true;
    QString movement;
    int count = this->count();

    if (input.isDigit()) {
        if (input.is('0') && m_mvcount.isEmpty()) {
            m_movetype = MoveExclusive;
            moveToStartOfLine();
            setTargetColumn();
            count = 1;
        } else {
            m_mvcount.append(input.text());
            return true;
        }
    } else if (input.is('a') || input.is('i')) {
        m_subsubmode = TextObjectSubSubMode;
        m_subsubdata = input;
    } else if (input.is('^') || input.is('_')) {
        moveToFirstNonBlankOnLine();
        setTargetColumn();
        m_movetype = MoveExclusive;
    } else if (0 && input.is(',')) {
        // FIXME: fakevim uses ',' by itself, so it is incompatible
        m_subsubmode = FtSubSubMode;
        // HACK: toggle 'f' <-> 'F', 't' <-> 'T'
        //m_subsubdata = m_semicolonType ^ 32;
        handleFfTt(m_semicolonKey);
        m_subsubmode = NoSubSubMode;
    } else if (input.is(';')) {
        m_subsubmode = FtSubSubMode;
        m_subsubdata = m_semicolonType;
        handleFfTt(m_semicolonKey);
        m_subsubmode = NoSubSubMode;
    } else if (input.is('/') || input.is('?')) {
        g.lastSearchForward = input.is('/');
        if (hasConfig(ConfigUseCoreSearch)) {
            // re-use the core dialog.
            g.findPending = true;
            m_findStartPosition = position();
            m_movetype = MoveExclusive;
            setAnchor(); // clear selection: otherwise, search is restricted to selection
            emit q->findRequested(!g.lastSearchForward);
        } else {
            // FIXME: make core find dialog sufficiently flexible to
            // produce the "default vi" behaviour too. For now, roll our own.
            g.currentMessage.clear();
            m_movetype = MoveExclusive;
            m_subsubmode = SearchSubSubMode;
            g.searchBuffer.setPrompt(g.lastSearchForward ? '/' : '?');
            m_searchStartPosition = position();
            m_searchFromScreenLine = firstVisibleLine();
            m_searchCursor = QTextCursor();
            g.searchBuffer.clear();
        }
    } else if (input.is('`')) {
        m_subsubmode = BackTickSubSubMode;
    } else if (input.is('#') || input.is('*')) {
        // FIXME: That's not proper vim behaviour
        QString needle;
        QTextCursor tc = cursor();
        tc.select(QTextCursor::WordUnderCursor);
        needle = QRegExp::escape(tc.selection().toPlainText());
        if (!m_gflag)
            needle = "\\<" + needle + "\\>";
        setAnchorAndPosition(tc.position(), tc.anchor());
        g.searchBuffer.historyPush(needle);
        g.lastSearch = needle;
        g.lastSearchForward = input.is('*');
        searchNext();
    } else if (input.is('\'')) {
        m_subsubmode = TickSubSubMode;
        if (m_submode != NoSubMode)
            m_movetype = MoveLineWise;
    } else if (input.is('|')) {
        moveToStartOfLine();
        moveRight(qMin(count, rightDist()) - 1);
        setTargetColumn();
    } else if (input.isReturn()) {
        moveToStartOfLine();
        moveDown();
        moveToFirstNonBlankOnLine();
        m_movetype = MoveLineWise;
    } else if (input.is('-')) {
        moveToStartOfLine();
        moveUp(count);
        moveToFirstNonBlankOnLine();
        m_movetype = MoveLineWise;
    } else if (input.is('+')) {
        moveToStartOfLine();
        moveDown(count);
        moveToFirstNonBlankOnLine();
        m_movetype = MoveLineWise;
    } else if (input.isKey(Key_Home)) {
        moveToStartOfLine();
        setTargetColumn();
        movement = "<HOME>";
    } else if (input.is('$') || input.isKey(Key_End)) {
        if (count > 1)
            moveDown(count - 1);
        moveToEndOfLine();
        m_movetype = MoveInclusive;
        setTargetColumn();
        if (m_submode == NoSubMode)
            m_targetColumn = -1;
        if (isVisualMode())
            m_visualTargetColumn = -1;
        movement = "$";
    } else if (input.is('%')) {
        recordJump();
        if (count == 1) {
            moveToMatchingParanthesis();
        } else {
            // set cursor position in percentage - formula taken from Vim help
            setPosition(firstPositionInLine((count * linesInDocument() + 99) / 100));
            moveToTargetColumn();
            handleStartOfLine();
        }
    } else if (input.is('b') || input.isShift(Key_Left)) {
        m_movetype = MoveExclusive;
        moveToNextWordStart(count, false, false);
        setTargetColumn();
        movement = "b";
    } else if (input.is('B')) {
        m_movetype = MoveExclusive;
        moveToNextWordStart(count, true, false);
        setTargetColumn();
    } else if (input.is('e') && m_gflag) {
        m_movetype = MoveInclusive;
        moveToNextWordEnd(count, false, false);
        setTargetColumn();
    } else if (input.is('e') || input.isShift(Key_Right)) {
        m_movetype = MoveInclusive;
        moveToNextWordEnd(count, false, true, false);
        setTargetColumn();
        movement = "e";
    } else if (input.is('E') && m_gflag) {
        m_movetype = MoveInclusive;
        moveToNextWordEnd(count, true, false);
        setTargetColumn();
    } else if (input.is('E')) {
        m_movetype = MoveInclusive;
        moveToNextWordEnd(count, true, true, false);
        setTargetColumn();
    } else if (input.isControl('e')) {
        // FIXME: this should use the "scroll" option, and "count"
        if (cursorLineOnScreen() == 0)
            moveDown(1);
        scrollDown(1);
        movement = "<C-E>";
    } else if (input.is('f')) {
        m_subsubmode = FtSubSubMode;
        m_movetype = MoveInclusive;
        m_subsubdata = input;
    } else if (input.is('F')) {
        m_subsubmode = FtSubSubMode;
        m_movetype = MoveExclusive;
        m_subsubdata = input;
    } else if (!m_gflag && input.is('g')) {
        m_gflag = true;
        return true;
    } else if (input.is('g') || input.is('G')) {
        QString dotCommand = QString("%1G").arg(count);
        recordJump();
        if (input.is('G') && m_mvcount.isEmpty())
            dotCommand = QString(QLatin1Char('G'));
        int n = (input.is('g')) ? 1 : linesInDocument();
        n = m_mvcount.isEmpty() ? n : count;
        if (m_submode == NoSubMode || m_submode == ZSubMode
                || m_submode == CapitalZSubMode || m_submode == RegisterSubMode) {
            setPosition(firstPositionInLine(n, false));
            handleStartOfLine();
        } else {
            m_movetype = MoveLineWise;
            m_rangemode = RangeLineMode;
            setAnchor();
            setPosition(firstPositionInLine(n, false));
        }
        setTargetColumn();
    } else if (input.is('h') || input.isKey(Key_Left) || input.isBackspace()) {
        m_movetype = MoveExclusive;
        int n = qMin(count, leftDist());
        if (m_fakeEnd && block().length() > 1)
            ++n;
        moveLeft(n);
        setTargetColumn();
        movement = "h";
    } else if (input.is('H')) {
        setCursor(EDITOR(cursorForPosition(QPoint(0, 0))));
        moveDown(qMax(count - 1, 0));
        handleStartOfLine();
    } else if (input.is('j') || input.isKey(Key_Down)
            || input.isControl('j') || input.isControl('n')) {
        m_movetype = MoveLineWise;
        moveDown(count);
        movement = "j";
    } else if (input.is('k') || input.isKey(Key_Up) || input.isControl('p')) {
        m_movetype = MoveLineWise;
        moveUp(count);
        movement = "k";
    } else if (input.is('l') || input.isKey(Key_Right) || input.is(' ')) {
        m_movetype = MoveExclusive;
        bool pastEnd = count >= rightDist() - 1;
        moveRight(qMax(0, qMin(count, rightDist() - (m_submode == NoSubMode))));
        setTargetColumn();
        if (pastEnd && isVisualMode())
            m_visualTargetColumn = -1;
    } else if (input.is('L')) {
        QTextCursor tc = EDITOR(cursorForPosition(QPoint(0, EDITOR(height()))));
        setCursor(tc);
        moveUp(qMax(count, 1));
        handleStartOfLine();
    } else if (m_gflag && input.is('m')) {
        moveToStartOfLine();
        moveRight(qMin(columnsOnScreen() / 2, rightDist()) - 1);
        setTargetColumn();
    } else if (input.is('M')) {
        QTextCursor tc = EDITOR(cursorForPosition(QPoint(0, EDITOR(height()) / 2)));
        setCursor(tc);
        handleStartOfLine();
    } else if (input.is('n') || input.is('N')) {
        if (hasConfig(ConfigUseCoreSearch)) {
            bool forward = (input.is('n')) ? g.lastSearchForward : !g.lastSearchForward;
            int pos = position();
            emit q->findNextRequested(!forward);
            if (forward && pos == cursor().selectionStart()) {
                // if cursor is already positioned at the start of a find result, this is returned
                emit q->findNextRequested(false);
            }
            setPosition(cursor().selectionStart());
        } else {
            searchNext(input.is('n'));
        }
    } else if (input.is('t')) {
        m_movetype = MoveInclusive;
        m_subsubmode = FtSubSubMode;
        m_subsubdata = input;
    } else if (input.is('T')) {
        m_movetype = MoveExclusive;
        m_subsubmode = FtSubSubMode;
        m_subsubdata = input;
    } else if (input.is('w') || input.is('W')) { // tested
        // Special case: "cw" and "cW" work the same as "ce" and "cE" if the
        // cursor is on a non-blank - except if the cursor is on the last
        // character of a word: only the current word will be changed
        bool simple = input.is('W');
        if (m_submode == ChangeSubMode) {
            moveToWordEnd(count, simple, true);
            m_movetype = MoveInclusive;
        } else {
            moveToNextWordStart(count, simple, true);
            m_movetype = MoveExclusive;
        }
        setTargetColumn();
    } else if (input.is('[')) {
        m_submode = OpenSquareSubMode;
    } else if (input.is(']')) {
        m_submode = CloseSquareSubMode;
    } else if (input.isKey(Key_PageDown) || input.isControl('f')) {
        moveDown(count * (linesOnScreen() - 2) - cursorLineOnScreen());
        scrollToLine(cursorLine());
        handleStartOfLine();
        movement = "f";
    } else if (input.isKey(Key_PageUp) || input.isControl('b')) {
        moveUp(count * (linesOnScreen() - 2) + cursorLineOnScreen());
        scrollToLine(cursorLine() + linesOnScreen() - 2);
        handleStartOfLine();
        movement = "b";
    } else if (input.isKey(Key_BracketLeft) || input.isKey(Key_BracketRight)) {

    } else {
        handled = false;
    }

    if (handled && m_subsubmode == NoSubSubMode) {
        if (m_submode == NoSubMode) {
            resetCommandMode();
        } else {
            // finish movement for sub modes
            const QString dotMovement =
                (count > 1 ? QString::number(count) : QString())
                + (m_gflag ? "g" : "")\
                + (movement.isNull() ? QString(input.asChar()) : movement);
            finishMovement(dotMovement);
            setTargetColumn();
        }
    }

    return handled;
}

EventResult FakeVimHandler::Private::handleCommandMode(const Input &input)
{
    bool handled = false;

    bool clearGflag = m_gflag;
    bool clearRegister = m_submode != RegisterSubMode;
    bool clearCount = m_submode != RegisterSubMode && !input.isDigit();

    // Process input for a sub-mode.
    if (input.isEscape()) {
        handled = handleEscape();
    } else if (m_subsubmode != NoSubSubMode) {
        handled = handleCommandSubSubMode(input);
    } else if (m_submode == NoSubMode) {
        handled = handleNoSubMode(input);
    } else if (m_submode == ChangeSubMode || m_submode == DeleteSubMode) {
        handled = handleChangeDeleteSubModes(input);
    } else if (m_submode == ReplaceSubMode) {
        handled = handleReplaceSubMode(input);
    } else if (m_submode == FilterSubMode) {
        handled = handleFilterSubMode(input);
    } else if (m_submode == RegisterSubMode) {
        handled = handleRegisterSubMode(input);
    } else if (m_submode == WindowSubMode) {
        handled = handleWindowSubMode(input);
    } else if (m_submode == YankSubMode) {
        handled = handleYankSubMode(input);
    } else if (m_submode == ZSubMode) {
        handled = handleZSubMode(input);
    } else if (m_submode == CapitalZSubMode) {
        handled = handleCapitalZSubMode(input);
    } else if (m_submode == OpenSquareSubMode) {
        handled = handleOpenSquareSubMode(input);
    } else if (m_submode == CloseSquareSubMode) {
        handled = handleCloseSquareSubMode(input);
    } else if (m_submode == ShiftLeftSubMode
        || m_submode == ShiftRightSubMode
        || m_submode == IndentSubMode) {
        handled = handleShiftSubMode(input);
    } else if (m_submode == InvertCaseSubMode
        || m_submode == DownCaseSubMode
        || m_submode == UpCaseSubMode) {
        handled = handleChangeCaseSubMode(input);
    }

    // Clear state and display incomplete command if necessary.
    if (handled) {
        bool noMode =
            (m_mode == CommandMode && m_submode == NoSubMode && m_subsubmode == NoSubSubMode);
        clearCount = clearCount && noMode && !m_gflag;
        if (clearCount && clearRegister) {
            resetCommandMode();
        } else {
            // Use gflag only for next input.
            if (clearGflag)
                m_gflag = false;
            // Clear [count] and [register] if its no longer needed.
            if (clearCount) {
                m_mvcount.clear();
                m_opcount.clear();
            }
            // Show or clear current command on minibuffer (showcmd).
            if (input.isEscape() || m_mode != CommandMode || clearCount)
                g.currentCommand.clear();
            else
                g.currentCommand.append(input.toString());
        }
    } else {
        resetCommandMode();
        //qDebug() << "IGNORED IN COMMAND MODE: " << key << text
        //    << " VISUAL: " << m_visualMode;

        // if a key which produces text was pressed, don't mark it as unhandled
        // - otherwise the text would be inserted while being in command mode
        if (input.text().isEmpty()) {
            handled = EventUnhandled;
        }
    }

    updateMiniBuffer();

    m_positionPastEnd = (m_visualTargetColumn == -1) && isVisualMode();

    return handled ? EventHandled : EventCancelled;
}

bool FakeVimHandler::Private::handleEscape()
{
    if (isVisualMode())
        leaveVisualMode();
    resetCommandMode();
    return true;
}

bool FakeVimHandler::Private::handleNoSubMode(const Input &input)
{
    bool handled = true;

    if (input.is('&')) {
        handleExCommand(m_gflag ? "%s//~/&" : "s");
    } else if (input.is(':')) {
        enterExMode();
    } else if (input.is('!') && isNoVisualMode()) {
        m_submode = FilterSubMode;
    } else if (input.is('!') && isVisualMode()) {
        enterExMode(QString("!"));
    } else if (input.is('"')) {
        m_submode = RegisterSubMode;
    } else if (input.is(',')) {
        passShortcuts(true);
    } else if (input.is('.')) {
        //qDebug() << "REPEATING" << quoteUnprintable(g.dotCommand) << count()
        //    << input;
        QString savedCommand = g.dotCommand;
        g.dotCommand.clear();
        replay(savedCommand);
        resetCommandMode();
        g.dotCommand = savedCommand;
    } else if (input.is('<') || input.is('>') || input.is('=')) {
        if (isNoVisualMode()) {
            if (input.is('<'))
                m_submode = ShiftLeftSubMode;
            else if (input.is('>'))
                m_submode = ShiftRightSubMode;
            else
                m_submode = IndentSubMode;
            setAnchor();
        } else {
            leaveVisualMode();
            const int lines = qAbs(lineForPosition(position()) - lineForPosition(anchor())) + 1;
            const int repeat = count();
            if (input.is('<'))
                shiftRegionLeft(repeat);
            else if (input.is('>'))
                shiftRegionRight(repeat);
            else
                indentSelectedText();
            const QString selectDotCommand =
                    (lines > 1) ? QString("V%1j").arg(lines - 1): QString();
            setDotCommand(selectDotCommand + QString("%1%2%2").arg(repeat).arg(input.raw()));
        }
    } else if ((!isVisualMode() && input.is('a')) || (isVisualMode() && input.is('A'))) {
        leaveVisualMode();
        breakEditBlock();
        enterInsertMode();
        setDotCommand("%1a", count());
        m_lastInsertion.clear();
        if (!atEndOfLine())
            moveRight();
        setUndoPosition();
    } else if (input.is('A')) {
        breakEditBlock();
        moveBehindEndOfLine();
        setUndoPosition();
        setAnchor();
        enterInsertMode();
        setDotCommand("%1A", count());
        m_lastInsertion.clear();
    } else if (input.isControl('a')) {
        changeNumberTextObject(count());
        setDotCommand("%1<c-a>", count());
    } else if ((input.is('c') || input.is('d')) && isNoVisualMode()) {
        setAnchor();
        m_opcount = m_mvcount;
        m_mvcount.clear();
        m_movetype = MoveExclusive;
        m_submode = input.is('c') ? ChangeSubMode : DeleteSubMode;
    } else if ((input.is('c') || input.is('C') || input.is('s') || input.is('R'))
          && (isVisualCharMode() || isVisualLineMode())) {
        setDotCommand(visualDotCommand() + input.asChar());
        if ((input.is('c')|| input.is('s')) && isVisualCharMode()) {
            leaveVisualMode();
            m_rangemode = RangeCharMode;
        } else {
            leaveVisualMode();
            m_rangemode = RangeLineMode;
            // leaveVisualMode() has set this to MoveInclusive for visual character mode
            m_movetype =  MoveLineWise;
        }
        m_submode = ChangeSubMode;
        finishMovement();
    } else if (input.is('C')) {
        setAnchor();
        moveToEndOfLine();
        m_submode = ChangeSubMode;
        setDotCommand(QString(QLatin1Char('C')));
        finishMovement();
    } else if (input.isControl('c')) {
        if (isNoVisualMode())
            showMessage(MessageInfo, "Type Alt-v,Alt-v  to quit FakeVim mode");
        else
            leaveVisualMode();
    } else if ((input.is('d') || input.is('x') || input.isKey(Key_Delete))
            && isVisualMode()) {
        setUndoPosition();
        setDotCommand(visualDotCommand() + 'x');
        if (isVisualCharMode()) {
            leaveVisualMode();
            m_submode = DeleteSubMode;
            finishMovement();
        } else if (isVisualLineMode()) {
            leaveVisualMode();
            m_rangemode = RangeLineMode;
            yankText(currentRange(), m_register);
            removeText(currentRange());
            handleStartOfLine();
        } else if (isVisualBlockMode()) {
            leaveVisualMode();
            m_rangemode = RangeBlockMode;
            yankText(currentRange(), m_register);
            removeText(currentRange());
            setPosition(qMin(position(), anchor()));
        }
    } else if (input.is('D') && isNoVisualMode()) {
        setUndoPosition();
        if (atEndOfLine())
            moveLeft();
        m_submode = DeleteSubMode;
        m_movetype = MoveInclusive;
        setAnchorAndPosition(position(), lastPositionInLine(cursorLine() + count()));
        setDotCommand(QString(QLatin1Char('D')));
        finishMovement();
        setTargetColumn();
    } else if ((input.is('D') || input.is('X')) &&
         (isVisualCharMode() || isVisualLineMode())) {
        setDotCommand(visualDotCommand() + 'X');
        leaveVisualMode();
        m_rangemode = RangeLineMode;
        m_submode = NoSubMode;
        yankText(currentRange(), m_register);
        removeText(currentRange());
        moveToFirstNonBlankOnLine();
    } else if ((input.is('D') || input.is('X')) && isVisualBlockMode()) {
        setDotCommand(visualDotCommand() + 'X');
        leaveVisualMode();
        m_rangemode = RangeBlockAndTailMode;
        yankText(currentRange(), m_register);
        removeText(currentRange());
        setPosition(qMin(position(), anchor()));
    } else if (input.isControl('d')) {
        int sline = cursorLineOnScreen();
        // FIXME: this should use the "scroll" option, and "count"
        moveDown(linesOnScreen() / 2);
        handleStartOfLine();
        scrollToLine(cursorLine() - sline);
    } else if (!m_gflag && input.is('g')) {
        m_gflag = true;
    } else if (!isVisualMode() && (input.is('i') || input.isKey(Key_Insert))) {
        setDotCommand("%1i", count());
        breakEditBlock();
        enterInsertMode();
        if (atEndOfLine())
            moveLeft();
    } else if (input.is('I')) {
        setUndoPosition();
        setDotCommand("%1I", count());
        if (isVisualMode()) {
            int beginLine = lineForPosition(anchor());
            int endLine = lineForPosition(position());
            m_visualInsertCount = qAbs(endLine - beginLine);
            setPosition(qMin(position(), anchor()));
        } else {
            if (m_gflag)
                moveToStartOfLine();
            else
                moveToFirstNonBlankOnLine();
            //m_tc.clearSelection();
        }
        breakEditBlock();
        enterInsertMode();
    } else if (input.isControl('i')) {
        jump(count());
    } else if (input.is('J')) {
        moveBehindEndOfLine();
        const int pos = position();
        beginEditBlock();
        if (m_submode == NoSubMode)
            joinLines(count(), m_gflag);
        endEditBlock();
        setPosition(pos);
        setDotCommand("%1J", count());
    } else if (input.isControl('l')) {
        // screen redraw. should not be needed
    } else if (input.is('m')) {
        m_subsubmode = MarkSubSubMode;
    } else if (isVisualMode() && (input.is('o') || input.is('O'))) {
        int pos = position();
        setAnchorAndPosition(pos, anchor());
        std::swap(m_positionPastEnd, m_anchorPastEnd);
        setTargetColumn();
        if (m_positionPastEnd)
            m_visualTargetColumn = -1;
    } else if (input.is('o') || input.is('O')) {
        bool insertAfter = input.is('o');
        setDotCommand(insertAfter ? "%1o" : "%1O", count());
        setUndoPosition();
        enterInsertMode();
        // Insert new line so that command can be repeated [count] times inserting new line
        // each time without unfolding any lines.
        QTextBlock block = cursor().block();
        bool appendLine = false;
        if (insertAfter) {
            const int line = lineNumber(block);
            appendLine = line >= document()->lineCount();
            setPosition(appendLine ? lastPositionInLine(line) : firstPositionInLine(line + 1));
        } else {
            setPosition(block.position());
        }
        beginEditBlock();
        insertText(QString("\n"));
        m_lastInsertion += '\n';
        if (!appendLine)
            moveUp();
        insertAutomaticIndentation(insertAfter);
        setTargetColumn();
        endEditBlock();
    } else if (input.isControl('o')) {
        jump(-count());
    } else if (input.is('p') || input.is('P')) {
        pasteText(input.is('p'));
        setTargetColumn();
        setDotCommand("%1p", count());
        finishMovement();
    } else if (input.is('r')) {
        m_submode = ReplaceSubMode;
    } else if (!isVisualMode() && input.is('R')) {
        setUndoPosition();
        breakEditBlock();
        enterReplaceMode();
    } else if (input.isControl('r')) {
        int repeat = count();
        while (--repeat >= 0)
            redo();
    } else if (input.is('s') && isVisualBlockMode()) {
        setUndoPosition();
        Range range(position(), anchor(), RangeBlockMode);
        int beginLine = lineForPosition(anchor());
        int endLine = lineForPosition(position());
        m_visualInsertCount = qAbs(endLine - beginLine);
        setPosition(qMin(position(), anchor()));
        yankText(range, m_register);
        removeText(range);
        setDotCommand("%1s", count());
        breakEditBlock();
        enterInsertMode();
    } else if (input.is('s')) {
        setUndoPosition();
        leaveVisualMode();
        if (atEndOfLine())
            moveLeft();
        setAnchor();
        moveRight(qMin(count(), rightDist()));
        setDotCommand("%1s", count());
        m_submode = ChangeSubMode;
        finishMovement();
    } else if (input.is('S')) {
        m_movetype = MoveLineWise;
        setUndoPosition();
        if (!isVisualMode()) {
            const int line = cursorLine() + 1;
            const int anc = firstPositionInLine(line);
            const int pos = lastPositionInLine(line + count() - 1);
            setAnchorAndPosition(anc, pos);
        }
        setDotCommand("%1S", count());
        m_submode = ChangeSubMode;
        finishMovement();
    } else if (m_gflag && input.is('t')) {
        handleExCommand("tabnext");
    } else if (m_gflag && input.is('T')) {
        handleExCommand("tabprev");
    } else if (input.isControl('t')) {
        handleExCommand("pop");
    } else if (!m_gflag && input.is('u') && !isVisualMode()) {
        int repeat = count();
        while (--repeat >= 0)
            undo();
    } else if (input.isControl('u')) {
        int sline = cursorLineOnScreen();
        // FIXME: this should use the "scroll" option, and "count"
        moveUp(linesOnScreen() / 2);
        handleStartOfLine();
        scrollToLine(cursorLine() - sline);
    } else if (m_gflag && input.is('v')) {
        if (m_lastVisualMode != NoVisualMode) {
            CursorPosition from = mark('<').position;
            CursorPosition to = mark('>').position;
            toggleVisualMode(m_lastVisualMode);
            setCursorPosition(from);
            setAnchor();
            setCursorPosition(to);
        }
    } else if (input.is('v')) {
        toggleVisualMode(VisualCharMode);
    } else if (input.is('V')) {
        toggleVisualMode(VisualLineMode);
    } else if (input.isControl('v')) {
        toggleVisualMode(VisualBlockMode);
    } else if (input.isControl('w')) {
        m_submode = WindowSubMode;
    } else if (input.is('x') && isNoVisualMode()) { // = "dl"
        m_movetype = MoveExclusive;
        m_submode = DeleteSubMode;
        const int n = qMin(count(), rightDist());
        setAnchorAndPosition(position(), position() + n);
        setDotCommand("%1x", count());
        finishMovement();
    } else if (input.isControl('x')) {
        changeNumberTextObject(-count());
        setDotCommand("%1<c-x>", count());
    } else if (input.is('X')) {
        if (leftDist() > 0) {
            setAnchor();
            moveLeft(qMin(count(), leftDist()));
            yankText(currentRange(), m_register);
            removeText(currentRange());
        }
    } else if (input.is('Y') && isNoVisualMode())  {
        handleYankSubMode(Input('y'));
    } else if (input.isControl('y')) {
        // FIXME: this should use the "scroll" option, and "count"
        if (cursorLineOnScreen() == linesOnScreen() - 1)
            moveUp(1);
        scrollUp(1);
    } else if (input.is('y') && isNoVisualMode()) {
        setAnchor();
        m_movetype = MoveExclusive;
        m_submode = YankSubMode;
    } else if (input.is('y') && isVisualCharMode()) {
        m_rangemode = RangeCharMode;
        m_movetype = MoveInclusive;
        m_submode = YankSubMode;
        finishMovement();
    } else if ((input.is('y') && isVisualLineMode())
            || (input.is('Y') && isVisualLineMode())
            || (input.is('Y') && isVisualCharMode())) {
        m_rangemode = RangeLineMode;
        m_submode = YankSubMode;
        finishMovement();
    } else if ((input.is('y') || input.is('Y')) && isVisualBlockMode()) {
        m_rangemode = RangeBlockMode;
        m_movetype = MoveInclusive;
        m_submode = YankSubMode;
        finishMovement();
    } else if (input.is('z')) {
        m_submode = ZSubMode;
    } else if (input.is('Z')) {
        m_submode = CapitalZSubMode;
    } else if ((input.is('~') || input.is('u') || input.is('U'))) {
        m_movetype = MoveExclusive;
        if (isVisualMode()) {
            if (isVisualLineMode())
                m_rangemode = RangeLineMode;
            else if (isVisualBlockMode())
                m_rangemode = RangeBlockMode;
            leaveVisualMode();
            if (input.is('~'))
                m_submode = InvertCaseSubMode;
            else if (input.is('u'))
                m_submode = DownCaseSubMode;
            else if (input.is('U'))
                m_submode = UpCaseSubMode;
            finishMovement();
        } else if (m_gflag) {
            setUndoPosition();
            if (atEndOfLine())
                moveLeft();
            setAnchor();
            if (input.is('~'))
                m_submode = InvertCaseSubMode;
            else if (input.is('u'))
                m_submode = DownCaseSubMode;
            else if (input.is('U'))
                m_submode = UpCaseSubMode;
        } else {
            if (!atEndOfLine()) {
                beginEditBlock();
                setAnchor();
                moveRight(qMin(count(), rightDist()));
                if (input.is('~'))
                    invertCase(currentRange());
                else if (input.is('u'))
                    downCase(currentRange());
                else if (input.is('U'))
                    upCase(currentRange());
                setDotCommand(QString::fromLatin1("%1%2").arg(count()).arg(input.raw()));
                endEditBlock();
            }
        }
    } else if (input.isKey(Key_Delete)) {
        setAnchor();
        moveRight(qMin(1, rightDist()));
        removeText(currentRange());
        if (atEndOfLine())
            moveLeft();
    } else if (input.isControl(Key_BracketRight)) {
        handleExCommand("tag");
    } else if (handleMovement(input)) {
        // movement handled
    } else {
        handled = false;
    }

    return handled;
}

bool FakeVimHandler::Private::handleChangeDeleteSubModes(const Input &input)
{
    bool handled = false;

    if ((m_submode == ChangeSubMode && input.is('c'))
        || (m_submode == DeleteSubMode && input.is('d'))) {
        m_movetype = MoveLineWise;
        setUndoPosition();
        const int line = cursorLine() + 1;
        const int anc = firstPositionInLine(line);
        const int pos = lastPositionInLine(line + count() - 1);
        setAnchorAndPosition(anc, pos);
        if (m_submode == ChangeSubMode) {
            m_lastInsertion.clear();
            setDotCommand("%1cc", count());
        } else {
            setDotCommand("%1dd", count());
        }
        finishMovement();
        m_submode = NoSubMode;
        handled = true;
    } else {
        handled = handleMovement(input);
    }

    return handled;
}

bool FakeVimHandler::Private::handleReplaceSubMode(const Input &input)
{
    bool handled = true;

    setDotCommand(visualDotCommand() + 'r' + input.asChar());
    if (isVisualMode()) {
        setUndoPosition();
        if (isVisualLineMode())
            m_rangemode = RangeLineMode;
        else if (isVisualBlockMode())
            m_rangemode = RangeBlockMode;
        else
            m_rangemode = RangeCharMode;
        leaveVisualMode();
        Range range = currentRange();
        if (m_rangemode == RangeCharMode)
            ++range.endPos;
        Transformation tr =
                &FakeVimHandler::Private::replaceByCharTransform;
        transformText(range, tr, input.asChar());
    } else if (count() <= rightDist()) {
        setUndoPosition();
        setAnchor();
        moveRight(count());
        Range range = currentRange();
        if (input.isReturn()) {
            beginEditBlock();
            replaceText(range, QString());
            insertText(QString("\n"));
            endEditBlock();
        } else {
            replaceText(range, QString(count(), input.asChar()));
            moveRight(count() - 1);
        }
        setTargetColumn();
        setDotCommand("%1r" + input.text(), count());
    } else {
        handled = false;
    }
    m_submode = NoSubMode;
    finishMovement();

    return handled;
}

bool FakeVimHandler::Private::handleFilterSubMode(const Input &input)
{
    return handleMovement(input);
}

bool FakeVimHandler::Private::handleRegisterSubMode(const Input &input)
{
    bool handled = false;

    QChar reg = input.asChar();
    if (QString("*+.%#:-\"").contains(reg) || reg.isLetterOrNumber()) {
        m_register = reg.unicode();
        m_rangemode = RangeLineMode;
        handled = true;
    }
    m_submode = NoSubMode;

    return handled;
}

bool FakeVimHandler::Private::handleShiftSubMode(const Input &input)
{
    bool handled = false;
    if ((m_submode == ShiftLeftSubMode && input.is('<'))
        || (m_submode == ShiftRightSubMode && input.is('>'))
        || (m_submode == IndentSubMode && input.is('='))) {
        m_movetype = MoveLineWise;
        setUndoPosition();
        moveDown(count() - 1);
        setDotCommand(QString("%2%1%1").arg(input.asChar()), count());
        finishMovement();
        handled = true;
        m_submode = NoSubMode;
    } else {
        handled = handleMovement(input);
    }
    return handled;
}

bool FakeVimHandler::Private::handleChangeCaseSubMode(const Input &input)
{
    bool handled = false;
    if ((m_submode == InvertCaseSubMode && input.is('~'))
        || (m_submode == DownCaseSubMode && input.is('u'))
        || (m_submode == UpCaseSubMode && input.is('U'))) {
        if (!isFirstNonBlankOnLine(position())) {
            moveToStartOfLine();
            moveToFirstNonBlankOnLine();
        }
        setTargetColumn();
        setUndoPosition();
        setAnchor();
        setPosition(lastPositionInLine(cursorLine() + count()) + 1);
        finishMovement(QString("%1%2").arg(count()).arg(input.raw()));
        handled = true;
        m_submode = NoSubMode;
    } else {
        handled = handleMovement(input);
    }
    return handled;
}

bool FakeVimHandler::Private::handleWindowSubMode(const Input &input)
{
    emit q->windowCommandRequested(input.key());
    m_submode = NoSubMode;
    return EventHandled;
}

bool FakeVimHandler::Private::handleYankSubMode(const Input &input)
{
    bool handled = false;
    if (input.is('y')) {
        m_movetype = MoveLineWise;
        int endPos = firstPositionInLine(lineForPosition(position()) + count() - 1);
        Range range(position(), endPos, RangeLineMode);
        yankText(range);
        m_submode = NoSubMode;
        handled = true;
    } else {
        handled = handleMovement(input);
    }
    return handled;
}

bool FakeVimHandler::Private::handleZSubMode(const Input &input)
{
    bool handled = true;
    bool foldMaybeClosed = false;
    if (input.isReturn() || input.is('t')
        || input.is('-') || input.is('b')
        || input.is('.') || input.is('z')) {
        // Cursor line to top/center/bottom of window.
        Qt::AlignmentFlag align;
        if (input.isReturn() || input.is('t'))
            align = Qt::AlignTop;
        else if (input.is('.') || input.is('z'))
            align = Qt::AlignBottom;
        else
            align = Qt::AlignVCenter;
        const bool moveToNonBlank = (input.is('.') || input.isReturn() || input.is('-'));
        const int line = m_mvcount.isEmpty() ? -1 : firstPositionInLine(count());
        alignViewportToCursor(align, line, moveToNonBlank);
        finishMovement();
    } else if (input.is('o') || input.is('c')) {
        // Open/close current fold.
        foldMaybeClosed = input.is('c');
        emit q->fold(count(), foldMaybeClosed);
        finishMovement();
    } else if (input.is('O') || input.is('C')) {
        // Recursively open/close current fold.
        foldMaybeClosed = input.is('C');
        emit q->fold(-1, foldMaybeClosed);
        finishMovement();
    } else if (input.is('a') || input.is('A')) {
        // Toggle current fold.
        foldMaybeClosed = true;
        emit q->foldToggle(input.is('a') ? count() : -1);
        finishMovement();
    } else if (input.is('R') || input.is('M')) {
        // Open/close all folds in document.
        foldMaybeClosed = input.is('M');
        emit q->foldAll(foldMaybeClosed);
        finishMovement();
    } else {
        handled = false;
    }
    if (foldMaybeClosed)
        ensureCursorVisible();
    m_submode = NoSubMode;
    return handled;
}

bool FakeVimHandler::Private::handleCapitalZSubMode(const Input &input)
{
    // Recognize ZZ and ZQ as aliases for ":x" and ":q!".
    bool handled = true;
    if (input.is('Z'))
        handleExCommand(QString(QLatin1Char('x')));
    else if (input.is('Q'))
        handleExCommand("q!");
    else
        handled = false;
    m_submode = NoSubMode;
    return handled;
}

EventResult FakeVimHandler::Private::handleReplaceMode(const Input &input)
{
    if (input.isEscape()) {
        moveLeft(qMin(1, leftDist()));
        setTargetColumn();
        enterCommandMode();
    } else if (input.isKey(Key_Left)) {
        breakEditBlock();
        moveLeft(1);
        setTargetColumn();
    } else if (input.isKey(Key_Right)) {
        breakEditBlock();
        moveRight(1);
        setTargetColumn();
    } else if (input.isKey(Key_Up)) {
        breakEditBlock();
        moveUp(1);
        setTargetColumn();
    } else if (input.isKey(Key_Down)) {
        breakEditBlock();
        moveDown(1);
    } else if (input.isKey(Key_Insert)) {
        m_mode = InsertMode;
    } else if (input.isControl('o')) {
        enterCommandMode(ReplaceMode);
    } else {
        joinPreviousEditBlock();
        if (!atEndOfLine()) {
            setAnchor();
            moveRight();
            m_lastDeletion += selectText(Range(position(), anchor()));
            removeText(currentRange());
        }
        const QString text = input.text();
        m_lastInsertion += text;
        setAnchor();
        insertText(text);
        endEditBlock();
        setTargetColumn();
    }
    updateMiniBuffer();

    return EventHandled;
}

EventResult FakeVimHandler::Private::handleInsertMode(const Input &input)
{
    //const int key = input.key;
    //const QString &text = input.text;

    if (input.isEscape()) {
        if (isVisualBlockMode() && !m_lastInsertion.contains('\n')) {
            leaveVisualMode();
            joinPreviousEditBlock();
            moveLeft(m_lastInsertion.size());
            setAnchor();
            int pos = position();
            setTargetColumn();
            for (int i = 0; i < m_visualInsertCount; ++i) {
                moveDown();
                insertText(m_lastInsertion);
            }
            moveLeft(1);
            Range range(pos, position(), RangeBlockMode);
            yankText(range);
            setPosition(pos);
            setDotCommand("p");
            endEditBlock();
        } else {
            // Normal insertion. Start with '1', as one instance was
            // already physically inserted while typing.
            const int repeat = count();
            if (repeat > 1) {
                const QString text = m_lastInsertion;
                for (int i = 1; i < repeat; ++i) {
                    m_lastInsertion.truncate(0);
                    foreach (const QChar &c, text)
                        handleInsertMode(Input(c));
                }
                m_lastInsertion = text;
            }
            moveLeft(qMin(1, leftDist()));
            setTargetColumn();
            leaveVisualMode();
            breakEditBlock();
        }
        // If command is 'o' or 'O' don't include the first line feed in dot command.
        if (g.dotCommand.endsWith(QChar('o'), Qt::CaseInsensitive))
            m_lastInsertion.remove(0, 1);
        g.dotCommand += m_lastInsertion;
        g.dotCommand += QChar(27);
        enterCommandMode();
        m_ctrlVActive = false;
        m_opcount.clear();
        m_mvcount.clear();
    } else if (m_ctrlVActive) {
        insertInInsertMode(input.raw());
    } else if (input.isControl('o')) {
        enterCommandMode(InsertMode);
    } else if (input.isControl('v')) {
        m_ctrlVActive = true;
    } else if (input.isControl('w')) {
        int endPos = position();
        moveToNextWordStart(count(), false, false);
        setTargetColumn();
        int beginPos = position();
        Range range(beginPos, endPos, RangeCharMode);
        removeText(range);
    } else if (input.isKey(Key_Insert)) {
        m_mode = ReplaceMode;
    } else if (input.isKey(Key_Left)) {
        moveLeft(count());
        setTargetColumn();
        breakEditBlock();
        m_lastInsertion.clear();
    } else if (input.isControl(Key_Left)) {
        moveToNextWordStart(count(), false, false);
        setTargetColumn();
        breakEditBlock();
        m_lastInsertion.clear();
    } else if (input.isKey(Key_Down)) {
        //removeAutomaticIndentation();
        m_submode = NoSubMode;
        moveDown(count());
        breakEditBlock();
        m_lastInsertion.clear();
    } else if (input.isKey(Key_Up)) {
        //removeAutomaticIndentation();
        m_submode = NoSubMode;
        moveUp(count());
        breakEditBlock();
        m_lastInsertion.clear();
    } else if (input.isKey(Key_Right)) {
        moveRight(count());
        setTargetColumn();
        breakEditBlock();
        m_lastInsertion.clear();
    } else if (input.isControl(Key_Right)) {
        moveToNextWordStart(count(), false, true);
        moveRight(); // we need one more move since we are in insert mode
        setTargetColumn();
        breakEditBlock();
        m_lastInsertion.clear();
    } else if (input.isKey(Key_Home)) {
        moveToStartOfLine();
        setTargetColumn();
        breakEditBlock();
        m_lastInsertion.clear();
    } else if (input.isKey(Key_End)) {
        if (count() > 1)
            moveDown(count() - 1);
        moveBehindEndOfLine();
        setTargetColumn();
        breakEditBlock();
        m_lastInsertion.clear();
    } else if (input.isReturn()) {
        joinPreviousEditBlock();
        m_submode = NoSubMode;
        insertText(QString("\n"));
        m_lastInsertion += '\n';
        insertAutomaticIndentation(true);
        setTargetColumn();
        endEditBlock();
    } else if (input.isBackspace()) {
        joinPreviousEditBlock();
        m_justAutoIndented = 0;
        if (!m_lastInsertion.isEmpty()
                || hasConfig(ConfigBackspace, "start")
                || hasConfig(ConfigBackspace, "2")) {
            const int line = cursorLine() + 1;
            const Column col = cursorColumn();
            QString data = lineContents(line);
            const Column ind = indentation(data);
            if (col.logical <= ind.logical && col.logical
                    && startsWithWhitespace(data, col.physical)) {
                const int ts = config(ConfigTabStop).toInt();
                const int newl = col.logical - 1 - (col.logical - 1) % ts;
                const QString prefix = tabExpand(newl);
                setLineContents(line, prefix + data.mid(col.physical));
                moveToStartOfLine();
                moveRight(prefix.size());
                m_lastInsertion.clear(); // FIXME
            } else {
                setAnchor();
                cursor().deletePreviousChar();
                m_lastInsertion.chop(1);
            }
            setTargetColumn();
        }
        endEditBlock();
    } else if (input.isKey(Key_Delete)) {
        setAnchor();
        cursor().deleteChar();
        m_lastInsertion.clear();
    } else if (input.isKey(Key_PageDown) || input.isControl('f')) {
        removeAutomaticIndentation();
        moveDown(count() * (linesOnScreen() - 2));
        breakEditBlock();
        m_lastInsertion.clear();
    } else if (input.isKey(Key_PageUp) || input.isControl('b')) {
        removeAutomaticIndentation();
        moveUp(count() * (linesOnScreen() - 2));
        breakEditBlock();
        m_lastInsertion.clear();
    } else if (input.isKey(Key_Tab)) {
        m_justAutoIndented = 0;
        if (hasConfig(ConfigExpandTab)) {
            const int ts = config(ConfigTabStop).toInt();
            const int col = logicalCursorColumn();
            QString str = QString(ts - col % ts, ' ');
            m_lastInsertion.append(str);
            insertText(str);
            setTargetColumn();
        } else {
            insertInInsertMode(input.raw());
        }
    } else if (input.isControl('d')) {
        // remove one level of indentation from the current line
        int shift = config(ConfigShiftWidth).toInt();
        int tab = config(ConfigTabStop).toInt();
        int line = cursorLine() + 1;
        int pos = firstPositionInLine(line);
        QString text = lineContents(line);
        int amount = 0;
        int i = 0;
        for (; i < text.size() && amount < shift; ++i) {
            if (text.at(i) == ' ')
                ++amount;
            else if (text.at(i) == '\t')
                amount += tab; // FIXME: take position into consideration
            else
                break;
        }
        removeText(Range(pos, pos+i));
    //} else if (key >= control('a') && key <= control('z')) {
    //    // ignore these
    } else if (input.isControl('p') || input.isControl('n')) {
        QTextCursor tc = cursor();
        moveToNextWordStart(count(), false, false);
        QString str = selectText(Range(position(), tc.position()));
        setCursor(tc);
        emit q->simpleCompletionRequested(str, input.isControl('n'));
    } else if (!input.text().isEmpty()) {
        insertInInsertMode(input.text());
    } else {
        // We don't want fancy stuff in insert mode.
        return EventHandled;
    }
    updateMiniBuffer();
    return EventHandled;
}

void FakeVimHandler::Private::insertInInsertMode(const QString &text)
{
    joinPreviousEditBlock();
    m_justAutoIndented = 0;
    m_lastInsertion.append(text);
    insertText(text);
    if (hasConfig(ConfigSmartIndent) && isElectricCharacter(text.at(0))) {
        const QString leftText = block().text()
               .left(position() - 1 - block().position());
        if (leftText.simplified().isEmpty()) {
            Range range(position(), position(), m_rangemode);
            indentText(range, text.at(0));
        }
    }
    setTargetColumn();
    endEditBlock();
    m_ctrlVActive = false;
}

EventResult FakeVimHandler::Private::handleExMode(const Input &input)
{
    if (input.isEscape()) {
        g.commandBuffer.clear();
        enterCommandMode(g.returnToMode);
        resetCommandMode();
        m_ctrlVActive = false;
    } else if (m_ctrlVActive) {
        g.commandBuffer.insertChar(input.raw());
        m_ctrlVActive = false;
    } else if (input.isControl('v')) {
        m_ctrlVActive = true;
        return EventHandled;
    } else if (input.isBackspace()) {
        if (g.commandBuffer.isEmpty()) {
            enterCommandMode(g.returnToMode);
            resetCommandMode();
        } else {
            g.commandBuffer.deleteChar();
        }
    } else if (input.isKey(Key_Tab)) {
        // FIXME: Complete actual commands.
        g.commandBuffer.historyUp();
    } else if (input.isKey(Key_Left)) {
        g.commandBuffer.moveLeft();
    } else if (input.isReturn()) {
        showMessage(MessageCommand, g.commandBuffer.display());
        handleExCommand(g.commandBuffer.contents());
        g.commandBuffer.clear();
        if (m_textedit || m_plaintextedit)
            leaveVisualMode();
    } else if (input.isKey(Key_Up) || input.isKey(Key_PageUp)) {
        g.commandBuffer.historyUp();
    } else if (input.isKey(Key_Down) || input.isKey(Key_PageDown)) {
        g.commandBuffer.historyDown();
    } else if (!g.commandBuffer.handleInput(input)) {
        qDebug() << "IGNORED IN EX-MODE: " << input.key() << input.text();
        return EventUnhandled;
    }
    updateMiniBuffer();
    return EventHandled;
}

EventResult FakeVimHandler::Private::handleSearchSubSubMode(const Input &input)
{
    EventResult handled = EventHandled;

    if (input.isEscape()) {
        g.currentMessage.clear();
        g.searchBuffer.clear();
        setAnchorAndPosition(m_searchStartPosition, m_searchStartPosition);
        scrollToLine(m_searchFromScreenLine);
        enterCommandMode(g.returnToMode);
        resetCommandMode();
    } else if (input.isBackspace()) {
        if (g.searchBuffer.isEmpty()) {
            resetCommandMode();
        } else {
            g.searchBuffer.deleteChar();
        }
    } else if (input.isKey(Key_Left)) {
        g.searchBuffer.moveLeft();
    } else if (input.isKey(Key_Right)) {
        g.searchBuffer.moveRight();
    } else if (input.isReturn()) {
        const QString &needle = g.searchBuffer.contents();
        if (!needle.isEmpty())
            g.lastSearch = needle;
        else
            g.searchBuffer.setContents(g.lastSearch);
        if (!g.lastSearch.isEmpty()) {
            updateFind(true);
            finishMovement(g.searchBuffer.prompt() + g.lastSearch + '\n');
        } else {
            finishMovement();
        }
        if (g.currentMessage.isEmpty())
            showMessage(MessageCommand, g.searchBuffer.display());
        else
            handled = EventCancelled;
        enterCommandMode(g.returnToMode);
        resetCommandMode();
        g.searchBuffer.clear();
    } else if (input.isKey(Key_Up) || input.isKey(Key_PageUp)) {
        g.searchBuffer.historyUp();
    } else if (input.isKey(Key_Down) || input.isKey(Key_PageDown)) {
        g.searchBuffer.historyDown();
    } else if (input.isKey(Key_Tab)) {
        g.searchBuffer.insertChar(QChar(9));
    } else if (!g.searchBuffer.handleInput(input)) {
        //qDebug() << "IGNORED IN SEARCH MODE: " << input.key() << input.text();
        return EventUnhandled;
    }

    updateMiniBuffer();

    if (!input.isReturn() && !input.isEscape())
        updateFind(false);

    return handled;
}

// This uses 0 based line counting (hidden lines included).
int FakeVimHandler::Private::parseLineAddress(QString *cmd)
{
    //qDebug() << "CMD: " << cmd;
    if (cmd->isEmpty())
        return -1;

    int result = -1;
    QChar c = cmd->at(0);
    if (c == '.') { // current line
        result = cursorBlockNumber();
        cmd->remove(0, 1);
    } else if (c == '$') { // last line
        result = document()->blockCount() - 1;
        cmd->remove(0, 1);
    } else if (c == '\'') { // mark
        cmd->remove(0, 1);
        if (cmd->isEmpty()) {
            showMessage(MessageError, msgMarkNotSet(QString()));
            return -1;
        }
        c = cmd->at(0);
        Mark m = mark(c);
        if (!m.isValid() || !m.isLocal(m_currentFileName)) {
            showMessage(MessageError, msgMarkNotSet(c));
            return -1;
        }
        cmd->remove(0, 1);
        result = m.position.line;
    } else if (c.isDigit()) { // line with given number
        result = 0;
    } else if (c == '-' || c == '+') { // add or subtract from current line number
        result = cursorBlockNumber();
    } else if (c == '/' || c == '?'
        || (c == '\\' && cmd->size() > 1 && QString("/?&").contains(cmd->at(1)))) {
        // search for expression
        SearchData sd;
        if (c == '/' || c == '?') {
            const int end = findUnescaped(c, *cmd, 1);
            if (end == -1)
                return -1;
            sd.needle = cmd->mid(1, end - 1);
            cmd->remove(0, end + 1);
        } else {
            c = cmd->at(1);
            cmd->remove(0, 2);
            sd.needle = (c == '&') ? g.lastSubstitutePattern : g.lastSearch;
        }
        sd.forward = (c != '?');
        const QTextBlock b = block();
        const int pos = b.position() + (sd.forward ? b.length() - 1 : 0);
        QTextCursor tc = search(sd, pos, 1, true);
        g.lastSearch = sd.needle;
        if (tc.isNull())
            return -1;
        result = tc.block().blockNumber();
    } else {
        return cursorBlockNumber();
    }

    // basic arithmetic ("-3+5" or "++" means "+2" etc.)
    int n = 0;
    bool add = true;
    int i = 0;
    for (; i < cmd->size(); ++i) {
        c = cmd->at(i);
        if (c == '-' || c == '+') {
            if (n != 0)
                result = result + (add ? n - 1 : -(n - 1));
            add = c == '+';
            result = result + (add ? 1 : -1);
            n = 0;
        } else if (c.isDigit()) {
            n = n * 10 + c.digitValue();
        } else if (!c.isSpace()) {
            break;
        }
    }
    if (n != 0)
        result = result + (add ? n - 1 : -(n - 1));
    *cmd = cmd->mid(i).trimmed();

    return result;
}

void FakeVimHandler::Private::setCurrentRange(const Range &range)
{
    setAnchorAndPosition(range.beginPos, range.endPos);
    m_rangemode = range.rangemode;
}

bool FakeVimHandler::Private::parseExCommmand(QString *line, ExCommand *cmd)
{
    *cmd = ExCommand();
    if (line->isEmpty())
        return false;

    // remove leading colons and spaces
    line->remove(QRegExp("^\\s*(:+\\s*)*"));

    // parse range first
    if (!parseLineRange(line, cmd))
        return false;

    // get first command from command line
    QChar close;
    bool subst = false;
    int i = 0;
    for (; i < line->size(); ++i) {
        const QChar &c = line->at(i);
        if (c == '\\') {
            ++i; // skip escaped character
        } else if (close.isNull()) {
            if (c == '|') {
                // split on |
                break;
            } else if (c == '/') {
                subst = i > 0 && (line->at(i - 1) == 's');
                close = c;
            } else if (c == '"' || c == '\'') {
                close = c;
            }
        } else if (c == close) {
            if (subst)
                subst = false;
            else
                close = QChar();
        }
    }

    cmd->cmd = line->mid(0, i).trimmed();

    // command arguments starts with first non-letter character
    cmd->args = cmd->cmd.section(QRegExp("(?=[^a-zA-Z])"), 1);
    if (!cmd->args.isEmpty()) {
        cmd->cmd.chop(cmd->args.size());
        cmd->args = cmd->args.trimmed();

        // '!' at the end of command
        cmd->hasBang = cmd->args.startsWith('!');
        if (cmd->hasBang)
            cmd->args = cmd->args.mid(1).trimmed();
    }

    // remove the first command from command line
    line->remove(0, i + 1);

    return true;
}

bool FakeVimHandler::Private::parseLineRange(QString *line, ExCommand *cmd)
{
    // FIXME: that seems to be different for %w and %s
    if (line->startsWith(QLatin1Char('%')))
        line->replace(0, 1, "1,$");

    int beginLine = parseLineAddress(line);
    int endLine;
    if (line->startsWith(',')) {
        *line = line->mid(1).trimmed();
        endLine = parseLineAddress(line);
    } else {
        endLine = beginLine;
    }
    if (beginLine == -1 || endLine == -1)
        return false;

    const int beginPos = firstPositionInLine(qMin(beginLine, endLine) + 1, false);
    const int endPos = lastPositionInLine(qMax(beginLine, endLine) + 1, false);
    cmd->range = Range(beginPos, endPos, RangeLineMode);
    cmd->count = beginLine;

    return true;
}

void FakeVimHandler::Private::parseRangeCount(const QString &line, Range *range) const
{
    bool ok;
    const int count = qAbs(line.trimmed().toInt(&ok));
    if (ok) {
        const int beginLine = document()->findBlock(range->endPos).blockNumber() + 1;
        const int endLine = qMin(beginLine + count - 1, document()->blockCount());
        range->beginPos = firstPositionInLine(beginLine, false);
        range->endPos = lastPositionInLine(endLine, false);
    }
}

// use handleExCommand for invoking commands that might move the cursor
void FakeVimHandler::Private::handleCommand(const QString &cmd)
{
    handleExCommand(cmd);
}

bool FakeVimHandler::Private::handleExSubstituteCommand(const ExCommand &cmd)
{
    // :substitute
    if (!cmd.matches("s", "substitute")
        && !(cmd.cmd.isEmpty() && !cmd.args.isEmpty() && QString("&~").contains(cmd.args[0]))) {
        return false;
    }

    int count = 1;
    QString line = cmd.args;
    const int countIndex = line.lastIndexOf(QRegExp("\\d+$"));
    if (countIndex != -1) {
        count = line.mid(countIndex).toInt();
        line = line.mid(0, countIndex).trimmed();
    }

    if (cmd.cmd.isEmpty()) {
        // keep previous substitution flags on '&&' and '~&'
        if (line.size() > 1 && line[1] == '&')
            g.lastSubstituteFlags += line.mid(2);
        else
            g.lastSubstituteFlags = line.mid(1);
        if (line[0] == '~')
            g.lastSubstitutePattern = g.lastSearch;
    } else {
        if (line.isEmpty()) {
            g.lastSubstituteFlags.clear();
        } else {
            // we have /{pattern}/{string}/[flags]  now
            const QChar separator = line.at(0);
            int pos1 = findUnescaped(separator, line, 1);
            if (pos1 == -1)
                return false;
            int pos2 = findUnescaped(separator, line, pos1 + 1);;
            if (pos2 == -1)
                pos2 = line.size();

            g.lastSubstitutePattern = line.mid(1, pos1 - 1);
            g.lastSubstituteReplacement = line.mid(pos1 + 1, pos2 - pos1 - 1);
            g.lastSubstituteFlags = line.mid(pos2 + 1);
        }
    }

    count = qMax(1, count);
    QString needle = g.lastSubstitutePattern;

    if (g.lastSubstituteFlags.contains('i'))
        needle.prepend("\\c");

    QRegExp pattern = vimPatternToQtPattern(needle, hasConfig(ConfigSmartCase));

    QTextBlock lastBlock;
    QTextBlock firstBlock;
    const bool global = g.lastSubstituteFlags.contains('g');
    for (int a = 0; a != count; ++a) {
        for (QTextBlock block = document()->findBlock(cmd.range.endPos);
            block.isValid() && block.position() + block.length() > cmd.range.beginPos;
            block = block.previous()) {
            QString text = block.text();
            if (substituteText(&text, pattern, g.lastSubstituteReplacement, global)) {
                firstBlock = block;
                if (!lastBlock.isValid()) {
                    lastBlock = block;
                    beginEditBlock();
                }
                QTextCursor tc = cursor();
                const int pos = block.position();
                const int anchor = pos + block.length() - 1;
                tc.setPosition(anchor);
                tc.setPosition(pos, KeepAnchor);
                tc.insertText(text);
            }
        }
    }

    if (lastBlock.isValid()) {
        State &state = m_undo.top();
        state.position = CursorPosition(firstBlock.blockNumber(), 0);

        leaveVisualMode();
        setPosition(lastBlock.position());
        setAnchor();
        moveToFirstNonBlankOnLine();
        setTargetColumn();

        endEditBlock();
    }

    return true;
}

bool FakeVimHandler::Private::handleExMapCommand(const ExCommand &cmd0) // :map
{
    QByteArray modes;
    enum Type { Map, Noremap, Unmap } type;

    QByteArray cmd = cmd0.cmd.toLatin1();

    // Strange formatting. But everything else is even uglier.
    if (cmd == "map") { modes = "nvo"; type = Map; } else
    if (cmd == "nm" || cmd == "nmap") { modes = "n"; type = Map; } else
    if (cmd == "vm" || cmd == "vmap") { modes = "v"; type = Map; } else
    if (cmd == "xm" || cmd == "xmap") { modes = "x"; type = Map; } else
    if (cmd == "smap") { modes = "s"; type = Map; } else
    if (cmd == "map!") { modes = "ic"; type = Map; } else
    if (cmd == "im" || cmd == "imap") { modes = "i"; type = Map; } else
    if (cmd == "lm" || cmd == "lmap") { modes = "l"; type = Map; } else
    if (cmd == "cm" || cmd == "cmap") { modes = "c"; type = Map; } else

    if (cmd == "no" || cmd == "noremap") { modes = "nvo"; type = Noremap; } else
    if (cmd == "nn" || cmd == "nnoremap") { modes = "n"; type = Noremap; } else
    if (cmd == "vn" || cmd == "vnoremap") { modes = "v"; type = Noremap; } else
    if (cmd == "xn" || cmd == "xnoremap") { modes = "x"; type = Noremap; } else
    if (cmd == "snor" || cmd == "snoremap") { modes = "s"; type = Noremap; } else
    if (cmd == "ono" || cmd == "onoremap") { modes = "o"; type = Noremap; } else
    if (cmd == "no!" || cmd == "noremap!") { modes = "ic"; type = Noremap; } else
    if (cmd == "ino" || cmd == "inoremap") { modes = "i"; type = Noremap; } else
    if (cmd == "ln" || cmd == "lnoremap") { modes = "l"; type = Noremap; } else
    if (cmd == "cno" || cmd == "cnoremap") { modes = "c"; type = Noremap; } else

    if (cmd == "unm" || cmd == "unmap") { modes = "nvo"; type = Unmap; } else
    if (cmd == "nun" || cmd == "nunmap") { modes = "n"; type = Unmap; } else
    if (cmd == "vu" || cmd == "vunmap") { modes = "v"; type = Unmap; } else
    if (cmd == "xu" || cmd == "xunmap") { modes = "x"; type = Unmap; } else
    if (cmd == "sunm" || cmd == "sunmap") { modes = "s"; type = Unmap; } else
    if (cmd == "ou" || cmd == "ounmap") { modes = "o"; type = Unmap; } else
    if (cmd == "unm!" || cmd == "unmap!") { modes = "ic"; type = Unmap; } else
    if (cmd == "iu" || cmd == "iunmap") { modes = "i"; type = Unmap; } else
    if (cmd == "lu" || cmd == "lunmap") { modes = "l"; type = Unmap; } else
    if (cmd == "cu" || cmd == "cunmap") { modes = "c"; type = Unmap; }

    else
        return false;

    QString args = cmd0.args;
    bool silent = false;
    bool unique = false;
    forever {
        if (eatString("<silent>", &args)) {
            silent = true;
        } else if (eatString("<unique>", &args)) {
            continue;
        } else if (eatString("<special>", &args)) {
            continue;
        } else if (eatString("<buffer>", &args)) {
            notImplementedYet();
            continue;
        } else if (eatString("<script>", &args)) {
            notImplementedYet();
            continue;
        } else if (eatString("<expr>", &args)) {
            notImplementedYet();
            return true;
        }
        break;
    }

    const QString lhs = args.section(QRegExp("\\s+"), 0, 0);
    const QString rhs = args.section(QRegExp("\\s+"), 1);
    if ((rhs.isNull() && type != Unmap) || (!rhs.isNull() && type == Unmap)) {
        // FIXME: Dump mappings here.
        //qDebug() << g.mappings;
        return true;
    }

    Inputs key(lhs);
    //qDebug() << "MAPPING: " << modes << lhs << rhs;
    switch (type) {
        case Unmap:
            foreach (char c, modes)
                MappingsIterator(&g.mappings, c, key).remove();
            break;
        case Map: // fall through
        case Noremap: {
            Inputs inputs(rhs, type == Noremap, silent);
            foreach (char c, modes)
                MappingsIterator(&g.mappings, c).setInputs(key, inputs, unique);
            break;
        }
    }
    return true;
}

bool FakeVimHandler::Private::handleExHistoryCommand(const ExCommand &cmd)
{
    // :his[tory]
    if (!cmd.matches("his", "history"))
        return false;

    if (cmd.args.isEmpty()) {
        QString info;
        info += "#  command history\n";
        int i = 0;
        foreach (const QString &item, g.commandBuffer.historyItems()) {
            ++i;
            info += QString("%1 %2\n").arg(i, -8).arg(item);
        }
        emit q->extraInformationChanged(info);
    } else {
        notImplementedYet();
    }
    updateMiniBuffer();
    return true;
}

bool FakeVimHandler::Private::handleExRegisterCommand(const ExCommand &cmd)
{
    // :reg[isters] and :di[splay]
    if (!cmd.matches("reg", "registers") && !cmd.matches("di", "display"))
        return false;

    QByteArray regs = cmd.args.toLatin1();
    if (regs.isEmpty()) {
        regs = "\"0123456789";
        QHashIterator<int, Register> it(g.registers);
        while (it.hasNext()) {
            it.next();
            if (it.key() > '9')
                regs += char(it.key());
        }
    }
    QString info;
    info += "--- Registers ---\n";
    foreach (char reg, regs) {
        QString value = quoteUnprintable(registerContents(reg));
        info += QString("\"%1   %2\n").arg(reg).arg(value);
    }
    emit q->extraInformationChanged(info);
    updateMiniBuffer();
    return true;
}

bool FakeVimHandler::Private::handleExSetCommand(const ExCommand &cmd)
{
    // :se[t]
    if (!cmd.matches("se", "set"))
        return false;

    clearMessage();
    SavedAction *act = theFakeVimSettings()->item(cmd.args);
    QTC_CHECK(!cmd.args.isEmpty()); // Handled by plugin.
    if (act && act->value().canConvert(QVariant::Bool)) {
        // Boolean config to be switched on.
        bool oldValue = act->value().toBool();
        if (oldValue == false)
            act->setValue(true);
        else if (oldValue == true)
            {} // nothing to do
    } else if (act) {
        // Non-boolean to show.
        showMessage(MessageInfo, cmd.args + '=' + act->value().toString());
    } else if (cmd.args.startsWith(_("no"))
            && (act = theFakeVimSettings()->item(cmd.args.mid(2)))) {
        // Boolean config to be switched off.
        bool oldValue = act->value().toBool();
        if (oldValue == true)
            act->setValue(false);
        else if (oldValue == false)
            {} // nothing to do
    } else if (cmd.args.contains('=')) {
        // Non-boolean config to set.
        int p = cmd.args.indexOf('=');
        QString error = theFakeVimSettings()
                ->trySetValue(cmd.args.left(p), cmd.args.mid(p + 1));
        if (!error.isEmpty())
            showMessage(MessageError, error);
    } else {
        showMessage(MessageError, FakeVimHandler::tr("Unknown option: ") + cmd.args);
    }
    updateMiniBuffer();
    updateEditor();
    return true;
}

bool FakeVimHandler::Private::handleExNormalCommand(const ExCommand &cmd)
{
    // :norm[al]
    if (!cmd.matches("norm", "normal"))
        return false;
    //qDebug() << "REPLAY NORMAL: " << quoteUnprintable(reNormal.cap(3));
    replay(cmd.args);
    return true;
}

bool FakeVimHandler::Private::handleExYankDeleteCommand(const ExCommand &cmd)
{
    // :[range]d[elete] [x] [count]
    // :[range]y[ank] [x] [count]
    const bool remove = cmd.matches("d", "delete");
    if (!remove && !cmd.matches("y", "yank"))
        return false;

    // get register from arguments
    const bool hasRegisterArg = !cmd.args.isEmpty() && !cmd.args.at(0).isDigit();
    const int r = hasRegisterArg ? cmd.args.at(0).unicode() : m_register;

    // get [count] from arguments
    Range range = cmd.range;
    parseRangeCount(cmd.args.mid(hasRegisterArg ? 1 : 0).trimmed(), &range);

    yankText(range, r);

    if (remove) {
        leaveVisualMode();
        setPosition(range.beginPos);
        setUndoPosition();
        setCurrentRange(range);
        removeText(currentRange());
    }

    return true;
}

bool FakeVimHandler::Private::handleExChangeCommand(const ExCommand &cmd)
{
    // :[range]c[hange]
    if (!cmd.matches("c", "change"))
        return false;

    const bool oldAutoIndent = hasConfig(ConfigAutoIndent);
    // Temporarily set autoindent if ! is present.
    if (cmd.hasBang)
        theFakeVimSetting(ConfigAutoIndent)->setValue(true, false);

    Range range = cmd.range;
    range.rangemode = RangeLineModeExclusive;
    removeText(range);
    insertAutomaticIndentation(true);

    // FIXME: In Vim same or less number of lines can be inserted and position after insertion is
    //        beginning of last inserted line.
    enterInsertMode();

    if (cmd.hasBang && !oldAutoIndent)
        theFakeVimSetting(ConfigAutoIndent)->setValue(false, false);

    return true;
}

bool FakeVimHandler::Private::handleExMoveCommand(const ExCommand &cmd)
{
    // :[range]m[ove] {address}
    if (!cmd.matches("m", "move"))
        return false;

    QString lineCode = cmd.args;

    const int startLine = document()->findBlock(cmd.range.beginPos).blockNumber();
    const int endLine = document()->findBlock(cmd.range.endPos).blockNumber();
    const int lines = endLine - startLine + 1;

    int targetLine = lineCode == "0" ? -1 : parseLineAddress(&lineCode);
    if (targetLine >= startLine && targetLine < endLine) {
        showMessage(MessageError, FakeVimHandler::tr("Move lines into themselves"));
        return true;
    }

    CursorPosition lastAnchor = mark('<').position;
    CursorPosition lastPosition = mark('>').position;

    recordJump();
    setPosition(cmd.range.beginPos);
    setUndoPosition();

    setCurrentRange(cmd.range);
    QString text = selectText(cmd.range);
    removeText(currentRange());

    const bool insertAtEnd = targetLine == document()->blockCount();
    if (targetLine >= startLine)
        targetLine -= lines;
    QTextBlock block = document()->findBlockByNumber(insertAtEnd ? targetLine : targetLine + 1);
    setPosition(block.position());
    setAnchor();

    if (insertAtEnd) {
        moveBehindEndOfLine();
        text.chop(1);
        insertText(QString('\n'));
    }
    insertText(text);

    if (!insertAtEnd)
        moveUp(1);
    if (hasConfig(ConfigStartOfLine))
        moveToFirstNonBlankOnLine();

    // correct last selection
    leaveVisualMode();
    if (lastAnchor.line >= startLine && lastAnchor.line <= endLine)
        lastAnchor.line += targetLine - startLine + 1;
    if (lastPosition.line >= startLine && lastPosition.line <= endLine)
        lastPosition.line += targetLine - startLine + 1;
    setMark('<', lastAnchor);
    setMark('>', lastPosition);

    if (lines > 2)
        showMessage(MessageInfo, FakeVimHandler::tr("%1 lines moved").arg(lines));

    return true;
}

bool FakeVimHandler::Private::handleExJoinCommand(const ExCommand &cmd)
{
    // :[range]j[oin][!] [count]
    // FIXME: Argument [count] can follow immediately.
    if (!cmd.matches("j", "join"))
        return false;

    // get [count] from arguments
    bool ok;
    int count = cmd.args.toInt(&ok);

    if (ok) {
        setPosition(cmd.range.endPos);
    } else {
        setPosition(cmd.range.beginPos);
        const int startLine = document()->findBlock(cmd.range.beginPos).blockNumber();
        const int endLine = document()->findBlock(cmd.range.endPos).blockNumber();
        count = endLine - startLine + 1;
    }

    moveToStartOfLine();
    setUndoPosition();
    joinLines(count, cmd.hasBang);

    moveToFirstNonBlankOnLine();

    return true;
}

bool FakeVimHandler::Private::handleExWriteCommand(const ExCommand &cmd)
{
    // :w, :x, :wq, ...
    //static QRegExp reWrite("^[wx]q?a?!?( (.*))?$");
    if (cmd.cmd != "w" && cmd.cmd != "x" && cmd.cmd != "wq")
        return false;

    int beginLine = lineForPosition(cmd.range.beginPos);
    int endLine = lineForPosition(cmd.range.endPos);
    const bool noArgs = (beginLine == -1);
    if (beginLine == -1)
        beginLine = 0;
    if (endLine == -1)
        endLine = linesInDocument();
    //qDebug() << "LINES: " << beginLine << endLine;
    //QString prefix = cmd.args;
    const bool forced = cmd.hasBang;
    //const bool quit = prefix.contains(QChar('q')) || prefix.contains(QChar('x'));
    //const bool quitAll = quit && prefix.contains(QChar('a'));
    QString fileName = cmd.args;
    if (fileName.isEmpty())
        fileName = m_currentFileName;
    QFile file1(fileName);
    const bool exists = file1.exists();
    if (exists && !forced && !noArgs) {
        showMessage(MessageError, FakeVimHandler::tr
            ("File \"%1\" exists (add ! to override)").arg(fileName));
    } else if (file1.open(QIODevice::ReadWrite)) {
        // Nobody cared, so act ourselves.
        file1.close();
        Range range(firstPositionInLine(beginLine),
            firstPositionInLine(endLine), RangeLineMode);
        QString contents = selectText(range);
        QFile::remove(fileName);
        QFile file2(fileName);
        if (file2.open(QIODevice::ReadWrite)) {
            QTextStream ts(&file2);
            ts << contents;
        } else {
            showMessage(MessageError, FakeVimHandler::tr
               ("Cannot open file \"%1\" for writing").arg(fileName));
        }
        // Check result by reading back.
        QFile file3(fileName);
        file3.open(QIODevice::ReadOnly);
        QByteArray ba = file3.readAll();
        showMessage(MessageInfo, FakeVimHandler::tr("\"%1\" %2 %3L, %4C written")
            .arg(fileName).arg(exists ? " " : tr(" [New] "))
            .arg(ba.count('\n')).arg(ba.size()));
        //if (quitAll)
        //    passUnknownExCommand(forced ? "qa!" : "qa");
        //else if (quit)
        //    passUnknownExCommand(forced ? "q!" : "q");
    } else {
        showMessage(MessageError, FakeVimHandler::tr
            ("Cannot open file \"%1\" for reading").arg(fileName));
    }
    return true;
}

bool FakeVimHandler::Private::handleExReadCommand(const ExCommand &cmd)
{
    // :r[ead]
    if (!cmd.matches("r", "read"))
        return false;

    beginEditBlock();
    moveToStartOfLine();
    setTargetColumn();
    moveDown();
    m_currentFileName = cmd.args;
    QFile file(m_currentFileName);
    file.open(QIODevice::ReadOnly);
    QTextStream ts(&file);
    QString data = ts.readAll();
    insertText(data);
    endEditBlock();
    showMessage(MessageInfo, FakeVimHandler::tr("\"%1\" %2L, %3C")
        .arg(m_currentFileName).arg(data.count('\n')).arg(data.size()));
    return true;
}

bool FakeVimHandler::Private::handleExBangCommand(const ExCommand &cmd) // :!
{
    if (!cmd.cmd.isEmpty() || !cmd.hasBang)
        return false;

    setCurrentRange(cmd.range);
    int targetPosition = firstPositionInLine(lineForPosition(cmd.range.beginPos));
    QString command = QString(cmd.cmd.mid(1) + ' ' + cmd.args).trimmed();
    QString text = selectText(cmd.range);
    QProcess proc;
    proc.start(command);
    proc.waitForStarted();
    if (Utils::HostOsInfo::isWindowsHost())
        text.replace(_("\n"), _("\r\n"));
    proc.write(text.toUtf8());
    proc.closeWriteChannel();
    proc.waitForFinished();
    QString result = QString::fromUtf8(proc.readAllStandardOutput());
    if (text.isEmpty()) {
        emit q->extraInformationChanged(result);
    } else {
        beginEditBlock();
        removeText(currentRange());
        insertText(result);
        setPosition(targetPosition);
        endEditBlock();
        leaveVisualMode();
        //qDebug() << "FILTER: " << command;
        showMessage(MessageInfo, FakeVimHandler::tr("%n lines filtered", 0,
            text.count('\n')));
    }
    return true;
}

bool FakeVimHandler::Private::handleExShiftCommand(const ExCommand &cmd)
{
    // :[range]{<|>}* [count]
    if (!cmd.cmd.isEmpty() || (!cmd.args.startsWith('<') && !cmd.args.startsWith('>')))
        return false;

    const QChar c = cmd.args.at(0);

    // get number of repetition
    int repeat = 1;
    int i = 1;
    for (; i < cmd.args.size(); ++i) {
        const QChar c2 = cmd.args.at(i);
        if (c2 == c)
            ++repeat;
        else if (!c2.isSpace())
            break;
    }

    // get [count] from arguments
    Range range = cmd.range;
    parseRangeCount(cmd.args.mid(i), &range);

    setCurrentRange(range);
    if (c == '<')
        shiftRegionLeft(repeat);
    else
        shiftRegionRight(repeat);

    leaveVisualMode();

    return true;
}

bool FakeVimHandler::Private::handleExNohlsearchCommand(const ExCommand &cmd)
{
    // :nohlsearch
    if (!cmd.cmd.startsWith("noh"))
        return false;

    highlightMatches(QString());
    return true;
}

bool FakeVimHandler::Private::handleExUndoRedoCommand(const ExCommand &cmd)
{
    // :undo
    // :redo
    bool undo = (cmd.cmd == "u" || cmd.cmd == "un" || cmd.cmd == "undo");
    if (!undo && cmd.cmd != "red" && cmd.cmd != "redo")
        return false;

    undoRedo(undo);
    updateMiniBuffer();
    return true;
}

bool FakeVimHandler::Private::handleExGotoCommand(const ExCommand &cmd)
{
    // :{address}
    if (!cmd.cmd.isEmpty() || !cmd.args.isEmpty())
        return false;

    const int beginLine = lineForPosition(cmd.range.endPos);
    setPosition(firstPositionInLine(beginLine));
    clearMessage();
    return true;
}

bool FakeVimHandler::Private::handleExSourceCommand(const ExCommand &cmd)
{
    // :source
    if (cmd.cmd != "so" && cmd.cmd != "source")
        return false;

    QString fileName = cmd.args;
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
        showMessage(MessageError, FakeVimHandler::tr("Cannot open file %1").arg(fileName));
        return true;
    }

    bool inFunction = false;
    QByteArray line;
    while (!file.atEnd() || !line.isEmpty()) {
        QByteArray nextline = !file.atEnd() ? file.readLine() : QByteArray();

        //  remove comment
        int i = nextline.lastIndexOf('"');
        if (i != -1)
            nextline = nextline.remove(i, nextline.size() - i);

        nextline = nextline.trimmed();

        // multi-line command?
        if (nextline.startsWith('\\')) {
            line += nextline.mid(1);
            continue;
        }

        if (line.startsWith("function")) {
            //qDebug() << "IGNORING FUNCTION" << line;
            inFunction = true;
        } else if (inFunction && line.startsWith("endfunction")) {
            inFunction = false;
        } else if (!line.isEmpty() && !inFunction) {
            //qDebug() << "EXECUTING: " << line;
            ExCommand cmd;
            QString commandLine = QString::fromLocal8Bit(line);
            while (parseExCommmand(&commandLine, &cmd)) {
                if (!handleExCommandHelper(cmd))
                    break;
            }
        }

        line = nextline;
    }
    file.close();
    return true;
}

bool FakeVimHandler::Private::handleExEchoCommand(const ExCommand &cmd)
{
    // :echo
    if (cmd.cmd != "echo")
        return false;
    showMessage(MessageInfo, cmd.args);
    return true;
}

void FakeVimHandler::Private::handleExCommand(const QString &line0)
{
    QString line = line0; // Make sure we have a copy to prevent aliasing.

    if (line.endsWith(QLatin1Char('%'))) {
        line.chop(1);
        int percent = line.toInt();
        setPosition(firstPositionInLine(percent * linesInDocument() / 100));
        clearMessage();
        return;
    }

    //qDebug() << "CMD: " << cmd;

    enterCommandMode(g.returnToMode);

    beginLargeEditBlock();
    ExCommand cmd;
    QString lastCommand = line;
    while (parseExCommmand(&line, &cmd)) {
        if (!handleExCommandHelper(cmd)) {
            showMessage(MessageError, tr("Not an editor command: %1").arg(lastCommand));
            break;
        }
        lastCommand = line;
    }
    endEditBlock();

    resetCommandMode();
}

bool FakeVimHandler::Private::handleExCommandHelper(ExCommand &cmd)
{
    return handleExPluginCommand(cmd)
        || handleExGotoCommand(cmd)
        || handleExBangCommand(cmd)
        || handleExHistoryCommand(cmd)
        || handleExRegisterCommand(cmd)
        || handleExYankDeleteCommand(cmd)
        || handleExChangeCommand(cmd)
        || handleExMoveCommand(cmd)
        || handleExJoinCommand(cmd)
        || handleExMapCommand(cmd)
        || handleExNohlsearchCommand(cmd)
        || handleExNormalCommand(cmd)
        || handleExReadCommand(cmd)
        || handleExUndoRedoCommand(cmd)
        || handleExSetCommand(cmd)
        || handleExShiftCommand(cmd)
        || handleExSourceCommand(cmd)
        || handleExSubstituteCommand(cmd)
        || handleExWriteCommand(cmd)
        || handleExEchoCommand(cmd);
}

bool FakeVimHandler::Private::handleExPluginCommand(const ExCommand &cmd)
{
    bool handled = false;
    emit q->handleExCommandRequested(&handled, cmd);
    //qDebug() << "HANDLER REQUEST: " << cmd.cmd << handled;
    return handled;
}

void FakeVimHandler::Private::searchBalanced(bool forward, QChar needle, QChar other)
{
    int level = 1;
    int pos = position();
    const int npos = forward ? lastPositionInDocument() : 0;
    while (true) {
        if (forward)
            ++pos;
        else
            --pos;
        if (pos == npos)
            return;
        QChar c = document()->characterAt(pos);
        if (c == other)
            ++level;
        else if (c == needle)
            --level;
        if (level == 0) {
            const int oldLine = cursorLine() - cursorLineOnScreen();
            // Making this unconditional feels better, but is not "vim like".
            if (oldLine != cursorLine() - cursorLineOnScreen())
                scrollToLine(cursorLine() - linesOnScreen() / 2);
            recordJump();
            setPosition(pos);
            setTargetColumn();
            return;
        }
    }
}

QTextCursor FakeVimHandler::Private::search(const SearchData &sd, int startPos, int count,
    bool showMessages)
{
    QRegExp needleExp = vimPatternToQtPattern(sd.needle, hasConfig(ConfigSmartCase));
    if (!needleExp.isValid()) {
        if (showMessages) {
            QString error = needleExp.errorString();
            showMessage(MessageError,
                        FakeVimHandler::tr("Invalid regular expression: %1").arg(error));
        }
        if (sd.highlightMatches)
            highlightMatches(QString());
        return QTextCursor();
    }

    int repeat = count;
    const int pos = startPos + (sd.forward ? 1 : -1);

    QTextCursor tc;
    if (pos >= 0 && pos < document()->characterCount()) {
        tc = QTextCursor(document());
        tc.setPosition(pos);
        if (sd.forward && afterEndOfLine(document(), pos))
            tc.movePosition(Right);

        if (!tc.isNull()) {
            if (sd.forward)
                searchForward(&tc, needleExp, &repeat);
            else
                searchBackward(&tc, needleExp, &repeat);
        }
    }

    if (tc.isNull()) {
        if (hasConfig(ConfigWrapScan)) {
            tc = QTextCursor(document());
            tc.movePosition(sd.forward ? StartOfDocument : EndOfDocument);
            if (sd.forward)
                searchForward(&tc, needleExp, &repeat);
            else
                searchBackward(&tc, needleExp, &repeat);
            if (tc.isNull()) {
                if (showMessages) {
                    showMessage(MessageError,
                        FakeVimHandler::tr("Pattern not found: %1").arg(sd.needle));
                }
            } else if (showMessages) {
                QString msg = sd.forward
                    ? FakeVimHandler::tr("search hit BOTTOM, continuing at TOP")
                    : FakeVimHandler::tr("search hit TOP, continuing at BOTTOM");
                showMessage(MessageWarning, msg);
            }
        } else if (showMessages) {
            QString msg = sd.forward
                ? FakeVimHandler::tr("search hit BOTTOM without match for: %1")
                : FakeVimHandler::tr("search hit TOP without match for: %1");
            showMessage(MessageError, msg.arg(sd.needle));
        }
    }

    if (sd.highlightMatches)
        highlightMatches(needleExp.pattern());

    return tc;
}

void FakeVimHandler::Private::search(const SearchData &sd, bool showMessages)
{
    const int oldLine = cursorLine() - cursorLineOnScreen();

    QTextCursor tc = search(sd, m_searchStartPosition, count(), showMessages);
    if (tc.isNull()) {
        tc = cursor();
        tc.setPosition(m_searchStartPosition);
    }

    if (isVisualMode()) {
        int d = tc.anchor() - tc.position();
        setPosition(tc.position() + d);
    } else {
        // Set Cursor. In contrast to the main editor we have the cursor
        // position before the anchor position.
        setAnchorAndPosition(tc.position(), tc.anchor());
    }

    // Making this unconditional feels better, but is not "vim like".
    if (oldLine != cursorLine() - cursorLineOnScreen())
        scrollToLine(cursorLine() - linesOnScreen() / 2);

    m_searchCursor = cursor();

    setTargetColumn();
}

void FakeVimHandler::Private::searchNext(bool forward)
{
    SearchData sd;
    sd.needle = g.lastSearch;
    sd.forward = forward ? g.lastSearchForward : !g.lastSearchForward;
    sd.highlightMatches = true;
    m_searchStartPosition = position();
    showMessage(MessageCommand, (g.lastSearchForward ? '/' : '?') + sd.needle);
    recordJump();
    search(sd);
}

void FakeVimHandler::Private::highlightMatches(const QString &needle)
{
    if (!hasConfig(ConfigHlSearch) || needle == m_oldNeedle)
        return;
    m_oldNeedle = needle;

    updateHighlights();
}

void FakeVimHandler::Private::moveToFirstNonBlankOnLine()
{
    QTextCursor tc2 = cursor();
    moveToFirstNonBlankOnLine(&tc2);
    setPosition(tc2.position());
}

void FakeVimHandler::Private::moveToFirstNonBlankOnLine(QTextCursor *tc)
{
    QTextDocument *doc = tc->document();
    int firstPos = tc->block().position();
    for (int i = firstPos, n = firstPos + block().length(); i < n; ++i) {
        if (!doc->characterAt(i).isSpace() || i == n - 1) {
            tc->setPosition(i);
            return;
        }
    }
    tc->setPosition(block().position());
}

void FakeVimHandler::Private::indentSelectedText(QChar typedChar)
{
    beginEditBlock();
    setTargetColumn();
    int beginLine = qMin(lineForPosition(position()), lineForPosition(anchor()));
    int endLine = qMax(lineForPosition(position()), lineForPosition(anchor()));

    Range range(anchor(), position(), m_rangemode);
    indentText(range, typedChar);

    setPosition(firstPositionInLine(beginLine));
    handleStartOfLine();
    setTargetColumn();
    setDotCommand("%1==", endLine - beginLine + 1);
    endEditBlock();

    const int lines = endLine - beginLine + 1;
    if (lines > 2)
        showMessage(MessageInfo, FakeVimHandler::tr("%n lines indented", 0, lines));
}

void FakeVimHandler::Private::indentText(const Range &range, QChar typedChar)
{
    int beginBlock = document()->findBlock(range.beginPos).blockNumber();
    int endBlock = document()->findBlock(range.endPos).blockNumber();
    if (beginBlock > endBlock)
        qSwap(beginBlock, endBlock);
    emit q->indentRegion(beginBlock, endBlock, typedChar);
}

bool FakeVimHandler::Private::isElectricCharacter(QChar c) const
{
    bool result = false;
    emit q->checkForElectricCharacter(&result, c);
    return result;
}

void FakeVimHandler::Private::shiftRegionRight(int repeat)
{
    int beginLine = lineForPosition(anchor());
    int endLine = lineForPosition(position());
    int targetPos = anchor();
    if (beginLine > endLine) {
        qSwap(beginLine, endLine);
        targetPos = position();
    }
    if (hasConfig(ConfigStartOfLine))
        targetPos = firstPositionInLine(beginLine);

    const int sw = config(ConfigShiftWidth).toInt();
    m_movetype = MoveLineWise;
    beginEditBlock();
    QTextBlock block = document()->findBlockByLineNumber(beginLine - 1);
    while (block.isValid() && lineNumber(block) <= endLine) {
        const Column col = indentation(block.text());
        QTextCursor tc = cursor();
        tc.setPosition(block.position());
        if (col.physical > 0)
            tc.setPosition(tc.position() + col.physical, KeepAnchor);
        tc.insertText(tabExpand(col.logical + sw * repeat));
        block = block.next();
    }
    endEditBlock();

    setPosition(targetPos);
    handleStartOfLine();
    setTargetColumn();

    const int lines = endLine - beginLine + 1;
    if (lines > 2) {
        showMessage(MessageInfo,
            FakeVimHandler::tr("%n lines %1ed %2 time", 0, lines)
            .arg(repeat > 0 ? '>' : '<').arg(qAbs(repeat)));
    }
}

void FakeVimHandler::Private::shiftRegionLeft(int repeat)
{
    shiftRegionRight(-repeat);
}

void FakeVimHandler::Private::moveToTargetColumn()
{
    const QTextBlock &bl = block();
    //Column column = cursorColumn();
    //int logical = logical
    const int pos = lastPositionInLine(bl.blockNumber() + 1, false);
    if (m_targetColumn == -1) {
        setPosition(pos);
        return;
    }
    const int physical = bl.position() + logicalToPhysicalColumn(m_targetColumn, bl.text());
    //qDebug() << "CORRECTING COLUMN FROM: " << logical << "TO" << m_targetColumn;
    setPosition(qMin(pos, physical));
}

/* if simple is given:
 *  class 0: spaces
 *  class 1: non-spaces
 * else
 *  class 0: spaces
 *  class 1: non-space-or-letter-or-number
 *  class 2: letter-or-number
 */


int FakeVimHandler::Private::charClass(QChar c, bool simple) const
{
    if (simple)
        return c.isSpace() ? 0 : 1;
    // FIXME: This means that only characters < 256 in the
    // ConfigIsKeyword setting are handled properly.
    if (c.unicode() < 256) {
        //int old = (c.isLetterOrNumber() || c.unicode() == '_') ? 2
        //    :  c.isSpace() ? 0 : 1;
        //qDebug() << c.unicode() << old << m_charClass[c.unicode()];
        return m_charClass[c.unicode()];
    }
    if (c.isLetterOrNumber() || c.unicode() == '_')
        return 2;
    return c.isSpace() ? 0 : 1;
}

void FakeVimHandler::Private::miniBufferTextEdited(const QString &text, int cursorPos)
{
    if (m_subsubmode != SearchSubSubMode && m_mode != ExMode) {
        editor()->setFocus();
    } else if (text.isEmpty()) {
        // editing cancelled
        handleDefaultKey(Input(Qt::Key_Escape, Qt::NoModifier, QString()));
        editor()->setFocus();
        updateCursorShape();
    } else {
        CommandBuffer &cmdBuf = (m_mode == ExMode) ? g.commandBuffer : g.searchBuffer;
        // prepend prompt character if missing
        if (!text.startsWith(cmdBuf.prompt())) {
            emit q->commandBufferChanged(cmdBuf.prompt() + text, cmdBuf.cursorPos() + 1, 0, q);
            cmdBuf.setContents(text, cursorPos - 1);
        } else {
            cmdBuf.setContents(text.mid(1), cursorPos - 1);
        }
        // update search expression
        if (m_subsubmode == SearchSubSubMode) {
            updateFind(false);
            exportSelection();
        }
    }
}

// Helper to parse a-z,A-Z,48-57,_
static int someInt(const QString &str)
{
    if (str.toInt())
        return str.toInt();
    if (str.size())
        return str.at(0).unicode();
    return 0;
}

void FakeVimHandler::Private::setupCharClass()
{
    for (int i = 0; i < 256; ++i) {
        const QChar c = QChar(QLatin1Char(i));
        m_charClass[i] = c.isSpace() ? 0 : 1;
    }
    const QString conf = config(ConfigIsKeyword).toString();
    foreach (const QString &part, conf.split(QLatin1Char(','))) {
        if (part.contains(QLatin1Char('-'))) {
            const int from = someInt(part.section(QLatin1Char('-'), 0, 0));
            const int to = someInt(part.section(QLatin1Char('-'), 1, 1));
            for (int i = qMax(0, from); i <= qMin(255, to); ++i)
                m_charClass[i] = 2;
        } else {
            m_charClass[qMin(255, someInt(part))] = 2;
        }
    }
}

void FakeVimHandler::Private::moveToBoundary(bool simple, bool forward)
{
    QTextDocument *doc = document();
    QTextCursor tc(doc);
    tc.setPosition(position());
    if (forward ? tc.atBlockEnd() : tc.atBlockStart())
        return;

    QChar c = document()->characterAt(tc.position() + (forward ? -1 : 1));
    int lastClass = tc.atStart() ? -1 : charClass(c, simple);
    QTextCursor::MoveOperation op = forward ? Right : Left;
    while (true) {
        c = doc->characterAt(tc.position());
        int thisClass = charClass(c, simple);
        if (thisClass != lastClass || (forward ? tc.atBlockEnd() : tc.atBlockStart())) {
            if (tc != cursor())
                tc.movePosition(forward ? Left : Right);
            break;
        }
        lastClass = thisClass;
        tc.movePosition(op);
    }
    setPosition(tc.position());
}

void FakeVimHandler::Private::moveToNextBoundary(bool end, int count, bool simple, bool forward)
{
    int repeat = count;
    while (repeat > 0 && !(forward ? atDocumentEnd() : atDocumentStart())) {
        setPosition(position() + (forward ? 1 : -1));
        moveToBoundary(simple, forward);
        if (atBoundary(end, simple))
            --repeat;
    }
}

void FakeVimHandler::Private::moveToNextBoundaryStart(int count, bool simple, bool forward)
{
    moveToNextBoundary(false, count, simple, forward);
}

void FakeVimHandler::Private::moveToNextBoundaryEnd(int count, bool simple, bool forward)
{
    moveToNextBoundary(true, count, simple, forward);
}

void FakeVimHandler::Private::moveToBoundaryStart(int count, bool simple, bool forward)
{
    moveToNextBoundaryStart(atBoundary(false, simple) ? count - 1 : count, simple, forward);
}

void FakeVimHandler::Private::moveToBoundaryEnd(int count, bool simple, bool forward)
{
    moveToNextBoundaryEnd(atBoundary(true, simple) ? count - 1 : count, simple, forward);
}

void FakeVimHandler::Private::moveToNextWord(bool end, int count, bool simple, bool forward, bool emptyLines)
{
    int repeat = count;
    while (repeat > 0 && !(forward ? atDocumentEnd() : atDocumentStart())) {
        setPosition(position() + (forward ? 1 : -1));
        moveToBoundary(simple, forward);
        if (atWordBoundary(end, simple) && (emptyLines || !atEmptyLine()) )
            --repeat;
    }
}

void FakeVimHandler::Private::moveToNextWordStart(int count, bool simple, bool forward, bool emptyLines)
{
    moveToNextWord(false, count, simple, forward, emptyLines);
}

void FakeVimHandler::Private::moveToNextWordEnd(int count, bool simple, bool forward, bool emptyLines)
{
    moveToNextWord(true, count, simple, forward, emptyLines);
}

void FakeVimHandler::Private::moveToWordStart(int count, bool simple, bool forward, bool emptyLines)
{
    moveToNextWordStart(atWordStart(simple) ? count - 1 : count, simple, forward, emptyLines);
}

void FakeVimHandler::Private::moveToWordEnd(int count, bool simple, bool forward, bool emptyLines)
{
    moveToNextWordEnd(atWordEnd(simple) ? count - 1 : count, simple, forward, emptyLines);
}

bool FakeVimHandler::Private::handleFfTt(QString key)
{
    int key0 = key.size() == 1 ? key.at(0).unicode() : 0;
    int oldPos = position();
    // m_subsubmode \in { 'f', 'F', 't', 'T' }
    bool forward = m_subsubdata.is('f') || m_subsubdata.is('t');
    int repeat = count();
    QTextDocument *doc = document();
    int n = block().position();
    if (forward)
        n += block().length();
    int pos = position();
    while (pos != n) {
        pos += forward ? 1 : -1;
        if (pos == n)
            break;
        int uc = doc->characterAt(pos).unicode();
        if (uc == ParagraphSeparator)
            break;
        if (uc == key0)
            --repeat;
        if (repeat == 0) {
            if (m_subsubdata.is('t'))
                --pos;
            else if (m_subsubdata.is('T'))
                ++pos;

            if (forward)
                moveRight(pos - position());
            else
                moveLeft(position() - pos);
            break;
        }
    }
    if (repeat == 0) {
        setTargetColumn();
        return true;
    }
    setPosition(oldPos);
    return false;
}

void FakeVimHandler::Private::moveToMatchingParanthesis()
{
    bool moved = false;
    bool forward = false;

    const int anc = anchor();
    QTextCursor tc = cursor();
    emit q->moveToMatchingParenthesis(&moved, &forward, &tc);
    if (moved && forward)
        tc.movePosition(Left, KeepAnchor, 1);
    setAnchorAndPosition(anc, tc.position());
    setTargetColumn();
}

int FakeVimHandler::Private::cursorLineOnScreen() const
{
    if (!editor())
        return 0;
    QRect rect = EDITOR(cursorRect());
    return rect.y() / rect.height();
}

int FakeVimHandler::Private::linesOnScreen() const
{
    if (!editor())
        return 1;
    QRect rect = EDITOR(cursorRect());
    return EDITOR(height()) / rect.height();
}

int FakeVimHandler::Private::columnsOnScreen() const
{
    if (!editor())
        return 1;
    QRect rect = EDITOR(cursorRect());
    // qDebug() << "WID: " << EDITOR(width()) << "RECT: " << rect;
    return EDITOR(width()) / rect.width();
}

int FakeVimHandler::Private::cursorLine() const
{
    return lineForPosition(position()) - 1;
}

int FakeVimHandler::Private::cursorBlockNumber() const
{
    return document()->findBlock(qMin(anchor(), position())).blockNumber();
}

int FakeVimHandler::Private::physicalCursorColumn() const
{
    return position() - block().position();
}

int FakeVimHandler::Private::physicalToLogicalColumn
    (const int physical, const QString &line) const
{
    const int ts = config(ConfigTabStop).toInt();
    int p = 0;
    int logical = 0;
    while (p < physical) {
        QChar c = line.at(p);
        //if (c == QLatin1Char(' '))
        //    ++logical;
        //else
        if (c == QLatin1Char('\t'))
            logical += ts - logical % ts;
        else
            ++logical;
            //break;
        ++p;
    }
    return logical;
}

int FakeVimHandler::Private::logicalToPhysicalColumn
    (const int logical, const QString &line) const
{
    const int ts = config(ConfigTabStop).toInt();
    int physical = 0;
    for (int l = 0; l < logical && physical < line.size(); ++physical) {
        QChar c = line.at(physical);
        if (c == QLatin1Char('\t'))
            l += ts - l % ts;
        else
            ++l;
    }
    return physical;
}

int FakeVimHandler::Private::logicalCursorColumn() const
{
    const int physical = physicalCursorColumn();
    const QString line = block().text();
    return physicalToLogicalColumn(physical, line);
}

Column FakeVimHandler::Private::cursorColumn() const
{
    return Column(physicalCursorColumn(), logicalCursorColumn());
}

int FakeVimHandler::Private::linesInDocument() const
{
    if (cursor().isNull())
        return 0;
    const int count = document()->blockCount();
    // Qt inserts an empty line if the last character is a '\n',
    // but that's not how vi does it.
    return document()->lastBlock().length() <= 1 ? count - 1 : count;
}

void FakeVimHandler::Private::scrollToLine(int line)
{
    // FIXME: works only for QPlainTextEdit
    QScrollBar *scrollBar = EDITOR(verticalScrollBar());
    //qDebug() << "SCROLL: " << scrollBar->value() << line;
    scrollBar->setValue(line);
    //QTC_CHECK(firstVisibleLine() == line);
}

int FakeVimHandler::Private::firstVisibleLine() const
{
    QScrollBar *scrollBar = EDITOR(verticalScrollBar());
    if (0 && scrollBar->value() != cursorLine() - cursorLineOnScreen()) {
        qDebug() << "SCROLLBAR: " << scrollBar->value()
            << "CURSORLINE IN DOC" << cursorLine()
            << "CURSORLINE ON SCREEN" << cursorLineOnScreen();
    }
    //return scrollBar->value();
    return cursorLine() - cursorLineOnScreen();
}

void FakeVimHandler::Private::scrollUp(int count)
{
    scrollToLine(cursorLine() - cursorLineOnScreen() - count);
}

void FakeVimHandler::Private::alignViewportToCursor(AlignmentFlag align, int line,
    bool moveToNonBlank)
{
    if (line > 0)
        setPosition(firstPositionInLine(line));
    if (moveToNonBlank)
        moveToFirstNonBlankOnLine();

    if (align == Qt::AlignTop)
        scrollUp(- cursorLineOnScreen());
    else if (align == Qt::AlignVCenter)
        scrollUp(linesOnScreen() / 2 - cursorLineOnScreen());
    else if (align == Qt::AlignBottom)
        scrollUp(linesOnScreen() - cursorLineOnScreen());
}

void FakeVimHandler::Private::setCursorPosition(const CursorPosition &p)
{
    const int firstLine = firstVisibleLine();
    const int firstBlock = document()->findBlockByLineNumber(firstLine).blockNumber();
    const int lastBlock =
        document()->findBlockByLineNumber(firstLine + linesOnScreen() - 2).blockNumber();
    bool isLineVisible = firstBlock <= p.line && p.line <= lastBlock;
    QTextCursor tc = cursor();
    setCursorPosition(&tc, p);
    setCursor(tc);
    if (!isLineVisible)
        alignViewportToCursor(Qt::AlignVCenter);
}

void FakeVimHandler::Private::setCursorPosition(QTextCursor *tc, const CursorPosition &p)
{
    const int line = qMin(document()->blockCount() - 1, p.line);
    QTextBlock block = document()->findBlockByNumber(line);
    const int column = qMin(p.column, block.length() - 1);
    tc->setPosition(block.position() + column, KeepAnchor);
}

int FakeVimHandler::Private::lastPositionInDocument(bool ignoreMode) const
{
    return document()->characterCount()
        - (ignoreMode || isVisualMode() || m_mode == InsertMode || m_mode == ReplaceMode ? 1 : 2);
}

QString FakeVimHandler::Private::selectText(const Range &range) const
{
    if (range.rangemode == RangeCharMode) {
        QTextCursor tc(document());
        tc.setPosition(range.beginPos, MoveAnchor);
        tc.setPosition(range.endPos, KeepAnchor);
        return tc.selection().toPlainText();
    }
    if (range.rangemode == RangeLineMode) {
        QTextCursor tc(document());
        int firstPos = firstPositionInLine(lineForPosition(range.beginPos));
        int lastLine = lineForPosition(range.endPos);
        bool endOfDoc = lastLine == lineNumber(document()->lastBlock());
        int lastPos = endOfDoc ? lastPositionInDocument(true) : firstPositionInLine(lastLine + 1);
        tc.setPosition(firstPos, MoveAnchor);
        tc.setPosition(lastPos, KeepAnchor);
        return tc.selection().toPlainText() + QString((endOfDoc? "\n" : ""));
    }
    // FIXME: Performance?
    int beginLine = lineForPosition(range.beginPos);
    int endLine = lineForPosition(range.endPos);
    int beginColumn = 0;
    int endColumn = INT_MAX;
    if (range.rangemode == RangeBlockMode) {
        int column1 = range.beginPos - firstPositionInLine(beginLine);
        int column2 = range.endPos - firstPositionInLine(endLine);
        beginColumn = qMin(column1, column2);
        endColumn = qMax(column1, column2);
    }
    int len = endColumn - beginColumn + 1;
    QString contents;
    QTextBlock block = document()->findBlockByLineNumber(beginLine - 1);
    for (int i = beginLine; i <= endLine && block.isValid(); ++i) {
        QString line = block.text();
        if (range.rangemode == RangeBlockMode) {
            line = line.mid(beginColumn, len);
            if (line.size() < len)
                line += QString(len - line.size(), QChar(' '));
        }
        contents += line;
        if (!contents.endsWith('\n'))
            contents += '\n';
        block = block.next();
    }
    //qDebug() << "SELECTED: " << contents;
    return contents;
}

void FakeVimHandler::Private::yankText(const Range &range, int reg)
{
    setRegister(reg, selectText(range), range.rangemode);

    const int lines = document()->findBlock(range.endPos).blockNumber()
        - document()->findBlock(range.beginPos).blockNumber() + 1;
    if (lines > 2)
        showMessage(MessageInfo, FakeVimHandler::tr("%1 lines yanked").arg(lines));
}

void FakeVimHandler::Private::transformText(const Range &range,
    Transformation transformFunc, const QVariant &extra)
{
    QTextCursor tc = cursor();
    int posAfter = range.beginPos;
    switch (range.rangemode) {
        case RangeCharMode: {
            // This can span multiple lines.
            beginEditBlock();
            tc.setPosition(range.beginPos, MoveAnchor);
            tc.setPosition(range.endPos, KeepAnchor);
            TransformationData td(tc.selectedText(), extra);
            (this->*transformFunc)(&td);
            tc.insertText(td.to);
            endEditBlock();
            break;
        }
        case RangeLineMode:
        case RangeLineModeExclusive: {
            beginEditBlock();
            tc.setPosition(range.beginPos, MoveAnchor);
            tc.movePosition(StartOfLine, MoveAnchor);
            tc.setPosition(range.endPos, KeepAnchor);
            tc.movePosition(EndOfLine, KeepAnchor);
            if (range.rangemode != RangeLineModeExclusive) {
                // make sure that complete lines are removed
                // - also at the beginning and at the end of the document
                if (tc.atEnd()) {
                    tc.setPosition(range.beginPos, MoveAnchor);
                    tc.movePosition(StartOfLine, MoveAnchor);
                    if (!tc.atStart()) {
                        // also remove first line if it is the only one
                        tc.movePosition(Left, MoveAnchor, 1);
                        tc.movePosition(EndOfLine, MoveAnchor, 1);
                    }
                    tc.setPosition(range.endPos, KeepAnchor);
                    tc.movePosition(EndOfLine, KeepAnchor);
                } else {
                    tc.movePosition(Right, KeepAnchor, 1);
                }
            }
            TransformationData td(tc.selectedText(), extra);
            (this->*transformFunc)(&td);
            posAfter = tc.anchor();
            tc.insertText(td.to);
            endEditBlock();
            break;
        }
        case RangeBlockAndTailMode:
        case RangeBlockMode: {
            int beginLine = lineForPosition(range.beginPos);
            int endLine = lineForPosition(range.endPos);
            int column1 = range.beginPos - firstPositionInLine(beginLine);
            int column2 = range.endPos - firstPositionInLine(endLine);
            int beginColumn = qMin(column1, column2);
            int endColumn = qMax(column1, column2);
            if (range.rangemode == RangeBlockAndTailMode)
                endColumn = INT_MAX - 1;
            QTextBlock block = document()->findBlockByLineNumber(endLine - 1);
            beginEditBlock();
            for (int i = beginLine; i <= endLine && block.isValid(); ++i) {
                int bCol = qMin(beginColumn, block.length() - 1);
                int eCol = qMin(endColumn + 1, block.length() - 1);
                tc.setPosition(block.position() + bCol, MoveAnchor);
                tc.setPosition(block.position() + eCol, KeepAnchor);
                TransformationData td(tc.selectedText(), extra);
                (this->*transformFunc)(&td);
                tc.insertText(td.to);
                block = block.previous();
            }
            endEditBlock();
            break;
        }
    }

    setPosition(posAfter);
    setTargetColumn();
}

void FakeVimHandler::Private::insertText(const Register &reg)
{
    QTC_ASSERT(reg.rangemode == RangeCharMode,
        qDebug() << "WRONG INSERT MODE: " << reg.rangemode; return);
    setAnchor();
    cursor().insertText(reg.contents);
    //dump("AFTER INSERT");
}

void FakeVimHandler::Private::removeText(const Range &range)
{
    //qDebug() << "REMOVE: " << range;
    transformText(range, &FakeVimHandler::Private::removeTransform);
}

void FakeVimHandler::Private::removeTransform(TransformationData *td)
{
    Q_UNUSED(td);
}

void FakeVimHandler::Private::downCase(const Range &range)
{
    transformText(range, &FakeVimHandler::Private::downCaseTransform);
}

void FakeVimHandler::Private::downCaseTransform(TransformationData *td)
{
    td->to = td->from.toLower();
}

void FakeVimHandler::Private::upCase(const Range &range)
{
    transformText(range, &FakeVimHandler::Private::upCaseTransform);
}

void FakeVimHandler::Private::upCaseTransform(TransformationData *td)
{
    td->to = td->from.toUpper();
}

void FakeVimHandler::Private::invertCase(const Range &range)
{
    transformText(range, &FakeVimHandler::Private::invertCaseTransform);
}

void FakeVimHandler::Private::invertCaseTransform(TransformationData *td)
{
    foreach (QChar c, td->from)
        td->to += c.isUpper() ? c.toLower() : c.toUpper();
}

void FakeVimHandler::Private::replaceText(const Range &range, const QString &str)
{
    Transformation tr = &FakeVimHandler::Private::replaceByStringTransform;
    transformText(range, tr, str);
}

void FakeVimHandler::Private::replaceByStringTransform(TransformationData *td)
{
    td->to = td->extraData.toString();
}

void FakeVimHandler::Private::replaceByCharTransform(TransformationData *td)
{
    // Replace each character but preserve lines.
    const int len = td->from.size();
    td->to = QString(len, td->extraData.toChar());
    for (int i = 0; i < len; ++i) {
        if (td->from.at(i) == ParagraphSeparator)
            td->to[i] = ParagraphSeparator;
    }
}

void FakeVimHandler::Private::pasteText(bool afterCursor)
{
    const QString text = registerContents(m_register);
    const RangeMode rangeMode = registerRangeMode(m_register);

    beginEditBlock();

    // In visual mode paste text only inside selection.
    bool pasteAfter = isVisualMode() ? false : afterCursor;

    bool visualCharMode = isVisualCharMode();
    if (visualCharMode) {
        leaveVisualMode();
        m_rangemode = RangeCharMode;
        Range range = currentRange();
        range.endPos++;
        yankText(range, m_register);
        removeText(range);
    } else if (isVisualLineMode()) {
        leaveVisualMode();
        m_rangemode = RangeLineMode;
        Range range = currentRange();
        range.endPos++;
        yankText(range, m_register);
        removeText(range);
        handleStartOfLine();
    } else if (isVisualBlockMode()) {
        leaveVisualMode();
        m_rangemode = RangeBlockMode;
        Range range = currentRange();
        yankText(range, m_register);
        removeText(range);
        setPosition(qMin(position(), anchor()));
    }

    switch (rangeMode) {
        case RangeCharMode: {
            m_targetColumn = 0;
            const int pos = position() + 1;
            if (pasteAfter && rightDist() > 0)
                moveRight();
            insertText(text.repeated(count()));
            if (text.contains('\n'))
                setPosition(pos);
            else
                moveLeft();
            break;
        }
        case RangeLineMode:
        case RangeLineModeExclusive: {
            QTextCursor tc = cursor();
            if (visualCharMode)
                tc.insertBlock();
            else
                moveToStartOfLine();
            m_targetColumn = 0;
            bool lastLine = false;
            if (pasteAfter) {
                lastLine = document()->lastBlock() == this->block();
                if (lastLine) {
                    tc.movePosition(EndOfLine, MoveAnchor);
                    tc.insertBlock();
                }
                moveDown();
            }
            const int pos = position();
            if (lastLine)
                insertText(text.repeated(count()).left(text.size() * count() - 1));
            else
                insertText(text.repeated(count()));
            setPosition(pos);
            moveToFirstNonBlankOnLine();
            break;
        }
        case RangeBlockAndTailMode:
        case RangeBlockMode: {
            const int pos = position();
            if (pasteAfter && rightDist() > 0)
                moveRight();
            QTextCursor tc = cursor();
            const int col = tc.columnNumber();
            QTextBlock block = tc.block();
            const QStringList lines = text.split(QChar('\n'));
            for (int i = 0; i < lines.size() - 1; ++i) {
                if (!block.isValid()) {
                    tc.movePosition(EndOfDocument);
                    tc.insertBlock();
                    block = tc.block();
                }

                // resize line
                int length = block.length();
                int begin = block.position();
                if (col >= length) {
                    tc.setPosition(begin + length - 1);
                    tc.insertText(QString(col - length + 1, QChar(' ')));
                } else {
                    tc.setPosition(begin + col);
                }

                // insert text
                const QString line = lines.at(i).repeated(count());
                tc.insertText(line);

                // next line
                block = block.next();
            }
            setPosition(pos);
            if (pasteAfter)
                moveRight();
            break;
        }
    }

    endEditBlock();
}

void FakeVimHandler::Private::joinLines(int count, bool preserveSpace)
{
    for (int i = qMax(count - 2, 0); i >= 0; --i) {
        moveBehindEndOfLine();
        setAnchor();
        moveRight();
        if (preserveSpace) {
            removeText(currentRange());
        } else {
            while (characterAtCursor() == ' ' || characterAtCursor() == '\t')
                moveRight();
            cursor().insertText(QString(QLatin1Char(' ')));
        }
    }
}

QString FakeVimHandler::Private::lineContents(int line) const
{
    return document()->findBlockByLineNumber(line - 1).text();
}

void FakeVimHandler::Private::setLineContents(int line, const QString &contents)
{
    QTextBlock block = document()->findBlockByLineNumber(line - 1);
    QTextCursor tc = cursor();
    const int begin = block.position();
    const int len = block.length();
    tc.setPosition(begin);
    tc.setPosition(begin + len - 1, KeepAnchor);
    tc.insertText(contents);
}

int FakeVimHandler::Private::blockBoundary(const QString &left,
    const QString &right, bool closing, int count) const
{
    const QString &begin = closing ? left : right;
    const QString &end = closing ? right : left;

    // shift cursor if it is already on opening/closing string
    QTextCursor tc1 = cursor();
    int pos = tc1.position();
    int max = document()->characterCount();
    int sz = left.size();
    int from = qMax(pos - sz + 1, 0);
    int to = qMin(pos + sz, max);
    tc1.setPosition(from);
    tc1.setPosition(to, KeepAnchor);
    int i = tc1.selectedText().indexOf(left);
    if (i != -1) {
        // - on opening string:
        tc1.setPosition(from + i + sz);
    } else {
        sz = right.size();
        from = qMax(pos - sz + 1, 0);
        to = qMin(pos + sz, max);
        tc1.setPosition(from);
        tc1.setPosition(to, KeepAnchor);
        i = tc1.selectedText().indexOf(right);
        if (i != -1) {
            // - on closing string:
            tc1.setPosition(from + i);
        } else {
            tc1 = cursor();
        }
    }

    QTextCursor tc2 = tc1;
    QTextDocument::FindFlags flags(closing ? 0 : QTextDocument::FindBackward);
    int level = 0;
    int counter = 0;
    while (true) {
        tc2 = document()->find(end, tc2, flags);
        if (tc2.isNull())
            return -1;
        if (!tc1.isNull())
            tc1 = document()->find(begin, tc1, flags);

        while (!tc1.isNull() && (closing ? (tc1 < tc2) : (tc2 < tc1))) {
            ++level;
            tc1 = document()->find(begin, tc1, flags);
        }

        while (level > 0
               && (tc1.isNull() || (closing ? (tc2 < tc1) : (tc1 < tc2)))) {
            --level;
            tc2 = document()->find(end, tc2, flags);
            if (tc2.isNull())
                return -1;
        }

        if (level == 0
            && (tc1.isNull() || (closing ? (tc2 < tc1) : (tc1 < tc2)))) {
            ++counter;
            if (counter >= count)
                break;
        }
    }

    return tc2.position() - end.size();
}

int FakeVimHandler::Private::lineNumber(const QTextBlock &block) const
{
    if (block.isVisible())
        return block.firstLineNumber() + 1;

    // Folded block has line number of the nearest previous visible line.
    QTextBlock block2 = block;
    while (block2.isValid() && !block2.isVisible())
        block2 = block2.previous();
    return block2.firstLineNumber() + 1;
}

int FakeVimHandler::Private::firstPositionInLine(int line, bool onlyVisibleLines) const
{
    QTextBlock block = onlyVisibleLines ? document()->findBlockByLineNumber(line - 1)
        : document()->findBlockByNumber(line - 1);
    return block.position();
}

int FakeVimHandler::Private::lastPositionInLine(int line, bool onlyVisibleLines) const
{
    QTextBlock block;
    if (onlyVisibleLines) {
        // respect folds
        block = document()->findBlockByLineNumber(line);
        if (block.isValid()) {
            if (line > 0)
                block = block.previous();
        } else {
            block = document()->lastBlock();
        }
    } else {
        block = document()->findBlockByNumber(line - 1);
    }

    const int position = block.position() + block.length() - 1;
    if (block.length() > 1 && !isVisualMode() && m_mode != InsertMode && m_mode != ReplaceMode)
        return position - 1;
    return position;
}

int FakeVimHandler::Private::lineForPosition(int pos) const
{
    QTextBlock block = document()->findBlock(pos);
    return lineNumber(block);
}

void FakeVimHandler::Private::toggleVisualMode(VisualMode visualMode)
{
    if (isVisualMode()) {
        leaveVisualMode();
    } else {
        m_positionPastEnd = false;
        m_anchorPastEnd = false;
        m_visualMode = visualMode;
        m_lastVisualMode = visualMode;
        const int pos = position();
        setAnchorAndPosition(pos, pos);
        updateMiniBuffer();
    }
}

void FakeVimHandler::Private::leaveVisualMode()
{
    if (!isVisualMode())
        return;

    setMark('<', mark('<').position);
    setMark('>', mark('>').position);
    if (isVisualLineMode())
        m_movetype = MoveLineWise;
    else if (isVisualCharMode())
        m_movetype = MoveInclusive;

    m_visualMode = NoVisualMode;
    updateMiniBuffer();
}

QWidget *FakeVimHandler::Private::editor() const
{
    return m_textedit
        ? static_cast<QWidget *>(m_textedit)
        : static_cast<QWidget *>(m_plaintextedit);
}

void FakeVimHandler::Private::joinPreviousEditBlock()
{
    UNDO_DEBUG("JOIN");
    if (m_breakEditBlock) {
        beginEditBlock();
    } else {
        if (m_editBlockLevel == 0)
            m_cursor = cursor();
        ++m_editBlockLevel;
        cursor().joinPreviousEditBlock();
    }
}

void FakeVimHandler::Private::beginEditBlock(bool rememberPosition)
{
    UNDO_DEBUG("BEGIN EDIT BLOCK");
    if (m_editBlockLevel == 0)
        m_cursor = cursor();
    ++m_editBlockLevel;
    cursor().beginEditBlock();
    if (rememberPosition)
        setUndoPosition(false);
    m_breakEditBlock = false;
}

void FakeVimHandler::Private::endEditBlock()
{
    UNDO_DEBUG("END EDIT BLOCK");
    QTC_ASSERT(m_editBlockLevel > 0,
        qDebug() << "beginEditBlock() not called before endEditBlock()!"; return);
    --m_editBlockLevel;
    cursor().endEditBlock();
    if (m_editBlockLevel == 0)
        setCursor(m_cursor);
}

char FakeVimHandler::Private::currentModeCode() const
{
    if (m_submode != NoSubMode)
        return ' ';
    else if (m_mode == ExMode)
        return 'c';
    else if (isVisualMode())
        return 'v';
    else if (m_mode == CommandMode)
        return 'n';
    else
        return 'i';
}

void FakeVimHandler::Private::undoRedo(bool undo)
{
    // FIXME: That's only an approximaxtion. The real solution might
    // be to store marks and old userData with QTextBlock setUserData
    // and retrieve them afterward.
    QStack<State> &stack = undo ? m_undo : m_redo;
    QStack<State> &stack2 = undo ? m_redo : m_undo;

    CursorPosition lastPos(cursor());
    const int current = revision();
    if (undo)
        EDITOR(undo());
    else
        EDITOR(redo());
    const int rev = revision();

    // rewind/forward to last saved revision
    while (!stack.empty() && stack.top().revision > rev)
        stack.pop();

    if (current == rev) {
        const QString msg = undo ? FakeVimHandler::tr("Already at oldest change")
            : FakeVimHandler::tr("Already at newest change");
        showMessage(MessageInfo, msg);
        return;
    }
    clearMessage();

    if (!stack.empty()) {
        State &state = stack.top();
        if (state.revision == rev) {
            m_lastChangePosition = state.position;
            m_marks = state.marks;
            m_lastVisualMode = state.lastVisualMode;
            setMark('\'', lastPos);
            setCursorPosition(m_lastChangePosition);
            setAnchor();
            state.revision = current;
            stack2.push(stack.pop());
        }
    }

    setTargetColumn();
    if (atEndOfLine())
        moveLeft();
}

void FakeVimHandler::Private::undo()
{
    undoRedo(true);
}

void FakeVimHandler::Private::redo()
{
    undoRedo(false);
}

void FakeVimHandler::Private::updateCursorShape()
{
    bool thinCursor = m_mode == ExMode
            || m_subsubmode == SearchSubSubMode
            || m_mode == InsertMode
            || isVisualMode()
            || cursor().hasSelection();
    EDITOR(setOverwriteMode(!thinCursor));
}

void FakeVimHandler::Private::enterReplaceMode()
{
    m_mode = ReplaceMode;
    m_submode = NoSubMode;
    m_subsubmode = NoSubSubMode;
    m_lastInsertion.clear();
    m_lastDeletion.clear();
    g.returnToMode = ReplaceMode;
}

void FakeVimHandler::Private::enterInsertMode()
{
    m_mode = InsertMode;
    m_submode = NoSubMode;
    m_subsubmode = NoSubSubMode;
    m_lastInsertion.clear();
    m_lastDeletion.clear();
    g.returnToMode = InsertMode;
}

void FakeVimHandler::Private::enterCommandMode(Mode returnToMode)
{
    if (atEndOfLine())
        moveLeft();
    m_mode = CommandMode;
    m_submode = NoSubMode;
    m_subsubmode = NoSubSubMode;
    g.returnToMode = returnToMode;
}

void FakeVimHandler::Private::enterExMode(const QString &contents)
{
    g.currentMessage.clear();
    if (isVisualMode())
        g.commandBuffer.setContents(QString("'<,'>") + contents, contents.size() + 5);
    else
        g.commandBuffer.setContents(contents, contents.size());
    m_mode = ExMode;
    m_submode = NoSubMode;
    m_subsubmode = NoSubSubMode;
}

void FakeVimHandler::Private::recordJump()
{
    CursorPosition pos(cursor());
    setMark('\'', pos);
    if (m_jumpListUndo.isEmpty() || m_jumpListUndo.top() != pos)
        m_jumpListUndo.push(pos);
    m_jumpListRedo.clear();
    UNDO_DEBUG("jumps: " << m_jumpListUndo);
}

void FakeVimHandler::Private::jump(int distance)
{
    QStack<CursorPosition> &from = (distance > 0) ? m_jumpListRedo : m_jumpListUndo;
    QStack<CursorPosition> &to   = (distance > 0) ? m_jumpListUndo : m_jumpListRedo;
    int len = qMin(qAbs(distance), from.size());
    CursorPosition m(cursor());
    setMark('\'', m);
    for (int i = 0; i < len; ++i) {
        to.push(m);
        setCursorPosition(from.top());
        from.pop();
    }
}

Column FakeVimHandler::Private::indentation(const QString &line) const
{
    int ts = config(ConfigTabStop).toInt();
    int physical = 0;
    int logical = 0;
    int n = line.size();
    while (physical < n) {
        QChar c = line.at(physical);
        if (c == QLatin1Char(' '))
            ++logical;
        else if (c == QLatin1Char('\t'))
            logical += ts - logical % ts;
        else
            break;
        ++physical;
    }
    return Column(physical, logical);
}

QString FakeVimHandler::Private::tabExpand(int n) const
{
    int ts = config(ConfigTabStop).toInt();
    if (hasConfig(ConfigExpandTab) || ts < 1)
        return QString(n, QLatin1Char(' '));
    return QString(n / ts, QLatin1Char('\t'))
         + QString(n % ts, QLatin1Char(' '));
}

void FakeVimHandler::Private::insertAutomaticIndentation(bool goingDown)
{
    if (!hasConfig(ConfigAutoIndent) && !hasConfig(ConfigSmartIndent))
        return;

    if (hasConfig(ConfigSmartIndent)) {
        QTextBlock bl = block();
        Range range(bl.position(), bl.position());
        const int oldSize = bl.text().size();
        indentText(range, QLatin1Char('\n'));
        m_justAutoIndented = bl.text().size() - oldSize;
    } else {
        QTextBlock bl = goingDown ? block().previous() : block().next();
        QString text = bl.text();
        int pos = 0;
        int n = text.size();
        while (pos < n && text.at(pos).isSpace())
            ++pos;
        text.truncate(pos);
        // FIXME: handle 'smartindent' and 'cindent'
        insertText(text);
        m_justAutoIndented = text.size();
    }
}

bool FakeVimHandler::Private::removeAutomaticIndentation()
{
    if (!hasConfig(ConfigAutoIndent) || m_justAutoIndented == 0)
        return false;
/*
    m_tc.movePosition(StartOfLine, KeepAnchor);
    m_tc.removeSelectedText();
    m_lastInsertion.chop(m_justAutoIndented);
*/
    m_justAutoIndented = 0;
    return true;
}

void FakeVimHandler::Private::handleStartOfLine()
{
    if (hasConfig(ConfigStartOfLine))
        moveToFirstNonBlankOnLine();
}

void FakeVimHandler::Private::replay(const QString &command)
{
    //qDebug() << "REPLAY: " << quoteUnprintable(command);
    Inputs inputs(command);
    foreach (Input in, inputs) {
        if (handleDefaultKey(in) != EventHandled)
            break;
    }
}

QString FakeVimHandler::Private::visualDotCommand() const
{
    QTextCursor start(cursor());
    QTextCursor end(start);
    end.setPosition(end.anchor());

    if (isVisualCharMode())
        return QString("v%1l").arg(qAbs(start.position() - end.position()));

    if (isVisualLineMode())
        return QString("V%1j").arg(qAbs(start.blockNumber() - end.blockNumber()));

    if (isVisualBlockMode()) {
        return QString("<c-v>%1l%2j")
            .arg(qAbs(start.positionInBlock() - end.positionInBlock()))
            .arg(qAbs(start.blockNumber() - end.blockNumber()));
    }

    return QString();
}

void FakeVimHandler::Private::selectTextObject(bool simple, bool inner)
{
    bool setupAnchor = (position() == anchor());

    // set anchor if not already set
    if (setupAnchor) {
        moveToBoundaryStart(1, simple, false);
        setAnchor();
    } else {
        moveRight();
        if (atEndOfLine())
            moveRight();
    }

    const int repeat = count();
    if (inner) {
        moveToBoundaryEnd(repeat, simple);
    } else {
        for (int i = 0; i < repeat; ++i) {
            // select leading spaces
            bool leadingSpace = characterAtCursor().isSpace();
            if (leadingSpace)
                moveToNextBoundaryStart(1, simple);

            // select word
            moveToWordEnd(1, simple);

            // select trailing spaces if no leading space
            if (!leadingSpace && document()->characterAt(position() + 1).isSpace()
                && !atBlockStart()) {
                moveToNextBoundaryEnd(1, simple);
            }

            // if there are no trailing spaces in selection select all leading spaces
            // after previous character
            if (setupAnchor && (!characterAtCursor().isSpace() || atBlockEnd())) {
                int min = block().position();
                int pos = anchor();
                while (pos >= min && document()->characterAt(--pos).isSpace()) {}
                if (pos >= min)
                    setAnchorAndPosition(pos + 1, position());
            }

            if (i + 1 < repeat) {
                moveRight();
                if (atEndOfLine())
                    moveRight();
            }
        }
    }

    if (inner) {
        m_movetype = MoveInclusive;
    } else {
        m_movetype = MoveExclusive;
        moveRight();
        if (atEndOfLine())
            moveRight();
    }

    setTargetColumn();
}

void FakeVimHandler::Private::selectWordTextObject(bool inner)
{
    selectTextObject(false, inner);
}

void FakeVimHandler::Private::selectWORDTextObject(bool inner)
{
    selectTextObject(true, inner);
}

void FakeVimHandler::Private::selectSentenceTextObject(bool inner)
{
    Q_UNUSED(inner);
}

void FakeVimHandler::Private::selectParagraphTextObject(bool inner)
{
    Q_UNUSED(inner);
}

bool FakeVimHandler::Private::selectBlockTextObject(bool inner,
    char left, char right)
{
    QString sleft = QString(QLatin1Char(left));
    QString sright = QString(QLatin1Char(right));

    int p1 = blockBoundary(sleft, sright, false, count());
    if (p1 == -1)
        return false;

    int p2 = blockBoundary(sleft, sright, true, count());
    if (p2 == -1)
        return false;

    if (inner) {
        p1 += sleft.size();
    } else {
        p2 -= sright.size() - 2;
    }

    if (isVisualMode())
        --p2;

    setAnchorAndPosition(p1, p2);
    m_movetype = MoveExclusive;

    return true;
}

static bool isSign(const QChar c)
{
    return c.unicode() == '-' || c.unicode() == '+';
}

void FakeVimHandler::Private::changeNumberTextObject(int count)
{
    QTextCursor tc = cursor();
    int pos = tc.position();
    const int n = lastPositionInLine(lineForPosition(pos)) + 1;
    QTextDocument *doc = document();
    QChar c = doc->characterAt(pos);
    while (!c.isNumber()) {
        if (pos == n)
            return;
        ++pos;
        c = doc->characterAt(pos);
    }
    int p1 = pos;
    while (p1 >= 1 && doc->characterAt(p1 - 1).isNumber())
        --p1;
    if (p1 >= 1 && isSign(doc->characterAt(p1 - 1)))
        --p1;
    int p2 = pos + 1;
    while (p2 <= n - 1 && doc->characterAt(p2).isNumber())
        ++p2;
    setAnchorAndPosition(p1, p2);

    QString orig = selectText(currentRange());
    int value = orig.toInt();
    value += count;
    QString repl = QString::fromLatin1("%1").arg(value);
    replaceText(currentRange(), repl);
    setPosition(p1 + repl.size() - 1);
}

bool FakeVimHandler::Private::selectQuotedStringTextObject(bool inner,
    const QString &quote)
{
    QTextCursor tc = cursor();
    int sz = quote.size();

    QTextCursor tc1;
    QTextCursor tc2(document());
    while (tc2 <= tc) {
        tc1 = document()->find(quote, tc2);
        if (tc1.isNull() || tc1.anchor() > tc.position())
            return false;
        tc2 = document()->find(quote, tc1);
        if (tc2.isNull())
            return false;
    }

    int p1 = tc1.position();
    int p2 = tc2.position();
    if (inner) {
        p2 = qMax(p1, p2 - sz);
        if (document()->characterAt(p1) == ParagraphSeparator)
            ++p1;
    } else {
        p1 -= sz;
        p2 -= sz - 1;
    }

    setAnchorAndPosition(p1, p2);
    m_movetype = MoveExclusive;

    return true;
}

Mark FakeVimHandler::Private::mark(QChar code) const
{
    if (isVisualMode()) {
        if (code == '<')
            return CursorPosition(document(), qMin(anchor(), position()));
        if (code == '>')
            return CursorPosition(document(), qMax(anchor(), position()));
    }
    if (code == '.')
        return m_lastChangePosition;
    if (code.isUpper())
        return g.marks.value(code);

    return m_marks.value(code);
}

void FakeVimHandler::Private::setMark(QChar code, CursorPosition position)
{
    if (code.isUpper())
        g.marks[code] = Mark(position, m_currentFileName);
    else
        m_marks[code] = Mark(position);
}

bool FakeVimHandler::Private::jumpToMark(QChar mark, bool backTickMode)
{
    Mark m = this->mark(mark);
    if (!m.isValid()) {
        showMessage(MessageError, msgMarkNotSet(mark));
        return false;
    }
    if (!m.isLocal(m_currentFileName)) {
        emit q->jumpToGlobalMark(mark, backTickMode, m.fileName);
        return false;
    }

    if (mark == '\'' && !m_jumpListUndo.isEmpty())
        m_jumpListUndo.pop();
    recordJump();
    setCursorPosition(m.position);
    if (!backTickMode)
        moveToFirstNonBlankOnLine();
    if (m_submode == NoSubMode)
        setAnchor();
    setTargetColumn();

    return true;
}

RangeMode FakeVimHandler::Private::registerRangeMode(int reg) const
{
    bool isClipboard;
    bool isSelection;
    getRegisterType(reg, &isClipboard, &isSelection);

    if (isClipboard || isSelection) {
        QClipboard *clipboard = QApplication::clipboard();
        QClipboard::Mode mode = isClipboard ? QClipboard::Clipboard : QClipboard::Selection;

        // Use range mode from Vim's clipboard data if available.
        const QMimeData *data = clipboard->mimeData(mode);
        if (data != 0 && data->hasFormat(vimMimeText)) {
            QByteArray bytes = data->data(vimMimeText);
            if (bytes.length() > 0)
                return static_cast<RangeMode>(bytes.at(0));
        }

        // If register content is clipboard:
        //  - return RangeLineMode if text ends with new line char,
        //  - return RangeCharMode otherwise.
        QString text = clipboard->text(mode);
        return (text.endsWith('\n') || text.endsWith('\r')) ? RangeLineMode : RangeCharMode;
    }

    return g.registers[reg].rangemode;
}

void FakeVimHandler::Private::setRegister(int reg, const QString &contents, RangeMode mode)
{
    bool copyToClipboard;
    bool copyToSelection;
    getRegisterType(reg, &copyToClipboard, &copyToSelection);

    QString contents2 = contents;
    if (mode == RangeLineMode && !contents2.endsWith('\n'))
        contents2.append('\n');

    if (copyToClipboard || copyToSelection) {
        if (copyToClipboard)
            setClipboardData(contents2, mode, QClipboard::Clipboard);
        if (copyToSelection)
            setClipboardData(contents2, mode, QClipboard::Selection);
    } else {
        g.registers[reg].contents = contents2;
        g.registers[reg].rangemode = mode;
    }
}

QString FakeVimHandler::Private::registerContents(int reg) const
{
    bool copyFromClipboard;
    bool copyFromSelection;
    getRegisterType(reg, &copyFromClipboard, &copyFromSelection);

    if (copyFromClipboard || copyFromSelection) {
        QClipboard *clipboard = QApplication::clipboard();
        if (copyFromClipboard)
            return clipboard->text(QClipboard::Clipboard);
        if (copyFromSelection)
            return clipboard->text(QClipboard::Selection);
    }

    return g.registers[reg].contents;
}

void FakeVimHandler::Private::getRegisterType(int reg, bool *isClipboard, bool *isSelection) const
{
    bool clipboard = false;
    bool selection = false;

    if (reg == '"') {
        QStringList list = config(ConfigClipboard).toString().split(',');
        clipboard = list.contains(QString("unnamedplus"));
        selection = list.contains(QString("unnamed"));
    } else if (reg == '+') {
        clipboard = true;
    } else if (reg == '*') {
        selection = true;
    }

    // selection (primary) is clipboard on systems without selection support
    if (selection && !QApplication::clipboard()->supportsSelection()) {
        clipboard = true;
        selection = false;
    }

    if (isClipboard != 0)
        *isClipboard = clipboard;
    if (isSelection != 0)
        *isSelection = selection;
}

///////////////////////////////////////////////////////////////////////
//
// FakeVimHandler
//
///////////////////////////////////////////////////////////////////////

FakeVimHandler::FakeVimHandler(QWidget *widget, QObject *parent)
    : QObject(parent), d(new Private(this, widget))
{}

FakeVimHandler::~FakeVimHandler()
{
    delete d;
}

// gracefully handle that the parent editor is deleted
void FakeVimHandler::disconnectFromEditor()
{
    d->m_textedit = 0;
    d->m_plaintextedit = 0;
}

bool FakeVimHandler::eventFilter(QObject *ob, QEvent *ev)
{
    bool active = theFakeVimSetting(ConfigUseFakeVim)->value().toBool();

    // Catch mouse events on the viewport.
    QWidget *viewport = 0;
    if (d->m_plaintextedit)
        viewport = d->m_plaintextedit->viewport();
    else if (d->m_textedit)
        viewport = d->m_textedit->viewport();
    if (ob == viewport) {
        if (active && ev->type() == QEvent::MouseButtonRelease) {
            QMouseEvent *mev = static_cast<QMouseEvent *>(ev);
            if (mev->button() == Qt::LeftButton) {
                d->importSelection();
                //return true;
            }
        }
        if (active && ev->type() == QEvent::MouseButtonPress) {
            QMouseEvent *mev = static_cast<QMouseEvent *>(ev);
            if (mev->button() == Qt::LeftButton) {
                d->m_visualMode = NoVisualMode;
            }
        }
        return QObject::eventFilter(ob, ev);
    }

    if (active && ev->type() == QEvent::Shortcut) {
        d->passShortcuts(false);
        return false;
    }

    if (active && ev->type() == QEvent::InputMethod && ob == d->editor()) {
        // This handles simple dead keys. The sequence of events is
        // KeyRelease-InputMethod-KeyRelease  for dead keys instead of
        // KeyPress-KeyRelease as for simple keys. As vi acts on key presses,
        // we have to act on the InputMethod event.
        // FIXME: A first approximation working for e.g. ^ on a German keyboard
        QInputMethodEvent *imev = static_cast<QInputMethodEvent *>(ev);
        KEY_DEBUG("INPUTMETHOD" << imev->commitString() << imev->preeditString());
        QString commitString = imev->commitString();
        int key = commitString.size() == 1 ? commitString.at(0).unicode() : 0;
        QKeyEvent kev(QEvent::KeyPress, key, Qt::KeyboardModifiers(), commitString);
        EventResult res = d->handleEvent(&kev);
        return res == EventHandled || res == EventCancelled;
    }

    if (active && ev->type() == QEvent::KeyPress &&
        (ob == d->editor() || (d->m_mode == ExMode || d->m_subsubmode == SearchSubSubMode))) {
        QKeyEvent *kev = static_cast<QKeyEvent *>(ev);
        KEY_DEBUG("KEYPRESS" << kev->key() << kev->text() << QChar(kev->key()));
        EventResult res = d->handleEvent(kev);
        //if (d->m_mode == InsertMode)
        //    emit completionRequested();
        // returning false core the app see it
        //KEY_DEBUG("HANDLED CODE:" << res);
        //return res != EventPassedToCore;
        //return true;
        return res == EventHandled || res == EventCancelled;
    }

    if (active && ev->type() == QEvent::ShortcutOverride && ob == d->editor()) {
        QKeyEvent *kev = static_cast<QKeyEvent *>(ev);
        if (d->wantsOverride(kev)) {
            KEY_DEBUG("OVERRIDING SHORTCUT" << kev->key());
            ev->accept(); // accepting means "don't run the shortcuts"
            return true;
        }
        KEY_DEBUG("NO SHORTCUT OVERRIDE" << kev->key());
        return true;
    }

    if (active && ev->type() == QEvent::FocusIn && ob == d->editor()) {
        d->focus();
    }

    return QObject::eventFilter(ob, ev);
}

void FakeVimHandler::installEventFilter()
{
    d->installEventFilter();
}

void FakeVimHandler::setupWidget()
{
    d->setupWidget();
}

void FakeVimHandler::restoreWidget(int tabSize)
{
    d->restoreWidget(tabSize);
}

void FakeVimHandler::handleCommand(const QString &cmd)
{
    d->handleCommand(cmd);
}

void FakeVimHandler::handleReplay(const QString &keys)
{
    d->replay(keys);
}

void FakeVimHandler::handleInput(const QString &keys)
{
    Inputs inputs(keys);
    foreach (const Input &input, inputs)
        d->handleKey(input);
}

void FakeVimHandler::setCurrentFileName(const QString &fileName)
{
   d->m_currentFileName = fileName;
}

QString FakeVimHandler::currentFileName() const
{
    return d->m_currentFileName;
}

void FakeVimHandler::showMessage(MessageLevel level, const QString &msg)
{
   d->showMessage(level, msg);
}

QWidget *FakeVimHandler::widget()
{
    return d->editor();
}

// Test only
int FakeVimHandler::physicalIndentation(const QString &line) const
{
    Column ind = d->indentation(line);
    return ind.physical;
}

int FakeVimHandler::logicalIndentation(const QString &line) const
{
    Column ind = d->indentation(line);
    return ind.logical;
}

QString FakeVimHandler::tabExpand(int n) const
{
    return d->tabExpand(n);
}

void FakeVimHandler::miniBufferTextEdited(const QString &text, int cursorPos)
{
    d->miniBufferTextEdited(text, cursorPos);
}

void FakeVimHandler::setTextCursorPosition(int position)
{
    int pos = qMax(0, qMin(position, d->lastPositionInDocument()));
    if (d->isVisualMode())
        d->setPosition(pos);
    else
        d->setAnchorAndPosition(pos, pos);
    d->setTargetColumn();
}

bool FakeVimHandler::jumpToLocalMark(QChar mark, bool backTickMode)
{
    return d->jumpToMark(mark, backTickMode);
}

} // namespace Internal
} // namespace FakeVim

#include "fakevimhandler.moc"
