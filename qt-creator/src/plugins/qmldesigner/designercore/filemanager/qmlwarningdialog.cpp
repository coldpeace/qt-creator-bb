#include "qmlwarningdialog.h"
#include "ui_qmlwarningdialog.h"

#include <qmldesignerplugin.h>

namespace QmlDesigner {

namespace Internal {

QmlWarningDialog::QmlWarningDialog(QWidget *parent, const QStringList &warnings) :
    QDialog(parent),
    ui(new Ui::QmlWarningDialog),
    m_warnings(warnings)
{
    ui->setupUi(this);
    setResult (0);

    ui->checkBox->setChecked(true);
    connect(ui->ignoreButton, SIGNAL(clicked()), this, SLOT(ignoreButtonPressed()));
    connect(ui->okButton, SIGNAL(clicked()), this, SLOT(okButtonPressed()));
    connect(ui->checkBox, SIGNAL(toggled(bool)), this, SLOT(checkBoxToggled(bool)));

    connect(ui->warnings, SIGNAL(linkActivated(QString)), this, SLOT(linkClicked(QString)));

    QString warningText;
    foreach (const QString &string, warnings)
        warningText += QLatin1String(" ") + string + QLatin1String("\n");
    ui->warnings->setText(warningText);

    ui->warnings->setForegroundRole(QPalette::ToolTipText);
    ui->warnings->setBackgroundRole(QPalette::ToolTipBase);
    ui->warnings->setAutoFillBackground(true);
}

QmlWarningDialog::~QmlWarningDialog()
{
    delete ui;
}

void QmlWarningDialog::ignoreButtonPressed()
{
    done(0);
}

void QmlWarningDialog::okButtonPressed()
{
    done(-1);
}

bool QmlWarningDialog::warningsEnabled() const
{
    DesignerSettings settings = BauhausPlugin::pluginInstance()->settings();
    return settings.warningsInDesigner;
}

void QmlWarningDialog::checkBoxToggled(bool b)
{
    DesignerSettings settings = BauhausPlugin::pluginInstance()->settings();
    settings.warningsInDesigner = b;
    BauhausPlugin::pluginInstance()->setSettings(settings);
}

void QmlWarningDialog::linkClicked(const QString &link)
{
    done(link.toInt());
}

} //Internal
} //QmlDesigner
