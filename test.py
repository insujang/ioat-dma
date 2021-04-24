import unittest
import struct
import os
import fcntl
import mmap
from ioctl_numbers import _IO, _IOR, _IOW, _IOWR

src_offset = 0x0
dst_offset = 0x10
size = 0x200000
data = os.urandom(size)    

class TestIoatDma(unittest.TestCase):
    def setUp(self):
        self.dax = os.open("/dev/dax0.0", os.O_RDWR)
        self.assertGreater(self.dax, 0)
        self.ioat = os.open("/dev/ioat-dma", os.O_RDWR)
        self.assertGreater(self.ioat, 0)

    def tearDown(self):
        os.close(self.dax)
        os.close(self.ioat)

    def test_00_get_device_num(self):
        arg = struct.pack('I', 0)
        try:
            result = fcntl.ioctl(self.ioat, _IOR(0xad, 0, 4), arg)
        except OSError as err:
            self.fail("ioctl() returns an error: {}".format(err))
        self.assertGreater(result[0], 0)
        
    def test_01_get_device(self):
        try:
            fcntl.ioctl(self.ioat, _IO(0xad, 0))
        except OSError as err:
            self.fail("ioctl() returns an error: {}".format(err))

    # @unittest.skip
    def test_11_dax_src_init(self):
        mm = mmap.mmap(self.dax, size, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE,
                        mmap.ACCESS_DEFAULT, src_offset * size)
        mm.write(data)
        mm.seek(0)
        written = mm.read(size)
        mm.close()
        self.assertEqual(data, written)

    # @unittest.skip
    def test_12_ioat_dma(self):
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
                            src_offset * size,
                            dst_offset * size,
                            size)
        try:
            fcntl.ioctl(self.ioat, _IO(0xad, 0))
            fcntl.ioctl(self.ioat, _IOW(0xad, 0, 88), arg)
        except OSError as err:
            self.fail("ioctl() returns an error: {}".format(err))

    # @unittest.skip
    def test_13_dax_dst_validate(self):
        mm = mmap.mmap(self.dax, size, mmap.MAP_SHARED, mmap.PROT_READ,
                        mmap.ACCESS_DEFAULT, dst_offset * size)
        dmaed = mm.read(size)
        mm.close()
        self.assertEqual(data, dmaed)

# Reference article: https://stackoverflow.com/a/16364514
if __name__ == '__main__':
    print("Randomly generated data: {}... ({} bytes)".format(data[:16], len(data)))
    unittest.main(failfast=True, exit=False)