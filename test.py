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
        try:
            arg = struct.pack('I', 0)
            result = fcntl.ioctl(self.ioat, _IOR(0xad, 0, 4), arg)
            result = struct.unpack('I', result)
            print("available devices: {}.".format(result[0]), end=' ', flush=True)
            self.assertGreater(result[0], 0)
        except OSError as err:
            self.fail("ioctl() returns an error: {}".format(err))
        
    def test_01_get_device(self):
        try:
            arg = struct.pack('I', 0)
            result = fcntl.ioctl(self.ioat, _IOR(0xad, 1, 4), arg)
            result = struct.unpack('I', result)
            print("device id: {}.".format(result[0]), end=' ', flush=True)
            self.assertGreaterEqual(result[0], 0)
        except OSError as err:
            self.fail("ioctl() returns an error: {}".format(err))

    def test_02_get_same_device_for_same_thread1(self):
        try:
            arg = struct.pack('I', 0)
            result = fcntl.ioctl(self.ioat, _IOR(0xad, 1, 4), arg)
            id1 = struct.unpack('I', result)[0]
            result = fcntl.ioctl(self.ioat, _IOR(0xad, 1, 4), arg)
            id2 = struct.unpack('I', result)[0]
            self.assertEqual(id1, id2)
        except OSError as err:
            self.fail("ioctl() returns an error: {}".format(err))

    def test_03_get_same_device_for_same_thread2(self):
        ioat2 = os.open("/dev/ioat-dma", os.O_RDWR)
        self.assertGreaterEqual(ioat2, 0)
        try:
            arg = struct.pack('I', 0)
            result = fcntl.ioctl(self.ioat, _IOR(0xad, 1, 4), arg)
            id1 = struct.unpack('I', result)[0]
            result = fcntl.ioctl(ioat2, _IOR(0xad, 1, 4), arg)
            id2 = struct.unpack('I', result)[0]
            self.assertEqual(id1, id2)
        except OSError as err:
            self.fail("ioctl() returns an error: {}".format(err))
        os.close(ioat2)

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
        
        try:
            arg = struct.pack('I', 0)
            fcntl.ioctl(self.ioat, _IOR(0xad, 1, 4), arg)
            arg = struct.pack('64sQQQ',
                            '/dev/dax0.0'.encode(),
                            src_offset * size,
                            dst_offset * size,
                            size)
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