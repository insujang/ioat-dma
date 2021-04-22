import unittest
import struct
import os
import fcntl
from ioctl_numbers import _IOWR

class TestIoatDma(unittest.TestCase):
    def setUp(self):
        self.fd = os.open("/dev/ioat-dma", os.O_RDWR)
        self.assertGreater(self.fd, 0)

    def tearDown(self):
        os.close(self.fd)

    def test_nothing(self):
        self.assertEqual(0, 0)

    # @unittest.skip
    def test_ioctl(self):
        # 64sQQQi format (https://docs.python.org/3/library/struct.html#format-characters)
        # 64 bytes char[], 3 consecutive unsigned long longs, int
        # Equivalent to:
        # struct {
        # char device_name[64];
        # u64 src_offset;
        # u64 dst_offset;
        # u64 size;
        # int result;
        # }
        arg = struct.pack('64sQQQi',
                            '/dev/dax0.0'.encode(),
                            0x0,
                            0x1000,
                            0x500,
                            -1)
        result = fcntl.ioctl(self.fd, _IOWR(0xad, 0, 92), arg)
        result = struct.unpack('64sQQQi', result)
        self.assertEqual(result[4], 0)


if __name__ == '__main__':
    # open dax device to prevent kernel NULLptr dereference
    fd = os.open("/dev/dax0.0", os.O_RDWR)
    os.close(fd)

    unittest.main()