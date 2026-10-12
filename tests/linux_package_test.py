"""Check an extracted bundled Linux package without installing it."""

import argparse
import os
from pathlib import Path
import re
import subprocess


def check_package(root):
    runtimes = [path for path in root.rglob("pastes")
                if path.is_dir() and (path / "pastes").is_file()]
    if len(runtimes) != 1:
        raise RuntimeError("Expected one private Pastes runtime directory")
    runtime = runtimes[0].resolve()
    executable = runtime / "pastes"
    launcher = root / "usr/bin/pastes"
    if not launcher.is_file() or not os.access(launcher, os.X_OK):
        raise RuntimeError("The /usr/bin/pastes launcher is missing")
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
    print("PASS bundled Qt/ICU SONAMEs, plugin dependencies and private paths")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    check_package(parser.parse_args().root.resolve())
