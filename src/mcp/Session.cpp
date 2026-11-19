#include "Session.hpp"

#include "Tools.hpp"

#include "treemcp/Version.hpp"

namespace treemcp {
namespace {

constexpr qsizetype MAX_SESSION_REQUESTS = 16384;

} // namespace

std::optional<QJsonObject> Session::handle(QByteArrayView frame)
{
    const auto decoded = JsonRpc::decode(frame);
    if (const auto* request = std::get_if<JsonRpc::Request>(&decoded)) {
        return dispatch(*request);
    }
    if (const auto* reply = std::get_if<QJsonObject>(&decoded)) {
        return *reply;
    }
    return std::nullopt;
}

std::optional<QJsonObject> Session::dispatch(const JsonRpc::Request& request)
{
    const QJsonValue& id = request.id;
    const QJsonObject params = request.params.toObject();
    const bool validParams = (request.params.isUndefined() || request.params.isObject()) &&
        (!params.contains("_meta") || params.value("_meta").isObject());
    if (id.isUndefined()) {
        if (validParams && request.method == "notifications/initialized" && phase == Phase::AwaitInitialized) {
            phase = Phase::Ready;
        }
        // No deferred work exists in this milestone. Cancellation of a finished request is silent.
        return std::nullopt;
    }
    if (usedIds.contains(id)) {
        return JsonRpc::error(id, -32600, QStringLiteral("Request ID already used in this session"));
    }
    if (usedIds.size() >= MAX_SESSION_REQUESTS) {
        return JsonRpc::error(id, -32001, QStringLiteral("Session request limit reached. Start a new session."));
    }
    usedIds.insert(id);
    if (!validParams) {
        return JsonRpc::error(id, -32602, QStringLiteral("Expected an object for params and optional _meta"));
    }
    if (request.method == "ping") {
        return JsonRpc::success(id, {});
    }
    if (request.method == "initialize") {
        return initialize(id, params);
    }
    if (phase != Phase::Ready) {
        return JsonRpc::error(id, -32002, QStringLiteral("Complete initialization first"));
    }
    if (request.method == "tools/list") {
        return Tools::list(id, params);
    }
    if (request.method == "tools/call") {
        return Tools::call(id, params);
    }
    return JsonRpc::error(id, -32601, QStringLiteral("Method not found"));
}

QJsonObject Session::initialize(const QJsonValue& id, const QJsonObject& params)
{
    if (phase != Phase::AwaitInitialize) {
        return JsonRpc::error(id, -32600, QStringLiteral("Session already initialized"));
    }
    const QJsonObject clientInfo = params.value("clientInfo").toObject();
    if (!params.value("protocolVersion").isString() || !params.value("capabilities").isObject() ||
        !clientInfo.value("name").isString() || !clientInfo.value("version").isString())
    {
        return JsonRpc::error(
            id,
            -32602,
            QStringLiteral("Expected protocolVersion, capabilities and clientInfo with name and version"));
    }
    phase = Phase::AwaitInitialized;
    return JsonRpc::success(
        id,
        { { "protocolVersion", "2025-11-25" },
          { "capabilities", QJsonObject { { "tools", QJsonObject {} } } },
          { "serverInfo",
            QJsonObject { { "name", "structural-mcp" }, { "version", QString::fromLatin1(VERSION) } } } });
}

} // namespace treemcp
