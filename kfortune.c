/*
 *
 * TODO: header
 *
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <asm/uaccess.h>

#include <linux/proc_fs.h>

/*  ─────────────────────── Settings ────────────────────────  */

#define kf_MAX_DEV_NAME   16                // _INCLUDING_ the null char
#define kf_MIN_DEV_NAME   2                 // _WITHOUT_ the null char
#define kf_MAX_DEVS       8
#define kf_PROCFS_NAME    "kfortune"

/*  ─────────────────── Convenience macros ──────────────────  */

#define STR_EXPAND(tok) #tok
#define STR(tok) STR_EXPAND(tok)

/*  ───────────────────────── Types ─────────────────────────  */

typedef struct
{
  int active;
  char filename[kf_MAX_DEV_NAME];
  struct file_operations fops;
  struct miscdevice dev;
  //TODO: forutnes - data
} kf_dev;

/*  ─────────────────────── Global Vars ─────────────────────  */

static struct proc_dir_entry *kf_procfile;
static kf_dev kf_devs[kf_MAX_DEVS];


/*  ──────────────────── Devices operation ──────────────────  */

static kf_dev *kf_get_kfdev(struct file *file)
{
  int i;

  if (!file) return NULL;
  for (i = 0; i < kf_MAX_DEVS; i++)
  {
    if (&kf_devs[i].dev == file->private_data) return &kf_devs[i];
  }
  return NULL;
}

static int kf_dev_open(struct inode *inode, struct file *file)
{
  /*
   * Although this function doesn't actually do anything useful,
   * it's presence is vital, because if fops.open was NULL,
   * the misc device driver would not hand over our miscdevice pointer
   * in file->private_data and there would be no way to identify which device
   * is being read or being written to in following two functions.
   *
   * (NOTE: this is based on latest version of miscdevice driver
   *  in the main kernel git repo tree. Other drivers might differ.
   *  For reference see source code of your miscdevice driver.)
   */
  printk("kfortune: kf_dev_open: file->private_data = '%p'\n", file->private_data);
  return 0;
}

static ssize_t kf_dev_read(struct file * file, char * buf,
                          size_t count, loff_t *ppos)
{
  //TODO:

  kf_dev *dev;

  printk("kfortune: kf_dev_read: file->private_data = '%p'\n", file->private_data);
  printk("kfortune: kf_dev_read: count = '%lu'\n", count);

  if (!(dev = kf_get_kfdev(file))) return 0;

  int len = strlen(dev->filename);

  if (count < len)
          return -EINVAL;

  if (*ppos != 0)
          return 0;

  if (copy_to_user(buf, dev->filename, len))
          return -EINVAL;

  *ppos = len;

  return len;
}


/*  ──────────────── Procfs file and control ────────────────  */

static int kf_check_dev_name(const char *fn)
{
  size_t len = strlen(fn);
  size_t i;
  for (i = 0; i < len; i++)
  {
    int c = fn[i];
    if (
        ((c < 0x30) || (c > 0x39)) &&                     //0-9
        ((c < 0x41) || (c > 0x5a)) &&                     //A-Z
        (c != '_') &&                                     // _
        ((c < 0x61) || (c > 0x7a))                        //a-z
       ) return 0;  //aka false
  }
  return 1;         //aka true
}

static void kf_enable_dev(kf_dev *dev, int enable)
{
  if ((!dev->active) && (!enable)) return;  //no work needed
  if (dev->active)
  {
    //deregister
    printk("kfortune: deregistering device '%s'\n", dev->filename);
    if (misc_deregister(&dev->dev))
    {
      printk(KERN_ERR "kfortune: Error: Unable to deregister device '%s'\n", dev->filename);
      return;
    }
    else
    {
      dev->dev.minor = MISC_DYNAMIC_MINOR;
      dev->active = 0;
    }
  }
  if (enable)
  {
    //register new device
    if (misc_register(&dev->dev))
    {
      printk(KERN_ERR "kfortune: Error: Unable to register device '%s'\n", dev->filename);
      return;
    }
    else
    {
      printk("kfortune: misc_register ok, minor = '%u'\n", dev->dev.minor);
      dev->active = 1;
    }
  }
}

#define kf_REPORT_LINE_SZ  (kf_MAX_DEV_NAME+128)
static int kf_proc_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
  char buffer[ kf_MAX_DEVS * kf_REPORT_LINE_SZ ];
  ssize_t written = 0;
  int i;

  if (off) return 0;               //no reading from the middle of the file

  //write kfortune status info lines:
  for (i = 0; i < kf_MAX_DEVS; i++)
  {
    int len = 0;
    if (kf_devs[i].active)
      len = snprintf(buffer+written, kf_REPORT_LINE_SZ,
                     " #%d:\t/dev/%s\t%u cookies\n", i, kf_devs[i].filename, 0);
    else
      len = snprintf(buffer+written, kf_REPORT_LINE_SZ,
                     " #%d:\t(inactive)\n", i);
    if (len < kf_REPORT_LINE_SZ)   //otherwise lets just forget about this line (which should not actually happen at all)
    {
      written += len;              //will never exceed buffer size
    }
  }

  //check size:
  if (written > count) return -EINVAL;

  //output data:
  buffer[written] = '\0';
  strncpy(page, buffer, count);
  *eof = 1;   //this would be all

  //kthxbai:
  return written;
}

static int kf_proc_write(struct file *file, const char __user *buffer, unsigned long count, void *data)
{
  char kbuf[ kf_MAX_DEV_NAME*5*kf_MAX_DEVS ];    //enough bytes
  int i;
  char *tmp = kbuf;
  size_t len;
  int dev_no = 0;

  printk("kfortune: count = '%lu'\n", count);
  //checks:
  if (count >= sizeof(kbuf))
    return -ENOSPC;

  if (copy_from_user(kbuf, buffer, count))
    return -EFAULT;

  //parsing:
  kbuf[count] = '\0';
  while (dev_no < kf_MAX_DEVS)
  {
    char *match = strsep(&tmp, " \n\r\t");
    if (!match) break;
    len = strlen(match);
    if ((len >= kf_MIN_DEV_NAME) && (len < kf_MAX_DEV_NAME))
      if (kf_check_dev_name(match))
      {
        strcpy(kf_devs[dev_no].filename, match);    //since we already checked the length, it's ok to use strcpy (I hope)
        printk("kfortune: match = '%s'\n", match);
        kf_enable_dev(&kf_devs[dev_no], 1);
        dev_no++;
      }
  }

  //disable remaining devs:
  for (i = dev_no; i < kf_MAX_DEVS; i++)
  {
    printk("kfortune: disable dev: i = '%d'\n", i);
    kf_enable_dev(kf_devs+i, 0);
  }

  //kthxbai:
  return count;
}

/*  ────────────────────────── Init ─────────────────────────  */

static int __init kfortune_init(void)
{
  int i;

  //Init kf_devs
  for (i = 0; i < kf_MAX_DEVS; i++)
  {
    if (!i) printk("kfortune: Init: &dev = '%p'\n", &kf_devs[i].dev);
    kf_devs[i].active = 0;
    strcpy(kf_devs[i].filename, "");
    kf_devs[i].fops.owner = THIS_MODULE;
    kf_devs[i].fops.read = kf_dev_read;
    kf_devs[i].fops.open = kf_dev_open;
    //kf_devs[i].fops.
    kf_devs[i].dev.minor = MISC_DYNAMIC_MINOR;
    kf_devs[i].dev.name = kf_devs[i].filename;       //let them point to the same statically-allocated area
    kf_devs[i].dev.fops = &kf_devs[i].fops;
  }

  //Register procfs file
  if (!(kf_procfile = create_proc_entry(kf_PROCFS_NAME, 0644, NULL)))
  {
    printk(KERN_ERR "kfortune: Error: Unable to register procfs file '"kf_PROCFS_NAME"'\n");
    return -ENOMEM;
  } else
  {
    kf_procfile->read_proc = kf_proc_read;
    kf_procfile->write_proc = kf_proc_write;
  }

  //kthxbai
  printk(KERN_INFO "kfortune: loaded\n");
  return 0;
}

/*  ────────────────────────── Exit ─────────────────────────  */

static void __exit kfortune_exit(void)
{
  //deregister devs
  int i;
  for (i = 0; i < kf_MAX_DEVS; i++)
  {
    kf_enable_dev(kf_devs+i, 0);
  }

  //deregister procfs file
  remove_proc_entry(kf_PROCFS_NAME, NULL);  //returns void, no checking, let's just hope it went ok...

  //kthxbai
  printk(KERN_INFO "kfortune: unloaded\n");
}

/*  ────────────────────── Module macros ────────────────────  */

module_init(kfortune_init);
module_exit(kfortune_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("");
MODULE_DESCRIPTION("");
MODULE_VERSION("");
