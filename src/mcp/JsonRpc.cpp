#include "JsonRpc.hpp"

#include <QJsonArray>
#include <QStringDecoder>

#include <yyjson.h>

#include <limits>
#include <memory>

namespace treemcp::JsonRpc {
namespace {

constexpr int MAX_DEPTH = 64;
constexpr size_t MAX_OBJECT_MEMBERS = 1024;
constexpr size_t MAX_ID_BYTES = 1024;

QString stringValue(yyjson_val* value)
{
    // U+FEFF inside a JSON string is data, including at the start of a request ID.
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::ConvertInitialBom);
    return decoder.decode({ yyjson_get_str(value), static_cast<qsizetype>(yyjson_get_len(value)) });
}

// Undefined propagates duplicate keys or excessive nesting without conflating them with JSON null.
QJsonValue convert(yyjson_val* value, int depth)
{
    if (depth > MAX_DEPTH || yyjson_obj_size(value) > MAX_OBJECT_MEMBERS) {
        return QJsonValue::Undefined;
    }
    if (yyjson_is_obj(value)) {
        QJsonObject object;
        auto iterator = yyjson_obj_iter_with(value);
        while (yyjson_val* key = yyjson_obj_iter_next(&iterator)) {
            const QString name = stringValue(key);
            const QJsonValue member = convert(yyjson_obj_iter_get_val(key), depth + 1);
            if (member.isUndefined() || object.contains(name)) {
                return QJsonValue::Undefined;
            }
            object.insert(name, member);
        }
        return object;
    }
    if (yyjson_is_arr(value)) {
        QJsonArray array;
        auto iterator = yyjson_arr_iter_with(value);
        while (yyjson_val* item = yyjson_arr_iter_next(&iterator)) {
            const QJsonValue member = convert(item, depth + 1);
            if (member.isUndefined()) {
                return QJsonValue::Undefined;
            }
            array.append(member);
        }
        return array;
    }
    if (yyjson_is_str(value)) {
        return stringValue(value);
    }
    if (yyjson_is_sint(value) ||
        (yyjson_is_uint(value) && yyjson_get_uint(value) <= std::numeric_limits<qint64>::max()))
    {
        const qint64 integer = yyjson_get_sint(value);
        return integer;
    }
    if (yyjson_is_num(value)) {
        return yyjson_get_num(value);
    }
    if (yyjson_is_bool(value)) {
        return yyjson_get_bool(value);
    }
    return QJsonValue::Null;
}

bool validId(yyjson_val* id)
{
    return (yyjson_is_str(id) && yyjson_get_len(id) <= MAX_ID_BYTES) || yyjson_is_sint(id) ||
        (yyjson_is_uint(id) && yyjson_get_uint(id) <= std::numeric_limits<qint64>::max());
}

} // namespace

QJsonObject error(const QJsonValue& id, int code, const QString& message)
{
    return { { "jsonrpc", "2.0" },
             { "id", id },
             { "error", QJsonObject { { "code", code }, { "message", message } } } };
}

QJsonObject success(const QJsonValue& id, const QJsonObject& result)
{
    return { { "jsonrpc", "2.0" }, { "id", id }, { "result", result } };
}

Decoded decode(QByteArrayView frame)
{
    using Document = std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)>;
    const Document document(yyjson_read(frame.data(), static_cast<size_t>(frame.size()), 0), yyjson_doc_free);
    if (!document) {
        return error(QJsonValue::Undefined, -32700, QStringLiteral("Parse error"));
    }

    yyjson_val* root = yyjson_doc_get_root(document.get());
    const QJsonObject object = convert(root, 0).toObject();
    yyjson_val* rawId = yyjson_obj_get(root, "id");
    const QJsonValue id = object.value("id");
    if (object.value("jsonrpc") != QStringLiteral("2.0") || (rawId != nullptr && !validId(rawId))) {
        return error(validId(rawId) ? id : QJsonValue::Undefined, -32600, QStringLiteral("Invalid request"));
    }

    const QJsonValue method = object.value("method");
    const QJsonValue result = object.value("result");
    const QJsonValue failure = object.value("error");
    if (!result.isUndefined() || !failure.isUndefined()) {
        const QJsonObject details = failure.toObject();
        const bool validResult = result.isObject() && failure.isUndefined() && !id.isUndefined();
        const bool validError = result.isUndefined() && details.value("message").isString() &&
            yyjson_is_int(yyjson_obj_get(yyjson_obj_get(root, "error"), "code"));
        if (method.isUndefined() && !object.contains("params") && (validResult || validError)) {
            return std::monostate {};
        }
        return error(id, -32600, QStringLiteral("Invalid response envelope"));
    }
    if (!method.isString()) {
        return error(id, -32600, QStringLiteral("Expected a method string"));
    }
    return Request { id, method.toString(), object.value("params") };
}

} // namespace treemcp::JsonRpc
