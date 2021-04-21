/**
 * @file ioat-dma.c
 * @author Insu Jang (insujang@casys.kaist.ac.kr)
 * @brief Kernel module for DMA operations with an I/OAT DMA engine.
 * @version 0.1
 * @date 2021-04-15
 * 
 * Reference articles
 * https://linux-kernel-labs.github.io/refs/heads/master/labs/device_drivers.html
 * https://forums.xilinx.com/xlnx/attachments/xlnx/ELINUX/31239/1/axidma.c
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/dax.h>
#include <linux/dmaengine.h>
#include "dax-private.h"


extern struct dax_device *dax_get_device(const char *devpath);

/* Device driver stuffs */
static dev_t dev;
static struct cdev cdev;
static struct class *pClass;
static struct device *pDev;
#define DEVICE_NAME "ioat-dma"
#define MAX_MINORS 5

#define IOCTL_MAGIC 0xad

struct ioctl_dma_args {
  char device_name[64];
  u64 src_offset;
  u64 dst_offset;
  u64 size;
  int result;
} __attribute__ ((packed));

#define IOCTL_IOAT_DMA_SUBMIT _IOWR(IOCTL_MAGIC, 0, struct ioctl_dma_args)

/* DMA stuffs */
static struct dma_chan *pChannel;
static struct completion cmp;

static int ioat_dma_open(struct inode *, struct file *);
static int ioat_dma_release(struct inode *, struct file *);
static long ioat_dma_ioctl(struct file *, unsigned int, unsigned long);

static struct file_operations ioat_dma_fops = {
  .open = ioat_dma_open,
  .release = ioat_dma_release,
  .unlocked_ioctl = ioat_dma_ioctl,
};


static int ioat_dma_open(struct inode *inode, struct file *file) {
  // printk(KERN_INFO "%s\n", __func__);

  struct dax_device *dax_device = dax_get_device("/dev/dax0.0");
  struct dev_dax *dev_dax = dax_get_private(dax_device);
  struct resource *res = &dev_dax->region->res;
  printk(KERN_INFO "%s: range: 0x%llu ~ 0x%llu\n", __func__, res->start, res->end);
  return 0;
}

static int ioat_dma_release(struct inode *inode, struct file *file) {
  printk(KERN_INFO "%s\n", __func__);
  return 0;
}

static long ioat_dma_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
  printk(KERN_INFO "%s\n", __func__);

  switch (cmd) {
    case IOCTL_IOAT_DMA_SUBMIT: {
      struct ioctl_dma_args args;
      if (copy_from_user(&args, (void __user *) arg, sizeof(args))) {
        return -EFAULT;
      }
      printk(KERN_INFO "%s: dev name: %s, src offset: 0x%llx, dst offset: 0x%llx, size: 0x%llx\n",
                __func__, args.device_name, args.src_offset, args.dst_offset, args.size);
      args.result = 0;
      if (copy_to_user((void __user *) arg, &args, sizeof(args))) {
        return -EFAULT;
      }
      break;
    }
    default:
      printk(KERN_WARNING "%s: unsupported command %x\n", __func__, cmd);
      return -EFAULT;
  }

  return 0;
}

/**
 * https://topic.alibabacloud.com/a/the-register_chrdev-and-register_chrdev_region-of-character-devices_8_8_31256510.html
 * Difference between register_chrdev and register_chrdev_region
 */
static int __init ioat_dma_init(void) {
  int ret;
  dma_cap_mask_t mask;

  printk(KERN_INFO "%s\n", __func__);

  // Register character device
  ret = alloc_chrdev_region(&dev, 0, 1, DEVICE_NAME);
  if (ret < 0) {
    printk (KERN_ALERT "%s: alloc_chrdev_region failed with %d.\n", __func__, ret);
    return ret;
  }

  ret = register_chrdev_region(dev, 0, DEVICE_NAME);
  if (ret < 0) {
    printk (KERN_ALERT "%s: register_chrdev_region failed with %d.\n", __func__, ret);
    return ret;
  }

  cdev_init(&cdev, &ioat_dma_fops);
  cdev_add(&cdev, dev, 1);

  // Create /sys/class/ioat-dma in preparation of creating /dev/ioat-dma
  pClass = class_create(THIS_MODULE, DEVICE_NAME);
  if (IS_ERR(pClass)) {
    printk (KERN_ALERT "%s: class_create failed.\n", __func__);
    goto unregister;
  }

  // Create /dev/ioat-dma
  pDev = device_create(pClass, NULL, dev, NULL, DEVICE_NAME);
  if (IS_ERR(pDev)) {
    printk(KERN_ALERT "%s: device_create failed.\n", __func__);
    goto class_destroy;
  }

  // Create a channel
  dma_cap_zero(mask);
  dma_cap_set(DMA_MEMCPY, mask);
  pChannel = dma_request_chan_by_mask(&mask);
  if (IS_ERR(pChannel)) {
    printk(KERN_ALERT "%s: DMA channel request failed.\n", __func__);
    goto device_destroy;
  }

  return 0;

device_destroy:
  device_destroy(pClass, dev);
class_destroy:
  class_destroy(pClass);
unregister:
  unregister_chrdev_region(dev, 1);
  return -1;
}

static void __exit ioat_dma_exit(void) {
  printk(KERN_INFO "%s\n", __func__);
  dma_release_channel(pChannel);
  device_destroy(pClass, dev);
  class_destroy(pClass);
  cdev_del(&cdev);
  unregister_chrdev_region(dev, 1);
}

MODULE_AUTHOR("Insu Jang");
MODULE_LICENSE("GPL v2");
module_init(ioat_dma_init);
module_exit(ioat_dma_exit);