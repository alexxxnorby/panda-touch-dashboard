param([string]$Python = 'python')
$ErrorActionPreference = 'Stop'
# Scope the Windows Git dependency-checkout workaround to this process.
$env:GIT_CONFIG_COUNT = '1'
$env:GIT_CONFIG_KEY_0 = 'core.longpaths'
$env:GIT_CONFIG_VALUE_0 = 'true'
& $Python -m esphome compile (Join-Path $PSScriptRoot 'firmware/workshop.yaml')
if ($LASTEXITCODE -ne 0) { throw 'Firmware compilation failed.' }
