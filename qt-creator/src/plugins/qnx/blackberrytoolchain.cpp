#include "blackberrytoolchain.h"
#include "qnxconstants.h"
#include "blackberryqtversion.h"
#include "blackberryconfigurations.h"

#include <projectexplorer/target.h>
#include <projectexplorer/toolchainmanager.h>
#include <projectexplorer/projectexplorer.h>

#include <qt4projectmanager/qt4project.h>

#include <qtsupport/qtkitinformation.h>
#include <qtsupport/qtversionmanager.h>

#include <utils/environment.h>

#include <QDir>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace Qnx {
namespace Internal {

using namespace ProjectExplorer;
using namespace Qt4ProjectManager;

static const char BB_QT_VERSION_KEY[] = "Qt4ProjectManager.BlackBerry.QtVersion";


BlackBerryToolChain::BlackBerryToolChain(bool autodetected) :
    GccToolChain(QLatin1String(Constants::BB_TOOLCHAIN_ID), autodetected),
    m_qtVersionId(-1)
{}

BlackBerryToolChain::BlackBerryToolChain(const BlackBerryToolChain &tc) :
    GccToolChain(tc),
    m_qtVersionId(tc.m_qtVersionId)
{ }

BlackBerryToolChain::~BlackBerryToolChain()
{ }

QString BlackBerryToolChain::type() const
{
    return QLatin1String("BlackBerryGcc");
}

QString BlackBerryToolChain::typeDisplayName() const
{
    return BlackBerryToolChainFactory::tr("BlackBerry GCC");
}

bool BlackBerryToolChain::isValid() const
{
    return GccToolChain::isValid() && m_qtVersionId >= 0 && targetAbi().isValid();
}

bool BlackBerryToolChain::operator ==(const ToolChain &tc) const
{
    if (!ToolChain::operator ==(tc))
        return false;

    const BlackBerryToolChain *tcPtr = static_cast<const BlackBerryToolChain *>(&tc);
    return m_qtVersionId == tcPtr->m_qtVersionId;
}

ToolChainConfigWidget *BlackBerryToolChain::configurationWidget()
{
    return new BlackBerryToolChainConfigWidget(this);
}

QVariantMap BlackBerryToolChain::toMap() const
{
    QVariantMap result = GccToolChain::toMap();
    result.insert(QLatin1String(BB_QT_VERSION_KEY), m_qtVersionId);
    return result;
}

bool BlackBerryToolChain::fromMap(const QVariantMap &data)
{
    if (!GccToolChain::fromMap(data))
        return false;

    m_qtVersionId = data.value(QLatin1String(BB_QT_VERSION_KEY), -1).toInt();

    return isValid();
}

QList<Utils::FileName> BlackBerryToolChain::suggestedMkspecList() const
{
    return QList<Utils::FileName>()<< Utils::FileName::fromString(QLatin1String("blackberry-armv7le-qcc"));
}

QString BlackBerryToolChain::makeCommand(const Utils::Environment &env) const
{
#if defined(Q_OS_WIN)
    QString make = QLatin1String("ma-make.exe");
#else
    QString make = QLatin1String("make");
#endif
    QString tmp = env.searchInPath(make);
    return tmp.isEmpty() ? make : tmp;
}

void BlackBerryToolChain::setQtVersionId(int id)
{
    if (id < 0) {
        setTargetAbi(Abi());
        m_qtVersionId = -1;
        toolChainUpdated();
        return;
    }

    QtSupport::BaseQtVersion *version = QtSupport::QtVersionManager::instance()->version(id);
    Q_ASSERT(version);
    m_qtVersionId = id;

    Q_ASSERT(version->qtAbis().count() == 1);
    setTargetAbi(version->qtAbis().at(0));

    toolChainUpdated();
    setDisplayName(BlackBerryToolChainFactory::tr("BlackBerry GCC for %1").arg(version->displayName()));
}

int BlackBerryToolChain::qtVersionId() const
{
    return m_qtVersionId;
}

QList<Abi> BlackBerryToolChain::detectSupportedAbis() const
{
    if (m_qtVersionId < 0)
        return QList<Abi>();

    BlackBerryQtVersion *aqv = dynamic_cast<BlackBerryQtVersion *>(QtSupport::QtVersionManager::instance()->version(m_qtVersionId));
    if (!aqv)
        return QList<Abi>();

    return aqv->qtAbis();
}

// --------------------------------------------------------------------------
// BlackBerryToolChainConfigWidget
// --------------------------------------------------------------------------

BlackBerryToolChainConfigWidget::BlackBerryToolChainConfigWidget(BlackBerryToolChain *tc) :
   ToolChainConfigWidget(tc),
   m_compilerCommand(new Utils::PathChooser),
   m_abiWidget(new ProjectExplorer::AbiWidget)
{
    QLabel *label = new QLabel(BlackBerryConfigurations::instance().ndkPath());
    m_mainLayout->addRow(tr("&NDK Root:"), label);

    m_compilerCommand->setExpectedKind(Utils::PathChooser::ExistingCommand);
    m_compilerCommand->setFileName(tc->compilerCommand());
    m_compilerCommand->setReadOnly(true);

    m_abiWidget->setAbis(tc->supportedAbis(), tc->targetAbi());
    m_abiWidget->setEnabled(false);

    m_mainLayout->addRow(tr("&Compiler path:"), m_compilerCommand);
    m_mainLayout->addRow(tr("&ABI:"), m_abiWidget);

}

// --------------------------------------------------------------------------
// BlackBerryToolChainFactory
// --------------------------------------------------------------------------

BlackBerryToolChainFactory::BlackBerryToolChainFactory() :
    ToolChainFactory()
{ }

QString BlackBerryToolChainFactory::displayName() const
{
    return tr("BlackBerry GCC");
}

QString BlackBerryToolChainFactory::id() const
{
    return QLatin1String(Constants::BB_TOOLCHAIN_ID);
}

QList<ToolChain *> BlackBerryToolChainFactory::autoDetect()
{
    QList<ToolChain *> result;

    QtSupport::QtVersionManager *vm = QtSupport::QtVersionManager::instance();
    connect(vm, SIGNAL(qtVersionsChanged(QList<int>,QList<int>,QList<int>)),
            this, SLOT(handleQtVersionChanges(QList<int>,QList<int>,QList<int>)));

    QList<int> versionList;
    foreach (QtSupport::BaseQtVersion *v, vm->versions())
        versionList.append(v->uniqueId());

    return createToolChainList(versionList);
}

bool BlackBerryToolChainFactory::canRestore(const QVariantMap &data)
{
    return idFromMap(data).startsWith(QLatin1String(Constants::BB_TOOLCHAIN_ID) + QLatin1Char(':'));
}

ToolChain *BlackBerryToolChainFactory::restore(const QVariantMap &data)
{
    BlackBerryToolChain *tc = new BlackBerryToolChain(false);
    if (tc->fromMap(data))
        return tc;

    delete tc;
    return 0;
}

void BlackBerryToolChainFactory::handleQtVersionChanges(const QList<int> &added, const QList<int> &removed, const QList<int> &changed)
{
    qDebug() << "BlackBerryToolChainFactory::handleQtVersionChanges";
    QList<int> changes;
    changes << added << removed << changed;
    ToolChainManager *tcm = ToolChainManager::instance();
    QList<ToolChain *> tcList = createToolChainList(changes);
    foreach (ToolChain *tc, tcList)
        tcm->registerToolChain(tc);
}

QList<ToolChain *> BlackBerryToolChainFactory::createToolChainList(const QList<int> &changes)
{
    qDebug() << "BlackBerryToolChainFactory::createToolChainList";
    ToolChainManager *tcm = ToolChainManager::instance();
    QtSupport::QtVersionManager *vm = QtSupport::QtVersionManager::instance();
    QList<ToolChain *> result;

    foreach (int i, changes) {
        QtSupport::BaseQtVersion *v = vm->version(i);
        QList<ToolChain *> toRemove;
        foreach (ToolChain *tc, tcm->toolChains()) {
            if (tc->id() != QLatin1String(Constants::BB_TOOLCHAIN_ID))
                continue;
            BlackBerryToolChain *aTc = static_cast<BlackBerryToolChain *>(tc);
            if (aTc->qtVersionId() == i)
                toRemove.append(aTc);
        }
        foreach (ToolChain *tc, toRemove)
            tcm->deregisterToolChain(tc);

        const BlackBerryQtVersion * const aqv = dynamic_cast<BlackBerryQtVersion *>(v);
        if (!aqv || !aqv->isValid())
            continue;

        BlackBerryToolChain *aTc = new BlackBerryToolChain(true);
        aTc->setQtVersionId(i);
        aTc->setCompilerCommand(BlackBerryConfigurations::instance().gccPath(/*aTc->targetAbi().architecture()*/));
        result.append(aTc);
    }
    return result;
}

} // namespace Internal
} // namespace BlackBerry
