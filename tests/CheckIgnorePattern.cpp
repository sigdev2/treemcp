#include "WorkspaceFilter.hpp"

#include <QDebug>
#include <QFile>
#include <QTemporaryDir>

int main()
{
    // Some legal pattern subjects cannot be created as portable workspace filenames.
    QTemporaryDir root;
    QFile file(root.filePath(".ignore"));
    const QByteArray contents = "one\\   \r\ntwo\\ \\  \nslash\\\\   \n\\*\n\\?\n"
                                "s[[:space:]]\nb[[:blank:]]\nc[[:cntrl:]]\np[[:print:]]\n";
    if (!root.isValid() || !file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size()) {
        return 1;
    }
    file.close();
    treemcp::IgnoreRules rules;
    const auto directory = treemcp::WorkspaceNode::openRoot(root.path());
    if (!rules.extend(directory, 0, {}).isEmpty()) {
        return 1;
    }
    for (const auto path : { "one ", "two  ", "slash\\", "*", "?", "s\t", "b ", "c\f", "p~" }) {
        if (!rules.excludes(path, false, {})) {
            qCritical() << "Expected excluded path" << path;
            return 1;
        }
    }
    for (const auto path : { "one", "one  ", "two ", "slash\\ ", "ordinary", "s\f", "b\n", "c ", "p\x7f" }) {
        if (rules.excludes(path, false, {})) {
            qCritical() << "Expected visible path" << path;
            return 1;
        }
    }
    const treemcp::IgnorePattern pattern("*a*b*c");
    int checks = 0;
    if (!pattern.matches("aabbcc", "aabbcc", false, {}) ||
        pattern.matches("aabbcc", "aabbcc", false, [&] { return ++checks == 2; }) || checks != 2)
    {
        qCritical() << "Expected matching to stop between wildcard steps";
        return 1;
    }
    return 0;
}
