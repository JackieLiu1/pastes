# Linux installation packages

The `package` target selects the native package format using distribution
metadata and package databases. Debian and Ubuntu produce DEB packages;
RPM-based distributions produce RPM packages. Kylin desktop and server variants
can share the same distribution ID, so their native package database determines
the format. Unknown distributions produce a TGZ archive.

On Debian or Ubuntu, install the build and packaging dependencies:

```sh
sudo apt-get install build-essential cmake qt6-base-dev qt6-tools-dev \
    qt6-tools-dev-tools libqt6svg6 libqt6sql6-sqlite qt6-image-formats-plugins \
    libglib2.0-dev libxtst-dev libx11-dev libxi-dev pkg-config dpkg-dev patchelf
```

For a self-built Qt 6.3 or newer, including on Ubuntu 20.04, install the
system dependencies without the distribution's Qt packages:

```sh
sudo apt-get install build-essential cmake libglib2.0-dev libxtst-dev \
    libx11-dev libxi-dev pkg-config dpkg-dev patchelf
```

Point `CMAKE_PREFIX_QT_PATH` at the Qt installation prefix, rather than its
source directory. Pastes adds this prefix to `CMAKE_PREFIX_PATH` before
finding Qt; the standard `CMAKE_PREFIX_PATH` option also remains supported.
The Qt installation must provide Core, Gui, Widgets, Sql, Network and
LinguistTools (from qttools). Packaging also requires the X11 platform,
SQLite and SVG plugins from qtbase and qtsvg.

```sh
cmake -S . -B build -DCMAKE_PREFIX_QT_PATH=/home/jackieliu/qt \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --build build --target package
```

If CMake cannot find the X11 `Xtst` component, install `libxtst-dev` and
rerun configuration. The `libxtst6` runtime package alone is insufficient
for building Pastes.

On Kylin V11 Server, install the build and packaging dependencies:

```sh
sudo dnf install gcc-c++ cmake qt6-qtbase-devel qt6-qttools-devel \
    qt6-qtsvg glib2-devel libXtst-devel libX11-devel libXi-devel \
    pkgconf rpm-build chrpath patchelf
```

Qt package names can differ between RPM distributions. Packages bundle Qt
by default, using the installation resolved by CMake. This also works with
self-built Qt on Ubuntu 20.04 and does not require system Qt 6 packages on
the destination machine.

Build and package with the same target used on Windows and macOS:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
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
Following OrcaTerminal's Linux packaging, application files are installed
under `/opt/pastes`: the executable is `/opt/pastes/pastes`, Qt/ICU libraries
are in `lib/`, plugins in `plugins/`, and translations in `translations/`.
The license and `qt.conf` also live in `/opt/pastes`. Desktop and autostart
entries launch `/opt/pastes/pastes`; `/usr/bin/pastes` remains a CLI launcher.
`qt.conf` selects the private plugin directory; relative RPATHs let the copied
libraries and plugins resolve one another without the build machine's Qt installation.
SQLite, image formats, platform, input method and TLS plugins are included.
System libraries such as glibc, X11 and OpenGL are not bundled.

In bundled mode, DEB declares system runtime dependencies explicitly and
disables `dpkg-shlibdeps`, as self-built Qt has no dpkg library metadata.
RPM retains automatic system dependency discovery and excludes private
Qt/ICU provides and requires. The Secret Service helper is a system runtime
dependency in both formats.

Set `-DPASTES_LINUX_BUNDLE_QT=OFF` to build a smaller package using system
Qt. That mode also installs the executable in `/opt/pastes`, uses
`dpkg-shlibdeps` for DEB and declares the system SQLite and SVG plugin packages.
For a bundled staging installation, use `DESTDIR=/path/to/stage cmake --install build`.

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

Linux packaging fixes the installation root at `/`, with application files
under `/opt/pastes` and desktop integration under `/usr/share` and `/etc/xdg`.
Reconfiguration also updates build directories previously using `/usr`.
Existing history, sync settings and credentials are not included in the package.
