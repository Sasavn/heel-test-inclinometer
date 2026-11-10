import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location('package', Path(__file__).parents[1] / 'tools/package.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class PackageTests(unittest.TestCase):
    def image(self):
        data = bytearray(288)
        data[0] = 0xE9
        struct.pack_into('<H', data, 12, 18)
        struct.pack_into('<I', data, 32, 0xABCD5432)
        data[48:61] = b'1.5.0-esp32\0\0\0'
        data[80:95] = b'krenomer-esp32\0\0'
        return data

    def test_p4_app(self):
        self.assertEqual(package.inspect_app(self.image()), '1.5.0-esp32')

    def test_other_architecture(self):
        for chip in (0, 9, 13):
            image = self.image()
            struct.pack_into('<H', image, 12, chip)
            with self.assertRaises(ValueError):
                package.inspect_app(image)

    def test_wrong_product(self):
        image = self.image()
        image[80] = ord('X')
        with self.assertRaises(ValueError):
            package.inspect_app(image)

    def test_not_app(self):
        for image in (b'', bytes(300), self.image()[:100]):
            with self.assertRaises(ValueError):
                package.inspect_app(image)


if __name__ == '__main__':
    unittest.main()
