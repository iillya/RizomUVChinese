"""Build-time validation must reject damaged dictionaries before packaging."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
import hashlib
import json
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('package_under_test', ROOT / 'source/inno/package.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class DictionaryPackageTest(unittest.TestCase):
    def test_official_dictionary(self):
        package.validate_dictionary(ROOT / 'translations/dictionary_zh.json')
        entries = package.payload_entries()
        self.assertEqual(len(entries), len({name.casefold() for name, _, _ in entries}))

    def test_rejects_invalid_dictionary(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'dictionary.json'
            for data in (
                b'{"translations":{"A":"one","A":"two"}}',
                b'{"translations":{"A":"valid"}} trailing',
                b'{"translations":{"A":"\\ud800"}}',
                b'{"translations":{"A":"\\u0000bad"}}',
                b'{"translations":{"A":7}}',
                b'{"translations":{}}',
                b'{"translations":{"A":"\\uXXXX"}}',
                b'{"translations":{"A":"ok"},"meta":NaN}',
                b'{"translations":{"A":"ok"},"meta":"\\ud800"}',
                b'{"translations":{"A":"ok"},"\\ud800":true}',
                b'\xff',
            ):
                with self.subTest(data=data):
                    path.write_bytes(data)
                    with self.assertRaises((ValueError, UnicodeError)):
                        package.validate_dictionary(path)

    def test_metadata_depth_matches_runtime(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'dictionary.json'
            for depth in (64, 65):
                value = 'leaf'
                for _ in range(depth):
                    value = [value]
                path.write_text(json.dumps({'translations': {'A': 'ok'}, 'meta': value}), encoding='utf-8')
                if depth == 64:
                    package.validate_dictionary(path)
                else:
                    with self.assertRaises(ValueError):
                        package.validate_dictionary(path)

    def test_payload_snapshot_and_permission_failure_preserve_release(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'runtime.dll'
            source.write_bytes(b'original payload')
            digest = hashlib.sha256(source.read_bytes()).hexdigest()
            snapshot = package.snapshot_payload([('runtime.dll', source, digest)], root / 'stage')
            source.write_bytes(b'changed payload')
            self.assertEqual(snapshot[0][1].read_bytes(), b'original payload')
            with self.assertRaises(ValueError):
                package.snapshot_payload([('runtime.dll', source, digest)], root / 'failed-stage')
            destination = root / 'installer.exe'
            destination.write_bytes(b'previous installer')
            with patch.object(package.subprocess, 'run', side_effect=RuntimeError('permission reset failed')):
                with self.assertRaises(RuntimeError):
                    package.publish_installer(source, destination)
            self.assertEqual(destination.read_bytes(), b'previous installer')
            self.assertFalse(list(root.glob('.*.tmp')))


if __name__ == '__main__':
    unittest.main()
