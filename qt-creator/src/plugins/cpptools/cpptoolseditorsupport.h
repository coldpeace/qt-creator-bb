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

#ifndef CPPTOOLSEDITORSUPPORT_H
#define CPPTOOLSEDITORSUPPORT_H

#include <QObject>
#include <QPointer>
#include <QFuture>
#include <QScopedPointer>
#include <QSharedPointer>

#include <QTextCursor>

#include <cplusplus/CppDocument.h>

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

namespace CPlusPlus {
    class AST;
}

namespace TextEditor {
    class ITextEditor;
    class ITextMark;
} // namespace TextEditor

namespace CppTools {
namespace Internal {

class CppModelManager;

class CppEditorSupport: public QObject
{
    Q_OBJECT

public:
    CppEditorSupport(CppModelManager *modelManager);
    virtual ~CppEditorSupport();

    TextEditor::ITextEditor *textEditor() const;
    void setTextEditor(TextEditor::ITextEditor *textEditor);

    int updateDocumentInterval() const;
    void setUpdateDocumentInterval(int updateDocumentInterval);

    QString contents();
    unsigned editorRevision() const;

Q_SIGNALS:
    void contentsChanged();

private Q_SLOTS:
    void updateDocument();
    void updateDocumentNow();

private:
    enum { UPDATE_DOCUMENT_DEFAULT_INTERVAL = 150 };

    CppModelManager *_modelManager;
    QPointer<TextEditor::ITextEditor> _textEditor;
    QTimer *_updateDocumentTimer;
    int _updateDocumentInterval;
    QFuture<void> _documentParser;
    QString _cachedContents;
    unsigned _revision;
};

} // namespace Internal
} // namespace CppTools

#endif // CPPTOOLSEDITORSUPPORT_H
