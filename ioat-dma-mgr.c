// #define DEBUG
#include <linux/dmaengine.h>
#include <linux/spinlock.h>
#include "ioat-dma.h"

struct list_head dma_devices = LIST_HEAD_INIT(dma_devices);
u32 n_dma_devices;
DEFINE_SPINLOCK(device_spinlock);

int create_dma_devices(void) {
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
    dev_dbg(dev, "Found DMA device: %s\n", dev_name(chan->device->dev));
  }

  return 0;
}

struct ioat_dma_device *find_ioat_dma_device(u32 device_id) {
  struct ioat_dma_device *dma_device;
  list_for_each_entry(dma_device, &dma_devices, list) {
    if (dma_device->device_id == device_id && dma_device->owner == current->tgid) {
      return dma_device;
    }
  }
  return NULL;
}

struct ioat_dma_device *get_available_ioat_dma_device(void) {
  unsigned long flags;
  struct ioat_dma_device *dma_device;

  spin_lock_irqsave(&device_spinlock, flags);
  list_for_each_entry(dma_device, &dma_devices, list) {
    if (dma_device->owner > 0) {
      continue;
    }

    dev_info(dev, "%s: using device %s by %d\n", __func__,
             dev_name(dma_device->chan->device->dev), current->tgid);
    dma_device->owner = current->tgid;
    break;
  }
  spin_unlock_irqrestore(&device_spinlock, flags);

  if (dma_device == NULL) dma_device = ERR_PTR(-ENODEV);
  return dma_device;
}

void release_ioat_dma_device(struct ioat_dma_device *dma_device) {
  unsigned long flags;

  spin_lock_irqsave(&device_spinlock, flags);
  dev_info(dev, "%s: releasing device %s\n", __func__, dev_name(dma_device->chan->device->dev));
  dma_device->owner = -1;
  spin_unlock_irqrestore(&device_spinlock, flags);
}
