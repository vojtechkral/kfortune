/*
 * KERNEL ULTRAS kernel module
 *
 * Big thanks to Valerie Henson for: / Mnohé díky Valerii Henson za:
 *   http://linuxdevcenter.com/pub/a/linux/2007/07/05/devhelloworld-a-simple-introduction-to-device-drivers-under-linux.html?page=1
 *
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
#define fortuna(text, url) (text"\n")
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
    //KU
    fortuna_sg("Jsem kernel panic, neni to lehky nesel bych za nic do naky /dev/ky...",
               "Kevin",
               "http://www.abclinuxu.cz/blog/Tuxuv_anal/2008/7/linuxove-ctyrversi/diskuse#36"),
    fortuna_sg("Tss, z MŠ mě pustili vo rok dřív, jak sem byl chytrej :P Už jsem uměl napsat PIVO i se zavázanýma očima.",
               "Darm",
               "http://www.abclinuxu.cz/blog/Saljack/2010/4/mala-pomoc-s-javou#43"),
    fortuna_sg("[Reakce na kalhotky C-String]\nJe to výbornej nápad, vohnul sem si naběračku na polívku, sklapnul půlky a teď tady v tom běhám po bytě... ",
               "Amigapower",
               "http://www.abclinuxu.cz/blog/puppylinux/2010/9/cstring#1"),
    fortuna_sg("Tím šťastlivcem se stal soutěžící kotyz, který s odpovědí přispěchal jen několik minut po vyhlášení soutěže a dostane tedy téměř dvoumetrové plátno s leteckým snímkem centra Sydney s rozlišením 8 541 x 4 357 pixelů.",
               "zive.cz",
               "http://www.zive.cz/bleskovky/vysledky-souteze-o-obrovsky-snimek-z-google-earth/sc-4-a-148305/default.aspx"),
    fortuna_sg("Ty mi děláš torčr!",
               "vlastikroot",
               "http://www.abclinuxu.cz/blog/HF/2010/7/reportaz-z-masters-of-rock"),
    fortuna_sg("...konzole pyčo, na tom se věšej záclony... :-D",
               "Amigapower",
               "http://www.abclinuxu.cz/blog/TryCatch/2010/2/jak-jsem-hledal-vyhovujici-terminal-pro-windows#55"),
    fortuna_sg("Sorry, ale když \"danajský dar\" vypadne z Amigapowera, \"nenormální přeumělkovaná řeč\" asi není na místě.",
               "Archer",
               "http://www.abclinuxu.cz/blog/merlins/2010/10/statni-maturita/diskuse#67"),
    fortuna_sg("Svoboda zůstává obrovskou překážkou pro růst moci.",
               "kralyk",
               "http://www.abclinuxu.cz/blog/yet_another_blog/2010/1/hudebni-prumysl-nas-bavi/diskuse#2"),
       fortuna("Jardík: Mně to přijde trhaný\n"
               "vlastikroot: Laguje ti mozek",
               "http://www.abclinuxu.cz/blog/Standovo/2009/10/windows-7-prvni-zklamani/diskuse#146"),
       fortuna("AsciiWolf: Chtělo by to upravit engine ábíčka tak, aby byl u blogů tag break vložen automaticky - před první obrázek, delší odstavec, atd...\n"
               "Amigapower: A aby ti to přineslo pivo k televizi nechceš?",
               "http://www.abclinuxu.cz/blog/txt/2010/10/zacala-tu-smrdet-mirna-cenzura-blogu.-koncim.-odchazim#43"),
       fortuna("belisarivs: A to mas jako dva holici strojky, jeden na vercajk a druhy na hubu?\n"
               "Amigapower: Přece si nebudu holit ptáka stejnym, víš kolik je na xichtu bakterií?!",
               "http://www.abclinuxu.cz/blog/BLB/2010/8/holite-si-pindoura#10"),
       fortuna("Amigapower: já chtěl k vánocům autolékárničku podle nový vyhlášky, ale stejně nemam vyměněnej řidičák, takže je to jedno...\n"
               "Grunt: Ty máš řidičák? Který blázen?\n"
               "Amigapower: To sis myslel, že jsem najezdil 300 000 km bez papírů?\n"
               "SPM: Auto jezdí na benzin, ne na papíry ;)",
               "http://www.abclinuxu.cz/blog/zabruceni/2010/12/gong-fu-cha/diskuse#72"),
       fortuna("gtz: a co Comix s nějakou kočkou\n"
               "Amigapower: V hlavě už je Comix s Marcelkou, bude mít děj se zápletkou ;-)\n"
               "kotyz: a bude dostatecne oplzlej?\n"
               "Amigapower: ...ty budeš Jaba",
               "http://www.abclinuxu.cz/blog/BLB/2010/12/comix-kernel-ultras-0002/diskuse#16"),
    fortuna_sg("Politika a zločin k sobě neodlučitelně patří ... asi jako zubaři a The Coca-Cola Company",
               "kralyk",
               "http://www.abclinuxu.cz/ankety/radio#83"),
//    fortuna_sg("Ba dokonce to není ani smilstvo, což je s podivem, páč smilstvo je dnes snad úplně všecko.",
//               "kralyk",
//               "http://www.abclinuxu.cz/blog/zblepty/2009/7/proc-nebudu-volit-ceskou-piratskou-stranu/diskuse#555"),
    fortuna_sg("Příští semestr si zapíšu předmět Administrace Windows jen proto, abych tam mohl přijít v triku KU.",
               "kralyk",
               "http://www.abclinuxu.cz/blog/BLB/2010/10/hleda-se-kernel-ultras-logistic-manager/diskuse#10"),
    fortuna_sg("Po F# vyvinou G♭, ale zjistí, že vyvinuli úplně to stejné...",
               "kralyk",
               "http://www.abclinuxu.cz/zpravicky/f-2.0-uvolnen-pod-open-source-licenci/diskuse#13"),
    fortuna_sg("Vždycky se najde eskymák, který vydá směrnici pro obyvatele rovníkové Afriky, jak se mají chovat v případě vlny neobvyklých veder.",
               "Dalibor Smolík",
               "http://www.abclinuxu.cz/blog/Mig_Alley/2010/10/kuba-zehli-7-kotev/diskuse#51"),
    fortuna_sg("Asi stárnu, ale pro mě je odpočinek také procházka se psem. Ten nikdy nezklame. Zejména, když je krásně a člověk se může dívat na krásy přírody, zejména pokud mají minisukně. :-)",
               "Dalibor Smolík",
               "http://www.abclinuxu.cz/blog/BLB/2010/11/osude-mam-z-tebe-strach/diskuse#58"),
    fortuna_sg("Marcelka je prostě docela dobrá samička, jenže s pavučinou přes lízátko, někde si to vybít holt musí...",
               "Amigapower",
               "http://www.abclinuxu.cz/blog/Strider_BSD_koutek/2010/10/svoboda-pro-vsechny/diskuse#167"),
    fortuna_sg("Profil - kokot\n"
               "Pokud jste kokot, přihlaste se a bude vám zobrazena vaše domovská stránka",
               "AsciiWolf",
               "http://www.abclinuxu.cz/blog/Mostly_IMDB/2010/10/sedm-kotev-aneb-jak-se-v-sedmi-krocich-zesmesnit/diskuse#223"),
    fortuna_sg("Asimetrický. Asi metr vysoký, asi metr široký :-)",
               "Dalibor Smolík",
               "http://www.abclinuxu.cz/blog/kotyzblog/2010/12/alkehol-ultras-comix-remix-by-kotyz#20"),
    fortuna_sg("ty vole! až do 21.12.2012 jen čínský polivky",
               "Amigapower",
               "http://www.abclinuxu.cz/blog/BLB/2011/2/dekujete-odchazite-tahnete-a-uz-se-nevracejte/diskuse#180"),
       fortuna("kotyz: je 17:30 dostatecne mimo misu? :-D\n"
               "kralyk: 17:30 obvykle bejvá míň mimo mísu než 14:00...",
               "http://www.abclinuxu.cz/blog/Untitled/2010/11/viceznacne-zasifrovani-a-autorsky-zakon/diskuse#21"),
       fortuna("qiRzT: Převést peníze na životy ještě nějak jde, ale opačně už to moc nejde. Jakou má cenu život dítěte?\n"
               "Dalibor Smolík: Když se narodí, má tak kolem 3,5 kg. Je to i s kostí, takže za to moc peněz nebude :)",
               "http://www.abclinuxu.cz/blog/pyx/2010/10/zrudy/diskuse#436"),
    //Tomeš
    fortuna_sg("Ty asi budeš retartovaný jako většina místní verbeže.",
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
    //Jardík
    fortuna_sg("Další 32bit šmejďárna...",
               "Jardík",
               "http://www.abclinuxu.cz/clanky/recenze/adobe-air-flash-a-ajax-na-desktopu#1"),
    fortuna_sg("Vidíte taky ty hnusný nekvalitní schodovitý přechody?",
               "Jardík",
               "http://www.abclinuxu.cz/desktopy/jardik-20091008"),
    fortuna_sg("Logo ubuntu vytejká z panelu.",
               "Jardík",
               "http://www.abclinuxu.cz/desktopy/xkucf03-20100516#1"),
    //etc
    fortuna_sg("Akorát si v jednom videu na YuTube někdo stěžoval, že mu při používání tohoto mikrofonu nestojí ve vzpřímené poloze (mikrofon).",
               "jirkaqwe",
               "http://www.abclinuxu.cz/poradna/hardware/show/315548#8"),
    fortuna_sg("A vida, echo z žumpy.",
               "podlesh",
               "http://www.abclinuxu.cz/blog/doli/2009/5/miting-cssd-na-andelu#584"),
    fortuna_sg("Wokna mnohým lidem vyhovujou víc. Ovšem ne proto, že by byly lepší, ale proto, že tito uživatelé jsou horší a platí svůj k svému ;)",
               "Ash",
               "http://www.abclinuxu.cz/ankety/microsoft-codeplex/diskuse#33"),
       fortuna("AltOS: Bah! Az budou dlouhe zimni vecery, tak bych snad uz opravdu mohl \"vytvorit\" tu novou distribuci Gambrinux...\n"
               "Marek Stopka: Chtěl si říct Gambribuntu, ne?\n"
               "AltOS: Ne, chtel jsem rici openGAMBRINUSE.",
               "http://www.abclinuxu.cz/blog/puppylinux/2008/9/gnu-linux-vs-pivo#10"),
    fortuna_sg("S platností do konce roku budu vzorným křesťanem, nebudu číst Dawkinse, vzývat Cthulhu a přestanu zabíjet koťatka.",
               "kyknos",
               "http://www.abclinuxu.cz/blog/quid_novi/2009/12/prechazim-na-krestanstvi"),
    fortuna_sg("Nemůžete publikovat v minulosti!",
               "abclinuxu.cz",
               "http://www.abclinuxu.cz/blog/Untitled/2010/10/kernel-modul-kernel-ultras/diskuse#57"),
    fortuna_sg("Já myslel že seznamka už tady dávno je - vždyť je tady všude odkaz \"Sbalit\" :-O",
               "podlesh",
               "http://www.abclinuxu.cz/blog/ze_zivota/2005/11/abcseznamka-ze-by-feature-request-.o#35"),
    fortuna_sg("Pokud jsi to vyčetl z hvězd, možná by stálo za to vyčistit si dalekohled.",
               "trekker.dk",
               "http://www.abclinuxu.cz/blog/engineering/2009/3/nove-blogy-na-linuxsoftu/diskuse#33"),
    fortuna_sg("Windows jsou jako hamburger. Drahé, zbytečné, ale kupuje ho většina lidí.",
               "Sten (anonym)",
               "http://www.abclinuxu.cz/zpravicky/linuxove-distribuce-prirovnany-k-jidlu#19"),
    fortuna_sg("MacOS X je jako kaviár. Extrémně drahý, chutná divně a lidé, co na něj mají, se jím rádi chlubí.",
               "Sten (anonym)",
               "http://www.abclinuxu.cz/zpravicky/linuxove-distribuce-prirovnany-k-jidlu#19"),
    fortuna_sg("Měříme si s kamarády uptime a pak si je ve škole porovnáváme, kdo ho má delší.",
               "Jenda",
               "http://www.abclinuxu.cz/blog/Untitled/2010/10/kernel-modul-kernel-ultras/diskuse#63"),
    fortuna_sg("Z Van Helsinga bys měl snad už vědět, že Praha a Budapešť jsou dvě jména pro totéž.",
               "Nicky 726",
               "http://www.abclinuxu.cz/blog/itckar/2010/10/microsoft-vs.-skoda-120/diskuse#39"),
    fortuna_sg("Kazdopadne nema zmysel riesit, preco sa z francuzkej skodovky v madarsku dymi z predu v reklame na shitosofti telefon :-)",
               "msk",
               "http://www.abclinuxu.cz/blog/itckar/2010/10/microsoft-vs.-skoda-120/diskuse#21"),
    fortuna_sg("Je tu někdo z USA? Nechcete si patentovat \"Patentování dlouhodobě a obecně používané technologie za účelem získání výhodné právní pozice\" ?",
               "Jakub Lucký",
               "http://www.abclinuxu.cz/zpravicky/kodovani-videa-na-gpu-patentovano/diskuse#13"),
    fortuna_sg("Držte svůj e-penis stále největší",
               "Jakub Lucký",
               "http://www.abclinuxu.cz/blog/Untitled/2010/10/kernel-modul-kernel-ultras/diskuse#61"),
       fortuna("V (anonym):  Obhospodarovat Arch je ako spat s vlastnou zenou - nenadchne, ale postaci...\n"
               "Karel Benák: Dovoluju si nesouhlasit, jsem ženatý 5 let a jsem stále nadšený :-)\n"
               "kyytaM:      Z Archu, ci zo zeny?",
               "http://www.abclinuxu.cz/blog/dw/2010/10/novy-ubuntu-font-je-fajn/diskuse#22"),
       fortuna("Jardík: Řetězce musí bít zarovnané vpravo.\n"
               "tsLnox: A sakra. Jdu se zarovnat vlevo, nerad bych dostal řetězcem.",
               "http://www.abclinuxu.cz/blog/jarda_bloguje/2010/11/gcc-umi-assembler-lepe-nez-ja#114"),
       fortuna("Zprávička: Text ACTA byl vyjednán\n"
               "heh: vzhuru na barikády....\n"
               "Piškot: Co si to nejdříve přečíst ?\n"
               "JS: Prectes si to na barikade. :-)",
               "http://www.abclinuxu.cz/zpravicky/text-acta-byl-vyjednan#5"),
       fortuna("Zprávička: Francouzská policie přechází na Ubuntu\n"
               "heh: Po dlouhé době jedna smysluplná zprávička obsahující slovo \"ubuntu\".\n"
               "Semo: To zas Ubuntu krivdis. Ale je to po dlhej dobe jedna zmysluplna spravicka obsahujuca slovo \"francuzska\".",
               "http://www.abclinuxu.cz/zpravicky/francouzska-policie-prechazi-na-ubuntu/diskuse#4"),
       fortuna("Dalibor Smolík: Když mi bylo 14 let, měli lékaři u mě podezření na lepru. Vážně, Dálný Východ .. naštěstí planý poplach.\n"
               "jozka: Asi bys byl jediny ctrnactilety, ktery by se neradoval z toho, ze mu neco odpadne...",
               "http://www.abclinuxu.cz/zpravicky/diakritika-v-.cz-domenach-opet-odmitnuta#34"),
       fortuna("Marcela: Co jste to zas vytvrořil, brouku? Nechcete taky někdy zprznit sám sebe? :-)\n"
               "Bedňa: Treba priložiť prst na klitoris a použíť rýchle pohyby. Hovorí sa tomu stimulácia nervových zakončení :-)\n"
               "Amigapower: :-D  Já bych toho Paní (Slečnu???????????? Jak to vlastně je???????????) Marcelu třeba i dobrovolně ušetřil ;-)\n"
               "Bedňa: To ja bysom som si tu stimuláciu minimálne rád pozrel :-)\n"
               "Amigapower: 10€\n"
               "Bedňa: To beru",
               "http://www.abclinuxu.cz/blog/BLB/2011/1/comix-kernel-ultras-0019/diskuse#14"),
       fortuna("Honz: K čemu je na ploše nějaká cizí pipka...?\n"
               "vain: No přece si tam nebudeš dávat tu vlastní, kterou vidíš taky každý den ;-)",
               "http://www.abclinuxu.cz/desktopy/drobek1015-20110128#11"),
       fortuna("Hrabě Nikolič: Jak je to s tou rybou? <o))<\n"
               "Mti.: Ryba smrdi od hlavy?\n"
               "mirec: Ryba smrdí od ropy.",
               "http://www.abclinuxu.cz/clanky/komiks-xkcd-857-archimedes/diskuse#51"),
       fortuna("xurfa: Klidek, to jen IPv6 fanatici vytvareji paniku, aby donutili vsechny ostatni prejit na ten jejich nedodelanej zmetek...\n"
               "kip: Proč lozit do záchranných člunů, když orchestr pořád hraje, že?",
               "http://www.abclinuxu.cz/zpravicky/v-regionu-asie-pacific-dojdou-v-dubnu-ipv4-adresy/diskuse#10"),
    fortuna_sg("Ale tohle je portál o Linuxu - lidi se tedy v diskuzi pravděpodobně budou chtít bavit o něm. Kdyby lidé chtěli řešit politiku, půjdou jinam.",
               "SPM",
               "http://www.abclinuxu.cz/blog/plastique/2010/10/liberix-ukazal-skolakum-ubuntu/diskuse#101"),
    fortuna_sg("Nakonec jsem rád, že se tu konkrétně tahle zprávička objevila, protože je zajímavá narozdíl od zpráviček, že zítra, pozítří a včera výjde třetí alpha verze Ubuntu Horny Heron.",
               "Neel",
               "http://www.abclinuxu.cz/zpravicky/ubuntu-prestane-pouzivat-x.org/diskuse#82"),
    fortuna_sg("SP k Windows prý mají přinést volbu Disable DDOS, kterou když uživatel zaškrtne, tak bude chráněn. Zkusím zapátrat na Slunečnici jestli tam na to není už nějaká utilitka.",
               "Xerces (anonym)",
               "http://www.abclinuxu.cz/zpravicky/jak-se-vyporadat-s-ddos/diskuse#2"),
    fortuna_sg("Chtělo by to nějaké prášky proti lenosti.",
               "xkucf03",
               "http://www.abclinuxu.cz/blog/jezkova_nora/2010/12/zda-se-mi-to/diskuse#39"),
    fortuna_sg("To já měl zas \"Ha! Láva fárá\". Pomůcka do Německy s úsměvem pro časování nepravidelných sloves. Funguje asi z 85 %, což bylo vhledem k hranici testu 50 %, zcela dostačující.",
               "Voty",
               "http://www.abclinuxu.cz/clanky/komiks-xkcd-809-los-alamos/diskuse#37"),
    fortuna_sg("Já to začal psát do HTML dokumentů:\n  <meta name=\"generator\" content=\"Zmagnetizovana jehla, osciloskop, pevna ruka a dokumentace ext3 :-)\">",
               "Jenda",
               "http://www.abclinuxu.cz/clanky/komiks-xkcd-378-opravdovi-programatori/diskuse#3"),
    fortuna_sg("Troll krmí trolla. Tohle jsem nikdy nepochopil jak může fungovat. To je jako když zloděje okrade zloděj. Nebo jako když markeťák naletí na reklamu.",
               "Grunt",
               "http://www.abclinuxu.cz/zpravicky/nokia-a-microsoft-zahajuji-spolupraci/diskuse#33"),
    fortuna_sg("už sem Android měl v prackách na mašince od THC",
               "Migilenik",
               "http://www.abclinuxu.cz/blog/odi_et_amo/2011/2/pohreb-pro-nokia-a-meego-...-r.i.p/diskuse#20"),
    fortuna_sg("Hurdisky jsou asi média s Hurdem.",
               "Jenda",
               "http://www.abclinuxu.cz/blog/Untitled/2011/2/vize-verbalniho-computingu/diskuse#14"),
    fortuna_sg("Dobrý pant se hmatem nepozná. Pozná se ale olizem, musí studit.",
               "frdrx",
               "http://www.abclinuxu.cz/blog/reklamace/2010/7/prosba-o-radu#50"),
    fortuna_sg("Mluvíte jako zapšklý dědek co závidí JJ živelnost, nadání a sex-appeal.",
               "Marcela",
               "http://www.abclinuxu.cz/blog/Strider_BSD_koutek/2010/8/napred-zabava-a-az-potom-prace/diskuse#400"),
    fortuna_sg("Zakaznici SONY nepotrebuji sex. Uz z nima vymrdala SONY.",
               "belisarivs",
               "http://www.abclinuxu.cz/zpravicky/sony-ma-pravo-ziskat-informace-o-uzivatelich-geohot.com/diskuse#14"),
    fortuna_sg("Když může měsíc způsobovat menstruaci, proč by nemohl způsobit zemětřesení? :-) By mě zajímalo, co z toho působí větší škody.",
               "frdrx",
               "http://www.abclinuxu.cz/blog/Annals_of_Taurelador/2011/3/mesic-a-zemetreseni/diskuse#20"),
    fortuna_sg("to je zenska pivo a mliko :-D",
               "kotyz",
               "http://www.abclinuxu.cz/blog/kotyzblog/2010/12/alkehol-ultras-comix-remix-by-kotyz#3"),
    fortuna_sg("$ TIME=12PM autoconf --with-knife --with-bible --sacrifice-dog",
               "Kyosuke",
               "http://www.abclinuxu.cz/clanky/programovani/cmake-zjednodus-si-zivot#3"),
    fortuna_sg("Pritelkyne ukladejte do tmp. Nikdy do home!",
               "Dag",
               "http://www.abclinuxu.cz/blog/instalace_vmwaru/2006/3/14/124648#8"),
    fortuna_sg("Nemohu nevzpomenout onen pruzkum, ktery prisel na to, ze zhruba polovina mladych lidi vidi svou situaci optimisticky. Druha polovina nema na drogy penize.",
               "jozka",
               "http://www.abclinuxu.cz/blog/martinek/2011/4/ziji-v-krasne-dobe/diskuse#34"),
    fortuna_sg("Donutili jste mne podivat se do analu :-)",
               "jozka",
               "http://www.abclinuxu.cz/blog/merlins/2011/4/ceny-benzinu/diskuse#145"),
    fortuna_sg("asociál: člověk s vlastním názorem, jedinec nepodléhající tlaku davové psychologie, osobnost mimo úředně schválené sociální kategorie.",
               "rADOn",
               "http://www.abclinuxu.cz/blog/bloK/2011/3/joke-of-the-day/diskuse#5"),
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
    "Fedora je nejlepší linuxová distribuce",
    "OpenSuse je nejlepší linuxová distribuce",
    "Slackware je nejlepší linuxová distribuce",
    "MS Windows jsou již dnes velmi dobře zabezpečený systém",
    "Nebojte se, je to absolutně bezpečné",
    "01:56:43 up 327 days, 6:41, 1 user, load average: 0.46, 0.51, 0.57",
    "14:07:29 up 291 days, 16:21, 1 user, load average: 0.41, 0.52, 0.77",
    "15:16:59 up 488 days, 6:21, 1 user, load average: 0.40, 0.49, 0.55",
    "640kB by mělo stačit všem",
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
    //printk(KERN_INFO "kumodule: sizeof[%u]: %u\n", i, j);
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
