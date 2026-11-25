#include <tree_sitter/api.h>

extern "C" const TSLanguage* tree_sitter_cpp();
extern "C" const TSLanguage* tree_sitter_python();

// Simulate a packaging error in the test executable. The ABI check succeeds
// for both registrations and the Python fixture exposes the incorrect mapping.
extern "C" const TSLanguage* tree_sitter_python()
{
    return tree_sitter_cpp();
}
