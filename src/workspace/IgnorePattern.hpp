#ifndef TREEMCP_IGNOREPATTERN_HPP
#define TREEMCP_IGNOREPATTERN_HPP

#include <QByteArray>
#include <QList>

#include <bitset>
#include <functional>

namespace treemcp {

class IgnorePattern {
public:
    explicit IgnorePattern(QByteArrayView pattern);
    bool
        matches(QByteArrayView relativePath, QByteArrayView basename, bool directory, const std::function<bool()>& stop)
            const;

private:
    enum class Kind {
        Byte,
        Star,
        AcrossSlashes,
        Directories
    };
    struct Token {
        Kind kind;
        std::bitset<256> bytes {};
    };

    QByteArray prefix;
    QList<Token> tokens;
    bool basenameOnly;
    bool directoriesOnly;
    bool valid = true;
};

} // namespace treemcp

#endif // TREEMCP_IGNOREPATTERN_HPP
