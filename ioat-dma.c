#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>

static int __init ioat_dma_init(void) {
  printk(KERN_INFO "%s\n", __func__);
  return 0;
}

static void __exit ioat_dma_exit(void) {
  printk(KERN_INFO "%s\n", __func__);
}

MODULE_AUTHOR("Insu Jang");
MODULE_LICENSE("GPL v2");
module_init(ioat_dma_init);
module_exit(ioat_dma_exit);