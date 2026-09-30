param(
	[Parameter(Mandatory=$true)][string]$Executable,
	[Parameter(Mandatory=$true)][string]$Translation,
	[Parameter(Mandatory=$true)][string]$Version,
	[Parameter(Mandatory=$true)][string]$QtBin,
	[Parameter(Mandatory=$true)][string]$Iscc,
	[Parameter(Mandatory=$true)][string]$Output
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$source = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$QtBin = (Resolve-Path $QtBin).Path
$qtRoot = Split-Path $QtBin
$objdump = Join-Path $QtBin 'objdump.exe'
$env:PATH = "$QtBin;$env:PATH"
foreach ($file in @($Executable, $Translation, $Iscc, $objdump)) {
	if (!(Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing input: $file" }
}
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "Invalid version: $Version" }
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$Output = (Resolve-Path $Output).Path
# A fresh directory avoids packaging development databases or stale libraries.
$stage = Join-Path (Split-Path $Output) ('windows-stage-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item -LiteralPath $Executable -Destination (Join-Path $stage 'pastes.exe')
Copy-Item -LiteralPath $Translation -Destination (Join-Path $stage 'Pastes_zh_CN.qm')
Copy-Item -LiteralPath (Join-Path $source 'resources/pastes.ico') -Destination $stage
Copy-Item -LiteralPath (Join-Path $source 'LICENSE') -Destination $stage

# Use an explicit plugin list: no unused SQL servers or development plugins.
$plugins = (& (Join-Path $QtBin 'qmake.exe') -query QT_INSTALL_PLUGINS).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot query Qt plugin directory' }
$pluginFiles = @(
	'platforms/qwindows.dll', 'styles/qmodernwindowsstyle.dll',
	'sqldrivers/qsqlite.dll', 'iconengines/qsvgicon.dll',
	'tls/qschannelbackend.dll', 'networkinformation/qnetworklistmanager.dll'
)
$pluginFiles += Get-ChildItem -LiteralPath (Join-Path $plugins 'imageformats') -Filter '*.dll' |
	ForEach-Object { 'imageformats/' + $_.Name }
$origins = @{}
foreach ($relative in $pluginFiles) {
	$origin = Join-Path $plugins $relative
	$dest = Join-Path $stage $relative
	New-Item -ItemType Directory -Force -Path (Split-Path $dest) | Out-Null
	Copy-Item -LiteralPath $origin -Destination $dest
	$origins[$origin] = $true
}
@'
[Paths]
Prefix=.
Plugins=.
'@ | Set-Content -LiteralPath (Join-Path $stage 'qt.conf') -Encoding ascii

# Follow PE imports recursively, including non-Qt MSYS2 dependencies.
$queue = [Collections.Generic.Queue[string]]::new()
Get-ChildItem -LiteralPath $stage -Recurse -File | Where-Object Extension -in '.exe','.dll' |
	ForEach-Object { $queue.Enqueue($_.FullName) }
$seen = @{}
$systemLibraries = @{}
while ($queue.Count) {
	$file = $queue.Dequeue()
	if ($seen.ContainsKey($file)) { continue }
	$seen[$file] = $true
	$headers = & $objdump -p $file
	if ($LASTEXITCODE -ne 0) { throw "Cannot inspect $file" }
	if (($headers -join "`n") -notmatch 'file format pei-x86-64') {
		throw "Only x64 PE binaries are supported: $file"
	}
	foreach ($line in $headers) {
		if ($line -notmatch 'DLL Name:\s*(\S+)') { continue }
		$name = $Matches[1]
		$dest = Join-Path $stage $name
		if (Test-Path -LiteralPath $dest) { continue }
		$origin = Join-Path $QtBin $name
		if (Test-Path -LiteralPath $origin) {
			Copy-Item -LiteralPath $origin -Destination $dest
			$origins[$origin] = $true
			$queue.Enqueue($dest)
		} elseif ($name -match '^(api-ms-|ext-ms-)' -or
			(Test-Path -LiteralPath (Join-Path "$env:WINDIR/System32" $name))) {
			$systemLibraries[$name] = $true
		} else { throw "Unresolved dependency $name imported by $file" }
	}
}

# Include the installed MSYS2 packages' notices and record exact versions.
$pacman = Join-Path (Split-Path $qtRoot) 'usr/bin/pacman.exe'
if (!(Test-Path -LiteralPath $pacman)) { throw 'This packager requires an MSYS2 Qt toolchain' }
$originPaths = foreach ($origin in $origins.Keys) {
	$relative = $origin.Substring($qtRoot.Length).Replace('\','/')
	'/' + (Split-Path $qtRoot -Leaf) + $relative
}
$owners = @(& $pacman -Qqo -- @originPaths | Sort-Object -Unique)
if ($LASTEXITCODE -ne 0) { throw 'Cannot determine runtime package owners' }
$licenseRoot = Join-Path $stage 'licenses'
New-Item -ItemType Directory -Path $licenseRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $source '3rd/SingleApplication/LICENSE') `
	-Destination (Join-Path $licenseRoot 'SingleApplication-MIT.txt')
$packageFiles = & $pacman -Ql @owners
if ($LASTEXITCODE -ne 0) { throw 'Cannot list runtime packages' }
foreach ($line in $packageFiles) {
	$path = ($line -split ' ',2)[1]
	if ($path -notmatch '/share/licenses/(.+[^/])$') { continue }
	$relative = $Matches[1]
	$origin = Join-Path (Split-Path $qtRoot) $path.TrimStart('/')
	if (!(Test-Path -LiteralPath $origin -PathType Leaf)) { continue }
	$dest = Join-Path $licenseRoot $relative
	New-Item -ItemType Directory -Force -Path (Split-Path $dest) | Out-Null
	Copy-Item -LiteralPath $origin -Destination $dest -Force
}
$packageVersions = & $pacman -Q @owners
if ($LASTEXITCODE -ne 0) { throw 'Cannot query runtime package versions' }
$packageVersions | Set-Content -LiteralPath (Join-Path $licenseRoot 'packages.txt') -Encoding utf8
@'
Pastes: https://github.com/JackieLiu1/pastes
Qt: https://www.qt.io/ (dynamically linked; licenses are included here)
MSYS2 runtime packages and source recipes: https://github.com/msys2/MINGW-packages
Exact bundled package versions are recorded in packages.txt.
'@ | Set-Content -LiteralPath (Join-Path $licenseRoot 'README.txt') -Encoding utf8

$manifest = Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName |
	ForEach-Object {
		'{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower(),
			$_.FullName.Substring($stage.Length + 1).Replace('\','/')
	}
$manifest | Set-Content -LiteralPath (Join-Path $stage 'manifest.sha256') -Encoding ascii
& $Iscc "/DDeployDir=$stage" "/DMyAppVersion=$Version" "/DOutputDir=$Output" (Join-Path $source 'windows-deploy.iss')
if ($LASTEXITCODE -ne 0) { throw 'Inno Setup compilation failed' }
$installer = Join-Path $Output "Pastes-$Version-windows-x64-setup.exe"
if (!(Test-Path -LiteralPath $installer)) { throw 'Installer was not produced' }
'{0}  {1}' -f (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLower(),
	(Split-Path $installer -Leaf) |
	Set-Content -LiteralPath "$installer.sha256" -Encoding ascii
Write-Host "Installer: $installer"
Write-Host "Staging directory: $stage"
Write-Host "Verified $($seen.Count) x64 binaries; $($systemLibraries.Count) Windows DLL imports."
