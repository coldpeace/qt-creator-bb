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

#include "kitinformation.h"

#include "devicesupport/desktopdevice.h"
#include "devicesupport/devicemanager.h"
#include "projectexplorerconstants.h"
#include "kit.h"
#include "kitinformationconfigwidget.h"
#include "toolchain.h"
#include "toolchainmanager.h"

#include <extensionsystem/pluginmanager.h>
#include <projectexplorer/abi.h>
#include <utils/pathchooser.h>

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

namespace ProjectExplorer {

// --------------------------------------------------------------------------
// SysRootInformation:
// --------------------------------------------------------------------------

static const char SYSROOT_INFORMATION[] = "PE.Profile.SysRoot";

SysRootKitInformation::SysRootKitInformation()
{
    setObjectName(QLatin1String("SysRootInformation"));
}

Core::Id SysRootKitInformation::dataId() const
{
    static const Core::Id id(SYSROOT_INFORMATION);
    return id;
}

unsigned int SysRootKitInformation::priority() const
{
    return 32000;
}

QVariant SysRootKitInformation::defaultValue(Kit *k) const
{
    Q_UNUSED(k)
    return QString();
}

QList<Task> SysRootKitInformation::validate(const Kit *k) const
{
    QList<Task> result;
    const Utils::FileName dir = SysRootKitInformation::sysRoot(k);
    if (!dir.toFileInfo().isDir() && SysRootKitInformation::hasSysRoot(k)) {
        result << Task(Task::Error, tr("Sys Root \"%1\" is not a directory.").arg(dir.toUserOutput()),
                       Utils::FileName(), -1, Core::Id(Constants::TASK_CATEGORY_BUILDSYSTEM));
    }
    return result;
}

KitConfigWidget *SysRootKitInformation::createConfigWidget(Kit *k) const
{
    Q_ASSERT(k);
    return new Internal::SysRootInformationConfigWidget(k);
}

KitInformation::ItemList SysRootKitInformation::toUserOutput(Kit *k) const
{
    return ItemList() << qMakePair(tr("Sys Root"), sysRoot(k).toUserOutput());
}

bool SysRootKitInformation::hasSysRoot(const Kit *k)
{
    if (k)
        return !k->value(Core::Id(SYSROOT_INFORMATION)).toString().isEmpty();
    return false;
}

Utils::FileName SysRootKitInformation::sysRoot(const Kit *k)
{
    if (!k)
        return Utils::FileName();
    return Utils::FileName::fromString(k->value(Core::Id(SYSROOT_INFORMATION)).toString());
}

void SysRootKitInformation::setSysRoot(Kit *k, const Utils::FileName &v)
{
    k->setValue(Core::Id(SYSROOT_INFORMATION), v.toString());
}

// --------------------------------------------------------------------------
// ToolChainInformation:
// --------------------------------------------------------------------------

static const char TOOLCHAIN_INFORMATION[] = "PE.Profile.ToolChain";

ToolChainKitInformation::ToolChainKitInformation()
{
    setObjectName(QLatin1String("ToolChainInformation"));
    connect(ToolChainManager::instance(), SIGNAL(toolChainRemoved(ProjectExplorer::ToolChain*)),
            this, SIGNAL(validationNeeded()));
    connect(ToolChainManager::instance(), SIGNAL(toolChainUpdated(ProjectExplorer::ToolChain*)),
            this, SIGNAL(validationNeeded()));
    connect(ToolChainManager::instance(), SIGNAL(toolChainUpdated(ProjectExplorer::ToolChain*)),
            this, SLOT(toolChainUpdated(ProjectExplorer::ToolChain*)));
}

Core::Id ToolChainKitInformation::dataId() const
{
    static const Core::Id id(TOOLCHAIN_INFORMATION);
    return id;
}

unsigned int ToolChainKitInformation::priority() const
{
    return 30000;
}

QVariant ToolChainKitInformation::defaultValue(Kit *k) const
{
    Q_UNUSED(k);
    QList<ToolChain *> tcList = ToolChainManager::instance()->toolChains();
    if (tcList.isEmpty())
        return QString();

    ProjectExplorer::Abi abi = ProjectExplorer::Abi::hostAbi();

    foreach (ToolChain *tc, tcList) {
        if (tc->targetAbi() == abi)
            return tc->id();
    }

    return tcList.at(0)->id();
}

QList<Task> ToolChainKitInformation::validate(const Kit *k) const
{
    QList<Task> result;
    if (!toolChain(k)) {
        result << Task(Task::Error, ToolChainKitInformation::msgNoToolChainInTarget(),
                       Utils::FileName(), -1, Core::Id(Constants::TASK_CATEGORY_BUILDSYSTEM));
    }
    return result;
}

void ToolChainKitInformation::fix(Kit *k)
{
    if (toolChain(k))
        return;

    qWarning("Tool chain is no longer known, removing from kit \"%s\".",
             qPrintable(k->displayName()));
    setToolChain(k, 0); // make sure to clear out no longer known tool chains
}

KitConfigWidget *ToolChainKitInformation::createConfigWidget(Kit *k) const
{
    Q_ASSERT(k);
    return new Internal::ToolChainInformationConfigWidget(k);
}

QString ToolChainKitInformation::displayNamePostfix(const Kit *k) const
{
    ToolChain *tc = toolChain(k);
    return tc ? tc->displayName() : QString();
}

KitInformation::ItemList ToolChainKitInformation::toUserOutput(Kit *k) const
{
    ToolChain *tc = toolChain(k);
    return ItemList() << qMakePair(tr("Compiler"), tc ? tc->displayName() : tr("None"));
}

void ToolChainKitInformation::addToEnvironment(const Kit *k, Utils::Environment &env) const
{
    ToolChain *tc = toolChain(k);
    if (tc)
        tc->addToEnvironment(env);
}

IOutputParser *ToolChainKitInformation::createOutputParser(const Kit *k) const
{
    ToolChain *tc = toolChain(k);
    if (tc)
        return tc->outputParser();
    return 0;
}

ToolChain *ToolChainKitInformation::toolChain(const Kit *k)
{
    if (!k)
        return 0;
    const QString id = k->value(Core::Id(TOOLCHAIN_INFORMATION)).toString();
    if (id.isEmpty())
        return 0;

    ToolChain *tc = ToolChainManager::instance()->findToolChain(id);
    if (tc)
        return tc;

    // ID is not found: Might be an ABI string...
    foreach (ToolChain *current, ToolChainManager::instance()->toolChains()) {
        if (current->targetAbi().toString() == id)
            return current;
    }
    return 0;
}

void ToolChainKitInformation::setToolChain(Kit *k, ToolChain *tc)
{
    k->setValue(Core::Id(TOOLCHAIN_INFORMATION), tc ? tc->id() : QString());
}

QString ToolChainKitInformation::msgNoToolChainInTarget()
{
    return tr("No compiler set in kit.");
}

void ToolChainKitInformation::toolChainUpdated(ToolChain *tc)
{
    foreach (Kit *k, KitManager::instance()->kits())
        if (toolChain(k) == tc)
            notifyAboutUpdate(k);
}

// --------------------------------------------------------------------------
// DeviceTypeInformation:
// --------------------------------------------------------------------------

static const char DEVICETYPE_INFORMATION[] = "PE.Profile.DeviceType";

DeviceTypeKitInformation::DeviceTypeKitInformation()
{
    setObjectName(QLatin1String("DeviceTypeInformation"));
}

Core::Id DeviceTypeKitInformation::dataId() const
{
    static const Core::Id id(DEVICETYPE_INFORMATION);
    return id;
}

unsigned int DeviceTypeKitInformation::priority() const
{
    return 33000;
}

QVariant DeviceTypeKitInformation::defaultValue(Kit *k) const
{
    Q_UNUSED(k);
    return QByteArray(Constants::DESKTOP_DEVICE_TYPE);
}

QList<Task> DeviceTypeKitInformation::validate(const Kit *k) const
{
    Q_UNUSED(k);
    return QList<Task>();
}

KitConfigWidget *DeviceTypeKitInformation::createConfigWidget(Kit *k) const
{
    return new Internal::DeviceTypeInformationConfigWidget(k);
}

KitInformation::ItemList DeviceTypeKitInformation::toUserOutput(Kit *k) const
{
    Core::Id type = deviceTypeId(k);
    QString typeDisplayName = tr("Unknown device type");
    if (type.isValid()) {
        QList<IDeviceFactory *> factories
                = ExtensionSystem::PluginManager::instance()->getObjects<IDeviceFactory>();
        foreach (IDeviceFactory *factory, factories) {
            if (factory->availableCreationIds().contains(type)) {
                typeDisplayName = factory->displayNameForId(type);
                break;
            }
        }
    }
    return ItemList() << qMakePair(tr("Device type"), typeDisplayName);
}

const Core::Id DeviceTypeKitInformation::deviceTypeId(const Kit *k)
{
    if (!k)
        return Core::Id();
    return Core::Id(k->value(Core::Id(DEVICETYPE_INFORMATION)).toByteArray());
}

void DeviceTypeKitInformation::setDeviceTypeId(Kit *k, Core::Id type)
{
    k->setValue(Core::Id(DEVICETYPE_INFORMATION), type.name());
}

// --------------------------------------------------------------------------
// DeviceInformation:
// --------------------------------------------------------------------------

static const char DEVICE_INFORMATION[] = "PE.Profile.Device";

DeviceKitInformation::DeviceKitInformation()
{
    setObjectName(QLatin1String("DeviceInformation"));
    connect(DeviceManager::instance(), SIGNAL(deviceRemoved(Core::Id)),
            this, SIGNAL(validationNeeded()));
    connect(DeviceManager::instance(), SIGNAL(deviceUpdated(Core::Id)),
            this, SIGNAL(validationNeeded()));
    connect(DeviceManager::instance(), SIGNAL(deviceUpdated(Core::Id)),
            this, SLOT(deviceUpdated(Core::Id)));
}

Core::Id DeviceKitInformation::dataId() const
{
    static const Core::Id id(DEVICE_INFORMATION);
    return id;
}

unsigned int DeviceKitInformation::priority() const
{
    return 32000;
}

QVariant DeviceKitInformation::defaultValue(Kit *k) const
{
    Core::Id type = DeviceTypeKitInformation::deviceTypeId(k);
    IDevice::ConstPtr dev = DeviceManager::instance()->defaultDevice(type);
    return dev.isNull() ? QString() : dev->id().toString();
}

QList<Task> DeviceKitInformation::validate(const Kit *k) const
{
    IDevice::ConstPtr dev = DeviceKitInformation::device(k);
    QList<Task> result;
    if (!dev.isNull() && dev->type() != DeviceTypeKitInformation::deviceTypeId(k))
        result.append(Task(Task::Error, tr("Device does not match device type."),
                           Utils::FileName(), -1, Core::Id(Constants::TASK_CATEGORY_BUILDSYSTEM)));
    if (dev.isNull())
        result.append(Task(Task::Warning, tr("No Device set."),
                           Utils::FileName(), -1, Core::Id(Constants::TASK_CATEGORY_BUILDSYSTEM)));
    return result;
}

void DeviceKitInformation::fix(Kit *k)
{
    IDevice::ConstPtr dev = DeviceKitInformation::device(k);
    if (!dev.isNull() && dev->type() == DeviceTypeKitInformation::deviceTypeId(k))
        return;

    const QString id = defaultValue(k).toString();
    setDeviceId(k, id.isEmpty() ? Core::Id() : Core::Id(id));
}

KitConfigWidget *DeviceKitInformation::createConfigWidget(Kit *k) const
{
    return new Internal::DeviceInformationConfigWidget(k);
}

QString DeviceKitInformation::displayNamePostfix(const Kit *k) const
{
    IDevice::ConstPtr dev = device(k);
    return dev.isNull() ? QString() : dev->displayName();
}

KitInformation::ItemList DeviceKitInformation::toUserOutput(Kit *k) const
{
    IDevice::ConstPtr dev = device(k);
    return ItemList() << qMakePair(tr("Device"), dev.isNull() ? tr("Unconfigured") : dev->displayName());
}

IDevice::ConstPtr DeviceKitInformation::device(const Kit *k)
{
    DeviceManager *dm = DeviceManager::instance();
    return dm ? dm->find(deviceId(k)) : IDevice::ConstPtr();
}

Core::Id DeviceKitInformation::deviceId(const Kit *k)
{
    if (k) {
        QString idname = k->value(Core::Id(DEVICE_INFORMATION)).toString();
        return idname.isEmpty() ? IDevice::invalidId() : Core::Id(idname);
    }
    return IDevice::invalidId();
}

void DeviceKitInformation::setDevice(Kit *k, IDevice::ConstPtr dev)
{
    setDeviceId(k, dev ? dev->id() : IDevice::invalidId());
}

void DeviceKitInformation::setDeviceId(Kit *k, const Core::Id id)
{
    k->setValue(Core::Id(DEVICE_INFORMATION), id.toString());
}

void DeviceKitInformation::deviceUpdated(const Core::Id &id)
{
    foreach (Kit *k, KitManager::instance()->kits())
        if (deviceId(k) == id)
            notifyAboutUpdate(k);
}

} // namespace ProjectExplorer
