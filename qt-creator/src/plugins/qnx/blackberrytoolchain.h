#ifndef BLACKBERRYTOOLCHAIN_H
#define BLACKBERRYTOOLCHAIN_H

#include <projectexplorer/gcctoolchain.h>
#include <projectexplorer/toolchainconfigwidget.h>
#include <projectexplorer/abi.h>
#include <projectexplorer/abiwidget.h>

#include <utils/pathchooser.h>

namespace Qnx {
namespace Internal {

class BlackBerryToolChain : public ProjectExplorer::GccToolChain
{
public:
    ~BlackBerryToolChain();
    QString type() const;
    QString typeDisplayName() const;
    bool isValid() const;
    bool operator ==(const ProjectExplorer::ToolChain &) const;
    ProjectExplorer::ToolChainConfigWidget *configurationWidget();
    QVariantMap toMap() const;
    bool fromMap(const QVariantMap &data);
    QList<Utils::FileName> suggestedMkspecList() const;
    QString makeCommand(const Utils::Environment &env) const;
    void setQtVersionId(int);
    int qtVersionId() const;
    //explicit BlackBerryToolChain(QObject *parent = 0);
    
protected:
    QList<ProjectExplorer::Abi> detectSupportedAbis() const;

private:
    explicit BlackBerryToolChain(bool);
    BlackBerryToolChain(const BlackBerryToolChain &);

    int m_qtVersionId;
    friend class BlackBerryToolChainFactory;
    
};

class BlackBerryToolChainConfigWidget : public ProjectExplorer::ToolChainConfigWidget
{
    Q_OBJECT

public:
    BlackBerryToolChainConfigWidget(BlackBerryToolChain *);

private:
    void applyImpl() {}
    void discardImpl() {}
    bool isDirtyImpl() const { return false; }
    void makeReadOnlyImpl() {}

private:
    Utils::PathChooser *m_compilerCommand;
    ProjectExplorer::AbiWidget *m_abiWidget;
    bool m_isReadOnly;
};

/*
 *public:
    GccToolChainConfigWidget(GccToolChain *);

private slots:
    void handleCompilerCommandChange();

private:
    void applyImpl();
    void discardImpl() { setFromToolchain(); }
    bool isDirtyImpl() const;
    void makeReadOnlyImpl();

    void setFromToolchain();

    Utils::PathChooser *m_compilerCommand;
    AbiWidget *m_abiWidget;
    Utils::FileName m_autoDebuggerCommand;

    QList<Abi> m_abiList;
    bool m_isReadOnly;
 */
class BlackBerryToolChainFactory : public ProjectExplorer::ToolChainFactory
{
    Q_OBJECT

public:
    BlackBerryToolChainFactory();

    QString displayName() const;
    QString id() const;

    QList<ProjectExplorer::ToolChain *> autoDetect();
    bool canRestore(const QVariantMap &data);
    ProjectExplorer::ToolChain *restore(const QVariantMap &data);

private slots:
    void handleQtVersionChanges(const QList<int> &added, const QList<int> &removed, const QList<int> &changed);
    QList<ProjectExplorer::ToolChain *> createToolChainList(const QList<int> &);

};

} // namespace Internal
} // namespace Qnx

#endif // BLACKBERRYTOOLCHAIN_H
