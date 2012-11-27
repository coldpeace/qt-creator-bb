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

#ifndef SUBMITMODEL_H
#define SUBMITMODEL_H

#include "vcsbase_global.h"

#include <QStandardItemModel>

namespace VcsBase {

class VCSBASE_EXPORT SubmitFileModel : public QStandardItemModel
{
    Q_OBJECT
public:
    explicit SubmitFileModel(QObject *parent = 0);

    // Convenience to create and add rows containing a file plus status text.
    QList<QStandardItem *> addFile(const QString &fileName, const QString &status = QString(),
                                   bool checked = true, const QVariant &data = QVariant());

    // Find convenience that returns the whole row (as opposed to QStandardItemModel::find).
    QList<QStandardItem *> findRow(const QString &text, int column = 0) const;

    // Convenience to obtain a row
    QList<QStandardItem *> rowAt(int row) const;

    QString state(int row) const;
    QString file(int row) const;
    bool checked(int row) const;
    QVariant extraData(int row) const;

    bool hasCheckedFiles() const;

    // Filter for entries contained in the filter list. Returns the
    // number of deleted entries.
    unsigned filter(const QStringList &filter, int column);
};

} // namespace VcsBase

#endif // SUBMITMODEL_H
