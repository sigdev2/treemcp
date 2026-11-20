#ifndef TREEMCP_STDIOSERVER_HPP
#define TREEMCP_STDIOSERVER_HPP

class QCoreApplication;

namespace treemcp {
class WorkspaceNode;
}

namespace treemcp::StdioServer {

/** @brief Run the main event loop until stdin closes or a transport operation fails. */
int run(QCoreApplication& application, const WorkspaceNode& root, int batchWorkers);

} // namespace treemcp::StdioServer

#endif // TREEMCP_STDIOSERVER_HPP
