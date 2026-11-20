#include "treemcp/Version.hpp"

#include "SelfCheck.hpp"
#include "StdioServer.hpp"
#include "Workspace.hpp"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>
#include <QThread>
#include <QTimer>

#include <algorithm>

#include <cstdio>

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("structural-mcp"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(treemcp::VERSION));

    QCommandLineParser arguments;
    arguments.setApplicationDescription(QStringLiteral("TreeMCP stdio server."));
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
        QStringLiteral("batch-workers"),
        QStringLiteral("Number of file workers per batch (1 to 8, default %1).").arg(defaultWorkers),
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
        diagnostics << "structural-mcp " << treemcp::VERSION << '\n';
        return 0;
    }
    if (!arguments.positionalArguments().isEmpty()) {
        diagnostics << "Unexpected positional argument. Use --help.\n";
        return 2;
    }
    bool validWorkers = false;
    const int batchWorkers = arguments.value(workersOption).toInt(&validWorkers);
    if (!validWorkers || batchWorkers < 1 || batchWorkers > 8) {
        diagnostics << "Expected --batch-workers between 1 and 8.\n";
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
        if (workspace.root.kind() != treemcp::NodeKind::Directory) {
            diagnostics << "Cannot open workspace root directory.\n";
            return 2;
        }
        return treemcp::StdioServer::run(application, workspace, batchWorkers);
    }

    QTimer::singleShot(0, &application, []() {
        const QStringList failures = treemcp::SelfCheck::run();
        QTextStream report(stderr);
        if (!failures.isEmpty()) {
            report << failures.join(QLatin1Char('\n')) << '\n';
            QCoreApplication::exit(1);
            return;
        }
        report << "Self-check passed: Qt " << qVersion() << ", Tree-sitter, C++ and Python grammars.\n";
        QCoreApplication::exit(0);
    });
    return application.exec();
}
