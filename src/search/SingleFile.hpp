#ifndef TREEMCP_SINGLEFILE_HPP
#define TREEMCP_SINGLEFILE_HPP

#include <QJsonObject>
#include <variant>

namespace treemcp {
struct Workspace;
struct SearchStop;
} // namespace treemcp

namespace treemcp::SingleFile {

using Result = std::variant<QJsonObject, QString>;

// Arguments have been validated against the descriptor and optional defaults applied.
Result run(const QString& name, const QJsonObject& arguments, const Workspace& workspace, const SearchStop& stop);

} // namespace treemcp::SingleFile

#endif // TREEMCP_SINGLEFILE_HPP
