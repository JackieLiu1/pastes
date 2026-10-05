# Linux installation packages

On Debian or Ubuntu, install the build and packaging dependencies:

```sh
sudo apt-get install build-essential cmake qt6-base-dev qt6-tools-dev \
    qt6-tools-dev-tools libglib2.0-dev libxtst-dev libx11-dev pkg-config dpkg-dev
```

Build and package with the same target used on Windows and macOS:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --build build --target package
```

CPack creates `build/dist/pastes_<version>_<architecture>.deb` and a SHA-256
checksum. The package includes the executable, Chinese translation, application
icon, license and existing UKUI autostart entry. Shared-library dependencies are derived
from the built executable by `dpkg-shlibdeps`; SQLite and SVG plugins and the
Secret Service credential helper are declared separately because they are
loaded at runtime. Qt libraries are supplied by the distribution.

Install the package with `sudo apt install ./build/dist/pastes_*.deb` so its
dependencies are installed too. Build on the oldest distribution you intend
to support; packaging on a newer system does not establish compatibility with
older Qt or system libraries. Packages are built for the current architecture.

New Linux build directories default to the `/usr` installation prefix. When
reusing a build directory configured with another prefix, set
`-DCMAKE_INSTALL_PREFIX=/usr` explicitly for a distribution package. Existing
history, sync settings and credentials are not included in the package.
