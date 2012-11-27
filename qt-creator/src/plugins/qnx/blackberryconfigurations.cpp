#include "blackberryconfigurations.h"
#include "blackberrytoolchain.h"
#include "blackberryqtversion.h"
#include "qnxutils.h"

#include <coreplugin/icore.h>

#include <qtsupport/baseqtversion.h>
#include <qtsupport/qtversionfactory.h>
#include <qtsupport/qtversionmanager.h>

#include <projectexplorer/toolchainmanager.h>
#include <projectexplorer/gcctoolchain.h>
#include <projectexplorer/gcctoolchainfactories.h>

#include <utils/persistentsettings.h>

#include <QFileInfo>
#include <QDateTime>
#include <QMessageBox>
#include <TranslationUnit.h>

#include <qDebug>

namespace Qnx {
namespace Internal {

namespace {
const QLatin1String SettingsGroup("BlackBerryConfigurations");
const QLatin1String NDKLocationKey("NDKLocation");
const QLatin1String GCCLocationKey("GCCLocation");
const QLatin1String changeTimeStamp("ChangeTimeStamp");

static Utils::FileName settingsFileName()
{
    return Utils::FileName::fromString(QString::fromLatin1("%1/qtcreator/blackberry.xml")
                                       .arg(QFileInfo(Core::ICore::settings(QSettings::SystemScope)->fileName()).absolutePath()));
}
}

BlackBerryConfigurations::BlackBerryConfigurations(QObject *parent)
    :QObject(parent)
{
    loadSetting();
}

void BlackBerryConfigurations::setToolChain(const QString &ndkPath)
{
    m_config.ndkPath = ndkPath;
    QString envFilePath;

#if defined Q_OS_WIN
    envFilePath = ndkPath + QLatin1String("/bbndk-env.bat");
#elif defined Q_OS_UNIX
    envFilePath = ndkPath + QLatin1String("/bbndk-env.sh");
#endif

    m_config.qnxEnv = QnxUtils::parseEnvironmentFile(envFilePath);
    QString qnxPath = m_config.qnxEnv.value(QLatin1String("PATH"));
    QString qmakePath, gccPath;

#if defined Q_OS_WIN
    // TODO: verifiy the binary filename in Windows
    qmakePath = qnxPath + QLatin1String("/qmake.exe");
    gccPath = qnxPath + QLatin1String("/qcc.exe");
#elif defined Q_OS_UNIX
    qmakePath = qnxPath + QLatin1String("/qmake");
    gccPath = qnxPath + QLatin1String("/qcc");
#endif

    if (!QFileInfo(qmakePath).exists())
    {
        QMessageBox::warning(0, QLatin1String("Qt not found"),
                             QLatin1String("No Qt version found"), QMessageBox::Ok);
        return;
    }

    if (!QFileInfo(gccPath).exists())
    {
        QMessageBox::warning(0, QLatin1String("Gcc Compiler not found"),
                             QLatin1String("No Gcc compiler found"), QMessageBox::Ok);
        return;
    }

    m_config.qmakeBinaryFile = Utils::FileName::fromString(qmakePath);
    m_config.gccCompiler = Utils::FileName::fromString(gccPath);

    addQtVersion();
}

void BlackBerryConfigurations::addQtVersion()
{
    if(m_config.qmakeBinaryFile.isEmpty())
        return;

    // This should be set in the bbndk-script
    QString cpuDir = QLatin1String("armle-v7");
    // Check if the qmake is not already register
    QtSupport::BaseQtVersion *version = QtSupport::QtVersionManager::instance()->qtVersionForQMakeBinary(m_config.qmakeBinaryFile);
    if (version) {
        QMessageBox::warning(0, QLatin1String("Qt known"),
                             QLatin1String("This Qt version was already registred"), QMessageBox::Ok);
        return;
    }

    version = new BlackBerryQtVersion(QnxUtils::cpudirToArch(cpuDir), m_config.qmakeBinaryFile, true, QString(), m_config.ndkPath);

    if (!version) {
        QMessageBox::warning(0, QLatin1String("Qt not valid"),
                             QLatin1String("Unable to add BlackBerry Qt version"), QMessageBox::Ok);
        return;
    }

    version->setDisplayName(QLatin1String("BlackBerry 10 NDK Qt"));
    QtSupport::QtVersionManager::instance()->addVersion(version);
    emit updated();

}

bool BlackBerryConfigurations::checkPath(const QString &path)
{
    QString envFile;
#if defined Q_OS_WIN
    envFile = path + QLatin1String("/bbndk-env.bat");
#elif defined Q_OS_UNIX
    envFile = path + QLatin1String("/bbndk-env.sh");
#endif

    if (QFileInfo(envFile).exists()) {
        return true;
    }

    return false;
}

QString BlackBerryConfigurations::ndkPath() const
{
    return m_config.ndkPath;
}

void BlackBerryConfigurations::loadSetting()
{
    QSettings *settings = Core::ICore::instance()->settings();
    settings->beginGroup(QString(SettingsGroup));
    m_config.ndkPath = settings->value(NDKLocationKey).toString();
    m_config.gccCompiler = Utils::FileName::fromString(settings->value(GCCLocationKey).toString());
    //    gdbDebuger, qmakePath ...
    settings->endGroup();
}

void BlackBerryConfigurations::saveSetting()
{
    QSettings *settings = Core::ICore::instance()->settings();
    settings->beginGroup(SettingsGroup);

    QFileInfo fileInfo = settingsFileName().toFileInfo();
    if (fileInfo.exists())
        settings->setValue(changeTimeStamp, fileInfo.lastModified().toMSecsSinceEpoch() / 1000);

    // save user settings
    settings->setValue(NDKLocationKey, m_config.ndkPath);
    settings->setValue(GCCLocationKey, m_config.gccCompiler.toString());
    settings->endGroup();
}

BlackBerryConfigurations &BlackBerryConfigurations::instance()
{
    if (m_instance == 0)
        m_instance = new BlackBerryConfigurations();
    return *m_instance;
}

BlackBerryConfig BlackBerryConfigurations::config() const
{
    return m_config;
}

Utils::FileName BlackBerryConfigurations::qmakePath() const
{
    return m_config.qmakeBinaryFile;
}

Utils::FileName BlackBerryConfigurations::gccPath() const
{
    return m_config.gccCompiler;
}

Utils::FileName BlackBerryConfigurations::gdbPath() const
{
    return m_config.gdbDebuger;
}

QMultiMap<QString, QString> BlackBerryConfigurations::qnxEnv() const
{
    return m_config.qnxEnv;
}

BlackBerryConfigurations* BlackBerryConfigurations::m_instance = 0;

} // namespace Internal
} // namespace Qnx
