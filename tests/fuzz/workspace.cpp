#include "WorkspaceFilter.hpp"

#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size > 4096) {
        return 0;
    }
    const QByteArrayView input(reinterpret_cast<const char*>(data), static_cast<qsizetype>(size));
    const auto split = input.indexOf('\n');
    const auto pattern = split < 0 ? input : input.first(split);
    const auto path = split < 0 ? input : input.sliced(split + 1);
    const QString selector = QString::fromUtf8(path);
    const auto parts = selector.split('/');
    treemcp::validSelector(selector);
    for (const auto& part : parts) {
        treemcp::validComponent(part);
    }
    const auto glob = treemcp::WorkspaceGlob::compile(QString::fromUtf8(pattern));
    const treemcp::IgnorePattern ignore(pattern);
    const auto basename = path.sliced(path.lastIndexOf('/') + 1);
    for (const bool directory : { false, true }) {
        if (glob) {
            glob->matches(parts, directory);
        }
        ignore.matches(path, basename, directory, {});
    }
    return 0;
}
