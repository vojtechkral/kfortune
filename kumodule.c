/*
 * KERNEL ULTRAS kernel module
 */

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/random.h>

#include <asm/uaccess.h>


/* Settings & convenience macros */

#define ku_numdevices 2
#define ku_maxfortunes 128

#define _(str) (str"\n")
#define ku_def_readfunc(num) \
    static ssize_t ku_read_##num(struct file * file, char * buf,\
                             size_t count, loff_t *ppos)\
    { return ku_read(file, buf, count, ppos, num); }
#define ku_def_fops(num) \
    static const struct file_operations ku_fops_##num = \
    {\
      .owner	= THIS_MODULE,\
      .read		= ku_read_##num,\
    }

/* Fortunes: the actual data */

static int ku_fortunes_nums[ku_numdevices] = { 0 };
static const char *ku_fortunes[ku_numdevices][ku_maxfortunes] =
{
  { _("prvni: jedna"), _("prvni: dva"), NULL },
  { _("druha: jedna"), _("druha: dva"), NULL },
};

/* The main chardev fortune reading function */

static ssize_t ku_read(struct file * file, char * buf, size_t count,
                       loff_t *ppos, const int fortune_number)
{
  unsigned int rand;
  const char **hello_str;
  int len;

  get_random_bytes(&rand, sizeof(rand));  // get random number and modulo it
  rand %= ku_fortunes_nums[fortune_number];

  hello_str = &ku_fortunes[fortune_number][rand];
  len = strlen(*hello_str);               // Don't include the null byte.

  if (count < len) return -EINVAL;        // Only read the whole fortune at once.

  if (*ppos != 0)	return 0;               // Assume the string has been read
                                          // and indicate there is no more data to be read.

  if (copy_to_user(buf, *hello_str, len))
    return -EINVAL;
  len++;

  *ppos = len;                            // Tell the user how much data we wrote.
	return len;
}

/* Read redirect functions */

ku_def_readfunc(0);
ku_def_readfunc(1);

/* Fops declaration */

ku_def_fops(0);
ku_def_fops(1);

/* KU Devices */


static struct miscdevice ku_devices[ku_numdevices] =
{
  { MISC_DYNAMIC_MINOR,  "ka",        &ku_fops_0 },
  { MISC_DYNAMIC_MINOR,  "finger",    &ku_fops_1 },
};

/* Init */

static int __init ku_init(void)
{
  int i, j, ret;

  printk(KERN_INFO "Beware: KERNEL ULTRAS kernel module summoned");

  for (i = 0; i < ku_numdevices; i++)
  {
    ret = misc_register(&(ku_devices[i]));
    if (ret) printk(KERN_ERR "KU module: Unable to register device #%d\n", i);
  }

  for (i = 0; i < ku_numdevices; i++)
  {
    for (j = 0; ku_fortunes[i][j]; j++);
    ku_fortunes_nums[i] = j;
  }

  return 0;
}

/* Exit */

static void __exit ku_exit(void)
{
  int i;

  printk(KERN_INFO "KERNEL ULTRAS kernel module dismissed");

  for (i = 0; i < ku_numdevices; i++)
  {
    misc_deregister(&(ku_devices[i]));
  }
}

/* Module macros */

module_init(ku_init);
module_exit(ku_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("kralyk");
MODULE_DESCRIPTION("KERNEL ULTRAS module");
MODULE_VERSION("prvni");
