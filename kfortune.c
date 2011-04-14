/*
 *
 * TODO: header
 *
 */

#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/proc_fs.h>


/*  ─────────────────────── Settings ────────────────────────  */

//global:
#define kf_MAX_DEV_NAME   16                // _INCLUDING_ the null char
#define kf_MIN_DEV_NAME   2                 // _WITHOUT_ the null char
#define kf_MAX_DEVS       8
#define kf_PROCFS_NAME    "kfortune"

//memory:
#define kf_MAX_COOKIEDATA  (1024*1024)      //total bytes kfortune will ever alloc for cookies
#define kf_DATA_BLKSZ      (32*1024)
#define kf_DATA_BLKNUM     (kf_MAX_COOKIEDATA/kf_DATA_BLKSZ)
#define kf_INDX_BLKSZ      4096
#define kf_INDX_BLKNUM     4

/*  ─────────────────── Convenience macros ──────────────────  */

#define STR_EXPAND(tok) #tok
#define STR(tok) STR_EXPAND(tok)

/*  ────────────────── Forward declaration ──────────────────  */

static size_t kf_overall_size(void);
static int kf_dev_open(struct inode *, struct file *);
static ssize_t kf_dev_read(struct file *, char *, size_t, loff_t *);
static ssize_t kf_dev_write(struct file *, const char *, size_t, loff_t *);
static int kf_dev_release(struct inode *, struct file *);

/*  ───────────────────────── Types ─────────────────────────  */

typedef char* kf_indxblock[kf_INDX_BLKSZ/sizeof(char*)];

typedef struct
{
  size_t size_used;
  char *block;
} kf_blockinfo;

typedef struct
{
  size_t size_used;
  unsigned int numcookies;
  unsigned int numblocks_data;
  unsigned int numblocks_indx;
  kf_indxblock *indx_tbl[kf_INDX_BLKNUM];
  kf_blockinfo *data_tbl;
} kf_cookiedata;

typedef enum
{
  kf_DEV_ACTIVE     = 0x1,
  kf_DEV_WRITELOCK  = 0x2,
  kf_DEV_2B_CLEARED = 0x4,
} kf_dev_state;

typedef struct
{
  kf_dev_state state;
  char filename[kf_MAX_DEV_NAME];
  struct file_operations fops;
  struct miscdevice dev;
  kf_cookiedata data;
} kf_dev;

/*  ──────────────────── Types - Methods ────────────────────  */

//printk("kfortune: kf_dev_read: file = '%p'\n", file);

//cookiedata:
void kf_cookiedata_init(kf_cookiedata *data)
{
  unsigned int i;

  if (!data) return;

  data->size_used = 0;
  data->numcookies = 0;
  data->numblocks_data = 0;
  data->numblocks_indx = 0;
  for (i = 0; i < kf_INDX_BLKNUM; i++) data->indx_tbl[i] = NULL;
  data->data_tbl = NULL;
}

void kf_cookiedata_clear(kf_cookiedata *data)
{
  unsigned int i;

  if (!data) return;

  for (i = 0; i < data->numblocks_indx; i++)
  {
    if (data->indx_tbl[i]) kfree(data->indx_tbl[i]);
  }
  for (i = 0; i < data->numblocks_data; i++)
  {
    if (data->data_tbl[i].block) vfree(data->data_tbl[i].block);
  }
  vfree(data->data_tbl);

  kf_cookiedata_init(data);
}

char *kf_cookiedata_get(kf_cookiedata *data, unsigned int n)
{
  unsigned int block_n;
  unsigned int offset;

  //check:
  if ((n >= data->numcookies) || (!data->data_tbl)) return NULL;

  block_n = n / (kf_INDX_BLKSZ/sizeof(char*));
  offset = n - block_n;

  return (*(data->indx_tbl[block_n]))[offset];
}

int kf_cookiedata_add(kf_cookiedata *data, const char* cookie, size_t size)
{
  unsigned int i;
  kf_blockinfo *block = NULL;
  kf_blockinfo *newtbl = NULL;
  char *target = NULL;
  unsigned int idx_block = 0;
  unsigned int idx_offset = 0;

  //initial checks:
  if ((!data) || (!cookie) || (!size)) return -1;                 //args sanity
  if (size > kf_DATA_BLKSZ) return -2;                            //size of the cookie
  if ((kf_overall_size() + size) > kf_MAX_COOKIEDATA) return -3;  //overall cookie data size
  idx_block = data->numcookies / (kf_INDX_BLKSZ/sizeof(char*));
  idx_offset = data->numcookies - idx_block;
  if (idx_block >= kf_INDX_BLKNUM) return -4;                     //index size check

  //get block with enough free space:
  for (i = 0; (i < data->numblocks_data) && (!block); i++)
  {
    if ((kf_DATA_BLKSZ - data->data_tbl[i].size_used) > size) block = data->data_tbl+i;
       //  - operator > only, not >=, because +1 byte is needed for final '\0'
  }

  if (data->numblocks_data >= kf_DATA_BLKNUM) return -5;

  if (!block)
  {
    //if there was none, lets allocate & init a new one:
    i = data->numblocks_data;         //(should already equal so, but one can never know...)
    newtbl = vmalloc((data->numblocks_data+1)*sizeof(kf_blockinfo));
      if (!newtbl) return -6;
    if (data->data_tbl)
    {
      memcpy(newtbl, data->data_tbl, (data->numblocks_data)*sizeof(kf_blockinfo));   //copy previous blockinfos
      vfree(data->data_tbl);
    }
    data->data_tbl = newtbl;
    block = data->data_tbl+i;
    block->size_used = 0;
    block->block = vmalloc(kf_DATA_BLKSZ);
      if (!block->block) return -6;    //kf_cookiedata_clear will take care
    data->numblocks_data++;
  }
  printk("kfortune: block = %p\n", block);

  //get pointer within block:
  target = (block->block+block->size_used);

  //update index:
  if (!data->indx_tbl[idx_block])
  {
    //this block is not allocated yet, let's do it:
    kf_indxblock *ptr;
    ptr = kmalloc(sizeof(kf_indxblock), GFP_KERNEL);
      if (!ptr) return -6;              //new datablock might have been allocated, but it don't
                                        //matter as data knows about that so it won't leak
    data->indx_tbl[idx_block] = ptr;
  }
  (*(data->indx_tbl[idx_block]))[idx_offset] = target;

  //copy data:
  memcpy(target, cookie, size);
  target[size] = '\0';              //NOTE: we don't care if cookie string contains '\0', we don't need to
  size++;                           //because of '\0'

  //update metadata:
  data->numcookies++;
  block->size_used += size;
  data->size_used += size;

  //kthxbai:
  return 0;
}

//kf_dev:
void kf_dev_init(kf_dev *dev)
{
  if (!dev) return;

  dev->state = 0;
  strcpy(dev->filename, "");
  dev->fops.owner = THIS_MODULE;
  dev->fops.open = kf_dev_open;
  dev->fops.read = kf_dev_read;
  dev->fops.write = kf_dev_write;
  dev->fops.release = kf_dev_release;
  dev->dev.minor = MISC_DYNAMIC_MINOR;
  dev->dev.name = dev->filename;            //let them point to the same statically-allocated area
  dev->dev.fops = &dev->fops;
  kf_cookiedata_clear(&dev->data);
}

/*  ─────────────────────── Global Vars ─────────────────────  */

static struct proc_dir_entry *kf_procfile;
static kf_dev kf_devs[kf_MAX_DEVS];



/*  ──────────────────── Devices operation ──────────────────  */

static size_t kf_overall_size(void)
{
  unsigned int i;
  size_t overall = 0;
  for (i = 0; i < kf_MAX_DEVS; i++) overall += kf_devs[i].data.size_used;
  return overall;
}

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
   * This function's presence is vital (even if it did onthing),
   * because if fops.open was NULL, the miscdevice driver would
   * not hand over our miscdevice pointer in file->private_data
   * and there would be no way to identify which device is being
   * read or being written to in following two functions.
   *
   * (NOTE: this is based on latest version of miscdevice driver
   *  in the main kernel git repo tree. Other drivers might differ.
   *  For reference see source code of your miscdevice driver.)
   */

  kf_dev *dev;
  if (!(dev = kf_get_kfdev(file))) return -EINVAL;

  if (file->f_mode & FMODE_WRITE)
  {
    if (!(dev->state & kf_DEV_WRITELOCK))
    {
      dev->state |= (kf_DEV_WRITELOCK | kf_DEV_2B_CLEARED);
      /* Lock this down: there may only be one write access at a time.
       * NOTE: Race condition won't occur here, because
       * miscdevice driver has a mutex locked during whole fops.open().
       * Also, set the 2B_CLEARED flag on. Data will be cleared later
       * (either by write() or release()), right now let's just return
       * so that the mutex is unlocked asap.
       */
    } else
    {
      return -EPERM;
    }
  }
  return 0;
}

static ssize_t kf_dev_read(struct file *file, char *buf, size_t size, loff_t *ppos)
{
  //TODO:

  kf_dev *dev;
  size_t len;

  if (!(dev = kf_get_kfdev(file))) return -EINVAL;

  len = strlen(dev->filename);

  if (size < len)
          return -EINVAL;

  if (*ppos != 0)
          return 0;

  if (copy_to_user(buf, dev->filename, len))
          return -EINVAL;

  *ppos = len;

  return len;
}

static ssize_t kf_dev_write(struct file *file, const char *buf, size_t size, loff_t *ppos)
{
  //TODO:

  //TODO: clear data if flag

  //parsing
  //saving

  return size;
}

static int kf_dev_release(struct inode *inode, struct file *file)
{
  kf_dev *dev;
  if (!(dev = kf_get_kfdev(file))) return -EINVAL;

  if ((file->f_mode & FMODE_WRITE) && (dev->state & kf_DEV_WRITELOCK))
  {
    //TODO: clear data if flag
    dev->state &= ~kf_DEV_WRITELOCK;
  }

  return 0;
}

/*  ──────────────── Procfs file and control ────────────────  */

//TODO: add open() (because of blank write access)

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
  if ((!(dev->state & kf_DEV_ACTIVE)) && (!enable)) return;  //no work needed
  if (dev->state & kf_DEV_ACTIVE)
  {
    //deregister
    if (misc_deregister(&dev->dev))
    {
      printk(KERN_ERR "kfortune: Error: Unable to deregister device '%s'\n", dev->filename);
      return;
    }
    else
    {
      kf_dev_init(dev);
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
      dev->state |= kf_DEV_ACTIVE;
    }
  }
}

#define kf_REPORT_LINE_SZ  (kf_MAX_DEV_NAME+128)
static int kf_proc_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
  char buffer[ kf_MAX_DEVS * kf_REPORT_LINE_SZ ];
  size_t written = 0;
  int i;

  if (off) return 0;               //no reading from the middle of the file

  //write kfortune status info lines:
  //  TODO: this might as well need update. A helper func would be nice...
  for (i = 0; i < kf_MAX_DEVS; i++)
  {
    int len = 0;
    if (kf_devs[i].state & kf_DEV_ACTIVE)
      len = snprintf(buffer+written, kf_REPORT_LINE_SZ,
                     " #%d:\t/dev/%s\t%u cookies\n", i, kf_devs[i].filename, kf_devs[i].data.numcookies);
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
        kf_enable_dev(&kf_devs[dev_no], 1);
        dev_no++;
      }
  }

  //disable remaining devs:
  for (i = dev_no; i < kf_MAX_DEVS; i++)
  {
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
    kf_dev_init(kf_devs+i);

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
  printk(KERN_INFO "kfortune: unloaded\n-------------------------------\n");
}

/*  ────────────────────── Module macros ────────────────────  */

module_init(kfortune_init);
module_exit(kfortune_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("");
MODULE_DESCRIPTION("");
MODULE_VERSION("");
