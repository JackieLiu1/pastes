"""Keep persistent local signing separate from Developer ID distribution."""

import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

source = Path(__file__).resolve().parents[1] / "packaging/macos/package.py"
spec = importlib.util.spec_from_file_location("macos_package", source)
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class SigningModes(unittest.TestCase):
    def check_mode(self, identity, local, hardened):
        with tempfile.TemporaryDirectory() as directory:
            app = Path(directory) / "Pastes.app"
            framework = app / "Contents/Frameworks/Example.framework"
            framework.mkdir(parents=True)
            binary = framework / "Example"
            with patch.object(package, "run") as run:
                package.sign_bundle(app, [binary], identity, local)
            calls = [call.args for call in run.call_args_list]
            self.assertEqual(len(calls), 4)
            for command, target in zip(calls[:3], [binary, framework, app]):
                self.assertEqual(command[:4], ("/usr/bin/codesign", "--force", "--sign", identity))
                self.assertEqual(command[-1], str(target))
                self.assertEqual("--timestamp" in command, hardened)
                self.assertEqual("runtime" in command, hardened)
            self.assertEqual(calls[-1],
                             ("/usr/bin/codesign", "--verify", "--deep", "--strict", str(app)))

    def test_local_certificate(self):
        self.check_mode("LOCAL_CERTIFICATE_FINGERPRINT", True, False)

    def test_developer_id(self):
        self.check_mode("Developer ID Application: Example", False, True)

    def test_adhoc(self):
        self.check_mode("-", False, False)


if __name__ == "__main__":
    unittest.main()
