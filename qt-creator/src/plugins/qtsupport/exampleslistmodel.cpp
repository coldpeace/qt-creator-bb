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

#include "exampleslistmodel.h"

#include <QFile>
#include <QDir>
#include <QXmlStreamReader>

#include <QDebug>

#include <coreplugin/icore.h>
#include <coreplugin/helpmanager.h>
#include <qtsupport/qtversionmanager.h>
#include <utils/qtcassert.h>

#include <algorithm>

using QtSupport::QtVersionManager;
using QtSupport::BaseQtVersion;

namespace QtSupport {

namespace Internal {

ExamplesListModel::ExamplesListModel(QObject *parent) :
    QAbstractListModel(parent),
    m_updateOnQtVersionsChanged(false),
    m_initialized(false),
    m_helpInitialized(false)
{
    QHash<int, QByteArray> roleNames;
    roleNames[Name] = "name";
    roleNames[ProjectPath] = "projectPath";
    roleNames[ImageUrl] = "imageUrl";
    roleNames[Description] = "description";
    roleNames[DocUrl] = "docUrl";
    roleNames[FilesToOpen] = "filesToOpen";
    roleNames[Tags] = "tags";
    roleNames[Difficulty] = "difficulty";
    roleNames[Type] = "type";
    roleNames[HasSourceCode] = "hasSourceCode";
    roleNames[Dependencies] = "dependencies";
    roleNames[IsVideo] = "isVideo";
    roleNames[VideoUrl] = "videoUrl";
    roleNames[VideoLength] = "videoLength";
    roleNames[Platforms] = "platforms";
    setRoleNames(roleNames);

    connect(Core::HelpManager::instance(), SIGNAL(setupFinished()),
            SLOT(helpInitialized()));
    connect(QtVersionManager::instance(), SIGNAL(qtVersionsChanged(QList<int>,QList<int>,QList<int>)),
            this, SLOT(handleQtVersionsChanged()));
}

static inline QString fixStringForTags(const QString &string)
{
    QString returnString = string;
    returnString.remove(QLatin1String("<i>"));
    returnString.remove(QLatin1String("</i>"));
    returnString.remove(QLatin1String("<tt>"));
    returnString.remove(QLatin1String("</tt>"));
    return returnString;
}

static inline QStringList trimStringList(const QStringList &stringlist)
{
    QStringList returnList;
    foreach (const QString &string, stringlist)
        returnList << string.trimmed();

    return returnList;
}

QList<ExampleItem> ExamplesListModel::parseExamples(QXmlStreamReader* reader, const QString& projectsOffset)
{
    QList<ExampleItem> examples;
    ExampleItem item;
    const QChar slash = QLatin1Char('/');
    while (!reader->atEnd()) {
        switch (reader->readNext()) {
        case QXmlStreamReader::StartElement:
            if (reader->name() == QLatin1String("example")) {
                item = ExampleItem();
                item.type = Example;
                QXmlStreamAttributes attributes = reader->attributes();
                item.name = attributes.value(QLatin1String("name")).toString();
                item.projectPath = attributes.value(QLatin1String("projectPath")).toString();
                item.hasSourceCode = !item.projectPath.isEmpty();
                item.projectPath.prepend(slash);
                item.projectPath.prepend(projectsOffset);
                item.imageUrl = attributes.value(QLatin1String("imageUrl")).toString();
                item.docUrl = attributes.value(QLatin1String("docUrl")).toString();
            } else if (reader->name() == QLatin1String("fileToOpen")) {
                item.filesToOpen.append(projectsOffset + slash + reader->readElementText(QXmlStreamReader::ErrorOnUnexpectedElement));
            } else if (reader->name() == QLatin1String("description")) {
                item.description =  fixStringForTags(reader->readElementText(QXmlStreamReader::ErrorOnUnexpectedElement));
            } else if (reader->name() == QLatin1String("dependency")) {
                item.dependencies.append(projectsOffset + slash + reader->readElementText(QXmlStreamReader::ErrorOnUnexpectedElement));
            } else if (reader->name() == QLatin1String("tags")) {
                item.tags = trimStringList(reader->readElementText(QXmlStreamReader::ErrorOnUnexpectedElement).split(QLatin1Char(','), QString::SkipEmptyParts));
                m_tags.append(item.tags);
            } else if (reader->name() == QLatin1String("platforms")) {
                item.platforms = trimStringList(reader->readElementText(QXmlStreamReader::ErrorOnUnexpectedElement).split(QLatin1Char(','), QString::SkipEmptyParts));
        }
            break;
        case QXmlStreamReader::EndElement:
            if (reader->name() == QLatin1String("example")) {
                if (item.projectPath.isEmpty() || !QFileInfo(item.projectPath).exists())
                    item.tags.append(QLatin1String("broken"));
                examples.append(item);
            } else if (reader->name() == QLatin1String("examples")) {
                return examples;
            }
            break;
        default: // nothing
            break;
        }
    }
    return examples;
}

QList<ExampleItem> ExamplesListModel::parseDemos(QXmlStreamReader* reader, const QString& projectsOffset)
{
    QList<ExampleItem> demos;
    ExampleItem item;
    const QChar slash = QLatin1Char('/');
    while (!reader->atEnd()) {
        switch (reader->readNext()) {
        case QXmlStreamReader::StartElement:
            if (reader->name() == QLatin1String("demo")) {
                item = ExampleItem();
                item.type = Demo;
                QXmlStreamAttributes attributes = reader->attributes();
                item.name = attributes.value(QLatin1String("name")).toString();
                item.projectPath = attributes.value(QLatin1String("projectPath")).toString();
                item.hasSourceCode = !item.projectPath.isEmpty();
                item.projectPath.prepend(slash);
                item.projectPath.prepend(projectsOffset);
                item.imageUrl = attributes.value(QLatin1String("imageUrl")).toString();
                item.docUrl = attributes.value(QLatin1String("docUrl")).toString();
            } else if (reader->name() == QLatin1String("fileToOpen")) {
                item.filesToOpen.append(projectsOffset + slash + reader->readElementText(QXmlStreamReader::ErrorOnUnexpectedElement));
            } else if (reader->name() == QLatin1String("description")) {
                item.description =  fixStringForTags(reader->readElementText(QXmlStreamReader::ErrorOnUnexpectedElement));
            } else if (reader->name() == QLatin1String("dependency")) {
                item.dependencies.append(projectsOffset + slash + reader->readElementText(QXmlStreamReader::ErrorOnUnexpectedElement));
            } else if (reader->name() == QLatin1String("tags")) {
                item.tags = reader->readElementText(QXmlStreamReader::ErrorOnUnexpectedElement).split(QLatin1Char(','));
            }
            break;
        case QXmlStreamReader::EndElement:
            if (reader->name() == QLatin1String("demo"))
                demos.append(item);
            else if (reader->name() == QLatin1String("demos"))
                return demos;
            break;
        default: // nothing
            break;
        }
    }
    return demos;
}

QList<ExampleItem> ExamplesListModel::parseTutorials(QXmlStreamReader* reader, const QString& projectsOffset)
{
    QList<ExampleItem> tutorials;
    ExampleItem item;
    const QChar slash = QLatin1Char('/');
    while (!reader->atEnd()) {
        switch (reader->readNext()) {
        case QXmlStreamReader::StartElement:
            if (reader->name() == QLatin1String("tutorial")) {
                item = ExampleItem();
                item.type = Tutorial;
                QXmlStreamAttributes attributes = reader->attributes();
                item.name = attributes.value(QLatin1String("name")).toString();
                item.projectPath = attributes.value(QLatin1String("projectPath")).toString();
                item.hasSourceCode = !item.projectPath.isEmpty();
                item.projectPath.prepend(slash);
                item.projectPath.prepend(projectsOffset);
                if (attributes.hasAttribute(QLatin1String("imageUrl")))
                    item.imageUrl = attributes.value(QLatin1String("imageUrl")).toString();
                if (attributes.hasAttribute(QLatin1String("docUrl")))
                    item.docUrl = attributes.value(QLatin1String("docUrl")).toString();
                if (attributes.hasAttribute(QLatin1String("isVideo")))
                    item.isVideo = attributes.value(QLatin1String("isVideo")).toString() == QLatin1String("true");
                if (attributes.hasAttribute(QLatin1String("videoUrl")))
                    item.videoUrl = attributes.value(QLatin1String("videoUrl")).toString();
                if (attributes.hasAttribute(QLatin1String("videoLength")))
                    item.videoLength = attributes.value(QLatin1String("videoLength")).toString();
            } else if (reader->name() == QLatin1String("fileToOpen")) {
                item.filesToOpen.append(projectsOffset + slash + reader->readElementText(QXmlStreamReader::ErrorOnUnexpectedElement));
            } else if (reader->name() == QLatin1String("description")) {
                item.description =  fixStringForTags(reader->readElementText(QXmlStreamReader::ErrorOnUnexpectedElement));
            } else if (reader->name() == QLatin1String("dependency")) {
                item.dependencies.append(projectsOffset + slash + reader->readElementText(QXmlStreamReader::ErrorOnUnexpectedElement));
            } else if (reader->name() == QLatin1String("tags")) {
                item.tags = reader->readElementText(QXmlStreamReader::ErrorOnUnexpectedElement).split(QLatin1Char(','));
            }
            break;
        case QXmlStreamReader::EndElement:
            if (reader->name() == QLatin1String("tutorial"))
                tutorials.append(item);
            else if (reader->name() == QLatin1String("tutorials"))
                return tutorials;
            break;
        default: // nothing
            break;
        }
    }

    return tutorials;
}

void ExamplesListModel::handleQtVersionsChanged()
{
    if (m_updateOnQtVersionsChanged)
        updateExamples();
}

void ExamplesListModel::updateExamples()
{
    clear();
    QString examplesFallback;
    QString demosFallback;
    QString sourceFallback;
    foreach (const QString &exampleSource,
             exampleSources(&examplesFallback, &demosFallback, &sourceFallback)) {
        QFile exampleFile(exampleSource);
        if (!exampleFile.open(QIODevice::ReadOnly)) {
            qDebug() << Q_FUNC_INFO << "Could not open file" << exampleSource;
            continue;
        }

        QFileInfo fi(exampleSource);
        QString offsetPath = fi.path();
        QDir examplesDir(offsetPath);
        QDir demosDir(offsetPath);
        if (!examplesFallback.isEmpty()) {
            // Look at Qt source directory at first,
            // since examplesPath() / demosPath() points at the build directory
            examplesDir = sourceFallback + QLatin1String("/examples");
            demosDir = sourceFallback + QLatin1String("/demos");
            // if examples or demos don't exist in source, try the directories
            // that qmake -query gave (i.e. in the build directory)
            if (!examplesDir.exists() || !demosDir.exists()) {
                examplesDir = examplesFallback;
                demosDir = demosFallback;
            }
        }

        QXmlStreamReader reader(&exampleFile);
        while (!reader.atEnd())
            switch (reader.readNext()) {
            case QXmlStreamReader::StartElement:
                if (reader.name() == QLatin1String("examples"))
                    addItems(parseExamples(&reader, examplesDir.path()));
                else if (reader.name() == QLatin1String("demos"))
                    addItems(parseDemos(&reader, demosDir.path()));
                else if (reader.name() == QLatin1String("tutorials"))
                    addItems(parseTutorials(&reader, examplesDir.path()));
                break;
            default: // nothing
                break;
            }

        if (reader.hasError())
            qDebug() << "error parsing file" <<  exampleSource << "as XML document";
    }

    m_tags.sort();
    m_tags.erase(std::unique(m_tags.begin(), m_tags.end()), m_tags.end());
    emit tagsUpdated();
}

QStringList ExamplesListModel::exampleSources(QString *examplesFallback, QString *demosFallback,
                                              QString *sourceFallback)
{
    QTC_CHECK(examplesFallback);
    QTC_CHECK(demosFallback);
    QTC_CHECK(sourceFallback);
    QStringList sources;
    QString resourceDir = Core::ICore::resourcePath() + QLatin1String("/welcomescreen/");

    // Qt Creator shipped tutorials
    sources << (resourceDir + QLatin1String("/qtcreator_tutorials.xml"));

    // Read keys from SDK installer
    QSettings *settings = Core::ICore::settings(QSettings::SystemScope);
    int size = settings->beginReadArray(QLatin1String("ExampleManifests"));
    for (int i = 0; i < size; ++i) {
        settings->setArrayIndex(i);
        sources.append(settings->value(QLatin1String("Location")).toString());
    }
    settings->endArray();
    // if the installer set something, that's enough for us
    if (size > 0)
        return sources;

    // try to find a suitable Qt version
    m_updateOnQtVersionsChanged = true; // this must be updated when the Qt versions change
    // fallbacks are passed back if no example manifest is found
    // and we fallback to Qt Creator's shipped manifest (e.g. only old Qt Versions found)
    QString potentialExamplesFallback;
    QString potentialDemosFallback;
    QString potentialSourceFallback;
    bool potentialFallbackHasDeclarative = false; // we prefer Qt's with declarative as fallback
    const QStringList pattern(QLatin1String("*.xml"));

    QtVersionManager *versionManager = QtVersionManager::instance();
    foreach (BaseQtVersion *version, versionManager->validVersions()) {
        QFileInfoList fis;
        if (version->hasExamples())
            fis << QDir(version->examplesPath()).entryInfoList(pattern);
        if (version->hasDemos())
            fis << QDir(version->demosPath()).entryInfoList(pattern);
        if (!fis.isEmpty()) {
            foreach (const QFileInfo &fi, fis)
                sources.append(fi.filePath());
            return sources;
        }

        // check if this Qt version would be the preferred fallback, Qt 4 only
        if (version->qtVersion().majorVersion == 4 && version->hasExamples() && version->hasDemos()) { // cached, so no performance hit
            bool hasDeclarative = QDir(version->examplesPath() + QLatin1String("/declarative")).exists();
            if (potentialExamplesFallback.isEmpty()
                    || (!potentialFallbackHasDeclarative && hasDeclarative)) {
                potentialFallbackHasDeclarative = hasDeclarative;
                potentialExamplesFallback = version->examplesPath();
                potentialDemosFallback = version->demosPath();
                potentialSourceFallback = version->sourcePath().toString();
            }
        }
    }

    if (!potentialExamplesFallback.isEmpty()) {
        // We didn't find a manifest, use Creator-provided XML file with fall back Qt version
        // qDebug() << Q_FUNC_INFO << "falling through to Creator-provided XML file";
        sources << QString(resourceDir + QLatin1String("/examples_fallback.xml"));
        if (examplesFallback)
            *examplesFallback = potentialExamplesFallback;
        if (demosFallback)
            *demosFallback = potentialDemosFallback;
        if (sourceFallback)
            *sourceFallback = potentialSourceFallback;
    }
    return sources;
}

void ExamplesListModel::clear()
{
    if (exampleItems.count() > 0) {
        beginRemoveRows(QModelIndex(), 0,  exampleItems.size()-1);
        exampleItems.clear();
        endRemoveRows();
    }
    m_tags.clear();
}

void ExamplesListModel::addItems(const QList<ExampleItem> &newItems)
{
    beginInsertRows(QModelIndex(), exampleItems.size(), exampleItems.size() - 1 + newItems.size());
    exampleItems.append(newItems);
    endInsertRows();
}

int ExamplesListModel::rowCount(const QModelIndex &) const
{
    ensureInitialized();
    return exampleItems.size();
}

QVariant ExamplesListModel::data(const QModelIndex &index, int role) const
{
    ensureInitialized();
    if (!index.isValid() || index.row()+1 > exampleItems.count()) {
        qDebug() << Q_FUNC_INFO << "invalid index requested";
        return QVariant();
    }

    ExampleItem item = exampleItems.at(index.row());
    switch (role)
    {
    case Qt::DisplayRole: // for search only
        return QString(item.name + QLatin1Char(' ') + item.tags.join(QLatin1String(" ")));
    case Name:
        return item.name;
    case ProjectPath:
        return item.projectPath;
    case Description:
        return item.description;
    case ImageUrl:
        return item.imageUrl;
    case DocUrl:
        return item.docUrl;
    case FilesToOpen:
        return item.filesToOpen;
    case Tags:
        return item.tags;
    case Difficulty:
        return item.difficulty;
    case Dependencies:
        return item.dependencies;
    case HasSourceCode:
        return item.hasSourceCode;
    case Type:
        return item.type;
    case IsVideo:
        return item.isVideo;
    case VideoUrl:
        return item.videoUrl;
    case VideoLength:
        return item.videoLength;
    case Platforms:
        return item.platforms;
    default:
        qDebug() << Q_FUNC_INFO << "role type not supported";
        return QVariant();
    }

}

QStringList ExamplesListModel::tags() const
{
    ensureInitialized();
    return m_tags;
}

void ExamplesListModel::helpInitialized()
{
    m_helpInitialized = true;
    if (m_initialized) // if we are already initialized we need to update nevertheless
        updateExamples();
}

void ExamplesListModel::ensureInitialized() const
{
    if (m_initialized || !m_helpInitialized)
        return;
    ExamplesListModel *that = const_cast<ExamplesListModel *>(this);
    that->m_initialized = true;
    that->updateExamples();
}

ExamplesListModelFilter::ExamplesListModelFilter(ExamplesListModel *sourceModel, QObject *parent) :
    QSortFilterProxyModel(parent), m_showTutorialsOnly(true), m_sourceModel(sourceModel), m_timerId(0)
{
    connect(this, SIGNAL(showTutorialsOnlyChanged()), SLOT(updateFilter()));
    setSourceModel(m_sourceModel);
}

void ExamplesListModelFilter::updateFilter()
{
    ExamplesListModel *exampleListModel = qobject_cast<ExamplesListModel*>(sourceModel());
    if (exampleListModel) {
        exampleListModel->beginReset();
        invalidateFilter();
        exampleListModel->endReset();
    }
}

bool containsSubString(const QStringList& list, const QString& substr, Qt::CaseSensitivity cs)
{
    foreach (const QString &elem, list) {
        if (elem.contains(substr, cs))
            return true;
    }

    return false;
}

bool ExamplesListModelFilter::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    if (m_showTutorialsOnly) {
        int type = sourceModel()->index(sourceRow, 0, sourceParent).data(Type).toInt();
        if (type != Tutorial)
            return false;
    }

    if (!m_showTutorialsOnly) {
        int type = sourceModel()->index(sourceRow, 0, sourceParent).data(Type).toInt();
        if (type != Example)
            return false;
    }

    const QStringList tags = sourceModel()->index(sourceRow, 0, sourceParent).data(Tags).toStringList();

    if (!m_filterTags.isEmpty()) {
        foreach(const QString &tag, m_filterTags) {
            if (!tags.contains(tag, Qt::CaseInsensitive))
                return false;
        }
        return true;
    }

    if (!m_searchString.isEmpty()) {
        const QString description = sourceModel()->index(sourceRow, 0, sourceParent).data(Description).toString();
        const QString name = sourceModel()->index(sourceRow, 0, sourceParent).data(Name).toString();


        foreach(const QString &subString, m_searchString) {
            bool wordMatch = false;
            wordMatch |= (bool)name.contains(subString, Qt::CaseInsensitive);
            if (wordMatch)
                continue;
            wordMatch |= containsSubString(tags, subString, Qt::CaseInsensitive);
            if (wordMatch)
                continue;
            wordMatch |= (bool)description.contains(subString, Qt::CaseInsensitive);
            if (!wordMatch)
                return false;
        }
    }

    bool ok = QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent);
    if (!ok)
        return false;

    return true;
}

int ExamplesListModelFilter::rowCount(const QModelIndex &parent) const
{
    m_sourceModel->ensureInitialized();
    return QSortFilterProxyModel::rowCount(parent);
}

QVariant ExamplesListModelFilter::data(const QModelIndex &index, int role) const
{
    m_sourceModel->ensureInitialized();
    return QSortFilterProxyModel::data(index, role);
}

void ExamplesListModelFilter::setShowTutorialsOnly(bool showTutorialsOnly)
{
    m_showTutorialsOnly = showTutorialsOnly;
    emit showTutorialsOnlyChanged();
}

void ExamplesListModelFilter::delayedUpdateFilter()
{
    if (m_timerId != 0)
        killTimer(m_timerId);

    m_timerId = startTimer(320);
}

void ExamplesListModelFilter::timerEvent(QTimerEvent *timerEvent)
{
    if (m_timerId == timerEvent->timerId()) {
        updateFilter();
        killTimer(m_timerId);
        m_timerId = 0;
    }
}

struct SearchStringLexer {
    QString code;
    const QChar *codePtr;
    QChar yychar;
    QString yytext;

    enum TokenKind {
        END_OF_STRING = 0,
        TAG,
        STRING_LITERAL,
        UNKNOWN
    };

    inline void yyinp() { yychar = *codePtr++; }

    SearchStringLexer(const QString &code)
        : code(code)
        , codePtr(code.unicode())
        , yychar(QLatin1Char(' ')) { }

    int operator()() { return yylex(); }

    int yylex() {
        while (yychar.isSpace())
            yyinp(); // skip all the spaces

        yytext.clear();

        if (yychar.isNull())
            return END_OF_STRING;

        QChar ch = yychar;
        yyinp();

        switch (ch.unicode()) {
        case '"':
        case '\'':
        {
            const QChar quote = ch;
            yytext.clear();
            while (!yychar.isNull()) {
                if (yychar == quote) {
                    yyinp();
                    break;
                } if (yychar == QLatin1Char('\\')) {
                    yyinp();
                    switch (yychar.unicode()) {
                    case '"': yytext += QLatin1Char('"'); yyinp(); break;
                    case '\'': yytext += QLatin1Char('\''); yyinp(); break;
                    case '\\': yytext += QLatin1Char('\\'); yyinp(); break;
                    }
                } else {
                    yytext += yychar;
                    yyinp();
                }
            }
            return STRING_LITERAL;
        }

        default:
            if (ch.isLetterOrNumber() || ch == QLatin1Char('_')) {
                yytext.clear();
                yytext += ch;
                while (yychar.isLetterOrNumber() || yychar == QLatin1Char('_')) {
                    yytext += yychar;
                    yyinp();
                }
                if (yychar == QLatin1Char(':') && yytext == QLatin1String("tag")) {
                    yyinp();
                    return TAG;
                }
                return STRING_LITERAL;
            }
        }

        yytext += ch;
        return UNKNOWN;
    }
};

void ExamplesListModelFilter::parseSearchString(const QString &arg)
{
    QStringList tags;
    QStringList searchTerms;
    SearchStringLexer lex(arg);
    bool isTag = false;
    while (int tk = lex()) {
        if (tk == SearchStringLexer::TAG) {
            isTag = true;
            searchTerms.append(lex.yytext);
        }

        if (tk == SearchStringLexer::STRING_LITERAL) {
            if (isTag) {
                searchTerms.pop_back();
                tags.append(lex.yytext);
                isTag = false;
            } else {
                searchTerms.append(lex.yytext);
            }
        }
    }

    setSearchStrings(searchTerms);
    setFilterTags(tags);
    delayedUpdateFilter();
}

} // namespace Internal
} // namespace QtSupport
