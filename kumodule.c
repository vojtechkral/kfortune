/*
 * KERNEL ULTRAS kernel module
 *
 * Big thanks to Valerie Henson for: / Mnohé díky Valerii Henson za:
 *   http://linuxdevcenter.com/pub/a/linux/2007/07/05/devhelloworld-a-simple-introduction-to-device-drivers-under-linux.html?page=1
 */

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/random.h>
#include <linux/slab.h>

#include <asm/uaccess.h>


/* Settings & convenience macros */

#define ku_numdevices 2
#define ku_maxfortunes 128

const unsigned int ku_maxwidth = 100;             //width padding
const unsigned int ku_fortune_maxsize = 1024;     //one-kilobyte-sized fortune should be enough for anybody
#define fortuna(text, url) (text)
#define padding "                                                                    "
#define fortuna_sg(text, author, url) (text"\n"padding"--"author)

#define ku_def_readfunc(num) \
    static ssize_t ku_read_##num(struct file * file, char * buf,\
                             size_t count, loff_t *ppos)\
    { return ku_read(file, buf, count, ppos, num); }
#define ku_def_fops(num) \
    static const struct file_operations ku_fops_##num = \
    {\
      .owner	= THIS_MODULE,\
      .read   = ku_read_##num,\
    }

/* Fortunes: the actual data */

static int ku_fortunes_nums[ku_numdevices] = { 0 };
static const char *ku_fortunes[ku_numdevices][ku_maxfortunes] =
{
  /*** /dev/ka ***/
  {
    fortuna_sg("Jsem kernel panic, neni to lehky nesel bych za nic do naky /dev/ky...",
               "Kevin (anonym)",
               "http://www.abclinuxu.cz/blog/Tuxuv_anal/2008/7/linuxove-ctyrversi/diskuse#36"),
    fortuna_sg("Tss, z MŠ mě pustili vo rok dřív, jak sem byl chytrej :P Už jsem uměl napsat PIVO i se zavázanýma očima.",
               "Darm",
               "http://www.abclinuxu.cz/blog/Saljack/2010/4/mala-pomoc-s-javou#43"),
    fortuna_sg("[Reakce na kalhotky C-String] Je to výbornej nápad, vohnul sem si naběračku na polívku, sklapnul půlky a teď tady v tom běhám po bytě... ",
               "Amigapower",
               "http://www.abclinuxu.cz/blog/puppylinux/2010/9/cstring#1"),
    fortuna_sg("Tím šťastlivcem se stal soutěžící kotyz, který s odpovědí přispěchal jen několik minut po vyhlášení soutěže a dostane tedy téměř dvoumetrové plátno s leteckým snímkem centra Sydney s rozlišením 8 541 x 4 357 pixelů.",
               "zive.cz",
               "http://www.zive.cz/bleskovky/vysledky-souteze-o-obrovsky-snimek-z-google-earth/sc-4-a-148305/default.aspx"),
    fortuna_sg("Ty mi děláš torčr!",
               "vlastikroot",
               "(MoR)"),
    fortuna_sg("...konzole pyčo, na tom se věšej záclony... :-D",
               "Amigapower",
               "http://www.abclinuxu.cz/blog/TryCatch/2010/2/jak-jsem-hledal-vyhovujici-terminal-pro-windows#55"),
    fortuna_sg("Ty asi budeš retartovaný jako většina místní verbeže",
               "Petr Tomeš",
               "http://www.abclinuxu.cz/blog/ptomes/2008/3/firefox-3-je-nejrychlejsi-ve-zpracovani-javascriptu/diskuse#215"),
    fortuna_sg("Mozilla dělá program pro 300 milionů lidí, díky kterýmu pokračují inovace na webu a vyvíjí se i dominantní prohlížeč.",
               "Petr Tomeš",
               "http://www.abclinuxu.cz/zpravicky/firefox-3.5-rc/diskuse#150"),
    fortuna_sg("Opera dělá vcelku bezvýznamný produkt vesměs pro pár procent méně náročných úchyláků, kteří se spokojí s omezenými možnostmi Opery a jejími vyššími paměťovými nároky.",
               "Petr Tomeš",
               "http://www.abclinuxu.cz/zpravicky/firefox-3.5-rc/diskuse#150"),
    fortuna_sg("Proč lžeš?",
               "Petr Tomeš",
               "http://www.abclinuxu.cz/clanky/firefox-4-mame-se-na-co-tesit#83"),
    fortuna_sg("Další 32bit šmejďárna...",
               "Jardík",
               "http://www.abclinuxu.cz/clanky/recenze/adobe-air-flash-a-ajax-na-desktopu#1"),
    fortuna_sg("Vidíte taky ty hnusný nekvalitní schodovitý přechody?",
               "Jardík",
               "http://www.abclinuxu.cz/desktopy/jardik-20091008"),
    fortuna_sg("Logo ubuntu vytejká z panelu.",
               "Jardík",
               "http://www.abclinuxu.cz/desktopy/xkucf03-20100516#1"),
    NULL
  },
  /*** /dev/finger ***/
  {
    "58% uživatelů desktopových PC používá Linux",
    "OGG poskytuje kvalitnější zvuk než MP3",
    "Emacs je na práci efektivnější než VIM",
    "VIM je na práci efektivnější než Emacs",
    "KDE je mnohem pohodlnější než Gnome",
    "Gnome je mnohem pohodlnější než KDE",
    "Zlaté vodiče v kabelu sluchátek poskytují znatelně věrnější poslech",
    "90% pravicových politiků jsou zločinci",
    "90% levicových politiků jsou zločinci",
    "Varování: nacházíte se v patogenní zóně",
    "Bůh prokazatelně existuje",
    "Bůh prokazatelně neexistuje",
    "Naprostá většina lidí by se uzdravila i bez zásahu léků.",           //Ponkrác
    "Mluvím naprostou pravdu",
    "Všechny moje informace jsou ověřené skupinou nezávislých odborníků",
    "Malá kovová pyramida umí přes noc nabrousit žiletku",
    "Vytvořením pirátské kopie díla uchází jeho autorům zisk",
    "Regulace internetu pomůže v boji proti terorismu a zneužívání dětí",
    "Klima se globálně otepluje",
    "Klima se globálně neotepluje",
    "C++ je rychlejší než C",
    "Arch je nejlepší linuxová distribuce",
    "Ubuntu je nejlepší linuxová distribuce",
    "Debian je nejlepší linuxová distribuce",
    "MS Windows jsou již dnes velmi dobře zabezpečené",
    "Nebojte se, je to absolutně bezpečné",
    NULL
  },
};

/* The main chardev fortune reading function */

static ssize_t ku_read(struct file * file, char * buf, size_t count,
                       loff_t *ppos, const int fortune_number)
{
  unsigned int rand, buf_pos, str_pos, i, last_cpy, num;
  const char **fortune;
  int len;
  char buffer[ku_fortune_maxsize + ku_fortune_maxsize/ku_maxwidth + 1];

  if (*ppos < 0) return 0;

  get_random_bytes(&rand, sizeof(rand));  // Get random number and modulo it
  if (!ku_fortunes_nums[fortune_number]) return 0;
  rand %= ku_fortunes_nums[fortune_number];

  fortune = &ku_fortunes[fortune_number][rand];
  len = strlen(*fortune);                 // Don't include the null byte.

  if (!len) return 0;
  if (len > ku_fortune_maxsize) return -EINVAL;
  if (len > count) return -EINVAL;        // Only read the whole fortune at once.

  if (*ppos != 0)	return 0;               // Assume the string has been read
                                          // and indicate there is no more data to be read.

  //padding
  for(buf_pos = str_pos = last_cpy = 0; str_pos < len; str_pos++)
  {
    num = str_pos-last_cpy+1;
    if ((*fortune)[str_pos] == '\n')
    {
      //kopirovat od last_cpy po soucasny
      strncpy(buffer+buf_pos, (*fortune)+last_cpy, num);
      buf_pos += num;
      last_cpy = str_pos+1;
    } else if (num == ku_maxwidth)
    {
      //najit mezeru a zkopirovat po mezeru
      for(i = 0; i < ku_maxwidth; i++)
        if ((*fortune)[str_pos-i] == ' ')
        {
          //zkopirovat po str_pos-i a nahradit mezeru za \n
          str_pos -= i;
          num = str_pos-last_cpy+1;
          strncpy(buffer+buf_pos, (*fortune)+last_cpy, num);
          buf_pos += num;
          buffer[buf_pos-1] = '\n';
          last_cpy = str_pos+1;
          i = 0;
          break;
        }
      //kdyz se nenajde mezera (tzn. i != 0), zkopirovat po soucasnej a pridat \n
      // -pozn.: tahle cast poradně nefunguje (utf-8,...). Kaslu na to. Stejne to je extremni situace.
      // Ale nicemu to nevadi. Prinejhorsim ten text bude vypadat hnusne no, se nikdo snad nezblazni...
//      if (i)
//      {
//        //printk("num: %d\n", num);
//        strncpy(buffer+buf_pos, (*fortune)+last_cpy, --num);
//        buf_pos += num;
//        buffer[buf_pos++] = '\n';
//        last_cpy = str_pos+1;
//      }
    }
  } //zbyvajici
  if ((num = str_pos-last_cpy))
  {
    strncpy(buffer+buf_pos, (*fortune)+last_cpy, num);
    buf_pos += num;
  }
  buffer[buf_pos++] = '\n';

  if (copy_to_user(buf, buffer, buf_pos)) return -EINVAL;

  *ppos = buf_pos;                            // Tell the user how much data we wrote.
  return buf_pos;
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

  printk(KERN_INFO "KERNEL ULTRAS kernel module summoned\n");

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

  printk(KERN_INFO "KERNEL ULTRAS kernel module dismissed\n");

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
MODULE_DESCRIPTION("KERNEL ULTRAS kernel module");
MODULE_VERSION("prvni");
