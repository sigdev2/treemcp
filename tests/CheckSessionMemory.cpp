#include "Session.hpp"
#include "WorkspaceNode.hpp"

#include <QDebug>
#include <QJsonDocument>

#include <cstddef>

// GCC's ASan runtime exports this allocator interface but does not ship its header.
extern "C" std::size_t __sanitizer_get_current_allocated_bytes();

int main()
{
    const auto root = treemcp::WorkspaceNode::openRoot(QStringLiteral("."));
    treemcp::Session session(root);
    constexpr int PAYLOAD_BYTES = 64 * 1024;
    const QJsonObject params { { "payload", QString(PAYLOAD_BYTES, 'x') } };
    const auto ping = [&](const QString& id) {
        const QJsonObject request { { "jsonrpc", "2.0" }, { "id", id }, { "method", "ping" }, { "params", params } };
        const auto response = session.handle(QJsonDocument(request).toJson(QJsonDocument::Compact));
        return response && response->value("id") == id && response->value("result").isObject();
    };
    if (!ping(QStringLiteral("warmup"))) {
        return 1;
    }
    const auto before = __sanitizer_get_current_allocated_bytes();
    for (int i = 0; i < 8; ++i) {
        if (!ping(QString::number(i))) {
            return 1;
        }
    }
    // History may allocate eight short IDs and set buckets. Allow one full payload
    // of overhead while detecting an envelope retained for each completed request.
    if (__sanitizer_get_current_allocated_bytes() > before + PAYLOAD_BYTES) {
        qCritical() << "Session ID history retained completed request data";
        return 1;
    }
    return 0;
}
