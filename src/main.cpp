#include "treemcp/Version.hpp"

#include "JsonRpc.hpp"
#include "Scheduler.hpp"
#include "SelfCheck.hpp"
#include "StdioServer.hpp"
#include "Tools.hpp"
#include "Workspace.hpp"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <system_error>

#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#else
#include <csignal>
#endif

namespace {

int runCall(
    const QString& tool,
    const QString& input,
    const treemcp::Workspace& workspace,
    int workers,
    QTextStream& diagnostics)
{
    constexpr qsizetype MAX_ARGUMENT_BYTES = 1024 * 1024;
    QByteArray bytes;
    if (input == "-") {
#ifdef Q_OS_WIN
        if (_setmode(_fileno(stdin), _O_BINARY) == -1) {
            diagnostics << "Cannot set stdin to binary mode.\n";
            return 1;
        }
#endif
        bytes.resize(MAX_ARGUMENT_BYTES + 1);
        const auto count = std::fread(bytes.data(), 1, static_cast<size_t>(bytes.size()), stdin);
        if (std::ferror(stdin)) {
            diagnostics << "Cannot read tool arguments from stdin.\n";
            return 1;
        }
        bytes.resize(static_cast<qsizetype>(count));
    } else {
        bytes = input.toUtf8();
    }
    if (bytes.size() > MAX_ARGUMENT_BYTES) {
        diagnostics << "Tool arguments exceed the 1 MiB limit.\n";
        return 2;
    }
    if (bytes.startsWith("\xef\xbb\xbf")) {
        bytes.remove(0, 3);
    }
    const auto args = treemcp::JsonRpc::decodeObject(bytes);
    if (!args) {
        diagnostics << "Expected one UTF-8 JSON object for tool arguments.\n";
        return 2;
    }

    const auto received = std::chrono::steady_clock::now();
    const QJsonValue id(1);
    const auto reply = treemcp::Tools::call(
        id,
        { { "name", tool }, { "arguments", *args } },
        [&](const QString& name, const QJsonObject& validated) -> QJsonObject {
            const std::atomic_bool cancelled { false };
            const auto timeout = std::chrono::milliseconds(validated.value("deadlineMs").toInt());
            const treemcp::SearchStop stop { cancelled, received + timeout };
            treemcp::Scheduler scheduler(workers);
            treemcp::FileJobs files;
            auto result = treemcp::Tools::execute(id, name, validated, workspace, stop, scheduler, &files);
            return stop.requested() ? treemcp::Tools::deadlineReply(id, result) : result;
        });
    if (!reply) {
        diagnostics << "Tool did not return a reply.\n";
        return 1;
    }
    if (reply->contains("error")) {
        diagnostics << reply->value("error").toObject().value("message").toString() << '\n';
        return 2;
    }
    const auto result = reply->value("result").toObject();
    if (result.value("isError").toBool()) {
        diagnostics << result.value("content").toArray().first().toObject().value("text").toString() << '\n';
        return 1;
    }

    const auto output =
        QJsonDocument(result.value("structuredContent").toObject()).toJson(QJsonDocument::Compact) + '\n';
#ifdef Q_OS_WIN
    if (_setmode(_fileno(stdout), _O_BINARY) == -1) {
        diagnostics << "Cannot set stdout to binary mode.\n";
        return 1;
    }
#else
    std::signal(SIGPIPE, SIG_IGN);
#endif
    const auto size = static_cast<size_t>(output.size());
    if (std::fwrite(output.constData(), 1, size, stdout) != size || std::fflush(stdout) != 0) {
        diagnostics << "Cannot write tool result to stdout.\n";
        return 1;
    }
    return 0;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("treemcp"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(treemcp::VERSION));

    QCommandLineParser arguments;
    arguments.setApplicationDescription(QStringLiteral("Run a TreeMCP stdio server or call one tool and exit."));
    arguments.addPositionalArgument(
        QStringLiteral("call"),
        QStringLiteral("Run a single tool call."),
        QStringLiteral("[call TOOL]"));
    const QCommandLineOption helpOption = arguments.addHelpOption();
    const QCommandLineOption versionOption = arguments.addVersionOption();
    const QCommandLineOption selfCheckOption(
        QStringLiteral("self-check"),
        QStringLiteral("Check the embedded Tree-sitter runtime and grammars, then exit."));
    arguments.addOption(selfCheckOption);
    const QCommandLineOption workspaceOption(
        QStringLiteral("workspace-root"),
        QStringLiteral("Directory for workspace tools (default: current directory)."),
        QStringLiteral("directory"),
        QStringLiteral("."));
    arguments.addOption(workspaceOption);
    const QCommandLineOption toolArgsOption(
        QStringLiteral("args"),
        QStringLiteral("Tool arguments as a JSON object, or - to read UTF-8 JSON from stdin (default: {})."),
        QStringLiteral("JSON|-"),
        QStringLiteral("{}"));
    arguments.addOption(toolArgsOption);
    const QStringList defaultIgnored { "docs", "cmake", "build", "CMakeFiles", "dist", "coverage", "__pycache__" };
    QCommandLineOption ignoreDirOption(
        QStringLiteral("ignore-dir"),
        QStringLiteral("Skip this exact directory name in recursive searches. Repeatable. Replaces the defaults (%1).")
            .arg(defaultIgnored.join(QStringLiteral(", "))),
        QStringLiteral("name"));
    ignoreDirOption.setDefaultValues(defaultIgnored);
    arguments.addOption(ignoreDirOption);
    const int defaultWorkers = std::clamp(QThread::idealThreadCount(), 1, 8);
    const QCommandLineOption workersOption(
        QStringList { QStringLiteral("workers"), QStringLiteral("batch-workers") },
        QStringLiteral("Number of shared search workers (1 to 8, default %1).").arg(defaultWorkers),
        QStringLiteral("count"),
        QString::number(defaultWorkers));
    arguments.addOption(workersOption);

    QTextStream diagnostics(stderr);
    if (!arguments.parse(application.arguments())) {
        diagnostics << arguments.errorText() << '\n';
        return 2;
    }
    if (arguments.isSet(helpOption)) {
        diagnostics << arguments.helpText();
        return 0;
    }
    if (arguments.isSet(versionOption)) {
        diagnostics << "treemcp " << treemcp::VERSION << '\n';
        return 0;
    }
    const auto positional = arguments.positionalArguments();
    const bool call = !positional.isEmpty() && positional.first() == "call";
    if (!positional.isEmpty() && !call) {
        diagnostics << "Unexpected positional argument. Use --help.\n";
        return 2;
    }
    if (call && (positional.size() != 2 || positional.at(1).isEmpty() || arguments.isSet(selfCheckOption))) {
        diagnostics << "Expected call TOOL as a separate mode from --self-check. Use --help.\n";
        return 2;
    }
    if (arguments.isSet(toolArgsOption) && (!call || arguments.values(toolArgsOption).size() != 1)) {
        diagnostics << "Use --args once with call TOOL.\n";
        return 2;
    }
#ifndef Q_OS_WIN
    if (call) {
        // Qt converts Unix arguments to QString, replacing invalid UTF-8.
        for (int i = 1; i < argc; ++i) {
            if (!QByteArrayView(argv[i]).isValidUtf8()) {
                diagnostics << "Expected UTF-8 command-line arguments.\n";
                return 2;
            }
        }
    }
#endif
    bool validWorkers = false;
    const int workers = arguments.value(workersOption).toInt(&validWorkers);
    if (!validWorkers || workers < 1 || workers > 8) {
        diagnostics << "Expected --batch-workers or --workers between 1 and 8.\n";
        return 2;
    }
    const QStringList ignoredDirectories = arguments.values(ignoreDirOption);
    if (!std::all_of(ignoredDirectories.begin(), ignoredDirectories.end(), treemcp::validComponent)) {
        diagnostics << "Expected a single directory name for --ignore-dir.\n";
        return 2;
    }
    if (!arguments.isSet(selfCheckOption)) {
        const treemcp::Workspace workspace { treemcp::WorkspaceNode::openRoot(arguments.value(workspaceOption)),
                                             { ignoredDirectories.begin(), ignoredDirectories.end() } };
        if (workspace.root->kind() != treemcp::NodeKind::Directory) {
            diagnostics << "Cannot open workspace root directory.\n";
            return 2;
        }
        if (call) {
            try {
                return runCall(positional.at(1), arguments.value(toolArgsOption), workspace, workers, diagnostics);
            } catch (const std::system_error& error) {
                diagnostics << "Tool execution failed: " << error.what() << '\n';
                return 1;
            }
        }
        return treemcp::StdioServer::run(application, workspace, workers);
    }

    QTimer::singleShot(0, &application, []() {
        const QStringList failures = treemcp::SelfCheck::run();
        QTextStream report(stderr);
        if (!failures.isEmpty()) {
            report << failures.join(QLatin1Char('\n')) << '\n';
            QCoreApplication::exit(1);
            return;
        }
        report << "Self-check passed: Qt " << qVersion() << ", Tree-sitter and all registered grammars.\n";
        QCoreApplication::exit(0);
    });
    return application.exec();
}
