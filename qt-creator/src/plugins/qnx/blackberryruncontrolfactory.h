/**************************************************************************
**
** Copyright (C) 2011 - 2012 Research In Motion
**
** Contact: Research In Motion (blackberry-qt@qnx.com)
** Contact: KDAB (info@kdab.com)
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

#ifndef QNX_INTERNAL_BLACKBERRYRUNCONTROLFACTORY_H
#define QNX_INTERNAL_BLACKBERRYRUNCONTROLFACTORY_H

#include <debugger/debuggerstartparameters.h>

#include <projectexplorer/runconfiguration.h>

namespace RemoteLinux {
class RemoteLinuxRunConfiguration;
}

namespace Qnx {
namespace Internal {

class BlackBerryRunConfiguration;

class BlackBerryRunControlFactory : public ProjectExplorer::IRunControlFactory
{
    Q_OBJECT

public:
    BlackBerryRunControlFactory(QObject *parent = 0);

    bool canRun(ProjectExplorer::RunConfiguration *runConfiguration,
                ProjectExplorer::RunMode mode) const;
    ProjectExplorer::RunControl *create(ProjectExplorer::RunConfiguration *runConfiguration,
                                        ProjectExplorer::RunMode mode,
                                        QString *errorMessage);

    QString displayName() const;

    ProjectExplorer::RunConfigWidget *createConfigurationWidget(
            ProjectExplorer::RunConfiguration *runConfiguration);

private:
    static Debugger::DebuggerStartParameters startParameters( const BlackBerryRunConfiguration *runConfig);

    mutable QMap<QString, QPointer<ProjectExplorer::RunControl> > m_activeRunControls;
};

} // namespace Internal
} // namespace Qnx

#endif // QNX_INTERNAL_BLACKBERRYRUNCONTROLFACTORY_H
