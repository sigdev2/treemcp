#include "Framer.hpp"
#include "JsonRpc.hpp"

#include <QJsonDocument>

#include <algorithm>
#include <cstdint>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size > 8192) {
        return 0;
    }
    const QByteArrayView input(reinterpret_cast<const char*>(data), static_cast<qsizetype>(size));
    const auto frames = [&](qsizetype chunkSize) {
        treemcp::Framer framer;
        QList<QByteArray> records;
        const auto receive = [&](QByteArrayView bytes) {
            records.append(bytes.toByteArray());
            return true;
        };
        for (qsizetype start = 0; start < input.size(); start += chunkSize) {
            framer.feed(input.sliced(start, std::min(chunkSize, input.size() - start)), receive);
        }
        framer.finish(receive);
        return records;
    };
    const auto records = frames(8192);
    if (records != frames(size ? 1 + data[0] % 64 : 1)) {
        std::abort();
    }
    const auto decode = [](QByteArrayView bytes) {
        using namespace treemcp::JsonRpc;
        const auto decoded = treemcp::JsonRpc::decode(bytes);
        QJsonObject response;
        if (const auto* request = std::get_if<Request>(&decoded)) {
            response = success(request->id, request->params.toObject());
        } else if (const auto* failure = std::get_if<QJsonObject>(&decoded)) {
            response = *failure;
        } else {
            return;
        }
        const auto encoded = QJsonDocument(response).toJson(QJsonDocument::Compact);
        if (QJsonDocument::fromJson(encoded).object() != response) {
            std::abort();
        }
    };
    decode(input);
    for (const auto& record : records) {
        decode(record);
    }
    return 0;
}
