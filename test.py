import unittest
import struct
import os
import fcntl
from ioctl_numbers import _IOW

class TestIoatDma(unittest.TestCase):
    def setUp(self):
        self.dax = os.open("/dev/dax0.0", os.O_RDWR)
        self.assertGreater(self.dax, 0)
        self.ioat = os.open("/dev/ioat-dma", os.O_RDWR)
        self.assertGreater(self.ioat, 0)

    def tearDown(self):
        os.close(self.dax)
        os.close(self.ioat)

    def test_nothing(self):
        self.assertEqual(0, 0)

    # @unittest.skip
    def test_ioat_dma(self):
        # 64sQQQ format (https://docs.python.org/3/library/struct.html#format-characters)
        # 64 bytes char[], 3 consecutive unsigned long longs, int
        # Equivalent to:
        # struct {
        # char device_name[64];
        # u64 src_offset;
        # u64 dst_offset;
        # u64 size;
        # }
        arg = struct.pack('64sQQQ',
                            '/dev/dax0.0'.encode(),
                            0x0,
                            0x1000,
                            0x500)
        try:
            fcntl.ioctl(self.ioat, _IOW(0xad, 0, 88), arg)
        except OSError as err:
            self.fail("ioctl() returns an error: {}".format(err))
        


if __name__ == '__main__':
    unittest.main()