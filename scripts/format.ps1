param([switch]$Check, [string]$ClangFormat = 'clang-format')
$options = if ($Check) { @('--dry-run', '--Werror') } else { @('-i') }
& $ClangFormat @options (Get-ChildItem src -Recurse -File -Include *.cpp, *.hpp).FullName
exit $LASTEXITCODE
