/**************************************************************************
**
** Copyright (c) 2012 Nicolas Arnaud-Cormos
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

#include "actionmacrohandler.h"
#include "macroevent.h"
#include "macro.h"

#include <texteditor/texteditorconstants.h>

#include <coreplugin/icore.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/id.h>
#include <coreplugin/icontext.h>

#include <QObject>
#include <QEvent>
#include <QSignalMapper>
#include <QtAlgorithms>
#include <QStringList>

#include <QAction>
#include <QShortcut>

using namespace Macros;
using namespace Macros::Internal;

static const char EVENTNAME[] = "Action";
static quint8 ACTIONNAME = 0;

ActionMacroHandler::ActionMacroHandler():
    m_mapper(new QSignalMapper(this))
{
    connect(m_mapper, SIGNAL(mapped(QString)),
            this, SLOT(addActionEvent(QString)));

    connect(Core::ActionManager::instance(), SIGNAL(commandAdded(QString)),
            this, SLOT(addCommand(QString)));

    // Register all existing scriptable actions
    QList<Core::Command *> commands = Core::ActionManager::commands();
    foreach (Core::Command *command, commands) {
        if (command->isScriptable()) {
            QString id = command->id().toString();
            registerCommand(id);
        }
    }
}

bool ActionMacroHandler::canExecuteEvent(const MacroEvent &macroEvent)
{
    return (macroEvent.id() == EVENTNAME);
}

bool ActionMacroHandler::executeEvent(const MacroEvent &macroEvent)
{
    QAction *action = Core::ActionManager::command(Core::Id(macroEvent.value(ACTIONNAME).toString()))->action();
    if (!action)
        return false;

    action->trigger();
    return true;
}

void ActionMacroHandler::addActionEvent(const QString &id)
{
    if (!isRecording())
        return;

    const Core::Command *cmd = Core::ActionManager::command(Core::Id(id));
    if (cmd->isScriptable(cmd->context())) {
        MacroEvent e;
        e.setId(EVENTNAME);
        e.setValue(ACTIONNAME, id);
        addMacroEvent(e);
    }
}

void ActionMacroHandler::registerCommand(const QString &id)
{
    if (!m_commandIds.contains(id)) {
        m_commandIds.insert(id);
        QAction* action = Core::ActionManager::command(Core::Id(id))->action();
        if (action) {
            connect(action, SIGNAL(triggered()), m_mapper, SLOT(map()));
            m_mapper->setMapping(action, id);
            return;
        }
        QShortcut* shortcut = Core::ActionManager::command(Core::Id(id))->shortcut();
        if (shortcut) {
            connect(shortcut, SIGNAL(activated()), m_mapper, SLOT(map()));
            m_mapper->setMapping(shortcut, id);
        }
    }
}

void ActionMacroHandler::addCommand(const QString &id)
{
    if (Core::ActionManager::command(Core::Id(id))->isScriptable())
        registerCommand(id);
}
