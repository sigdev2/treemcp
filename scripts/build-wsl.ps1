param([string]$Preset = 'linux-debug')
wsl.exe --exec bash scripts/build-linux.sh $Preset
exit $LASTEXITCODE
