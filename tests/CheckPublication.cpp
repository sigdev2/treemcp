#include "Session.hpp"
#include "Workspace.hpp"

#include <cstdio>
#include <QCoreApplication>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>

namespace {

// The worker has already stored the reply when it posts the completion event.
class BeforePublication : public QObject {
public:
    BeforePublication(treemcp::Session& target, QEventLoop& events) :
            session(target),
            loop(events)
    {
    }
    int id = 0;
    int completions = 0;

protected:
    bool eventFilter(QObject*, QEvent* event) override
    {
        if (event->type() == QEvent::MetaCall) {
            ++completions;
            if (id) {
                session.handle(
                    R"({"jsonrpc":"2.0","method":"notifications/cancelled","params":{"requestId":)" +
                    QByteArray::number(id) + "}}");
            }
            loop.quit();
        }
        return false;
    }

private:
    treemcp::Session& session;
    QEventLoop& loop;
};

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir directory;
    QFile file(directory.filePath("source.py"));
    if (!file.open(QIODevice::WriteOnly) || file.write("expected") != 8) {
        return 1;
    }
    file.close();
    const treemcp::Workspace workspace { treemcp::WorkspaceNode::openRoot(directory.path()), {} };
    QList<QJsonObject> replies;
    treemcp::Session session(workspace, 2, [&](const QJsonObject& reply) { replies.append(reply); });
    if (!session.handle(
            R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{},"protocolVersion":"2025-11-25","clientInfo":{"name":"test","version":"1"}}})"))
    {
        return 1;
    }
    session.handle(R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
    QEventLoop loop;
    BeforePublication gate(session, loop);
    session.installEventFilter(&gate);
    const QByteArray single =
        R"({"name":"structural_search","arguments":{"paths":["."],"language":"python","query":"(identifier) @name"}})";
    const QByteArray batch =
        R"({"name":"batch_search","arguments":{"paths":["."],"queries":[{"id":"names","language":"python","query":"(identifier) @name"}]}})";
    int id = 1;
    for (const auto& params : { single, batch }) {
        for (const bool cancel : { true, false }) {
            ++id;
            gate.id = cancel ? id : 0;
            const auto immediate = session.handle(
                R"({"jsonrpc":"2.0","method":"tools/call","id":)" + QByteArray::number(id) + R"(,"params":)" + params +
                '}');
            if (immediate) {
                return 1;
            }
            loop.exec();
            if (gate.completions != id - 1 || replies.size() != (cancel ? 0 : 1)) {
                return 1;
            }
            if (!cancel) {
                const auto reply = replies.takeFirst();
                auto result = reply.value("result").toObject().value("structuredContent").toObject();
                if (params == batch) {
                    result = result.value("items").toArray().at(0).toObject();
                }
                const auto match = result.value("matches").toArray().at(0).toObject();
                const auto capture = match.value("captures").toArray().at(0).toObject();
                if (reply.value("id") != id || capture.value("text") != "expected") {
                    return 1;
                }
            }
        }
    }
    std::puts("Cancelled completed single and batch replies before publication; subsequent results delivered");
    return 0;
}
