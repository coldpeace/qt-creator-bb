#ifndef BLACKBERRYSETTINGSPAGE_H
#define BLACKBERRYSETTINGSPAGE_H

#include <coreplugin/dialogs/ioptionspage.h>

namespace Qnx {
namespace Internal {

class BlackBerrySettingsWidget;

class BlackBerrySettingsPage : public Core::IOptionsPage
{
    Q_OBJECT
public:
    explicit BlackBerrySettingsPage(QObject *parent = 0);
    QWidget *createPage(QWidget *parent);
    void apply();
    void finish();

private:
    BlackBerrySettingsWidget *m_widget;
};

} // namespace Internal
} // namespace Qnx

#endif // BLACKBERRYSETTINGSPAGE_H
