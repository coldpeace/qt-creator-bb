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

#include "infobar.h"

#include <coreplugin/coreconstants.h>

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>

#include <QVariant>

namespace Core {

InfoBarEntry::InfoBarEntry(Id _id, const QString &_infoText)
    : id(_id)
    , infoText(_infoText)
    , object(0)
    , buttonPressMember(0)
    , cancelObject(0)
    , cancelButtonPressMember(0)
{
}

void InfoBarEntry::setCustomButtonInfo(const QString &_buttonText, QObject *_object, const char *_member)
{
    buttonText = _buttonText;
    object = _object;
    buttonPressMember = _member;
}

void InfoBarEntry::setCancelButtonInfo(QObject *_object, const char *_member)
{
    cancelObject = _object;
    cancelButtonPressMember = _member;
}

void InfoBarEntry::setCancelButtonInfo(const QString &_cancelButtonText, QObject *_object, const char *_member)
{
    cancelButtonText = _cancelButtonText;
    cancelObject = _object;
    cancelButtonPressMember = _member;
}


void InfoBar::addInfo(const InfoBarEntry &info)
{
    m_infoBarEntries << info;
    emit changed();
}

void InfoBar::removeInfo(Id id)
{
    QMutableListIterator<InfoBarEntry> it(m_infoBarEntries);
    while (it.hasNext())
        if (it.next().id == id) {
            it.remove();
            emit changed();
            return;
        }
}

bool InfoBar::containsInfo(Id id) const
{
    QListIterator<InfoBarEntry> it(m_infoBarEntries);
    while (it.hasNext())
        if (it.next().id == id)
            return true;

    return false;
}

void InfoBar::clear()
{
    if (!m_infoBarEntries.isEmpty()) {
        m_infoBarEntries.clear();
        emit changed();
    }
}


InfoBarDisplay::InfoBarDisplay(QObject *parent)
    : QObject(parent)
    , m_infoBar(0)
    , m_boxLayout(0)
    , m_boxIndex(0)
{
}

void InfoBarDisplay::setTarget(QBoxLayout *layout, int index)
{
    m_boxLayout = layout;
    m_boxIndex = index;
}

void InfoBarDisplay::setInfoBar(InfoBar *infoBar)
{
    if (m_infoBar == infoBar)
        return;

    if (m_infoBar)
        m_infoBar->disconnect(this);
    m_infoBar = infoBar;
    if (m_infoBar) {
        connect(infoBar, SIGNAL(changed()), SLOT(update()));
        connect(infoBar, SIGNAL(destroyed()), SLOT(infoBarDestroyed()));
    }
    update();
}

void InfoBarDisplay::infoBarDestroyed()
{
    m_infoBar = 0;
    // Calling update() here causes a complicated crash on shutdown.
    // So instead we rely on the view now being either destroyed (in which case it
    // will delete the widgets itself) or setInfoBar() being called explicitly.
}

void InfoBarDisplay::update()
{
    foreach (QWidget *widget, m_infoWidgets) {
        widget->disconnect(this); // We want no destroyed() signal now
        delete widget;
    }
    m_infoWidgets.clear();

    if (!m_infoBar)
        return;

    foreach (const InfoBarEntry &info, m_infoBar->m_infoBarEntries) {
        QFrame *infoWidget = new QFrame;

        QPalette pal = infoWidget->palette();
        pal.setColor(QPalette::Window, QColor(255, 255, 225));
        pal.setColor(QPalette::WindowText, Qt::black);

        infoWidget->setPalette(pal);
        infoWidget->setFrameStyle(QFrame::Panel | QFrame::Raised);
        infoWidget->setLineWidth(1);
        infoWidget->setAutoFillBackground(true);

        QHBoxLayout *hbox = new QHBoxLayout(infoWidget);
        hbox->setMargin(2);

        QLabel *infoWidgetLabel = new QLabel(info.infoText);
        infoWidgetLabel->setWordWrap(true);
        hbox->addWidget(infoWidgetLabel);

        if (!info.buttonText.isEmpty()) {
            QToolButton *infoWidgetButton = new QToolButton;
            infoWidgetButton->setText(info.buttonText);
            connect(infoWidgetButton, SIGNAL(clicked()), info.object, info.buttonPressMember);

            hbox->addWidget(infoWidgetButton);
        }

        QToolButton *infoWidgetCloseButton = new QToolButton;
        infoWidgetCloseButton->setProperty("infoId", info.id.uniqueIdentifier());

        // need to connect to cancelObjectbefore connecting to cancelButtonClicked,
        // because the latter removes the button and with it any connect
        if (info.cancelObject)
            connect(infoWidgetCloseButton, SIGNAL(clicked()),
                    info.cancelObject, info.cancelButtonPressMember);
        connect(infoWidgetCloseButton, SIGNAL(clicked()), SLOT(cancelButtonClicked()));

        if (info.cancelButtonText.isEmpty()) {
            infoWidgetCloseButton->setAutoRaise(true);
            infoWidgetCloseButton->setIcon(QIcon(QLatin1String(Core::Constants::ICON_CLEAR)));
            infoWidgetCloseButton->setToolTip(tr("Close"));
        } else {
            infoWidgetCloseButton->setText(info.cancelButtonText);
        }

        hbox->addWidget(infoWidgetCloseButton);

        connect(infoWidget, SIGNAL(destroyed()), SLOT(widgetDestroyed()));
        m_boxLayout->insertWidget(m_boxIndex, infoWidget);
        m_infoWidgets << infoWidget;
    }
}

void InfoBarDisplay::widgetDestroyed()
{
    m_infoWidgets.removeOne(static_cast<QWidget *>(sender()));
}

void InfoBarDisplay::cancelButtonClicked()
{
    m_infoBar->removeInfo(Id::fromUniqueIdentifier(sender()->property("infoId").toInt()));
}

} // namespace Core
