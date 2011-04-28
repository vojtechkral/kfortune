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


//TODO: add (un)likely

/*  ─────────────────────── Settings ────────────────────────  */

//global:
#define kf_MAX_DEV_NAME   16                // _INCLUDING_ the null char
#define kf_MIN_DEV_NAME   2                 // _WITHOUT_ the null char
#define kf_MAX_DEVS       8
#define kf_PROCFS_NAME    "kfortune"

//memory:
#define kf_MAX_COOKIESZ    (4*1024)         //max size of one cookie in bytes
#define kf_MAX_COOKIEDATA  (1024*1024)      //total bytes kfortune will ever alloc for cookies
#define kf_DATA_BLKSZ      (32*1024)
#define kf_DATA_BLKNUM     (kf_MAX_COOKIEDATA/kf_DATA_BLKSZ)
#define kf_INDX_BLKSZ      4096
#define kf_INDX_BLKNUM     4

/*  ─────────────────── Convenience macros ──────────────────  */

#define kf_STR_EXPAND(tok) #tok
#define kf_STR(tok) kf_STR_EXPAND(tok)

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
  kf_DEV_ACTIVE     = (1 << 0),
  kf_DEV_WRITELOCK  = (1 << 1),
  kf_DEV_2B_CLEARED = (1 << 2),
  kf_DEV_ERRORLOCK  = (1 << 3),
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
  unsigned int i;

  if (!data) return;

  data->size_used = 0;
  data->numcookies = 0;
  data->numblocks_data = 0;
  data->numblocks_indx = 0;
  for (i = 0; i < kf_INDX_BLKNUM; i++) data->indx_tbl[i] = NULL;
  data->data_tbl = NULL;
}

static void kf_cookiedata_clear(kf_cookiedata *data)
{
  unsigned int i;

  if (!data) return;

  for (i = 0; i < data->numblocks_indx; i++)
  {
    printk("kfortune: kfree() ← data->indx_tbl[%u]\n", i);
    if (data->indx_tbl[i]) kfree(data->indx_tbl[i]);
  }
  if (data->data_tbl)
  {
    for (i = 0; i < data->numblocks_data; i++)
    {
      printk("kfortune: vfree() ← data->data_tbl[%u]\n", i);
      if (data->data_tbl[i].block) vfree(data->data_tbl[i].block);
    }
    printk("kfortune: vfree() ← data->data_tbl\n");
    vfree(data->data_tbl);
  }

  kf_cookiedata_init(data);
}

static char *kf_cookiedata_get(kf_cookiedata *data, unsigned int n)
{
  unsigned int block_n;
  unsigned int offset;

  //check:
  if (!data) return NULL;
  if ((n >= data->numcookies) || (!data->data_tbl)) return NULL;

  block_n = n / (kf_INDX_BLKSZ/sizeof(char*));
  offset = n - block_n;

  //printk("kfortune: block_n = %u, offset = %u", block_n, offset);

  //return (*(data->indx_tbl[block_n]))[offset];
  return NULL;
}

static int kf_cookiedata_add(kf_cookiedata *data, const char* cookie, size_t size)
{
  unsigned int i;
  kf_blockinfo *block = NULL;
  kf_blockinfo *newtbl = NULL;
  char *target = NULL;
  unsigned int idx_block = 0;
  unsigned int idx_offset = 0;

  //initial checks:
  if ((!data) || (!cookie) || (!size)) return -1;                 //args sanity
  if (size > kf_MAX_COOKIESZ) return -2;                          //size of the cookie
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
    printk("kfortune: vmalloc() ← %u * %lu\n", (data->numblocks_data+1), sizeof(kf_blockinfo));
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
    printk("kfortune: vmalloc() ← %i\n", kf_DATA_BLKSZ);
    block->block = vmalloc(kf_DATA_BLKSZ);
      if (!block->block) return -6;    //kf_cookiedata_clear will take care
    data->numblocks_data++;
  }

  //get pointer within block:
  target = (block->block+block->size_used);

  //update index:
  if (!data->indx_tbl[idx_block])
  {
    //this block is not allocated yet, let's do it:
    kf_indxblock *ptr;
    printk("kfortune: kmalloc() ← %lu\n", sizeof(kf_indxblock));
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
static void kf_dev_init(kf_dev *dev)
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
  dev->cookiebuffer_len = 0;
  kf_cookiedata_init(&dev->data);
  memset(dev->cookiebuffer, 0, sizeof(dev->cookiebuffer));
}

static void kf_dev_clear(kf_dev *dev)
{
  if (!dev) return;
  kf_cookiedata_clear(&dev->data);
}

/*  ─────────────────────── Global Vars ─────────────────────  */

static struct proc_dir_entry *kf_procfile;
static kf_dev kf_devs[kf_MAX_DEVS];




//TODO: dmesg error reports

/*  ──────────────────────── Random ─────────────────────────  */
static unsigned int kf_random(unsigned int limit)
{
  /* This function generates a random number.
   * The rsult is always at least one less than limit
   */
  unsigned int res = 0;

  if (!limit) return 0;
  //get_random_bytes(&res, sizeof(res));
  res = 39;
  res %= limit;
  //TODO: temporary!:
  //printk("kfortune: kf_random() = %u\n", res);
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
  if (!(dev = kf_get_kfdev(file))) return -EINVAL;

  //state checks:
  if (dev->state & kf_DEV_ERRORLOCK) return -EPERM;
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
       * so that the mutex can be unlocked asap.
       */
    } else
    {
      return -EPERM;
    }
  }
  printk("kfortune: open\n");
  return 0;
}

static ssize_t kf_dev_read(struct file *file, char *buf, size_t size, loff_t *ppos)
{
  char pk[21];

  kf_dev *dev;
  const char* cookie;
  size_t len;

  if (*ppos) return 0;                //If the position is non-zero, assume cookie has been read
                                      //reading cookies from in the middle is not supported

  //get the device:
  if (!(dev = kf_get_kfdev(file))) return -EINVAL;

  //check errorlock:
  if (dev->state & kf_DEV_ERRORLOCK) return -EPERM;

  //get random cookie:
  cookie = kf_cookiedata_get(&dev->data, kf_random(dev->data.numcookies));

  if (!cookie)
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

  if (!s) return NULL;
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

static ssize_t kf_dev_write(struct file *file, const char *buf, size_t size, loff_t *ppos)
{
  //TODO: locking (via var + mutex)

  //TODO: TMP!:
  static unsigned int tmp = 0;
  char pk[20];

  kf_dev *dev;
  const char *pos = buf;
  const char *hit = NULL;
  size_t sz = 0, resid = 0;

  if (!(dev = kf_get_kfdev(file))) return -EINVAL;

  //clear data if the clear flag is set:
  if (dev->state & kf_DEV_2B_CLEARED)
  {
    kf_cookiedata_clear(&dev->data);
    dev->state &= ~kf_DEV_2B_CLEARED;
  }
  if (dev->state & kf_DEV_ERRORLOCK) return -EPERM;

  //parsing:
  hit = kf_sndelim(pos, buf+size);
//  if (dev->cookiebuffer_len && hit)      //residual buffer, because some cookies might end up being split among device write calls
//  {
//    sz = hit-pos;
//    if ((sz+dev->cookiebuffer_len) <= kf_MAX_COOKIESZ)
//    {
//      if (copy_from_user(dev->cookiebuffer+dev->cookiebuffer_len, pos, sz)) return -EFAULT;
//      sz += dev->cookiebuffer_len;
//      dev->cookiebuffer_len = 0;
//      goto insert;
//    }
//  }
  while (hit)
  {
    sz = hit-pos;
    if ((sz) && (sz <= kf_MAX_COOKIESZ) && (tmp < 900))
    {
      //a valid cookie has been found
      //first copy it into kernel memory
      if (copy_from_user(dev->cookiebuffer, pos, sz)) return -EFAULT;
      insert:
      if (kf_cookiedata_add(&dev->data, dev->cookiebuffer, sz))   //and then save it
      {
        //something went terribly wrong, lock this device down totally
        dev->state |= (kf_DEV_ERRORLOCK | kf_DEV_2B_CLEARED);
        printk("kfortune: chyba\n");   //TODO
        return -EPERM;
      }
      tmp++;
    }
    pos = hit+2;                             //so that zero length cookies are recognized as such
    hit = kf_sndelim(pos++, buf+size);           // ( - delimiters may overlap)
  }
  //TODO: TMP!:
  //strncpy(pk, buf, 20); pk[20] = '\0';
  printk("kfortune: hits: %u, size = %lu, ppos = %ld\n", tmp, size, *ppos);

  //fill residual buffer if necessary:
  resid = size-(pos-buf);
  if (resid && (resid <= kf_MAX_COOKIESZ))
  {
    if (copy_from_user(dev->cookiebuffer, pos, resid)) return -EFAULT;
    dev->cookiebuffer_len = resid;
  }

  //kthxbai:
  *ppos += size;
  return size;                               //say that size bytes have been processed
}

static int kf_dev_release(struct inode *inode, struct file *file)
{
  kf_dev *dev;
  if (!(dev = kf_get_kfdev(file))) return -EINVAL;

  if ((file->f_mode & FMODE_WRITE) && (dev->state & kf_DEV_WRITELOCK))
  {
    //clear data if the clear flag is set
    if (dev->state & kf_DEV_2B_CLEARED) kf_cookiedata_clear(&dev->data);
    dev->state &= ~(kf_DEV_WRITELOCK | kf_DEV_2B_CLEARED);
  }

  //TODO: residual buffer?

  printk("kfortune: release\n");
  return 0;
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
      kf_dev_clear(dev);
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


/*  ───────────────────── Procfs file ───────────────────────  */

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
  if (count >= sizeof(kbuf)) return -ENOSPC;

  if (copy_from_user(kbuf, buffer, count)) return -EFAULT;

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
