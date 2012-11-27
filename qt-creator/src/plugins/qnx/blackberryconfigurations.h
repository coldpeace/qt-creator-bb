#ifndef BLACKBERRYCONFIGURATIONS_H
#define BLACKBERRYCONFIGURATIONS_H

#include <utils/environment.h>
#include <utils/fileutils.h>

#include <QSettings>
#include <QObject>

namespace Qnx {
namespace Internal {

struct BlackBerryConfig
{
    QString ndkPath;
    Utils::FileName qmakeBinaryFile;
    Utils::FileName gccCompiler;
    Utils::FileName gdbDebuger;
    QMultiMap<QString, QString> qnxEnv;
};

class BlackBerryConfigurations: public QObject
{
    Q_OBJECT
public:
    static BlackBerryConfigurations &instance();
    BlackBerryConfig config() const;
    Utils::FileName qmakePath() const;
    Utils::FileName gccPath() const;
    Utils::FileName gdbPath() const;
    QMultiMap<QString, QString> qnxEnv() const;
    void setToolChain(const QString &ndkPath);
    static bool checkPath(const QString & path);
    QString ndkPath() const;
    void loadSetting();
    void saveSetting();

protected:
    void addQtVersion();

protected slots:
    void removeToolChain();

private:
    BlackBerryConfigurations(QObject *parent = 0);
    static BlackBerryConfigurations *m_instance;
    BlackBerryConfig m_config;
signals:
    void updated();
};

} // namespace Internal
} // namespace Qnx

#endif // BLACKBERRYCONFIGURATIONS_H
