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

#ifndef CPPTOOLS_H
#define CPPTOOLS_H

#include "cpptools_global.h"

#include <extensionsystem/iplugin.h>
#include <projectexplorer/projectexplorer.h>
#include <find/ifindfilter.h>
#include <utils/filesearch.h>

#include <QTextDocument>
#include <QKeySequence>
#include <QSharedPointer>
#include <QFutureInterface>
#include <QPointer>
#include <QFutureWatcher>
#include <QHash>

QT_BEGIN_NAMESPACE
class QFileInfo;
class QDir;
QT_END_NAMESPACE

namespace CppTools {

class CppToolsSettings;

namespace Internal {

class CppModelManager;
struct CppFileSettings;

class CppToolsPlugin : public ExtensionSystem::IPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QtCreatorPlugin" FILE "CppTools.json")

public:
    CppToolsPlugin();
    ~CppToolsPlugin();

    bool initialize(const QStringList &arguments, QString *errorMessage);
    void extensionsInitialized();
    ShutdownFlag aboutToShutdown();

private slots:
    void switchHeaderSource();
#ifdef WITH_TESTS

    // codegen tests
    void test_codegen_public_in_empty_class();
    void test_codegen_public_in_nonempty_class();
    void test_codegen_public_before_protected();
    void test_codegen_private_after_protected();
    void test_codegen_protected_in_nonempty_class();
    void test_codegen_protected_between_public_and_private();
    void test_codegen_qtdesigner_integration();
    void test_codegen_definition_empty_class();
    void test_codegen_definition_first_member();
    void test_codegen_definition_last_member();
    void test_codegen_definition_middle_member();

    void test_completion_forward_declarations_present();
    void test_completion_basic_1();
    void test_completion_template_1();
    void test_completion_template_2();
    void test_completion_template_3();
    void test_completion_template_4();
    void test_completion_template_5();
    void test_completion_template_6();
    void test_completion_template_as_base();
    void test_completion_template_as_base_data();
    void test_completion_use_global_identifier_as_base_class();
    void test_completion_use_global_identifier_as_base_class_data();
    void test_completion_base_class_has_name_the_same_as_derived();
    void test_completion_base_class_has_name_the_same_as_derived_data();
    void test_completion_cyclic_inheritance();
    void test_completion_cyclic_inheritance_data();
    void test_completion_enclosing_template_class();
    void test_completion_enclosing_template_class_data();

private:
    void test_completion();
#endif

private:
    QSharedPointer<CppFileSettings> m_fileSettings;
    CppToolsSettings *m_settings;
};

} // namespace Internal
} // namespace CppTools

#endif // CPPTOOLS_H
