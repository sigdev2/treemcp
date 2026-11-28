#ifndef TREEMCP_BATCHSEARCH_HPP
#define TREEMCP_BATCHSEARCH_HPP

#include "SingleFile.hpp"

#include "SearchStop.hpp"

namespace treemcp {

class Scheduler;
class FileJobs;

QString validateBatch(const QJsonObject& arguments);
SingleFile::Result batchSearch(
    const QJsonValue& id,
    const QJsonObject& arguments,
    const Workspace& workspace,
    const SearchStop& stop,
    Scheduler& scheduler,
    FileJobs& files);

} // namespace treemcp
#endif // TREEMCP_BATCHSEARCH_HPP
