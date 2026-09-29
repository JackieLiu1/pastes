# macOS installation packages

Build with Qt 6, its `macdeployqt` tool, Python 3.9 or later, and Apple's
Command Line Tools. The application and every bundled dependency must support
the requested architecture. This does not create Intel binaries from an
Apple Silicon Qt installation.

```sh
cmake -S . -B build/macos-package -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0
cmake --build build/macos-package --target package_macos --parallel
```

The target creates a `.pkg` installer, a drag-to-Applications `.dmg`, and SHA-256
checksums in `build/macos-package/dist`. It stages a separate `Pastes.app`,
bundles Qt frameworks and plugins, verifies runtime dependencies and
architectures, and signs the final application. The original build bundle
is unchanged. The minimum system version is derived from all shipped binaries;
the installer checks that version and the supported architecture.
Prebuilt Qt and other libraries can require a newer macOS version than
`CMAKE_OSX_DEPLOYMENT_TARGET`; that flag cannot lower their existing requirements.

Quit the running development or installed copy before installing. The `.pkg`
installs `/Applications/Pastes.app`; alternatively, drag the application from
the `.dmg` to Applications. Launch it there and use `Shift+Cmd+V`. Pastes runs
in the menu bar without a Dock icon. Existing history and settings remain in
the user's Library. After moving from a development bundle, turn launch at
sign-in off and on in Settings to register the installed copy. Automatic paste
requires Accessibility permission for that copy.

If automatic paste still requests permission while the Pastes switch is on,
quit Pastes, remove its old entry in System Settings > Privacy & Security >
Accessibility (Device Control and Data Access on newer macOS), then add
`/Applications/Pastes.app` and enable it again. Reopen that installed copy.
Restarting alone does not repair an authorization tied to another signature.

The default application identity `-` is an ad-hoc signature for local use.
The default installer is unsigned. These packages are not notarized and do
not claim Gatekeeper approval for public distribution.
Ad-hoc authorization is tied to the exact signed build. Rebuilding or switching
between development and installed copies can invalidate a previous grant;
using the same bundle identifier does not preserve it. Use the same Developer
ID Application identity across distributed updates for a stable signing
identity. Do not replace signature checks with an identifier-only requirement.

For distribution, configure `PASTES_MACOS_SIGNING_IDENTITY` with a Developer ID
Application identity and `PASTES_MACOS_INSTALLER_SIGNING_IDENTITY` with a
Developer ID Installer identity. Submit the resulting artifacts to Apple's
notarization service and staple successful tickets before publishing them.
Notarization credentials are not stored or managed by this target.
