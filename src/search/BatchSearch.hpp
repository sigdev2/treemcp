#ifndef TREEMCP_BATCHSEARCH_HPP
#define TREEMCP_BATCHSEARCH_HPP

#include "SingleFile.hpp"

#include <atomic>
#include <chrono>

namespace treemcp {

struct BatchControl {
    const std::atomic_bool& cancelled;
    int workers;
    std::chrono::steady_clock::time_point received;
};

QString validateBatch(const QJsonObject& arguments);
SingleFile::Result batchSearch(
    const QJsonValue& id,
    const QJsonObject& arguments,
    const WorkspaceNode& workspace,
    const BatchControl& control);

} // namespace treemcp
#endif // TREEMCP_BATCHSEARCH_HPP
