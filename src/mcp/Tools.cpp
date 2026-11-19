#include "Tools.hpp"

#include "JsonRpc.hpp"
#include "LanguageRegistry.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

namespace treemcp::Tools {
namespace {

const QJsonObject& descriptor()
{
    static const QJsonObject LANGUAGES = []() {
        QFile file(QStringLiteral(":/schemas/languages.json"));
        return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject {};
    }();
    return LANGUAGES;
}

QJsonArray textContent(const QString& text)
{
    return { QJsonObject { { "type", "text" }, { "text", text } } };
}

} // namespace

QJsonObject list(const QJsonValue& id, const QJsonObject& params)
{
    if (params.contains("cursor")) {
        return JsonRpc::error(id, -32602, QStringLiteral("Invalid tool list cursor"));
    }
    if (descriptor().isEmpty()) {
        return JsonRpc::error(id, -32603, QStringLiteral("Cannot read the embedded tool catalog"));
    }
    return JsonRpc::success(id, { { "tools", QJsonArray { descriptor() } } });
}

QJsonObject call(const QJsonValue& id, const QJsonObject& params)
{
    const QJsonValue name = params.value("name");
    const QJsonValue arguments = params.value("arguments");
    if (!name.isString() || (!arguments.isUndefined() && !arguments.isObject()) || params.contains("task")) {
        return JsonRpc::error(
            id,
            -32602,
            QStringLiteral("Expected a tool name and optional arguments object for a synchronous call"));
    }
    if (name != descriptor().value("name")) {
        return JsonRpc::error(id, -32602, QStringLiteral("Unknown tool"));
    }
    if (!arguments.toObject().isEmpty()) {
        return JsonRpc::success(
            id,
            { { "isError", true }, { "content", textContent(QStringLiteral("languages takes no arguments")) } });
    }

    QJsonArray languages;
    for (const LanguageDefinition& language : LanguageRegistry::getLanguages()) {
        languages.append(
            QJsonObject { { "id", language.id }, { "extensions", QJsonArray::fromStringList(language.extensions) } });
    }
    const QJsonObject result { { "languages", languages } };
    return JsonRpc::success(
        id,
        { { "structuredContent", result },
          { "content", textContent(QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact))) } });
}

} // namespace treemcp::Tools
