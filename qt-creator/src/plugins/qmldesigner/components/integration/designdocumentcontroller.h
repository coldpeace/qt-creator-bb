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

#ifndef DesignDocumentController_h
#define DesignDocumentController_h

#include "rewriterview.h"

#include <model.h>
#include <rewriterview.h>
#include <itemlibraryview.h>
#include <navigatorview.h>
#include <stateseditorview.h>
#include <formeditorview.h>
#include <propertyeditor.h>
#include <componentview.h>
#include <basetexteditmodifier.h>
#include <componenttextmodifier.h>
#include <subcomponentmanager.h>
#include <model/viewlogger.h>

#include <QObject>
#include <QString>

#include <QStackedWidget>

QT_BEGIN_NAMESPACE
class QUndoStack;
class QWidget;
class QIODevice;
class QProcess;
class QPlainTextEdit;
class QDeclarativeError;
QT_END_NAMESPACE

namespace QmlDesigner {

class ModelNode;
class TextModifier;
class QmlObjectNode;
struct CrumbleBarInfo;

class DesignDocumentController: public QObject
{
    Q_OBJECT
public:
    DesignDocumentController(QObject *parent);
    ~DesignDocumentController();

    QString displayName() const;
    QString simplfiedDisplayName() const;

    QString fileName() const;
    void setFileName(const QString &m_fileName);

    QList<RewriterView::Error> loadMaster(QPlainTextEdit *edit);
    QList<RewriterView::Error> loadMaster(const QByteArray &qml);
    void loadCurrentModel();
    void close();

    bool isDirty() const;
    bool isUndoAvailable() const;
    bool isRedoAvailable() const;

    Model *model() const;
    Model *masterModel() const;

    RewriterView *rewriterView() const;

    bool isModelSyncBlocked() const;
    void blockModelSync(bool block);

    QString contextHelpId() const;
    QList<RewriterView::Error> qmlErrors() const;

    void setItemLibraryView(ItemLibraryView* m_itemLibraryView);
    void setNavigator(NavigatorView* navigatorView);
    void setPropertyEditorView(PropertyEditor *propertyEditor);
    void setStatesEditorView(StatesEditorView* m_statesEditorView);
    void setFormEditorView(FormEditorView *m_formEditorView);
    void setNodeInstanceView(NodeInstanceView *m_nodeInstanceView);
    void setComponentView(ComponentView *m_componentView);

    void setCrumbleBarInfo(const CrumbleBarInfo &crumbleBarInfo);
    static void setBlockCrumbleBar(bool);

signals:
    void displayNameChanged(const QString &newFileName);
    void dirtyStateChanged(bool newState);

    void undoAvailable(bool isAvailable);
    void redoAvailable(bool isAvailable);
    void designDocumentClosed();
    void qmlErrorsChanged(const QList<RewriterView::Error> &errors);

    void fileToOpen(const QString &path);

public slots:
    bool save(QWidget *parent = 0);
    void saveAs(QWidget *parent = 0);
    void deleteSelected();
    void copySelected();
    void cutSelected();
    void paste();
    void selectAll();
    void undo();
    void redo();
    void activeQtVersionChanged();
    void changeCurrentModelTo(const ModelNode &node);
    void changeToSubComponent(const ModelNode &node);
    void changeToExternalSubComponent(const QString &m_fileName);
    void goIntoComponent();

#ifdef ENABLE_TEXT_VIEW
    void showText();
    void showForm();
#endif // ENABLE_TEXT_VIEW

private slots:
    void doRealSaveAs(const QString &m_fileName);

private: // functions
    void detachNodeInstanceView();
    void attachNodeInstanceView();
    void changeToMasterModel();
    QVariant createCrumbleBarInfo();

    QWidget *centralWidget() const;
    QString pathToQt() const;

private: // variables
    QWeakPointer<FormEditorView> m_formEditorView;

    QWeakPointer<ItemLibraryView> m_itemLibraryView;
    QWeakPointer<NavigatorView> m_navigator;
    QWeakPointer<PropertyEditor> m_propertyEditorView;
    QWeakPointer<StatesEditorView> m_statesEditorView;
    QWeakPointer<QStackedWidget> m_stackedWidget;
    QWeakPointer<NodeInstanceView> m_nodeInstanceView;
    QWeakPointer<ComponentView> m_componentView;

    QWeakPointer<Model> m_model;
    QWeakPointer<Model> m_subComponentModel;
    QWeakPointer<Model> m_masterModel;
    QWeakPointer<QPlainTextEdit> m_textEdit;
    QWeakPointer<RewriterView> m_rewriterView;
    BaseTextEditModifier *m_textModifier;
    ComponentTextModifier *m_componentTextModifier;
    QWeakPointer<SubComponentManager> m_subComponentManager;
    QWeakPointer<Internal::ViewLogger> m_viewLogger;
    ModelNode m_componentNode;

    QString m_fileName;
    QUrl m_searchPath;
    bool m_documentLoaded;
    bool m_syncBlocked;
    int m_qt_versionId;
    static bool s_clearCrumblePath;
    static bool s_pushCrumblePath;
};


struct CrumbleBarInfo {
    ModelNode modelNode;
    QString fileName;
};

} // namespace QmlDesigner

Q_DECLARE_METATYPE(QmlDesigner::CrumbleBarInfo)

#endif // DesignDocumentController_h
