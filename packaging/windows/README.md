# Windows installer

Use the MSYS2 UCRT64 Qt 6 toolchain, PowerShell, CMake and
[Inno Setup 6.5 or newer](https://jrsoftware.org/isdl.php). The packager currently
supports x64 MSYS2 builds. It does not package MSVC builds.

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release `
    -DPASTES_ISCC_EXECUTABLE="C:/path/to/Inno Setup 6/ISCC.exe"
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
cmake --build build --target package
```

The target writes `build/dist/Pastes-<version>-windows-x64-setup.exe` and a
SHA-256 checksum. The version comes from `project()` in the root CMake file.
Each run uses a new `build/windows-stage-*` directory, copies only the application,
translation and required plugins, recursively resolves PE imports from the same
toolchain, and rejects missing dependencies or non-x64 binaries. Package license
files, exact MSYS2 package versions and a file checksum manifest are included.
No development database, credentials or settings are included.

The installer targets Windows 10 version 1809 or later and Windows 11 on x64.
It installs for the current user in `%LOCALAPPDATA%\Programs\Pastes` without
requesting elevation. This is intentional: the current Windows application
stores `PastesDatabase.db` beside its executable. Choose a directory writable
by the current user, not Program Files. Start Menu integration and uninstall
are included; desktop shortcut and launch at sign-in are optional.
Both English and Simplified Chinese setup languages are included. The Chinese
translation is vendored from Inno Setup's `is-6_7_3` release in
`Files/Languages/Unofficial/ChineseSimplified.isl`, with its contributor header.

Quit other copies before normal use to avoid duplicate clipboard monitoring;
the single-instance identity includes the executable path. Upgrades reuse the same AppId and installation
directory. Uninstall preserves the history database and application settings.
The installer is unsigned unless a signing workflow is added separately.

## Older machine-wide installations

Before moving from the old Program Files installation, exit Pastes and back up
its `PastesDatabase.db`. Uninstall that old copy, install the current-user build,
and copy the database beside the new executable before launching it. History is
not automatically migrated between installation directories. If the legacy
machine-wide Run value remains, running this installer elevated under the same
account removes both its 32-bit and 64-bit registry entries. A normal unelevated
installation cannot remove machine-wide entries. Startup for the new install
is controlled by the optional task or by the application's Settings page.

## Local packaging tool

Inno Setup's own installer supports portable extraction without registering
file associations or installing the compiler system-wide:

```powershell
.\innosetup-6.7.3.exe /PORTABLE=1 /CURRENTUSER /VERYSILENT /NORESTART `
    /DIR="C:\path\to\pastes\build\windows-tools\inno"
```

Verify the downloaded compiler's Authenticode signature first. CMake also searches
`build/windows-tools/inno/ISCC.exe`; otherwise pass `PASTES_ISCC_EXECUTABLE`.
