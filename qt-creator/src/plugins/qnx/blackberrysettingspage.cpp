#include "blackberrysettingspage.h"
#include "blackberrysettingswidget.h"
#include "qnxconstants.h"

#include <QCoreApplication>
#include <qDebug>

namespace Qnx {
namespace Internal {

BlackBerrySettingsPage::BlackBerrySettingsPage(QObject *parent) :
    Core::IOptionsPage(parent)
{
    setId(QLatin1String(Constants::QNX_SETTINGS_ID));
    setDisplayName(tr("BlackBerry Configurations"));
    setCategory(QLatin1String(Constants::QNX_SETTINGS_CATEGORY));
    setDisplayCategory(QCoreApplication::translate("BlackBerry",
                                                   Constants::QNX_SETTINGS_TR_CATEGORY));
    setCategoryIcon(QLatin1String(Constants::QNX_SETTINGS_CATEGORY_ICON));
}

QWidget *BlackBerrySettingsPage::createPage(QWidget *parent)
{
    m_widget = new BlackBerrySettingsWidget(parent);
    return m_widget;
}

void BlackBerrySettingsPage::apply()
{
//    if ( m_widget->checkSdkPath()) {
//        m_widget->setToolChain();
//        m_widget->saveSettings();
//    }
}

void BlackBerrySettingsPage::finish()
{
//    qDebug() << "BlackBerrySettingsPage::finish";
}

} // namespace Internal
} // namespace Qnx
