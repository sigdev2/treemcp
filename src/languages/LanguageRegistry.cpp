#include "LanguageRegistry.hpp"

#include <tree_sitter/api.h>

extern "C" {
const TSLanguage* tree_sitter_cpp();
const TSLanguage* tree_sitter_python();
const TSLanguage* tree_sitter_c();
const TSLanguage* tree_sitter_rust();
const TSLanguage* tree_sitter_javascript();
const TSLanguage* tree_sitter_typescript();
const TSLanguage* tree_sitter_tsx();
const TSLanguage* tree_sitter_go();
const TSLanguage* tree_sitter_java();
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
        { QStringLiteral("python"), { QStringLiteral(".py"), QStringLiteral(".pyi") }, tree_sitter_python() },
        { QStringLiteral("c"), { QStringLiteral(".c"), QStringLiteral(".h") }, tree_sitter_c() },
        { QStringLiteral("rust"), { QStringLiteral(".rs") }, tree_sitter_rust() },
        { QStringLiteral("javascript"),
          { QStringLiteral(".js"), QStringLiteral(".jsx"), QStringLiteral(".mjs"), QStringLiteral(".cjs") },
          tree_sitter_javascript() },
        { QStringLiteral("typescript"),
          { QStringLiteral(".ts"), QStringLiteral(".mts"), QStringLiteral(".cts") },
          tree_sitter_typescript() },
        { QStringLiteral("tsx"), { QStringLiteral(".tsx") }, tree_sitter_tsx() },
        { QStringLiteral("go"), { QStringLiteral(".go") }, tree_sitter_go() },
        { QStringLiteral("java"), { QStringLiteral(".java") }, tree_sitter_java() }
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
