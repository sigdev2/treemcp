#ifndef TREEMCP_FRAMER_HPP
#define TREEMCP_FRAMER_HPP

#include <QByteArray>
#include <QByteArrayView>

#include <functional>

namespace treemcp {

/** @brief Splits arbitrary stdin chunks into bounded records. Invalid frames deliver an empty view. */
class Framer {
public:
    using Receiver = std::function<bool(QByteArrayView)>;
    static constexpr qsizetype MAX_FRAME_BYTES = 1024 * 1024;

    bool feed(QByteArrayView chunk, const Receiver& receive);
    bool finish(const Receiver& receive);

private:
    QByteArray pending;
    bool discarding = false;
};

} // namespace treemcp

#endif // TREEMCP_FRAMER_HPP
