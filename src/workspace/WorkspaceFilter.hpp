#ifndef TREEMCP_WORKSPACEFILTER_HPP
#define TREEMCP_WORKSPACEFILTER_HPP

#include "IgnorePattern.hpp"
#include "WorkspaceNode.hpp"

#include <QRegularExpression>
#include <QStringList>

namespace treemcp {

class WorkspaceGlob {
public:
    static std::optional<WorkspaceGlob> compile(QString pattern);
    bool matches(const QStringList& parts, bool directory) const;

private:
    // An empty expression represents a whole ** component.
    QList<QRegularExpression> components;
    bool basename = false;
    bool directoriesOnly = false;
};

class IgnoreRules {
public:
    QString extend(const WorkspaceNode& directory, qsizetype baseOffset, const std::function<bool()>& stop);
    bool excludes(QByteArrayView path, bool directory, const std::function<bool()>& stop) const;

private:
    struct Rule {
        IgnorePattern pattern;
        qsizetype baseOffset;
        bool exclude;
    };
    QList<Rule> rules;

    QString readFile(
        const WorkspaceNode& directory,
        const QString& name,
        qsizetype baseOffset,
        const std::function<bool()>& stop);
};

} // namespace treemcp
#endif // TREEMCP_WORKSPACEFILTER_HPP
