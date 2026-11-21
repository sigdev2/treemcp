#include "IgnorePattern.hpp"

#include <locale>

namespace treemcp {
namespace {

using ByteSet = std::bitset<256>;

bool addNamedClass(ByteSet& bytes, QByteArrayView name)
{
    if (name == "space") {
        for (const char ch : QByteArrayView(" \t\r\n")) {
            bytes.set(static_cast<unsigned char>(ch));
        }
        return true;
    }
    using Ctype = std::ctype<char>;
    static const struct {
        QByteArrayView name;
        Ctype::mask mask;
    } classes[] = { { "alnum", Ctype::alnum }, { "alpha", Ctype::alpha },  { "blank", Ctype::blank },
                    { "cntrl", Ctype::cntrl }, { "digit", Ctype::digit },  { "graph", Ctype::graph },
                    { "lower", Ctype::lower }, { "print", Ctype::print },  { "punct", Ctype::punct },
                    { "upper", Ctype::upper }, { "xdigit", Ctype::xdigit } };
    for (const auto& entry : classes) {
        if (entry.name == name) {
            const auto& ascii = std::use_facet<Ctype>(std::locale::classic());
            for (unsigned byte = 0; byte < 128; ++byte) {
                if (ascii.is(entry.mask, static_cast<char>(byte))) {
                    bytes.set(byte);
                }
            }
            return true;
        }
    }
    return false;
}

// On entry the opening '[' has been consumed. ']' is a member in the first position.
bool readBracket(QByteArrayView pattern, qsizetype& pos, ByteSet& bytes)
{
    const bool negate = pos < pattern.size() && (pattern[pos] == '!' || pattern[pos] == '^');
    if (negate) {
        ++pos;
    }
    int previous = -1;
    while (pos < pattern.size()) {
        char ch = pattern[pos++];
        if (ch == '-' && previous >= 0 && pos < pattern.size() && pattern[pos] != ']') {
            if (pattern[pos] == '\\' && ++pos == pattern.size()) {
                return false;
            }
            const int last = static_cast<unsigned char>(pattern[pos++]);
            for (int value = previous; value <= last; ++value) {
                bytes.set(static_cast<size_t>(value));
            }
            previous = -1;
        } else if (ch == '[' && pos < pattern.size() && pattern[pos] == ':') {
            const auto end = pattern.indexOf(']', pos + 1);
            if (end > pos + 1 && pattern[end - 1] == ':') {
                if (!addNamedClass(bytes, pattern.sliced(pos + 1, end - pos - 2))) {
                    return false;
                }
                pos = end + 1;
                previous = -1;
            } else {
                previous = '[';
                bytes.set('[');
            }
        } else {
            if (ch == '\\') {
                if (pos == pattern.size()) {
                    return false;
                }
                ch = pattern[pos++];
            }
            previous = static_cast<unsigned char>(ch);
            bytes.set(static_cast<size_t>(previous));
        }
        if (pos < pattern.size() && pattern[pos] == ']') {
            ++pos;
            if (negate) {
                bytes.flip();
            }
            bytes.reset('/');
            return true;
        }
    }
    return false;
}

} // namespace

IgnorePattern::IgnorePattern(QByteArrayView pattern)
{
    directoriesOnly = pattern.endsWith('/');
    if (directoriesOnly) {
        pattern.chop(1);
    }
    basenameOnly = !pattern.contains('/');
    if (pattern.startsWith('/')) {
        pattern = pattern.sliced(1);
    }
    for (qsizetype pos = 0; pos < pattern.size();) {
        const auto start = pos;
        char ch = pattern[pos++];
        Token token { Kind::Byte };
        if (ch == '*') {
            while (pos < pattern.size() && pattern[pos] == '*') {
                ++pos;
            }
            token.kind = Kind::Star;
            if (pos - start >= 2 && (start == 0 || pattern[start - 1] == '/')) {
                if (pos == pattern.size() || pattern.sliced(pos).startsWith("\\/")) {
                    token.kind = Kind::AcrossSlashes;
                } else if (pattern[pos] == '/') {
                    token.kind = Kind::Directories;
                    ++pos;
                }
            }
        } else if (ch == '?') {
            token.bytes.set();
            token.bytes.reset('/');
        } else if (ch == '[') {
            if (!readBracket(pattern, pos, token.bytes)) {
                valid = false;
                return;
            }
        } else {
            if (ch == '\\') {
                if (pos == pattern.size()) {
                    valid = false;
                    return;
                }
                ch = pattern[pos++];
            }
            if (tokens.isEmpty()) {
                prefix += ch;
                continue;
            }
            token.bytes.set(static_cast<unsigned char>(ch));
        }
        tokens.append(token);
    }
}

bool IgnorePattern::matches(
    QByteArrayView relativePath,
    QByteArrayView basename,
    bool directory,
    const std::function<bool()>& stop) const
{
    auto path = basenameOnly ? basename : relativePath;
    if (!valid || (directoriesOnly && !directory) || !path.startsWith(prefix)) {
        return false;
    }
    path = path.sliced(prefix.size());
    if (tokens.isEmpty()) {
        return path.isEmpty();
    }

    // reachable[i] means the tokens processed so far match the first i bytes.
    // Each token updates one row in place, without recursive wildcard backtracking.
    QByteArray reachable(path.size() + 1, 0);
    char* row = reachable.data();
    row[0] = 1;
    for (const Token& token : tokens) {
        if (stop && stop()) {
            return false;
        }
        switch (token.kind) {
        case Kind::Byte:
            for (qsizetype i = path.size(); i > 0; --i) {
                row[i] = row[i - 1] && token.bytes.test(static_cast<unsigned char>(path[i - 1]));
            }
            row[0] = 0;
            break;
        case Kind::Star:
        case Kind::AcrossSlashes:
            for (qsizetype i = 0; i < path.size(); ++i) {
                row[i + 1] = row[i + 1] || (row[i] && (token.kind == Kind::AcrossSlashes || path[i] != '/'));
            }
            break;
        case Kind::Directories: {
            // **/ can stay at any old position or consume through a later slash.
            bool started = false;
            for (qsizetype i = 0; i < path.size(); ++i) {
                started = started || row[i];
                row[i + 1] = row[i + 1] || (started && path[i] == '/');
            }
            break;
        }
        }
    }
    return row[path.size()] != 0;
}

} // namespace treemcp
