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
#include <linux/dma-mapping.h>
#include <linux/path.h>
#include <linux/namei.h>
#include <linux/list.h>
#include "dax-private.h"

/* https://stackoverflow.com/a/27901009 */
struct dax_device *dax_get_device(const char *devpath) {
  struct inode *inode;
	struct path path;
	struct dax_device *dax_dev;
	struct dev_dax *dev_dax;

	int rc = kern_path(devpath, LOOKUP_FOLLOW, &path);
	if (rc) {
		return NULL;
	}
	inode = path.dentry->d_inode;
	dax_dev = inode_dax(inode);
	dev_dax = dax_get_private(dax_dev);
	if (dax_dev && dev_dax) {
		return dax_dev;
	}

	return NULL;
}

/* Device driver stuffs */
static dev_t dev;
static struct cdev cdev;
static struct class *pClass;
static struct device *pDev;
#define DEVICE_NAME "ioat-dma"
#define MAX_MINORS 5

/* ioctl stuffs */
#define IOCTL_MAGIC 0xad

struct ioctl_dma_args {
  char device_name[64];
  u64 src_offset;
  u64 dst_offset;
  u64 size;
} __attribute__ ((packed));

#define IOCTL_IOAT_DMA_SUBMIT _IOW(IOCTL_MAGIC, 0, struct ioctl_dma_args)

/* DMA stuffs */
#define DMA_QUEUE_DEPTH 32
struct ioat_dma_device {
  struct list_head list;
  struct dma_chan *chan;
  struct completion comp[DMA_QUEUE_DEPTH];
};

static LIST_HEAD(dma_device_list);

static int ioat_dma_open(struct inode *, struct file *);
static int ioat_dma_release(struct inode *, struct file *);
static long ioat_dma_ioctl(struct file *, unsigned int, unsigned long);

static struct file_operations ioat_dma_fops = {
  .open = ioat_dma_open,
  .release = ioat_dma_release,
  .unlocked_ioctl = ioat_dma_ioctl,
};


static int ioat_dma_open(struct inode *inode, struct file *file) {
  printk(KERN_INFO "%s\n", __func__);

  return 0;
}

static int ioat_dma_release(struct inode *inode, struct file *file) {
  printk(KERN_INFO "%s\n", __func__);
  return 0;
}

static void dma_sync_callback(void *completion) {
  complete(completion);
  printk(KERN_INFO "%s: callback completes DMA completion.\n", __func__);
}

static int ioat_dma_ioctl_dma_submit(struct ioctl_dma_args *args, struct dev_dax *dev_dax) {
  struct resource *res = &dev_dax->region->res;
  struct page *page;
  dma_addr_t src, dest;

  struct dma_async_tx_descriptor *chan_desc;
  enum dma_ctrl_flags flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;
  dma_cookie_t cookie;

  unsigned long timeout;
  enum dma_status status;

  int result = -EINVAL;

  page = pfn_to_page(res->start >> PAGE_SHIFT);
  if (IS_ERR(page)) {
    return PTR_ERR(page);
  }

  src = dma_map_page(pChannel->device->dev, page, args->src_offset, args->size, DMA_BIDIRECTIONAL);
  dest = dma_map_page(pChannel->device->dev, page, args->dst_offset, args->size, DMA_BIDIRECTIONAL);

  printk("%s: DMA about to initiate: 0x%llx -> 0x%llx (size: 0x%llx bytes)\n",
          __func__, src, dest, args->size);

  chan_desc = dmaengine_prep_dma_memcpy(pChannel, dest, src, args->size, flags);
  if (chan_desc == NULL) {
    result = -EINVAL;
    goto unmap;
  }

  chan_desc->callback = dma_sync_callback;
  chan_desc->callback_param = &cmp;
  cookie = dmaengine_submit(chan_desc);
  
  init_completion(&cmp);
  dma_async_issue_pending(pChannel);

  timeout = wait_for_completion_timeout(&cmp, msecs_to_jiffies(5000));
  status = dma_async_is_tx_complete(pChannel, cookie, NULL, NULL);
  printk(KERN_INFO "%s: wait completed.\n", __func__);

  if (timeout == 0) {
    printk(KERN_WARNING "%s: DMA timed out.\n", __func__);
    result = -ETIMEDOUT;
  } else if (status != DMA_COMPLETE) {
    printk(KERN_ERR "%s: DMA returned completion callback status of: %s\n",
      __func__, status == DMA_ERROR ? "error" : "in progress");
    result = -EBUSY;
  } else {
    printk(KERN_INFO "%s: DMA completed!\n", __func__);
    result = 0;
  }

unmap:
  dma_unmap_page(pChannel->device->dev, src, args->size, DMA_BIDIRECTIONAL);
  dma_unmap_page(pChannel->device->dev, dest, args->size, DMA_BIDIRECTIONAL);

  return result;
}

static long ioat_dma_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
  printk(KERN_INFO "%s\n", __func__);

  switch (cmd) {
    case IOCTL_IOAT_DMA_SUBMIT: {
      struct ioctl_dma_args args;
      struct dax_device *dax_device;
      struct dev_dax *dev_dax;

      if (copy_from_user(&args, (void __user *) arg, sizeof(args))) {
        return -EFAULT;
      }
      printk(KERN_INFO "%s: dev name: %s, src offset: 0x%llx, dst offset: 0x%llx, size: 0x%llx\n",
                __func__, args.device_name, args.src_offset, args.dst_offset, args.size);

      dax_device = dax_get_device(args.device_name);
      if (dax_device == NULL) {
        return -ENODEV;
      }

      dev_dax = (struct dev_dax *)dax_get_private(dax_device);
      return ioat_dma_ioctl_dma_submit(&args, dev_dax);
    }
    default:
      printk(KERN_WARNING "%s: unsupported command %x\n", __func__, cmd);
      return -EFAULT;
  }

  return -EINVAL;
}

/**
 * https://topic.alibabacloud.com/a/the-register_chrdev-and-register_chrdev_region-of-character-devices_8_8_31256510.html
 * Difference between register_chrdev and register_chrdev_region
 */
static int create_chardev(void) {
  int ret = 0;
  ret = alloc_chrdev_region(&dev, 0, 1, DEVICE_NAME);
  if (ret < 0) {
    printk (KERN_ALERT "%s: alloc_chrdev_region failed with %d.\n", __func__, ret);
    return ret;
  }

  cdev_init(&cdev, &ioat_dma_fops);
  cdev_add(&cdev, dev, 1);

  // Create /sys/class/ioat-dma in preparation of creating /dev/ioat-dma
  pClass = class_create(THIS_MODULE, DEVICE_NAME);
  if (IS_ERR(pClass)) {
    printk (KERN_ALERT "%s: class_create failed.\n", __func__);
    ret = PTR_ERR(pClass);
    goto unregister;
  }

  // Create /dev/ioat-dma
  pDev = device_create(pClass, NULL, dev, NULL, DEVICE_NAME);
  if (IS_ERR(pDev)) {
    printk(KERN_ALERT "%s: device_create failed.\n", __func__);
    ret = PTR_ERR(pDev);
    goto class_destroy;
  }

  return 0;

class_destroy:
  class_destroy(pClass);
unregister:
  unregister_chrdev_region(dev, 1);
  return ret;
}

static int create_dma_channels(void) {
  dma_cap_mask_t mask;
  struct dma_chan *chan = NULL;
  dma_cap_zero(mask);
  dma_cap_set(DMA_MEMCPY, mask);

  chan = dma_request_chan_by_mask(&mask);

  while (!IS_ERR(chan)) {
    struct ioat_dma_device *dma_device = devm_kzalloc(pDev, sizeof(struct ioat_dma_device), GFP_KERNEL);
    dma_device->chan = chan;
    list_add_tail(&dma_device->list, &dma_device_list);
    dev_info(pDev, "Found DMA device: %s\n", dev_name(chan->device->dev));

    chan = dma_request_chan_by_mask(&mask);
  }

  return 0;
}

static int __init ioat_dma_init(void) {
  int ret;

  printk(KERN_INFO "%s\n", __func__);
  ret = create_chardev();
  if (ret < 0) {
    return ret;
  }

  return create_dma_channels();
}

static void __exit ioat_dma_exit(void) {
  printk(KERN_INFO "%s\n", __func__);
  device_destroy(pClass, dev);
  class_destroy(pClass);
  cdev_del(&cdev);
  unregister_chrdev_region(dev, 1);
}

MODULE_AUTHOR("Insu Jang");
MODULE_LICENSE("GPL v2");
module_init(ioat_dma_init);
module_exit(ioat_dma_exit);