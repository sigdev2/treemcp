param([string]$Preset = 'windows-debug')
cmake --preset $Preset
cmake --build --preset $Preset
ctest --preset $Preset
exit $LASTEXITCODE
