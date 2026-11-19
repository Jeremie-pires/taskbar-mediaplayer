# Builds dist\TaskbarMediaPlayer-Setup-<version>.exe and dist\TaskbarMediaPlayer-<version>-portable.zip.
# Needs: Visual Studio 2022 and CMake (see build.bat) and Inno Setup 6 or later.
# The version comes from project(... VERSION x.y.z) in CMakeLists.txt.
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

$match = Select-String -Path (Join-Path $root "CMakeLists.txt") -Pattern 'project\([^)]*VERSION\s+(\d+\.\d+\.\d+)'
if (-not $match) { throw "No project VERSION found in CMakeLists.txt." }
$version = $match.Matches[0].Groups[1].Value

& (Join-Path $root "build.bat")
if ($LASTEXITCODE -ne 0) { throw "build.bat failed." }
$exe = Join-Path $root "build\Release\taskbar-mediaplayer.exe"

$iscc = foreach ($v in 7, 6) {
    foreach ($base in ${env:ProgramFiles(x86)}, $env:ProgramFiles, "$env:LOCALAPPDATA\Programs") { "$base\Inno Setup $v\ISCC.exe" }
}
$iscc = $iscc | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { $iscc = (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source }
if (-not $iscc) { throw "Inno Setup not found (https://jrsoftware.org/isdl.php)." }
& $iscc "/DAppVersion=$version" (Join-Path $PSScriptRoot "TaskbarMediaPlayer.iss")
if ($LASTEXITCODE -ne 0) { throw "ISCC failed." }

# Portable zip: same files, no installer.
$stage = Join-Path $root "build\portable"
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item $exe $stage
Copy-Item LICENSE (Join-Path $stage "LICENSE.txt")
Copy-Item THIRD-PARTY-NOTICES.md (Join-Path $stage "THIRD-PARTY-NOTICES.txt")
Compress-Archive -Path "$stage\*" -DestinationPath "dist\TaskbarMediaPlayer-$version-portable.zip" -Force

# sha256sum format with LF line ends, so `sha256sum -c SHA256SUMS.txt` works.
$sums = foreach ($name in "TaskbarMediaPlayer-Setup-$version.exe", "TaskbarMediaPlayer-$version-portable.zip") {
    "$((Get-FileHash (Join-Path 'dist' $name) -Algorithm SHA256).Hash.ToLower())  $name"
}
[IO.File]::WriteAllText((Join-Path $root 'dist\SHA256SUMS.txt'), (($sums -join "`n") + "`n"), (New-Object Text.UTF8Encoding($false)))
Write-Host "Done: dist\TaskbarMediaPlayer-Setup-$version.exe, dist\TaskbarMediaPlayer-$version-portable.zip and dist\SHA256SUMS.txt"
