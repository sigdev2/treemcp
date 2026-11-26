#ifndef TREEMCP_JSONRPC_HPP
#define TREEMCP_JSONRPC_HPP

#include <QByteArrayView>
#include <QJsonObject>

#include <optional>
#include <variant>

namespace treemcp::JsonRpc {

struct Request {
    QJsonValue id;
    QString method;
    QJsonValue params;
};

/** @brief A request/notification, an immediate error reply, or an unsolicited response to ignore. */
using Decoded = std::variant<Request, QJsonObject, std::monostate>;

Decoded decode(QByteArrayView frame);
std::optional<QJsonObject> decodeObject(QByteArrayView input);
QJsonObject error(const QJsonValue& id, int code, const QString& message);
QJsonObject success(const QJsonValue& id, const QJsonObject& result);

} // namespace treemcp::JsonRpc

#endif // TREEMCP_JSONRPC_HPP
