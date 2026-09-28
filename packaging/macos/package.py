#!/usr/bin/env python3
"""Build a self-contained macOS application, installer and disk image."""

import argparse
import hashlib
import os
import plistlib
import re
import shutil
import subprocess
import tempfile
from pathlib import Path


def run(*command, capture=False):
    result = subprocess.run(command, check=True, text=True,
                            stdout=subprocess.PIPE if capture else None)
    return result.stdout if capture else None


def macho_files(app):
    magics = {b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe",
              b"\xfe\xed\xfa\xcf", b"\xcf\xfa\xed\xfe",
              b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca",
              b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca"}
    binaries = []
    for path in app.rglob("*"):
        if path.is_file() and not path.is_symlink():
            with path.open("rb") as stream:
                if stream.read(4) in magics:
                    binaries.append(path)
    return sorted(binaries)


def version_tuple(version):
    return tuple(int(part) for part in version.split("."))


def rpaths(commands):
    return re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.+?) \(offset",
                      commands)


def deploy_bundle(app, macdeployqt, qmake):
    libraries = run(qmake, "-query", "QT_INSTALL_LIBS", capture=True).strip()
    plugins = Path(run(qmake, "-query", "QT_INSTALL_PLUGINS", capture=True).strip())
    selected = [plugins / "platforms/libqcocoa.dylib",
                plugins / "sqldrivers/libqsqlite.dylib"]
    for category in ("imageformats", "iconengines", "styles"):
        selected += sorted((plugins / category).glob("*.dylib"))
    for relative in ("tls/libqsecuretransportbackend.dylib",
                     "tls/libqcertonlybackend.dylib",
                     "networkinformation/libqapplenetworkinformation.dylib"):
        if (plugins / relative).is_file():
            selected.append(plugins / relative)
    arguments = []
    for source in selected:
        destination = app / "Contents/PlugIns" / source.parent.name / source.name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)
        arguments.append("-executable=" + str(destination))
    # Avoid deploying unrelated virtual keyboards and offscreen platforms.
    run(macdeployqt, str(app), "-no-plugins", "-no-codesign", "-no-strip",
        "-verbose=1", "-libpath=" + libraries, *arguments)
    (app / "Contents/Resources/qt.conf").write_text(
        "[Paths]\nPlugins = PlugIns\n", encoding="utf-8")
    for binary in macho_files(app):
        commands = run("/usr/bin/otool", "-l", str(binary), capture=True)
        for path in rpaths(commands):
            expanded = Path(path.replace("@loader_path", str(binary.parent))
                           .replace("@executable_path", str(app / "Contents/MacOS")))
            if expanded.is_absolute() and not expanded.resolve().is_relative_to(app):
                run("/usr/bin/install_name_tool", "-delete_rpath", path, str(binary))


def verify_bundle(app):
    """Reject host dependencies and derive requirements from shipped code."""
    executable = app / "Contents/MacOS/pastes"
    architectures = run("/usr/bin/lipo", "-archs", str(executable),
                        capture=True).split()
    if not set(architectures) <= {"arm64", "x86_64"}:
        raise RuntimeError("Unsupported macOS architecture")
    executable_commands = run("/usr/bin/otool", "-l", str(executable),
                              capture=True)
    minimum = "0.0"
    binaries = macho_files(app)
    if not binaries:
        raise RuntimeError("Application contains no executable code")

    def expand(path, binary):
        return Path(path.replace("@loader_path", str(binary.parent))
                    .replace("@executable_path", str(executable.parent)))

    for binary in binaries:
        arches = run("/usr/bin/lipo", "-archs", str(binary), capture=True).split()
        if not set(architectures) <= set(arches):
            raise RuntimeError("Architecture mismatch: " + str(binary))
        commands = run("/usr/bin/otool", "-l", str(binary), capture=True)
        versions = re.findall(r"\bminos ([\d.]+)", commands)
        versions += re.findall(
            r"cmd LC_VERSION_MIN_MACOSX\s+cmdsize \d+\s+version ([\d.]+)",
            commands)
        for version in versions:
            if version_tuple(version) > version_tuple(minimum):
                minimum = version
        search_paths = [expand(path, binary) for path in rpaths(commands)]
        search_paths += [expand(path, executable)
                         for path in rpaths(executable_commands)]
        for path in search_paths:
            if path.is_absolute() and not path.resolve().is_relative_to(app):
                raise RuntimeError("External runtime search path: " + str(path))
        # LC_ID_DYLIB names the library itself; it is not a dependency.
        dependencies = re.findall(
            r"cmd LC_(?:LOAD|LOAD_WEAK|REEXPORT|LAZY_LOAD|LOAD_UPWARD)_DYLIB"
            r"\s+cmdsize \d+\s+name (.+?) \(offset", commands)
        for dependency in dependencies:
            if dependency.startswith(("/System/Library/", "/usr/lib/")):
                continue
            if dependency.startswith("@rpath/"):
                suffix = dependency[len("@rpath/"):]
                candidates = [path / suffix for path in search_paths]
            else:
                candidates = [expand(dependency, binary)]
            if not any(path.exists() and path.resolve().is_relative_to(app)
                       for path in candidates):
                raise RuntimeError("Unbundled dependency in {}: {}".format(
                    binary.relative_to(app), dependency))
    for relative in ("Contents/PlugIns/platforms/libqcocoa.dylib",
                     "Contents/PlugIns/sqldrivers/libqsqlite.dylib",
                     "Contents/Resources/Pastes_zh_CN.qm",
                     "Contents/Resources/qt.conf",
                     "Contents/Resources/pastes.icns"):
        if not (app / relative).is_file():
            raise RuntimeError("Missing application resource: " + relative)
    if minimum == "0.0":
        raise RuntimeError("Unable to determine minimum macOS version")
    return architectures, minimum, binaries


def sign_bundle(app, binaries, identity):
    options = ["--options", "runtime", "--timestamp"] if identity != "-" else []
    for binary in binaries:
        run("/usr/bin/codesign", "--force", "--sign", identity,
            *options, str(binary))
    frameworks = sorted(app.rglob("*.framework"),
                        key=lambda path: len(path.parts), reverse=True)
    for framework in frameworks:
        run("/usr/bin/codesign", "--force", "--sign", identity,
            *options, str(framework))
    run("/usr/bin/codesign", "--force", "--sign", identity, *options, str(app))
    run("/usr/bin/codesign", "--verify", "--deep", "--strict", str(app))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--macdeployqt", required=True)
    parser.add_argument("--qmake", required=True)
    parser.add_argument("--identity", default="-")
    parser.add_argument("--installer-identity", default="")
    args = parser.parse_args()
    # macdeployqt invokes Darwin tools by name; GNU strip corrupts Mach-O files.
    os.environ["PATH"] = "/usr/bin:/bin:/usr/sbin:/sbin:" + os.environ.get("PATH", "")
    source = args.app.resolve()
    if not (source / "Contents/Info.plist").is_file():
        raise RuntimeError("Expected a built macOS application bundle")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    repository = Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix=".macos-package-", dir=output) as temp:
        stage = Path(temp)
        root = stage / "root"
        app = root / "Applications/Pastes.app"
        app.parent.mkdir(parents=True)
        run("/usr/bin/ditto", str(source), str(app))
        # Re-sign only after dependencies, metadata and resources are final.
        print("Deploying Qt frameworks and plugins...", flush=True)
        deploy_bundle(app, args.macdeployqt, args.qmake)
        resources = app / "Contents/Resources"
        shutil.copy2(repository / "LICENSE", resources / "LICENSE.txt")
        shutil.copy2(repository / "3rd/SingleApplication/LICENSE",
                     resources / "SingleApplication-LICENSE.txt")
        (resources / "ThirdPartyNotices.txt").write_text(
            "Pastes: https://github.com/JackieLiu1/pastes (LGPL-3.0)\n"
            "Qt is dynamically linked. Licenses and corresponding source:\n"
            "https://doc.qt.io/qt-6/licensing.html\n"
            "https://download.qt.io/archive/qt/\n"
            "Qt third-party notices: https://doc.qt.io/qt-6/licenses-used-in-qt.html\n",
            encoding="utf-8")
        arches, minimum, binaries = verify_bundle(app)
        print("Verified {} binaries for macOS {}+ ({})".format(
            len(binaries), minimum, ", ".join(arches)), flush=True)
        info_path = app / "Contents/Info.plist"
        with info_path.open("rb") as stream:
            info = plistlib.load(stream)
        info["LSMinimumSystemVersion"] = minimum
        # CMake's generated Info.plist can be read-only in the build bundle.
        info_path.chmod(0o644)
        with info_path.open("wb") as stream:
            plistlib.dump(info, stream)
        print("Signing the final application...", flush=True)
        sign_bundle(app, binaries, args.identity)
        architecture = "universal" if len(arches) > 1 else arches[0]
        name = "Pastes-{}-macos-{}".format(args.version, architecture)

        components = stage / "components.plist"
        run("/usr/bin/pkgbuild", "--analyze", "--root", str(root), str(components))
        with components.open("rb") as stream:
            bundles = plistlib.load(stream)
        for bundle in bundles:
            bundle["BundleIsRelocatable"] = False
            bundle["BundleHasStrictIdentifier"] = True
            bundle["BundleOverwriteAction"] = "upgrade"
        with components.open("wb") as stream:
            plistlib.dump(bundles, stream)
        component = stage / "Pastes-component.pkg"
        run("/usr/bin/pkgbuild", "--root", str(root), "--install-location", "/",
            "--component-plist", str(components), "--identifier",
            "io.github.JackieLiu1.pastes.pkg", "--version", args.version,
            str(component))
        requirements = stage / "requirements.plist"
        with requirements.open("wb") as stream:
            plistlib.dump({"arch": arches, "os": [minimum]}, stream)
        distribution = stage / "Distribution.xml"
        run("/usr/bin/productbuild", "--synthesize", "--product", str(requirements),
            "--package", str(component), str(distribution))
        package = output / (name + ".pkg")
        installer_sign = (["--sign", args.installer_identity]
                          if args.installer_identity else [])
        run("/usr/bin/productbuild", "--distribution", str(distribution),
            "--package-path", str(stage), *installer_sign, str(package))

        image_root = stage / "image"
        image_root.mkdir()
        run("/usr/bin/ditto", str(app), str(image_root / "Pastes.app"))
        (image_root / "Applications").symlink_to("/Applications")
        instructions = (
            "Pastes {} — macOS {}+ ({})\n\n"
            "先退出正在运行的 Pastes，将 Pastes.app 拖入 Applications。\n"
            "从应用程序打开 Pastes，使用 Shift+Cmd+V 唤出剪贴板历史。\n"
            "程序运行在菜单栏，不显示 Dock 图标。\n"
            "在设置中开启开机启动；自动粘贴需要系统辅助功能权限。\n"
            "更新不会删除已有历史记录或设置。\n\n"
            "Quit Pastes before copying it to Applications, then launch it there.\n"
            "Open clipboard history with Shift+Cmd+V. Enable launch at sign-in\n"
            "in Settings. Automatic paste needs Accessibility permission.\n"
        ).format(args.version, minimum, architecture)
        if args.identity == "-":
            instructions += "\n本地签名版本，未经 Apple 公证。\n"
        (image_root / "安装说明.txt").write_text(instructions, encoding="utf-8")
        image = output / (name + ".dmg")
        run("/usr/bin/hdiutil", "create", "-ov", "-srcfolder", str(image_root),
            "-volname", "Pastes", "-format", "UDZO", "-fs", "HFS+", str(image))
        run("/usr/bin/hdiutil", "verify", str(image))
        checksums = []
        for artifact in (package, image):
            digest = hashlib.sha256()
            with artifact.open("rb") as stream:
                for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                    digest.update(chunk)
            checksums.append(digest.hexdigest() + "  " + artifact.name)
        (output / (name + "-SHA256.txt")).write_text(
            "\n".join(checksums) + "\n", encoding="utf-8")
        print("Packaged macOS {}+ ({})".format(minimum, architecture))
        print(package)
        print(image)


if __name__ == "__main__":
    main()
