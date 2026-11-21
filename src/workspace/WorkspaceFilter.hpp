#ifndef TREEMCP_WORKSPACEFILTER_HPP
#define TREEMCP_WORKSPACEFILTER_HPP

#include "WorkspaceNode.hpp"

#include <QRegularExpression>
#include <QStringList>

namespace treemcp {

class WorkspaceGlob {
public:
    static std::optional<WorkspaceGlob> compile(QString pattern, qsizetype baseDepth = 0);
    bool matches(const QStringList& parts, bool directory) const;

private:
    // An empty expression represents a whole ** component.
    QList<QRegularExpression> components;
    qsizetype base = 0;
    bool basename = false;
    bool directoriesOnly = false;
};

class IgnoreRules {
public:
    QString extend(const WorkspaceNode& directory, qsizetype depth, const std::function<bool()>& stop);
    bool excludes(const QStringList& parts, bool directory) const;

private:
    struct Rule {
        WorkspaceGlob glob;
        bool exclude;
    };
    QList<Rule> rules;
};

} // namespace treemcp
#endif // TREEMCP_WORKSPACEFILTER_HPP
