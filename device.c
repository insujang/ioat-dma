// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2016-2018 Intel Corporation. All rights reserved. */
#include <linux/module.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/dax.h>
#include <linux/mman.h>
#include "dax-private.h"
#include "bus.h"

static int dax_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct dev_dax *dev_dax = filp->private_data;
	int rc, id;

	dev_dbg(&dev_dax->dev, "trace\n");

	/*
	 * We lock to check dax_dev liveness and will re-check at
	 * fault time.
	 */
	id = dax_read_lock();
	rc = remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
						vma->vm_end - vma->vm_start, vma->vm_page_prot);
	dax_read_unlock(id);
	if (rc)
		return rc;

	return 0;
}

/* return an unmapped area aligned to the dax region specified alignment */
static unsigned long dax_get_unmapped_area(struct file *filp,
		unsigned long addr, unsigned long len, unsigned long pgoff,
		unsigned long flags)
{
	unsigned long off, off_end, off_align, len_align, addr_align, align;
	struct dev_dax *dev_dax = filp ? filp->private_data : NULL;
	struct dax_region *dax_region;

	if (!dev_dax || addr)
		goto out;

	dax_region = dev_dax->region;
	align = dax_region->align;
	off = pgoff << PAGE_SHIFT;
	off_end = off + len;
	off_align = round_up(off, align);

	if ((off_end <= off_align) || ((off_end - off_align) < align))
		goto out;

	len_align = len + align;
	if ((off + len_align) < off)
		goto out;

	addr_align = current->mm->get_unmapped_area(filp, addr, len_align,
			pgoff, flags);
	if (!IS_ERR_VALUE(addr_align)) {
		addr_align += (off - addr_align) & (align - 1);
		return addr_align;
	}
 out:
	return current->mm->get_unmapped_area(filp, addr, len, pgoff, flags);
}

static const struct address_space_operations dev_dax_aops = {
	.set_page_dirty		= noop_set_page_dirty,
	.invalidatepage		= noop_invalidatepage,
};

static int dax_open(struct inode *inode, struct file *filp)
{
	struct dax_device *dax_dev = inode_dax(inode);
	struct inode *__dax_inode = dax_inode(dax_dev);
	struct dev_dax *dev_dax = dax_get_private(dax_dev);

	dev_dbg(&dev_dax->dev, "trace\n");
	inode->i_mapping = __dax_inode->i_mapping;
	inode->i_mapping->host = __dax_inode;
	inode->i_mapping->a_ops = &dev_dax_aops;
	filp->f_mapping = inode->i_mapping;
	filp->f_wb_err = filemap_sample_wb_err(filp->f_mapping);
	filp->private_data = dev_dax;
	inode->i_flags = S_DAX;

	return 0;
}

static int dax_release(struct inode *inode, struct file *filp)
{
	struct dev_dax *dev_dax = filp->private_data;

	dev_dbg(&dev_dax->dev, "trace\n");
	return 0;
}

static const struct file_operations dax_fops = {
	.llseek = noop_llseek,
	.owner = THIS_MODULE,
	.open = dax_open,
	.release = dax_release,
	.get_unmapped_area = dax_get_unmapped_area,
	.mmap = dax_mmap,
	.mmap_supported_flags = MAP_SYNC,
};

static void dev_dax_cdev_del(void *cdev)
{
	cdev_del(cdev);
}

static void dev_dax_kill(void *dev_dax)
{
	kill_dev_dax(dev_dax);
}

int dev_dax_probe(struct device *dev)
{
	struct dev_dax *dev_dax = to_dev_dax(dev);
	struct dax_device *dax_dev = dev_dax->dax_dev;
	struct resource *res = &dev_dax->region->res;
	struct inode *inode;
	struct cdev *cdev;
	void *addr;
	int rc;

	/* 1:1 map region resource range to device-dax instance range */
	if (!devm_request_mem_region(dev, res->start, resource_size(res),
				dev_name(dev))) {
		dev_warn(dev, "could not reserve region %pR\n", res);
		return -EBUSY;
	}

	dev_dax->pgmap.type = MEMORY_DEVICE_DEVDAX;
	addr = devm_memremap_pages(dev, &dev_dax->pgmap);
	if (IS_ERR(addr))
		return PTR_ERR(addr);

	inode = dax_inode(dax_dev);
	cdev = inode->i_cdev;
	cdev_init(cdev, &dax_fops);
	if (dev->class) {
		/* for the CONFIG_DEV_DAX_PMEM_COMPAT case */
		cdev->owner = dev->parent->driver->owner;
	} else
		cdev->owner = dev->driver->owner;
	cdev_set_parent(cdev, &dev->kobj);
	rc = cdev_add(cdev, dev->devt, 1);
	if (rc)
		return rc;

	rc = devm_add_action_or_reset(dev, dev_dax_cdev_del, cdev);
	if (rc)
		return rc;

	run_dax(dax_dev);
	return devm_add_action_or_reset(dev, dev_dax_kill, dev_dax);
}
EXPORT_SYMBOL_GPL(dev_dax_probe);

static int dev_dax_remove(struct device *dev)
{
	/* all probe actions are unwound by devm */
	return 0;
}

static struct dax_device_driver device_dax_driver = {
	.drv = {
		.probe = dev_dax_probe,
		.remove = dev_dax_remove,
	},
	.match_always = 1,
};

static int __init dax_init(void)
{
	return dax_driver_register(&device_dax_driver);
}

static void __exit dax_exit(void)
{
	dax_driver_unregister(&device_dax_driver);
}

MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL v2");
module_init(dax_init);
module_exit(dax_exit);
MODULE_ALIAS_DAX_DEVICE(0);
