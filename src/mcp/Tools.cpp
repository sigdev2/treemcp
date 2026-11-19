#include "Tools.hpp"

#include "JsonRpc.hpp"
#include "LanguageRegistry.hpp"
#include "SingleFile.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

namespace treemcp::Tools {
namespace {

const QJsonArray& catalog()
{
    static const QJsonArray TOOLS = []() {
        QJsonArray tools;
        for (const auto* name : { "languages", "query_validate", "syntax_tree", "structural_search" }) {
            QFile file(QStringLiteral(":/schemas/%1.json").arg(QString::fromLatin1(name)));
            if (!file.open(QIODevice::ReadOnly)) {
                return QJsonArray {};
            }
            const QJsonObject descriptor = QJsonDocument::fromJson(file.readAll()).object();
            if (descriptor.value("name") != QString::fromLatin1(name)) {
                return QJsonArray {};
            }
            tools.append(descriptor);
        }
        return tools;
    }();
    return TOOLS;
}

QJsonArray textContent(const QString& text)
{
    return { QJsonObject { { "type", "text" }, { "text", text } } };
}

QString validateArguments(const QJsonObject& schema, QJsonObject& arguments)
{
    const QJsonObject properties = schema.value("properties").toObject();
    for (auto it = arguments.begin(); it != arguments.end(); ++it) {
        if (!properties.contains(it.key())) {
            return QStringLiteral("Unknown argument: %1").arg(it.key().left(80));
        }
    }
    const QJsonArray required = schema.value("required").toArray();
    for (auto it = properties.begin(); it != properties.end(); ++it) {
        const QString key = it.key();
        const QJsonObject spec = it.value().toObject();
        const QJsonValue value = arguments.value(key);
        if (value.isUndefined()) {
            if (required.contains(key)) {
                return QStringLiteral("Missing argument: %1").arg(key);
            }
            arguments.insert(key, spec.value("default"));
        } else if (spec.value("type") == "string") {
            if (!value.isString() || (spec.contains("enum") && !spec.value("enum").toArray().contains(value))) {
                return QStringLiteral("Invalid value for %1").arg(key);
            }
            if (spec.contains("x-maxUtf8Bytes")) {
                const qsizetype bytes = value.toString().toUtf8().size();
                if (bytes < spec.value("minLength").toInt() || bytes > spec.value("x-maxUtf8Bytes").toInt()) {
                    return QStringLiteral("%1 is outside its UTF-8 byte limit").arg(key);
                }
            }
        } else {
            const int integer = value.toInt(-1);
            if (integer < spec.value("minimum").toInt() || integer > spec.value("maximum").toInt()) {
                return QStringLiteral("Invalid integer for %1").arg(key);
            }
        }
    }
    return {};
}

QJsonObject toolError(const QJsonValue& id, const QString& message)
{
    return JsonRpc::success(id, { { "isError", true }, { "content", textContent(message) } });
}

QJsonObject languageList()
{
    QJsonArray languages;
    for (const LanguageDefinition& language : LanguageRegistry::getLanguages()) {
        languages.append(
            QJsonObject { { "id", language.id }, { "extensions", QJsonArray::fromStringList(language.extensions) } });
    }
    return { { "languages", languages } };
}

} // namespace

QJsonObject list(const QJsonValue& id, const QJsonObject& params)
{
    if (params.contains("cursor")) {
        return JsonRpc::error(id, -32602, QStringLiteral("Invalid tool list cursor"));
    }
    if (catalog().isEmpty()) {
        return JsonRpc::error(id, -32603, QStringLiteral("Cannot read the embedded tool catalog"));
    }
    return JsonRpc::success(id, { { "tools", catalog() } });
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
    for (const auto tool : catalog()) {
        const QJsonObject descriptor = tool.toObject();
        if (name != descriptor.value("name")) {
            continue;
        }
        QJsonObject args = arguments.toObject();
        const QString error = validateArguments(descriptor.value("inputSchema").toObject(), args);
        if (!error.isEmpty()) {
            return toolError(id, error);
        }
        const SingleFile::Result result = name == "languages" ? languageList() : SingleFile::run(name.toString(), args);
        if (const auto* failure = std::get_if<QString>(&result)) {
            return toolError(id, *failure);
        }
        const QJsonObject data = std::get<QJsonObject>(result);
        const QJsonObject reply = JsonRpc::success(
            id,
            { { "structuredContent", data },
              { "content", textContent(QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact))) } });
        constexpr qsizetype MAX_RESPONSE_BYTES = 1024 * 1024;
        if (QJsonDocument(reply).toJson(QJsonDocument::Compact).size() + 1 > MAX_RESPONSE_BYTES) {
            return toolError(id, QStringLiteral("Response exceeds byte limit"));
        }
        return reply;
    }
    return JsonRpc::error(id, -32602, QStringLiteral("Unknown tool"));
}

} // namespace treemcp::Tools
