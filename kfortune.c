/*
 * kfortune.c
 *  - kernel-space implementation of the popular fortune program
 *
 * License: GNU GPLv3
 * Vojtech 'kralyk' Kral (c) 2011
 *
 */

#include <linux/kernel.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/proc_fs.h>


/*  ─────────────────────── Settings ────────────────────────  */

//ver:
#define kf_VER_MAJOR      "1"
#define kf_VER_MINOR      "0"

//global:
#define kf_MAX_DEV_NAME   16                // _INCLUDING_ the null char
#define kf_MIN_DEV_NAME   2                 // _WITHOUT_ the null char
#define kf_MAX_DEVS       8
#define kf_PROCFS_NAME    "kfortune"

//procfs report:
#define kf_REPORT_LINE_SZ  (kf_MAX_DEV_NAME+112)
#define kf_REPORT_HEADER   "kfortune status:\n"
#define kf_REPORT_FOOTER   "\n\nkfortune "kf_VER_MAJOR"."kf_VER_MINOR"\n"\
                           "By Vojtech 'kralyk' Kral (c) 2011\n"\
                           "Distributed under the GNU GPLv3 license.\n"\
                           "For usage information and documentation see 'man kfortune'\n"

//memory:
#define kf_MAX_COOKIESZ    (4*1024)         //max size of one cookie in bytes
#define kf_MAX_COOKIEDATA  (1024*1024)      //total bytes kfortune will ever alloc for cookies
#define kf_DATA_BLKSZ      (32*1024)
#define kf_DATA_BLKNUM     (kf_MAX_COOKIEDATA/kf_DATA_BLKSZ)
#define kf_INDX_BLKSZ      4096
#define kf_INDX_BLKNUM     4

/*  ────────────────── Forward declaration ──────────────────  */

static size_t kf_overall_size(void);
static int kf_dev_open(struct inode *, struct file *);
static ssize_t kf_dev_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t kf_dev_write(struct file *, const char __user *, size_t, loff_t *);
static int kf_dev_release(struct inode *, struct file *);
static ssize_t kf_proc_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t kf_proc_write(struct file *, const char __user *, size_t, loff_t *);

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
  char ** indx_tbl;
  kf_blockinfo *data_tbl;
} kf_cookiedata;

typedef enum
{
  kf_DEV_ACTIVE     = (1 << 0),
  kf_DEV_WOPENLOCK  = (1 << 1),
  kf_DEV_WCALLLOCK  = (1 << 2),
  kf_DEV_2B_CLEARED = (1 << 3),
  kf_DEV_ERRORLOCK  = (1 << 4),
} kf_dev_state;

typedef struct
{
  kf_dev_state state;
  char filename[kf_MAX_DEV_NAME];
  struct file_operations fops;
  struct miscdevice dev;
  kf_cookiedata data;
  char cookiebuffer[kf_MAX_COOKIESZ];
  size_t cookiebuffer_len;
} kf_dev;

/*  ──────────────────── Types - Methods ────────────────────  */

//cookiedata:
static void kf_cookiedata_init(kf_cookiedata *data)
{
  if (unlikely(!data)) return;

  data->size_used = 0;
  data->numcookies = 0;
  data->numblocks_data = 0;
  data->numblocks_indx = 0;
  data->indx_tbl = NULL;
  data->data_tbl = NULL;
}

static void kf_cookiedata_clear(kf_cookiedata *data)
{
  unsigned int i;

  if (unlikely(!data)) return;
  if (likely(data->data_tbl))
  {
    for (i = 0; i < data->numblocks_data; i++)
    {
      if (likely(data->data_tbl[i].block))
      {
        vfree(data->data_tbl[i].block);
        data->data_tbl[i].block = NULL;
      }
    }
    kfree(data->data_tbl);
    data->data_tbl = NULL;
  }
  if (likely(data->indx_tbl))
  {
    kfree(data->indx_tbl);
    data->indx_tbl = NULL;
  }

  kf_cookiedata_init(data);
}

static char *kf_cookiedata_get(kf_cookiedata *data, unsigned int n)
{
  //check:
  if (unlikely(!data)) return NULL;
  if (unlikely((n >= data->numcookies) || (!data->data_tbl))) return NULL;

  //return cookie:
  return data->indx_tbl[n];
}

static int kf_cookiedata_add(kf_cookiedata *data, const char* cookie, size_t size)
{
  unsigned int i;
  kf_blockinfo *block = NULL;
  char *target = NULL;

  //sanity checks:
  if (unlikely((!data) || (!cookie) || (!size))) return -1;                 //args sanity
  if (unlikely(size > kf_MAX_COOKIESZ)) return -2;                          //size of the cookie
  if (unlikely((kf_overall_size() + size) > kf_MAX_COOKIEDATA)) return -3;  //overall cookie data size
  if (unlikely(data->numcookies*sizeof(void*) >= kf_INDX_BLKSZ*kf_INDX_BLKNUM)) return -4;  //index size check

  //get block with enough free space:
  for (i = 0; (i < data->numblocks_data) && (!block); i++)
  {
    if ((kf_DATA_BLKSZ - data->data_tbl[i].size_used) > size) block = data->data_tbl+i;
      //  - operator > only, not >=, because +1 byte is needed for final '\0'
  }

  //if there was no free space, allocate new block:
  if (unlikely(!block))
  {
    if (data->numblocks_data >= kf_DATA_BLKNUM) return -5;          //no new block can be allocated
    data->data_tbl = krealloc(data->data_tbl, (data->numblocks_data += 1)*kf_DATA_BLKSZ, GFP_KERNEL);
      if (unlikely(!data->data_tbl)) return -6;
    block = &data->data_tbl[data->numblocks_data-1];
    block->size_used = 0;
    block->block =  vmalloc(kf_DATA_BLKSZ);
      if (unlikely(!block->block)) return -7;
  }

  //get pointer within block:
  target = (block->block+block->size_used);

  //update index:
  if (unlikely(data->numcookies*sizeof(void*) >= data->numblocks_indx*kf_INDX_BLKSZ))
  {
    //new index block needed
    data->indx_tbl = krealloc(data->indx_tbl, (data->numblocks_indx += 1)*kf_INDX_BLKSZ, GFP_KERNEL);
      if (unlikely(!data->indx_tbl)) return -6;
  }
  data->indx_tbl[data->numcookies] = target;
  data->numcookies++;

  //write data:
  memcpy(target, cookie, size);
  target[size] = '\0';
  size++;
  block->size_used += size;
  data->size_used  += size;

  //kthxbai:
  return 0;
}

//kf_dev:
static void kf_dev_init(kf_dev *dev)
{
  if (unlikely(!dev)) return;

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
  dev->cookiebuffer_len = 0;
  kf_cookiedata_init(&dev->data);
  memset(dev->cookiebuffer, 0, sizeof(dev->cookiebuffer));
}

static void kf_dev_clear(kf_dev *dev)
{
  if (unlikely(!dev)) return;
  kf_cookiedata_clear(&dev->data);
}

/*  ─────────────────────── Global Vars ─────────────────────  */

static struct proc_dir_entry *kf_procfile;
static kf_dev kf_devs[kf_MAX_DEVS];
static DEFINE_MUTEX(kf_mtx_wcall);

struct file_operations kf_procfile_fops =
{
  .owner = THIS_MODULE,
  .read = kf_proc_read,
  .write = kf_proc_write
};



/*  ──────────────────────── Random ─────────────────────────  */
static unsigned int kf_random(unsigned int cap)
{
  /* This function generates a random number.
   * The result is always at least one less than cap
   */
  unsigned int res = 0;

  if (unlikely(!cap)) return 0;
  get_random_bytes(&res, sizeof(res));
  res %= cap;
  return res;
}


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

  if (unlikely(!file)) return NULL;
  for (i = 0; i < kf_MAX_DEVS; i++)
  {
    if (&kf_devs[i].dev == file->private_data) return &kf_devs[i];
  }
  return NULL;
}

static int kf_dev_open(struct inode *inode, struct file *file)
{
  /*
   * This function's presence is vital (even if it did nothing),
   * because if fops.open was NULL, the miscdevice driver would
   * not hand over our miscdevice pointer in file->private_data
   * and there would be no way to identify which device is being
   * read or being written to in other fops.
   *
   * (NOTE: this is based on latest version of miscdevice driver
   *  in the main kernel git repo tree. Other drivers might differ.)
   */

  kf_dev *dev;
  if (unlikely(!(dev = kf_get_kfdev(file))))
  {
    printk(KERN_WARNING "kfortune: kf_dev_open(): unknown device\n");
    return -EINVAL;
  }

  //state checks:
  if (dev->state & kf_DEV_ERRORLOCK) return -EPERM;
  if (file->f_mode & FMODE_WRITE)
  {
    if (!(dev->state & kf_DEV_WOPENLOCK))
    {
      dev->state |= (kf_DEV_WOPENLOCK | kf_DEV_2B_CLEARED);
      /* Lock this down: there may only be one write access at a time.
       * NOTE: Race condition won't occur here, because
       * miscdevice driver has a mutex locked during whole fops.open().
       * Also, set the 2B_CLEARED flag on. Data will be cleared later
       * (either by write() or release()), right now let's just return
       * so that the mutex can be unlocked asap.
       */
    } else
    {
      return -EPERM;
    }
  }
  return 0;
}

static ssize_t kf_dev_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
  kf_dev *dev;
  const char* cookie;
  size_t len;

  if (*ppos) return 0;                //If the position is non-zero, assume cookie has been read
                                      //reading cookies from in the middle is not supported

  //get the device:
  if (unlikely(!(dev = kf_get_kfdev(file))))
  {
    printk(KERN_WARNING "kfortune: kf_dev_read(): unknown device\n");
    return -EINVAL;
  }

  //check errorlock:
  if (dev->state & kf_DEV_ERRORLOCK) return -EPERM;

  //get random cookie:
  cookie = kf_cookiedata_get(&dev->data, kf_random(dev->data.numcookies));

  if (unlikely(!cookie))
  {
    len = 0;
    goto no_cookies;
  }
  else len = strlen(cookie);
  if (len >= size) return -EINVAL;     // >= because final '\n' must fit in there too

  //copy data to the user:
  if (copy_to_user(buf, cookie, len)) return -EINVAL;
  no_cookies:
  if (copy_to_user(buf+len, "\n", 1)) return -EINVAL;

  //kthxbai:
  *ppos = len+1;
  return len+1;
}

const char *kf_sndelim(const char *s, const char* const end_addr)
{
  /* This function searches for a cookie delimiter substring,
   * namely "\n%\n". Due to performance, it is implemented using
   * a finite state machine. The downside is that the delimiter
   * is thus hardcoded (but I personaly don't give a damn :p).
   */
#define kf_SNDELIM_GET if(s < end_addr) c = *(s++); else goto not_found
  size_t pos = 0;
  const char *res;
  char c;

  if (unlikely(!s)) return NULL;
  start:
    kf_SNDELIM_GET;
    if (c != '\n') goto start;
  found_1st_nl:
    res = &s[pos-1];
    kf_SNDELIM_GET;
    if (c == '%') goto found_percent;
    else if (c == '\n') goto found_1st_nl;
    else goto start;
  found_percent:
    kf_SNDELIM_GET;
    if (c == '\n') return res;
    else goto start;
  not_found:
    return NULL;
}

static ssize_t kf_dev_write(struct file *file, const char __user *buf, size_t size, loff_t *ppos)
{
  kf_dev *dev;
  int mutex_exit = 0;
  const char *pos = buf;
  const char *hit = NULL;
  size_t sz = 0, resid = 0;

  if (unlikely(!(dev = kf_get_kfdev(file))))
  {
    printk(KERN_WARNING "kfortune: kf_dev_write(): unknown device\n");
    return -EINVAL;
  }

  //mutual exclusion:
  mutex_lock(&kf_mtx_wcall);                                //so that no write call could ever kick in while
    if (unlikely(dev->state & kf_DEV_WCALLLOCK))            //another write call is in progress for the same device
      mutex_exit = 1;
    else
      dev->state |= kf_DEV_WCALLLOCK;
  mutex_unlock(&kf_mtx_wcall);
  if (mutex_exit) return 0;

  //clear data if the clear flag is set:
  if (dev->state & kf_DEV_2B_CLEARED)
  {
    kf_cookiedata_clear(&dev->data);
    dev->state &= ~kf_DEV_2B_CLEARED;
  }
  if (dev->state & kf_DEV_ERRORLOCK) return -EPERM;

  //parsing:
  hit = kf_sndelim(pos, buf+size);

  //residual buffer:
  if (dev->cookiebuffer_len && hit)      //because some cookies might end up being split among device write calls
  {
    sz = hit-pos;
    if ((sz+dev->cookiebuffer_len) <= kf_MAX_COOKIESZ)
    {
      if (copy_from_user(dev->cookiebuffer+dev->cookiebuffer_len, pos, sz)) return -EFAULT;
      sz += dev->cookiebuffer_len;
      dev->cookiebuffer_len = 0;
      goto insert;
    }
  }
  while (hit)
  {
    sz = hit-pos;
    if ((sz) && (sz <= kf_MAX_COOKIESZ))
    {
      //a valid cookie has been found
      //first copy it into kernel memory
      if (copy_from_user(dev->cookiebuffer, pos, sz)) return -EFAULT;
      insert:
      if (kf_cookiedata_add(&dev->data, dev->cookiebuffer, sz))   //and then save it
      {
        //something went terribly wrong, lock this device down totally
        dev->state |= (kf_DEV_ERRORLOCK | kf_DEV_2B_CLEARED);
        printk(KERN_ERR "kfortune: could not get memory, locking device '/dev/%s'\n", dev->filename);
        return -EPERM;
      }
    }
    pos = hit+2;                              //so that zero length cookies are recognized as such
    hit = kf_sndelim(pos++, buf+size);        //  ( - delimiters may overlap)
  }

  //fill residual buffer if necessary:
  resid = size-(pos-buf);
  if (resid && (resid <= kf_MAX_COOKIESZ))
  {
    if (copy_from_user(dev->cookiebuffer, pos, resid)) return -EFAULT;
    dev->cookiebuffer_len = resid;
  }

  //kthxbai:
  *ppos += size;
  dev->state &= ~kf_DEV_WCALLLOCK;           //unlock write call
  return size;                               //say that size bytes have been processed
}

static int kf_dev_release(struct inode *inode, struct file *file)
{
  kf_dev *dev;
  if (unlikely(!(dev = kf_get_kfdev(file))))
  {
    printk(KERN_WARNING "kfortune: kf_dev_release(): unknown device\n");
    return -EINVAL;
  }

  if ((file->f_mode & FMODE_WRITE) && (dev->state & kf_DEV_WOPENLOCK))
  {
    //clear data if the clear flag is set
    if (dev->state & kf_DEV_2B_CLEARED) kf_cookiedata_clear(&dev->data);
    dev->state &= ~(kf_DEV_WOPENLOCK | kf_DEV_2B_CLEARED);
  }
  dev->cookiebuffer_len = 0;

  return 0;
}

static void kf_enable_dev(kf_dev *dev, int enable)
{
  int errno = 0;
  if (unlikely(!dev)) return;
  if (!(dev->state & kf_DEV_ACTIVE) == !enable) return;  //no work needed
  if (enable)
  {
    //register new device
    if (errno = misc_register(&dev->dev))
    {
      printk(KERN_ERR "kfortune: Error: Unable to register device '/dev/%s' (errno = %d)\n", dev->filename, errno);
      return;
    }
    else
    {
      dev->state |= kf_DEV_ACTIVE;
    }
  } else
  {
    //deregister
    if (errno = misc_deregister(&dev->dev))
    {
      printk(KERN_ERR "kfortune: Error: Unable to deregister device '/dev/%s' (errno = %d)\n", dev->filename, errno);
      return;
    }
    else
    {
      kf_dev_clear(dev);
      kf_dev_init(dev);
    }
  }
}


/*  ───────────────────── Procfs file ───────────────────────  */

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

static ssize_t kf_proc_read(struct file *file, char __user *userbuf, size_t size, loff_t *ppos)
{
  const char header[] = kf_REPORT_HEADER;
  const char footer[] = kf_REPORT_FOOTER;

  char buffer[kf_MAX_DEVS*kf_REPORT_LINE_SZ];
  size_t written = 0;
  int len = 0;
  int i;

  if (*ppos) return 0;             //no reading from the middle of the file

  //init buffer:
  memset(buffer, (int)' ', sizeof(buffer));

  //write header
  memcpy(buffer, header, sizeof(header)-1);                    //-1 because we don't want the final '\0' in there...
  written += sizeof(header)-1;

  //write kfortune status lines:
  for (i = 0; i < kf_MAX_DEVS; i++)
  {
    if (kf_devs[i].state & kf_DEV_ACTIVE)
    {
      written += ((len = sprintf(buffer+written, "  #%d:    /dev/", i)) >= 0 ? len : 0);
      len = ((len = sprintf(buffer+written, "%s", kf_devs[i].filename)) >= 0 ? len : 0);
      buffer[written+len] = ' ';
      written += kf_MAX_DEV_NAME;
      written += ((len = sprintf(buffer+written, " %4u cookies in %lu kB\n",
                                 kf_devs[i].data.numcookies, kf_devs[i].data.size_used/1024)) >= 0 ? len : 0);
    }
    else
    {
      len = sprintf(buffer+written, "  #%d:     (inactive)\n", i);
      written += len;
    }
  }

  //write footer
  memcpy(buffer+written, footer, sizeof(footer)-1);            //-1 because we don't want the final '\0' in there...
  written += sizeof(footer)-1;

  //check size:
  if (written > size) return -EINVAL;

  //output data:
  if (copy_to_user(userbuf, buffer, written)) return -EINVAL;

  //kthxbai:
  *ppos = written;
  return written;
}

static ssize_t kf_proc_write(struct file *file, const char __user *userbuf, size_t size, loff_t *ppos)
{
  char buffer[ kf_MAX_DEV_NAME*5*kf_MAX_DEVS ];    //enough bytes
  int i;
  char *tmp = buffer;
  size_t len;
  int dev_no = 0;

  //checks:
  if (*ppos) return 0;
  if (unlikely(size >= sizeof(buffer))) return -ENOSPC;
  if (copy_from_user(buffer, userbuf, size)) return -EFAULT;

  //parsing:
  buffer[size] = '\0';
  while (dev_no < kf_MAX_DEVS)
  {
    char *match = strsep(&tmp, " \n\r\t");
    if (!match) break;
    len = strlen(match);
    if ((len >= kf_MIN_DEV_NAME) && (len < kf_MAX_DEV_NAME))
      if (kf_check_dev_name(match))
      {
        kf_enable_dev(&kf_devs[dev_no], 0);
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
  *ppos = size;
  return size;
}

/*  ────────────────────────── Init ─────────────────────────  */

static int __init kfortune_init(void)
{
  int i;

  //Init kf_devs
  for (i = 0; i < kf_MAX_DEVS; i++)
    kf_dev_init(kf_devs+i);

  //Register procfs file
  if (unlikely(!(kf_procfile = proc_create(kf_PROCFS_NAME, 0644, NULL, &kf_procfile_fops))))
  {
    printk(KERN_ERR "kfortune: Error: Unable to register procfs file '"kf_PROCFS_NAME"'\n");
    return -ENOMEM;
  }

  //It's ready
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
  proc_remove(kf_procfile);

  //kthxbai
  printk(KERN_INFO "kfortune: unloaded\n");
}

/*  ────────────────────── Module macros ────────────────────  */

module_init(kfortune_init);
module_exit(kfortune_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vojtech 'kralyk' Kral");
MODULE_DESCRIPTION("kfortune: kernel-space remake of the popular fortune program");
MODULE_VERSION(kf_VER_MAJOR"."kf_VER_MINOR);
