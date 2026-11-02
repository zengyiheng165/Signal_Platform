# find_vivado.ps1
#   Print the full path of <Vivado>\bin\vivado.bat and exit 0 when found.
#   Print nothing and exit 1 otherwise.
#
# Search order (same idea as the Vitis side of this project):
#   1. environment variables  XILINX_VIVADO / VIVADO_ROOT
#   2. PATH
#   3. Windows uninstall registry entries written by the AMD installer
#      (the unified installer registers the product as "... Vitis Unified
#       Software Platform <ver>", which also contains Vivado, so both
#       "Vivado" and "Vitis" names are matched)
#   4. common installation layouts on each drive
#
# Keep this file ASCII-only: Windows PowerShell 5.1 reads .ps1 files with the
# system code page unless there is a BOM.

$ErrorActionPreference = 'SilentlyContinue'

$found = New-Object System.Collections.Generic.List[string]

function Add-VivadoDir([string]$dir) {
    if (-not $dir) { return }
    $exe = Join-Path $dir 'bin\vivado.bat'
    try {
        if (Test-Path -LiteralPath $exe) {
            $full = (Get-Item -LiteralPath $exe).FullName
            if (-not $found.Contains($full)) { $found.Add($full) }
        }
    } catch { }
}

# ---- 1) environment ------------------------------------------------------
foreach ($v in @($env:XILINX_VIVADO, $env:VIVADO_ROOT)) {
    if ($v) { Add-VivadoDir $v }
}

# ---- 2) PATH -------------------------------------------------------------
$cmd = Get-Command 'vivado.bat' -ErrorAction SilentlyContinue
if ($cmd -and $cmd.Source) {
    Add-VivadoDir (Split-Path (Split-Path -LiteralPath $cmd.Source -Parent) -Parent)
}

# ---- 3) registry ---------------------------------------------------------
$keys = @(
    'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
    'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*',
    'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*'
)
foreach ($key in $keys) {
    Get-ItemProperty -Path $key -ErrorAction SilentlyContinue |
        Where-Object { $_.DisplayName -match 'Vivado|Vitis' } |
        ForEach-Object {
            $ver = $_.DisplayVersion
            $bases = @()
            if ($_.InstallLocation) { $bases += $_.InstallLocation }
            if ($_.DisplayIcon) {
                $iconDir = Split-Path -LiteralPath $_.DisplayIcon -Parent
                if ($iconDir) { $bases += (Split-Path -LiteralPath $iconDir -Parent) }
            }
            foreach ($base in $bases) {
                if (-not $base) { continue }
                if ($ver) {
                    Add-VivadoDir (Join-Path $base "$ver\Vivado")
                    Add-VivadoDir (Join-Path $base "Vivado\$ver")
                    Add-VivadoDir (Join-Path $base $ver)
                }
                Add-VivadoDir (Join-Path $base 'Vivado')
                Add-VivadoDir $base
            }
        }
}

# ---- 4) common layouts ---------------------------------------------------
$drives = @('C:', 'D:', 'E:', 'F:', 'G:', 'H:')
foreach ($d in $drives) {
    if (-not (Test-Path -LiteralPath "$d\")) { continue }
    foreach ($ver in @('2026.2', '2026.1', '2025.2', '2025.1', '2024.2', '2024.1')) {
        Add-VivadoDir (Join-Path "$d\" "$ver\Vivado")
        Add-VivadoDir (Join-Path "$d\" "Xilinx\Vivado\$ver")
        Add-VivadoDir (Join-Path "$d\" "Xilinx\$ver\Vivado")
        Add-VivadoDir (Join-Path "$d\" "AMD\Vivado\$ver")
        Add-VivadoDir (Join-Path "$d\" "AMDDesignTools\$ver\Vivado")
    }
    foreach ($base in @('Xilinx\Vivado', 'AMD\Vivado', 'AMDDesignTools')) {
        $root = Join-Path "$d\" $base
        if (Test-Path -LiteralPath $root) {
            Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue | ForEach-Object {
                Add-VivadoDir (Join-Path $_.FullName 'Vivado')
                Add-VivadoDir $_.FullName
            }
        }
    }
}

if ($found.Count -eq 0) { exit 1 }

# prefer the version this project targets, otherwise the first hit
$preferred = $found | Where-Object { $_ -match '2026\.1' } | Select-Object -First 1
if ($preferred) {
    Write-Output $preferred
    exit 0
}
Write-Output ($found | Select-Object -First 1)
exit 0
