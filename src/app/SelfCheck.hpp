#ifndef TREEMCP_SELFCHECK_HPP
#define TREEMCP_SELFCHECK_HPP

#include <QStringList>

namespace treemcp::SelfCheck {

/** @brief Exercise the linked runtime and grammars with in-memory fixtures; return failures. */
QStringList run();

} // namespace treemcp::SelfCheck

#endif // TREEMCP_SELFCHECK_HPP
