# Tree-sitter query language

Status: accepted.

Use Tree-sitter S-expression queries as the canonical structural search language.
Do not add a second pattern matcher, YAML rule engine or external CLI wrapper.
The runtime and selected grammars are called directly through the C API.

This keeps query errors, captures and ranges aligned with the parser API. Predicate
semantics require explicit application implementation and validation; compiling a
query alone does not establish full CLI predicate compatibility.

Single-file validation and search accept up to 65,536 UTF-8 bytes per query.
Both tools compile and check the same predicate steps. The input descriptors
publish the byte cap alongside JSON Schema's character limit. [Predicate semantics and output limits](../reference/tools.md)

Future convenience syntax may generate a restricted Tree-sitter query without
introducing an independent matching engine.
