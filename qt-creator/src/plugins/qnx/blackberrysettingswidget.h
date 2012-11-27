#ifndef BLACKBERRYSETTINGSWIDGET_H
#define BLACKBERRYSETTINGSWIDGET_H

#include "blackberryconfigurations.h"

#include <QWidget>
#include <QStandardItemModel>

QT_BEGIN_NAMESPACE
class Ui_BlackBerrySettingsWidget;
QT_END_NAMESPACE

namespace Qnx {
namespace Internal {

class BlackBerrySettingsWidget : public QWidget
{
    Q_OBJECT
public:
    explicit BlackBerrySettingsWidget(QWidget *parent = 0);
    void saveSettings();
    
signals:
    void sdkPathChanged();
    
public slots:
    void checkSdkPath();
    void setToolChain();
    void updateInfoTable();

private:
    void initInfoTable();
    QString m_sdkPath;
    Ui_BlackBerrySettingsWidget *m_ui;
    BlackBerryConfigurations *m_bbConfig;
    QStandardItemModel *m_infoModel;

};

} // namespace Internal
} // namespeace Qnx

#endif // BLACKBERRYSETTINGSWIDGET_H
