import unittest
import os

class TestIoatDma(unittest.TestCase):
    def setUp(self):
        self.fd = os.open("/dev/ioat-dma", os.O_RDWR)

    def tearDown(self):
        os.close(self.fd)

    def test_open(self):
        self.assertGreater(self.fd, 0)

if __name__ == '__main__':
    # open dax device to prevent kernel NULLptr dereference
    fd = os.open("/dev/dax0.0", os.O_RDWR)
    os.close(fd)

    unittest.main()