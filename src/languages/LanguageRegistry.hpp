#ifndef TREEMCP_LANGUAGEREGISTRY_HPP
#define TREEMCP_LANGUAGEREGISTRY_HPP

#include <QList>
#include <QString>
#include <QStringList>

struct TSLanguage;

namespace treemcp {

struct LanguageDefinition {
    QString id;
    QStringList extensions;
    const TSLanguage* language = nullptr;
};

/** @brief Immutable grammar metadata; no workspace discovery or content is retained. */
namespace LanguageRegistry {

const QList<LanguageDefinition>& getLanguages();
const LanguageDefinition* getLanguage(const QString& id);

} // namespace LanguageRegistry

} // namespace treemcp

#endif // TREEMCP_LANGUAGEREGISTRY_HPP
