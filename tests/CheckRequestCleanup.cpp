#include "Scheduler.hpp"
#include "Tools.hpp"
#include "Workspace.hpp"

#include <QDebug>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>

#include <array>
#include <set>

extern "C" int __sanitizer_install_malloc_and_free_hooks(
    void (*allocated)(const volatile void*, size_t),
    void (*released)(const volatile void*));

namespace {

// Record across workers without allocating inside allocator callbacks. Replay after the call.
struct Event {
    const volatile void* pointer;
    bool allocated;
};
std::array<Event, 65536> events;
size_t count = 0;
std::mutex mutex;
bool recording = false;

void record(const volatile void* pointer, bool allocated)
{
    std::lock_guard lock(mutex);
    if (recording) {
        if (count < events.size()) {
            events[count] = { pointer, allocated };
        }
        ++count;
    }
}

} // namespace

int main()
{
    QTemporaryDir directory;
    QFile file(directory.filePath("source.py"));
    const treemcp::Workspace workspace { treemcp::WorkspaceNode::openRoot(directory.path()), {} };
    treemcp::Scheduler scheduler(1);
    const std::atomic_bool cancelled = false;
    using Clock = std::chrono::steady_clock;
    if (!__sanitizer_install_malloc_and_free_hooks(
            [](const volatile void* p, size_t) { record(p, true); },
            [](const volatile void* p) { record(p, false); }))
    {
        return 1;
    }
    for (const auto& name : { QStringLiteral("structural_search"), QStringLiteral("batch_search") }) {
        // Warm Qt resource and thread-local initialization before observing the next request.
        for (const bool measure : { false, true }) {
            const QByteArray source = measure ? "expected" : "warmup";
            if (!file.open(QIODevice::WriteOnly) || file.write(source) != source.size()) {
                return 1;
            }
            file.close();
            QJsonObject query { { "language", "python" },
                                { "query", measure ? "(identifier) @name" : "(identifier) @warmup" } };
            QJsonObject args;
            if (name == "batch_search") {
                query.insert("id", "names");
                args.insert("queries", QJsonArray { query });
            } else {
                args = query;
            }
            args.insert("paths", QJsonArray { "." });
            const QJsonObject params { { "name", name }, { "arguments", args } };
            {
                std::lock_guard lock(mutex);
                count = 0;
                recording = measure;
            }
            size_t withReply = 0;
            {
                auto reply = treemcp::Tools::call(1, params, [&](const QString& tool, const QJsonObject& admitted) {
                    return treemcp::Tools::execute(
                        1,
                        tool,
                        admitted,
                        workspace,
                        { cancelled, Clock::now() + std::chrono::seconds(10) },
                        scheduler);
                });
                if (!reply) {
                    return 1;
                }
                auto data = reply->value("result").toObject().value("structuredContent").toObject();
                if (name == "batch_search") {
                    data = data.value("items").toArray().at(0).toObject();
                }
                const auto match = data.value("matches").toArray().at(0).toObject();
                const auto capture = match.value("captures").toArray().at(0).toObject();
                if (capture.value("text").toString() != QString::fromLatin1(source)) {
                    return 1;
                }
                std::lock_guard lock(mutex);
                withReply = count;
            }
            {
                std::lock_guard lock(mutex);
                recording = false;
            }
            if (measure) {
                if (count > events.size() || withReply == 0 || withReply == count) {
                    return 1;
                }
                std::set<const volatile void*> live;
                for (size_t i = 0; i < count; ++i) {
                    if (events[i].allocated) {
                        live.insert(events[i].pointer);
                    } else {
                        live.erase(events[i].pointer);
                    }
                }
                qInfo() << name << count << "allocation/free events," << live.size()
                        << "request allocations still live after releasing reply";
                if (!live.empty()) {
                    return 1;
                }
            }
        }
    }
    return 0;
}
