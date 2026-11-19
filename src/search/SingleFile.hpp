#ifndef TREEMCP_SINGLEFILE_HPP
#define TREEMCP_SINGLEFILE_HPP

#include <QJsonObject>
#include <variant>

namespace treemcp::SingleFile {

using Result = std::variant<QJsonObject, QString>;

// Arguments have been validated against the descriptor and optional defaults applied.
Result run(const QString& name, const QJsonObject& arguments);

} // namespace treemcp::SingleFile

#endif // TREEMCP_SINGLEFILE_HPP
