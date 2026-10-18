import importlib.util
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('export_ota', ROOT / 'tools/export_ota.py')
export = importlib.util.module_from_spec(spec)
spec.loader.exec_module(export)


def image():
    data = bytearray(4096)
    data[0] = 0xE9
    data[23] = 1
    struct.pack_into('<H', data, 12, 9)
    struct.pack_into('<I', data, 32, 0xABCD5432)
    data[48:59] = b'1.1.0-ota\0\0'
    data[80:95] = b'ComputerScreen\0'
    return data


class ReleaseTests(unittest.TestCase):
    def test_export_metadata(self):
        info = export.describe(image())
        self.assertEqual(info['version'], '1.1.0-ota')
        self.assertEqual(info['size'], 4096)
        self.assertEqual(len(info['sha256']), 64)

    def test_reject_other_chip_old_release_and_corrupt_header(self):
        for offset, value in [(0, 0), (12, 2), (23, 0), (32, 0), (48, 0), (80, 0)]:
            data = image()
            data[offset] = value
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                export.describe(data)

    def test_partition_layout(self):
        rows = {}
        end = 0x9000
        for line in (ROOT / 'partitions.csv').read_text().splitlines():
            if not line or line.startswith('#'):
                continue
            name, kind, subtype, offset, size, *_ = line.split(',')
            offset, size = int(offset, 0), int(size, 0)
            self.assertGreaterEqual(offset, end)
            if kind == 'app':
                self.assertEqual(offset % 0x10000, 0)
            rows[name] = (offset, size)
            end = offset + size
        self.assertEqual(end, 0x1000000)
        self.assertEqual(rows['nvs'], (0x9000, 0x6000))
        self.assertEqual(rows['factory'], (0x10000, 0x300000))
        self.assertEqual(rows['otadata'][1], 0x2000)
        self.assertEqual(rows['ota_0'][1], 0x400000)
        self.assertEqual(rows['ota_1'][1], 0x400000)
        self.assertIn('CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y', (ROOT / 'sdkconfig.defaults').read_text())


if __name__ == '__main__':
    unittest.main()
