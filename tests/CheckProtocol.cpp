#include "CheckSearch.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>

#include <stdexcept>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

QByteArray message(const QJsonValue& id, const QString& method, const QJsonValue& params = QJsonValue::Undefined)
{
    const QJsonObject request { { "jsonrpc", "2.0" }, { "id", id }, { "method", method }, { "params", params } };
    return QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
}

QJsonObject readReply(QProcess& server)
{
    while (!server.canReadLine()) {
        require(server.waitForReadyRead(5000), "Timed out waiting for a complete response");
    }
    const QByteArray line = server.readLine();
    QJsonParseError error;
    const QJsonObject reply = QJsonDocument::fromJson(line, &error).object();
    require(
        error.error == QJsonParseError::NoError && reply.value("jsonrpc") == "2.0" &&
            reply.contains("result") != reply.contains("error") && line.endsWith('\n') && !line.contains('\r') &&
            line.size() <= 1024 * 1024,
        "Unexpected stdout record");
    return reply;
}

void expectError(QProcess& server, const QJsonValue& id, int expected)
{
    const QJsonObject reply = readReply(server);
    require(
        reply.value("id") == id && reply.value("error").toObject().value("code").toInt() == expected,
        "Incorrect error code or request ID");
}

void handshake(QProcess& server)
{
    const QJsonValue noId = QJsonValue::Undefined;
    server.write(message(noId, "notifications/initialized"));
    server.write(message(1, "tools/list"));
    expectError(server, 1, -32002);
    server.write(message(2, "initialize", QJsonObject {}));
    expectError(server, 2, -32602);

    const QJsonObject params { { "protocolVersion", "2099-01-01" },
                               { "capabilities", QJsonObject {} },
                               { "clientInfo", QJsonObject { { "name", "treemcp-wire-test" }, { "version", "1" } } } };
    const QByteArray init = message("init", "initialize", params);
    server.write(init.left(11));
    require(server.waitForBytesWritten(5000), "Could not write the first fragment");
    server.write(init.mid(11));
    const QJsonObject reply = readReply(server);
    const QJsonObject result = reply.value("result").toObject();
    require(
        reply.value("id") == "init" && result.value("protocolVersion") == "2025-11-25" &&
            result.value("capabilities").toObject() ==
                QJsonObject { { "tools", QJsonObject {} } }&& result.value("serverInfo").toObject().value("name") ==
                "structural-mcp",
        "Unexpected handshake");
    server.write(message(3, "tools/list"));
    expectError(server, 3, -32002);
    server.write(message(noId, "notifications/initialized", QJsonArray {}));
    server.write(message(4, "tools/list"));
    expectError(server, 4, -32002);
    server.write(message(noId, "notifications/initialized"));
    server.write(message(5, "initialize", params));
    expectError(server, 5, -32600);
}

void checkRecovery(QProcess& server)
{
    const QJsonValue noId = QJsonValue::Undefined;
    server.write("{\"bad\":\"\xff\"}\n");
    server.write(QByteArray(1024 * 1024 + 1, ' '));
    server.write("\n[]\n");
    expectError(server, noId, -32700);
    expectError(server, noId, -32700);
    expectError(server, noId, -32600);
    server.write("{\"jsonrpc\":\"2.0\",\"id\":73,\"method\":\"ping\",\"params\":{\"k\":1,\"k\":2}}\n");
    expectError(server, 73, -32600);
    server.write("{\"jsonrpc\":\"2.0\",\"id\":1,\"id\":2,\"method\":\"ping\"}\n");
    expectError(server, noId, -32600);
    server.write(message(noId, "notifications/cancelled", QJsonObject { { "requestId", 5 } }));
    server.write(message(noId, "notifications/example"));
    server.write("{\"jsonrpc\":\"2.0\",\"id\":5,\"result\":{}}\n");
    server.write(message("method", "missing"));
    expectError(server, "method", -32601);
    server.write(message("method", "ping"));
    expectError(server, "method", -32600);
    server.write(message("cursor", "tools/list", QJsonObject { { "cursor", "invalid" } }));
    expectError(server, "cursor", -32602);
    server.write(message("unknown", "tools/call", QJsonObject { { "name", "unknown_tool" } }));
    expectError(server, "unknown", -32602);
    server.write(
        message("args", "tools/call", QJsonObject { { "name", "languages" }, { "arguments", QJsonArray {} } }));
    expectError(server, "args", -32602);
    server.write(message(
        "extra",
        "tools/call",
        QJsonObject { { "name", "languages" }, { "arguments", QJsonObject { { "extra", 42 } } } }));
    const QJsonObject reply = readReply(server);
    require(
        reply.value("id") == "extra" && reply.value("result").toObject().value("isError") == true,
        "Input schema violation must be a tool error");
}

void checkTools(QProcess& server, const QString& schemaPath)
{
    QJsonArray descriptors;
    for (const auto* name : { "languages", "query_validate", "syntax_tree", "structural_search", "batch_search" }) {
        QFile file(schemaPath + '/' + QString::fromLatin1(name) + QStringLiteral(".json"));
        require(file.open(QIODevice::ReadOnly), "Cannot open expected tool descriptor");
        const QJsonObject descriptor = QJsonDocument::fromJson(file.readAll()).object();
        require(descriptor.value("name") == QString::fromLatin1(name), "Invalid tool descriptor");
        descriptors.append(descriptor);
    }
    server.write(message("list", "tools/list"));
    const QJsonObject list = readReply(server);
    require(
        list.value("id") == "list" && list.value("result").toObject() == QJsonObject { { "tools", descriptors } },
        "Incorrect tool catalog");

    server.write(message("call", "tools/call", QJsonObject { { "name", "languages" } }));
    const QJsonObject call = readReply(server);
    const QJsonObject result = call.value("result").toObject();
    const QJsonObject data = result.value("structuredContent").toObject();
    const QJsonArray languages = data.value("languages").toArray();
    require(call.value("id") == "call" && data.size() == 1 && languages.size() == 2, "Incorrect language list");
    for (const auto entry : languages) {
        const QJsonObject language = entry.toObject();
        require(
            language.size() == 2 && language.value("id").isString() && language.value("extensions").isArray(),
            "Language entry violates output schema");
        const QJsonArray extensions = language.value("extensions").toArray();
        for (const auto extension : extensions) {
            require(extension.isString(), "Invalid extension");
        }
    }
    require(
        languages.first().toObject().value("id") == "cpp" && languages.last().toObject().value("id") == "python",
        "Unexpected grammar IDs");
    const QJsonArray content = result.value("content").toArray();
    require(
        content.size() == 1 && content.first().toObject().value("type") == "text" &&
            content.first().toObject().value("text").toString().toUtf8() ==
                QJsonDocument(data).toJson(QJsonDocument::Compact),
        "Text result disagrees with structuredContent");
}

void pipeline(QProcess& server)
{
    // Send all requests before consuming responses. Numeric and string IDs with the same spelling are distinct.
    QSet<QJsonValue> outstanding;
    for (int i = 1000; i < 1500; ++i) {
        const QString textId = QString::number(i);
        server.write(message(i, "ping"));
        server.write(
            message(textId, "tools/call", QJsonObject { { "name", "languages" }, { "arguments", QJsonObject {} } }));
        outstanding.insert(i);
        outstanding.insert(textId);
    }
    server.write("{\"jsonrpc\":\"2.0\",\"method\":\"ping\",\"id\":99999}");
    server.closeWriteChannel();
    while (!outstanding.isEmpty()) {
        const QJsonObject reply = readReply(server);
        const QJsonValue id = reply.value("id");
        require(outstanding.remove(id) && reply.value("result").isObject(), "Missing, duplicate or unexpected ID");
        const QJsonObject result = reply.value("result").toObject();
        require(
            id.isString() ? result.contains("structuredContent") : result.isEmpty(),
            "Response belongs to another request");
    }
    expectError(server, QJsonValue::Undefined, -32700);
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    try {
        require(args.size() == 3, "Expected server executable and tool descriptor paths");
        QProcess server;
        server.start(args.at(1), QStringList {});
        require(server.waitForStarted(), "Cannot launch the MCP server");
        handshake(server);
        if (args.at(2) == "--search") {
            int id = 10;
            checkSearch([&](const QString& tool, const QJsonObject& arguments) {
                server.write(message(++id, "tools/call", QJsonObject { { "name", tool }, { "arguments", arguments } }));
                const QJsonObject reply = readReply(server);
                require(reply.value("id") == id && reply.value("result").isObject(), "Expected tool result");
                const QJsonObject result = reply.value("result").toObject();
                if (result.value("isError") == true) {
                    return result;
                }
                const QJsonObject data = result.value("structuredContent").toObject();
                require(
                    !data.isEmpty() &&
                        result.value("content").toArray().first().toObject().value("text").toString().toUtf8() ==
                            QJsonDocument(data).toJson(QJsonDocument::Compact),
                    "Text and structured results differ");
                return data;
            });
            server.closeWriteChannel();
        } else {
            checkRecovery(server);
            checkTools(server, args.at(2));
            pipeline(server);
        }
        require(
            server.state() == QProcess::NotRunning || server.waitForFinished(5000),
            "Server did not exit after EOF");
        require(
            server.exitStatus() == QProcess::NormalExit && server.exitCode() == 0 &&
                server.readAllStandardOutput().isEmpty() && server.readAllStandardError().isEmpty(),
            "Unexpected exit or trailing output");
    } catch (const std::exception& error) {
        qCritical() << error.what();
        return 1;
    }
    return 0;
}
