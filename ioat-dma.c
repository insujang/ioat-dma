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
#define DEBUG
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
#include <linux/types.h>
#include <linux/spinlock.h>
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
  u64 device_id;
  char device_name[32];
  u64 src_offset;
  u64 dst_offset;
  u64 size;
} __attribute__ ((packed));

#define IOCTL_IOAT_DMA_SUBMIT _IOW(IOCTL_MAGIC, 0, struct ioctl_dma_args)
#define IOCTL_IOAT_DMA_GET_DEVICE_NUM _IOR(IOCTL_MAGIC, 0, u32)
#define IOCTL_IOAT_DMA_GET_DEVICE _IOR(IOCTL_MAGIC, 1, u64)

/* DMA stuffs */
struct ioat_dma_device {
  struct list_head list;
  u64 device_id;
  pid_t owner;
  struct dma_chan *chan;
  struct completion comp;
  bool comp_in_use;
};

static LIST_HEAD(dma_devices);
static u32 n_dma_devices;

static DEFINE_SPINLOCK(device_spinlock);

static struct ioat_dma_device *find_ioat_dma_device(u32 device_id) {
  struct ioat_dma_device *device;
  list_for_each_entry(device, &dma_devices, list) {
    if (device->device_id == device_id && device->owner == current->tgid) {
      return device;
    }
  }
  return NULL;
}

static struct ioat_dma_device *get_ioat_dma_device(const pid_t tgid) {
  unsigned long flags;
  struct ioat_dma_device *device;

  spin_lock_irqsave(&device_spinlock, flags);
  list_for_each_entry(device, &dma_devices, list) {
    if (device->owner > 0) {
      continue;
    }

    dev_info(pDev, "%s: using device %s by %d\n", __func__,
             dev_name(device->chan->device->dev), tgid);
    device->owner = tgid;
    break;
  }
  spin_unlock_irqrestore(&device_spinlock, flags);

  if (device == NULL) device = ERR_PTR(-ENODEV);
  return device;
}

static void release_ioat_dma_device(struct ioat_dma_device *dma_device) {
  unsigned long flags;

  spin_lock_irqsave(&device_spinlock, flags);
  dev_info(pDev, "%s: releasing device %s\n", __func__, dev_name(dma_device->chan->device->dev));
  dma_device->owner = -1;
  spin_unlock_irqrestore(&device_spinlock, flags);
}

static int ioat_dma_open(struct inode *, struct file *);
static int ioat_dma_release(struct inode *, struct file *);
static long ioat_dma_ioctl(struct file *, unsigned int, unsigned long);

static struct file_operations ioat_dma_fops = {
  .open = ioat_dma_open,
  .release = ioat_dma_release,
  .unlocked_ioctl = ioat_dma_ioctl,
};


static int ioat_dma_open(struct inode *inode, struct file *file) {
  dev_dbg(pDev, "%s\n", __func__);

  return 0;
}

static int ioat_dma_release(struct inode *inode, struct file *file) {
  struct ioat_dma_device *device;
  list_for_each_entry(device, &dma_devices, list) {
    if (device->owner == current->tgid) {
      release_ioat_dma_device(device);
    }
  }

  return 0;
}

static void dma_sync_callback(void *completion) {
  complete(completion);
  printk(KERN_INFO "%s: callback completes DMA completion.\n", __func__);
}

static int ioat_dma_ioctl_dma_submit(struct ioctl_dma_args *args, struct dev_dax *dev_dax, struct ioat_dma_device *dma_device) {
  struct resource *res = &dev_dax->region->res;
  struct page *page;
  dma_addr_t src, dst;

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

  src = dma_map_page(dma_device->chan->device->dev, page, args->src_offset, args->size, DMA_TO_DEVICE);
  dst = dma_map_page(dma_device->chan->device->dev, page, args->dst_offset, args->size, DMA_FROM_DEVICE);

  printk("%s: DMA about to initiate: 0x%llx -> 0x%llx (size: 0x%llx bytes)\n",
          __func__, src, dst, args->size);

  chan_desc = dmaengine_prep_dma_memcpy(dma_device->chan, dst, src, args->size, flags);
  if (chan_desc == NULL) {
    result = -EFAULT;
    goto unmap;
  }

  init_completion(&dma_device->comp);
  chan_desc->callback = dma_sync_callback;
  chan_desc->callback_param = &dma_device->comp;
  cookie = dmaengine_submit(chan_desc);
  dma_device->comp_in_use = true;
  
  dma_async_issue_pending(dma_device->chan);

  timeout = wait_for_completion_timeout(&dma_device->comp, msecs_to_jiffies(5000));
  status = dma_async_is_tx_complete(dma_device->chan, cookie, NULL, NULL);
  dev_dbg(pDev, "%s: wait completed.\n", __func__);

  if (timeout == 0) {
    dev_warn(pDev, "%s: DMA timed out!\n", __func__);
    result = -ETIMEDOUT;
  } else if (status != DMA_COMPLETE) {
    dev_warn(pDev, "%s: DMA returned completion callback status of: %s\n",
      __func__, status == DMA_ERROR ? "error" : "in progress");
    result = -EBUSY;
  } else {
    dev_info(pDev, "%s: DMA completed!\n", __func__);
    result = 0;
  }

unmap:
  dma_unmap_page(dma_device->chan->device->dev, src, args->size, DMA_BIDIRECTIONAL);
  dma_unmap_page(dma_device->chan->device->dev, dst, args->size, DMA_BIDIRECTIONAL);

  return result;
}

static long ioat_dma_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
  dev_dbg(pDev, "%s\n", __func__);

  switch (cmd) {
    case IOCTL_IOAT_DMA_SUBMIT: {
      struct ioctl_dma_args args;
      struct dax_device *dax_device;
      struct dev_dax *dev_dax;
      struct ioat_dma_device *dma_device;

      if (copy_from_user(&args, (void __user *) arg, sizeof(args))) {
        return -EFAULT;
      }
      dev_dbg(pDev, "dev name: %s, src offset: 0x%llx, dst offset: 0x%llx, size: 0x%llx\n",
              args.device_name, args.src_offset, args.dst_offset, args.size);

      dax_device = dax_get_device(args.device_name);
      if (dax_device == NULL) {
        return -ENODEV;
      }
      dev_dax = (struct dev_dax *)dax_get_private(dax_device);

      dma_device = find_ioat_dma_device(args.device_id);
      if (dma_device == NULL) return -ENODEV;

      return ioat_dma_ioctl_dma_submit(&args, dev_dax, dma_device);
    }
    case IOCTL_IOAT_DMA_GET_DEVICE_NUM: {
      if (copy_to_user((void __user *) arg, &n_dma_devices, sizeof(n_dma_devices))) {
        return -EFAULT;
      }
      return 0;
    }
    case IOCTL_IOAT_DMA_GET_DEVICE: {
      struct ioat_dma_device *device;

      device = get_ioat_dma_device(current->tgid);
      if (IS_ERR(device)) {
        return PTR_ERR(device);
      }

      if (copy_to_user((void __user *) arg, &device->device_id, sizeof(device->device_id))) {
        release_ioat_dma_device(device);
        return -EFAULT;
      }

      return 0;
    }
    default:
      dev_warn(pDev, "unsupported command %x\n", cmd);
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
    struct ioat_dma_device *dma_device = kzalloc(sizeof(struct ioat_dma_device), GFP_KERNEL);
    dma_device->owner = -1;
    dma_device->device_id = n_dma_devices;
    dma_device->chan = chan;
    list_add_tail(&dma_device->list, &dma_devices);
    n_dma_devices++;
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
  struct ioat_dma_device *device, *tmp;
  dev_dbg(pDev, "%s\n", __func__);
  list_for_each_entry_safe(device, tmp, &dma_devices, list) {
    list_del(&device->list);
    dma_release_channel(device->chan);
    kfree(device);
  }
  device_destroy(pClass, dev);
  class_destroy(pClass);
  cdev_del(&cdev);
  unregister_chrdev_region(dev, 1);
}

MODULE_AUTHOR("Insu Jang");
MODULE_LICENSE("GPL v2");
module_init(ioat_dma_init);
module_exit(ioat_dma_exit);