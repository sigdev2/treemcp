#ifndef TREEMCP_CHECKSEARCH_HPP
#define TREEMCP_CHECKSEARCH_HPP

#include <functional>
#include <QJsonObject>

using ToolCall = std::function<QJsonObject(const QString&, const QJsonObject&)>;
void checkSearch(const ToolCall& call);

#endif // TREEMCP_CHECKSEARCH_HPP
