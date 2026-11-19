#include "Framer.hpp"

namespace treemcp {

bool Framer::feed(QByteArrayView chunk, const Receiver& receive)
{
    Q_ASSERT(receive != nullptr);
    while (!chunk.isEmpty()) {
        const qsizetype newline = chunk.indexOf('\n');
        const qsizetype count = newline < 0 ? chunk.size() : newline;
        if (count > MAX_FRAME_BYTES - pending.size()) {
            pending.clear();
            discarding = true;
        }
        if (!discarding) {
            pending.append(chunk.data(), count);
        }
        if (newline < 0) {
            break;
        }
        QByteArrayView record(pending);
        if (record.endsWith('\r')) {
            record.chop(1);
        }
        if (!receive(record.contains('\r') ? QByteArrayView {} : record)) {
            return false;
        }
        pending.clear();
        discarding = false;
        chunk = chunk.sliced(newline + 1);
    }
    return true;
}

bool Framer::finish(const Receiver& receive)
{
    Q_ASSERT(receive != nullptr);
    const bool partial = discarding || !pending.isEmpty();
    pending.clear();
    discarding = false;
    return !partial || receive({});
}

} // namespace treemcp
