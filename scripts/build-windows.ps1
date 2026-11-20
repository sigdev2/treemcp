param([string]$Preset = 'windows-debug')
$ErrorActionPreference = 'Stop'
cmake --preset $Preset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build --preset $Preset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
ctest --preset $Preset
exit $LASTEXITCODE
