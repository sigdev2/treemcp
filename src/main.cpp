#include "treemcp/Version.hpp"

#include "SelfCheck.hpp"
#include "StdioServer.hpp"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>
#include <QTimer>

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
    if (!arguments.isSet(selfCheckOption)) {
        return treemcp::StdioServer::run(application);
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
