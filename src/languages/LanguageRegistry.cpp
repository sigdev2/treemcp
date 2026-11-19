#include "LanguageRegistry.hpp"

#include <tree_sitter/api.h>

extern "C" {
const TSLanguage* tree_sitter_cpp();
const TSLanguage* tree_sitter_python();
}

namespace treemcp::LanguageRegistry {

const QList<LanguageDefinition>& getLanguages()
{
    static const QList<LanguageDefinition> LANGUAGES {
        { QStringLiteral("cpp"),
          { QStringLiteral(".cpp"),
            QStringLiteral(".cc"),
            QStringLiteral(".cxx"),
            QStringLiteral(".hpp"),
            QStringLiteral(".hh"),
            QStringLiteral(".hxx"),
            QStringLiteral(".h") },
          tree_sitter_cpp() },
        { QStringLiteral("python"), { QStringLiteral(".py"), QStringLiteral(".pyi") }, tree_sitter_python() }
    };
    return LANGUAGES;
}

const LanguageDefinition* getLanguage(const QString& id)
{
    for (const LanguageDefinition& definition : getLanguages()) {
        if (definition.id == id) {
            return &definition;
        }
    }
    return nullptr;
}

} // namespace treemcp::LanguageRegistry
