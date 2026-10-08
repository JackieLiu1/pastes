# Linux installation packages

The `package` target selects the native package format using distribution
metadata and package databases. Debian and Ubuntu produce DEB packages;
RPM-based distributions produce RPM packages. Kylin desktop and server variants
can share the same distribution ID, so their native package database determines
the format. Unknown distributions produce a TGZ archive.

On Debian or Ubuntu, install the build and packaging dependencies:

```sh
sudo apt-get install build-essential cmake qt6-base-dev qt6-tools-dev \
    qt6-tools-dev-tools libglib2.0-dev libxtst-dev libx11-dev libxi-dev pkg-config dpkg-dev
```

On Kylin V11 Server, install the build and packaging dependencies:

```sh
sudo dnf install gcc-c++ cmake qt6-qtbase-devel qt6-qttools-devel \
    glib2-devel libXtst-devel libX11-devel libXi-devel pkgconf rpm-build chrpath
```

Qt package names can differ between RPM distributions. The application also
needs the SQLite and SVG Qt plugins and `secret-tool` at runtime; the RPM
declares file requirements for these instead of assuming distribution-specific
package names.

Build and package with the same target used on Windows and macOS:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --build build --target package
```

To override the format, pass `-DCPACK_GENERATOR=RPM` or `-DCPACK_GENERATOR=DEB`
when configuring. Cross-builds inspect the target sysroot rather than the host;
set the generator explicitly if the sysroot has no distribution metadata.
To return to automatic selection, remove the override with
`cmake -S . -B build -U CPACK_GENERATOR`.

CPack writes the package and its SHA-256 checksum to `build/dist`:

- RPM: `pastes-<version>-<release>.<architecture>.rpm`, such as
  `pastes-0.7.0-1.ky11.aarch64.rpm`. The release can include a distribution suffix.
- DEB: `pastes_<version>_<architecture>.deb`, such as
  `pastes_0.7.0_arm64.deb`.

The package includes the executable, Chinese translation, application icon,
license, application menu launcher and existing UKUI autostart entry.
The menu launcher is installed to `/usr/share/applications/pastes.desktop`
with the default installation prefix. It is visible across desktop environments;
the separate `/etc/xdg/autostart/pastes.desktop` retains the UKUI startup policy.
Qt libraries are supplied by the distribution. RPM automatically collects
linked-library requirements through `rpmbuild`; DEB uses `dpkg-shlibdeps`.
Plugins and the Secret Service credential
helper are declared separately because they are loaded at runtime.

The interface follows the session's message locale. Installing the Chinese
translation does not force Chinese when Qt detects an English or `C` locale.
Startup diagnostics report the selected locale and loaded translation path.
To check Chinese without changing the system locale, fully quit the existing
tray instance, then run `LC_ALL=zh_CN.UTF-8 pastes --show` from a terminal.

DEB architecture names differ from processor names: `aarch64` becomes `arm64`,
`x86_64` becomes `amd64`, and 32-bit x86 becomes `i386`. These targets do not
require `dpkg` for architecture detection. Other native DEB targets use
`dpkg --print-architecture`; for other cross-compilation targets or hosts
without `dpkg`, configure with
`-DCPACK_DEBIAN_PACKAGE_ARCHITECTURE=<Debian architecture>`.
RPM uses the target processor name, with `arm64` and `amd64` normalized to
`aarch64` and `x86_64`; override it with
`-DCPACK_RPM_PACKAGE_ARCHITECTURE=<RPM architecture>` when needed.
Without `dpkg-shlibdeps`, CPack omits automatically discovered DEB library
dependencies. Build distribution packages with the packaging tools for their
target distribution installed.

Inspect an RPM before installing it:

```sh
rpm -qpi build/dist/pastes-*.rpm
rpm -qp --requires build/dist/pastes-*.rpm
rpm -qpl build/dist/pastes-*.rpm
sudo dnf install ./build/dist/pastes-*.rpm
```

For DEB packages, use `dpkg-deb --info build/dist/pastes_*.deb` to inspect them
and `sudo apt install ./build/dist/pastes_*.deb` to install dependencies too.
Build on the oldest distribution you intend to support; building on a newer
system does not establish compatibility with older Qt or system libraries.
Packages are built for the target architecture.

New Linux build directories default to the `/usr` installation prefix. When
reusing a build directory configured with another prefix, set
`-DCMAKE_INSTALL_PREFIX=/usr` explicitly for a distribution package. Existing
history, sync settings and credentials are not included in the package.
