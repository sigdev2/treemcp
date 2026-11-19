#include "Framer.hpp"
#include "JsonRpc.hpp"

#include <QDebug>
#include <QJsonDocument>

namespace {

bool checkFrames()
{
    treemcp::Framer framer;
    QList<QByteArray> records;
    const auto collect = [&records](QByteArrayView bytes) {
        records.append(bytes.toByteArray());
        return true;
    };
    const QByteArray limit(treemcp::Framer::MAX_FRAME_BYTES, 'x');
    return framer.feed("one\ntw", collect) && framer.feed("o\r\n", collect) && framer.feed(limit, collect) &&
        framer.feed("\n", collect) && framer.feed(limit, collect) && framer.feed("x", collect) &&
        framer.feed("bad\nok\npartial", collect) && framer.finish(collect) && framer.finish(collect) &&
        records == QList<QByteArray> { "one", "two\r", limit, "", "ok", "" };
}

bool checkError(const QByteArray& frame, int code)
{
    const auto decoded = treemcp::JsonRpc::decode(frame);
    const auto* reply = std::get_if<QJsonObject>(&decoded);
    return reply != nullptr && reply->value("error").toObject().value("code").toInt() == code;
}

} // namespace

int main()
{
    using namespace treemcp::JsonRpc;
    if (!checkFrames() || !checkError("true", -32600) || !checkError("[]", -32600) ||
        !checkError(R"({"jsonrpc":"2.0","id":null,"method":"ping"})", -32600) ||
        !checkError(R"({"jsonrpc":"2.0","id":1.25,"method":"ping"})", -32600) ||
        !checkError(R"({"jsonrpc":"2.0","id":1,"\u0069d":2,"method":"ping"})", -32600) ||
        !checkError(R"({"method":"\x"})", -32700) || !checkError("{\"method\":\"\xff\"}", -32700) ||
        !checkError("{}{}", -32700) || !checkError("[1,]", -32700))
    {
        qCritical() << "Frame or JSON-RPC validation failed";
        return 1;
    }
    const auto decoded = decode(R"({"jsonrpc":"2.0","id":9223372036854775807,"method":"ping"})");
    const auto* request = std::get_if<Request>(&decoded);
    if (request == nullptr || !QJsonDocument(success(request->id, {})).toJson().contains("9223372036854775807")) {
        qCritical() << "Integer ID lost precision";
        return 1;
    }
    return 0;
}
