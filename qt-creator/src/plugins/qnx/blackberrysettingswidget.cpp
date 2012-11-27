#include "blackberrysettingswidget.h"
#include "ui_blackberrysettingswidget.h"

#include <utils/pathchooser.h>
#include <QDebug>
namespace Qnx {
namespace Internal {

BlackBerrySettingsWidget::BlackBerrySettingsWidget(QWidget *parent) :
    QWidget(parent),
    m_ui(new Ui_BlackBerrySettingsWidget)
{
    m_bbConfig = &BlackBerryConfigurations::instance();
    m_ui->setupUi(this);
    m_ui->setupButton->setEnabled(false);
    m_ui->clearButton->setEnabled(false);
    m_ui->sdkPath->setExpectedKind(Utils::PathChooser::ExistingDirectory);
    m_ui->sdkPath->setPath(m_bbConfig->ndkPath());

    initInfoTable();

    connect(m_ui->sdkPath, SIGNAL(changed(QString)), this, SLOT(checkSdkPath()));
    connect(m_ui->setupButton, SIGNAL(clicked()), this, SLOT(setToolChain()));
    connect(m_bbConfig, SIGNAL(updated()), this, SLOT(updateInfoTable()));
}

void BlackBerrySettingsWidget::saveSettings()
{
    //    m_bbConfig->saveSetting(); Das macht ein bug ;)
}

void BlackBerrySettingsWidget::checkSdkPath()
{
    if (!m_ui->sdkPath->path().isEmpty() &&
            m_bbConfig->checkPath(m_ui->sdkPath->path()))
        m_ui->setupButton->setEnabled(true);
    else
        m_ui->setupButton->setEnabled(false);
}

void BlackBerrySettingsWidget::setToolChain()
{
    m_bbConfig->setToolChain(m_ui->sdkPath->path());
    // setToolChain return bool kann sein?
    //    updateInfoTable();
}

void BlackBerrySettingsWidget::updateInfoTable()
{
    QMultiMap<QString, QString> env = m_bbConfig->qnxEnv();

    if(env.isEmpty())
        return;

    m_infoModel->setHorizontalHeaderItem(0, new QStandardItem(QString(QLatin1String("Variable"))));
    m_infoModel->setHorizontalHeaderItem(1, new QStandardItem(QString(QLatin1String("Value"))));

    m_ui->ndkInfosTableView->horizontalHeader()->setResizeMode(0, QHeaderView::ResizeToContents);
    m_ui->ndkInfosTableView->horizontalHeader()->setResizeMode(1, QHeaderView::ResizeToContents);

    QMultiMap<QString, QString>::const_iterator it;
    QMultiMap<QString, QString>::const_iterator end(env.constEnd());
    for (it = env.constBegin(); it != end; ++it) {
        const QString key = it.key();
        const QString value = it.value();
        QList <QStandardItem*> row;
        row << new QStandardItem(key) << new QStandardItem(value);
        m_infoModel->appendRow(row);
    }

    m_infoModel->appendRow( QList<QStandardItem*>() << new QStandardItem(QString(QLatin1String("QMake Path"))) << new QStandardItem(m_bbConfig->qmakePath().toString()));
    m_infoModel->appendRow( QList<QStandardItem*>() << new QStandardItem(QString(QLatin1String("Gcc Path"))) << new QStandardItem(m_bbConfig->gccPath().toString()));

}

void BlackBerrySettingsWidget::initInfoTable()
{
    m_infoModel = new QStandardItemModel(this);

    m_ui->ndkInfosTableView->setModel(m_infoModel);
    m_ui->ndkInfosTableView->verticalHeader()->hide();
    m_ui->ndkInfosTableView->setEditTriggers(QAbstractItemView::NoEditTriggers);

    updateInfoTable();
}

} // namespace Internal
} // namespace Qnx

