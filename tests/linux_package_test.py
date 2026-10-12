"""Check an extracted bundled Linux package without installing it."""

import argparse
import os
from pathlib import Path
import re
import stat
import subprocess


def check_permissions(root):
    executable = root / "opt/pastes/pastes"
    for prefix in ("etc", "opt", "usr"):
        directory = root / prefix
        for path in [directory, *directory.rglob("*")]:
            if path.is_symlink():
                continue
            expected = 0o755 if path.is_dir() or path == executable else 0o644
            actual = stat.S_IMODE(path.stat().st_mode)
            if actual != expected:
                raise RuntimeError("Incorrect package permissions: "
                                   + str(path.relative_to(root))
                                   + ": " + oct(actual)
                                   + ", expected " + oct(expected))


def check_package(root):
    if os.path.lexists(root / "usr/bin/pastes"):
        raise RuntimeError("Unexpected /usr/bin/pastes launcher in the package")
    check_permissions(root)
    runtime = (root / "opt/pastes").resolve()
    runtimes = [path for path in root.rglob("pastes")
                if path.is_dir() and (path / "pastes").is_file()]
    if [path.resolve() for path in runtimes] != [runtime]:
        raise RuntimeError("Expected the sole Pastes runtime in /opt/pastes")
    executable = runtime / "pastes"
    for desktop in ("usr/share/applications/pastes.desktop",
                    "etc/xdg/autostart/pastes.desktop"):
        if "Exec=/opt/pastes/pastes\n" not in (root / desktop).read_text():
            raise RuntimeError("The desktop entry does not target /opt/pastes")
    if not (runtime / "translations/Pastes_zh_CN.qm").is_file():
        raise RuntimeError("The /opt/pastes translation is missing")
    if not (runtime / "LICENSE.txt").is_file():
        raise RuntimeError("The /opt/pastes license is missing")
    for pattern in ("libQt6*.so*", "libicu*.so*"):
        for path in root.rglob(pattern):
            if runtime not in path.resolve().parents:
                raise RuntimeError("Bundled library outside /opt/pastes: " + str(path))
    if not (runtime / "qt.conf").is_file():
        raise RuntimeError("The private Qt plugin configuration is missing")
    for module in ("Core", "Gui", "Widgets", "Sql", "Network"):
        library = runtime / "lib" / ("libQt6" + module + ".so.6")
        if not library.is_file():
            raise RuntimeError("Missing Qt runtime SONAME: " + str(library))
    for plugin in ("platforms/libqxcb.so", "sqldrivers/libqsqlite.so",
                   "imageformats/libqsvg.so", "iconengines/libqsvgicon.so"):
        if not (runtime / "plugins" / plugin).is_file():
            raise RuntimeError("Missing required Qt plugin: " + plugin)

    files = [executable]
    files.extend(path for path in (runtime / "lib").glob("*.so*")
                 if not path.is_symlink())
    files.extend((runtime / "plugins").rglob("*.so"))
    environment = os.environ.copy()
    environment.pop("LD_LIBRARY_PATH", None)
    private_library = re.compile(r"\b(lib(?:Qt6|icu)\S*) => (.+?) \(")
    for path in files:
        result = subprocess.run(["ldd", str(path)], env=environment,
                                text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
        if result.returncode or "not found" in result.stdout:
            raise RuntimeError(str(path) + ": " + result.stdout)
        for name, location in private_library.findall(result.stdout):
            if runtime not in Path(location).resolve().parents:
                raise RuntimeError(name + " resolves outside the package: "
                                   + location)
    print("PASS public permissions, /opt/pastes layout, entry points "
          "and private Qt/ICU dependencies")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    check_package(parser.parse_args().root.resolve())
