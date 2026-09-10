/*
*
*   file: ws2812_drv.c
*   date: 2024-09-03
*   notice:
*       ws2812驱动，适用于瑞芯微RK356x和RK3588系列，目前只在野火鲁班猫平台验证过。
*	usage:
*		1、编译前，需根据目标芯片，打开对应宏定义。
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/spinlock.h>

#define DEV_NAME            			"ws2812"
#define LED_NUM_MAX						(20)
//#define DEBUG

/* 根据目标芯片，打开对应宏定义 */
//#define RK3528
//#define RK356x
//#define RK3562
#define RK3576
//#define RK3588

#if defined(RK3528)
/* RK3528 GPIO BASE */
#define GPIO0_BASE_ADDR					UL(0xFF210000)
#define GPIO1_BASE_ADDR					UL(0xFF220000)
#define GPIO2_BASE_ADDR					UL(0xFF230000)
#define GPIO3_BASE_ADDR					UL(0xFF240000)
#elif defined(RK356x)
/* RK356x GPIO BASE */
#define GPIO0_BASE_ADDR					UL(0xFDD60000)
#define GPIO1_BASE_ADDR					UL(0xFE740000)
#define GPIO2_BASE_ADDR					UL(0xFE750000)
#define GPIO3_BASE_ADDR					UL(0xFE760000)
#define GPIO4_BASE_ADDR					UL(0xFE770000)
#elif defined(RK3562)
/* RK3562 GPIO BASE */
#define GPIO0_BASE_ADDR					UL(0xFF260000)
#define GPIO1_BASE_ADDR					UL(0xFF620000)
#define GPIO2_BASE_ADDR					UL(0xFF630000)
#define GPIO3_BASE_ADDR					UL(0xFFac0000)
#define GPIO4_BASE_ADDR					UL(0xFFad0000)
#elif defined(RK3576)
/* RK3576 GPIO BASE */
#define GPIO0_BASE_ADDR					UL(0x27320000)
#define GPIO1_BASE_ADDR					UL(0x2AE10000)
#define GPIO2_BASE_ADDR					UL(0x2AE20000)
#define GPIO3_BASE_ADDR					UL(0x2AE30000)
#define GPIO4_BASE_ADDR					UL(0x2AE40000)
#elif defined(RK3588)
/* RK3588 GPIO BASE */
#define GPIO0_BASE_ADDR					UL(0xFD8A0000)
#define GPIO1_BASE_ADDR					UL(0xFEC20000)
#define GPIO2_BASE_ADDR					UL(0xFEC30000)
#define GPIO3_BASE_ADDR					UL(0xFEC40000)
#define GPIO4_BASE_ADDR					UL(0xFEC50000)
#else
#error "必须定义一个目标芯片宏: RK3528/RK356x/RK3562/RK3576/RK3588"
#endif

/* GPIO Level REG OFFSET */
#define GPIO_SWPORT_DR_L_OFFSET			(0x0000)
#define GPIO_SWPORT_DR_H_OFFSET			(0x0004)
/* GPIO Direction REG OFFSET */
#define GPIO_SWPORT_DDR_L_OFFSET		(0x0008)
#define GPIO_SWPORT_DDR_H_OFFSET		(0x000C)

static volatile unsigned int *GPIO_DIR_REG;
static volatile unsigned int *GPIO_LEVEL_REG;

struct ws2812_mes {
    unsigned int gpiochip;      		// data引脚的gpiochip
    unsigned int gpionum;       		// data引脚的gpionum
    unsigned int lednum;        		// 起始LED序号，从1开始
    unsigned int ledcount;      		// 要连续控制的LED数量
    unsigned char color[LED_NUM_MAX][3];	// 每个LED的 R:G:B，color[i][0]=R [1]=G [2]=B
};

static int major = 0;
static struct class *ws2812_class;
static int bit;
static unsigned int temp;

/* 自旋锁：保护WS2812时序临界区，防止中断/抢占打断bit-banging */
static DEFINE_SPINLOCK(ws2812_lock);

static int ws2812_drv_open(struct inode *node, struct file *file)
{
	return 0;
}

static void ws2812_reset(void)
{
	/* Reset: 拉低 > 280us，给足余量防止udelay偏短导致数据未latch */
	temp &= (~(1 << bit));
	*GPIO_LEVEL_REG = temp;
	udelay(500);
}

/*
 * 宏定义寄存器写堆叠，编译时展开，无函数调用开销。
 * 每次写耗时取决于CPU频率，必须固定CPU频率才能保证时序稳定。
 */
#define W1  *GPIO_LEVEL_REG = temp;
#define W2  W1 W1
#define W4  W2 W2
#define W8  W4 W4
#define W16 W8 W8

/*
 * 宏定义0码/1码，编译时内联展开，消除函数调用开销。
 * 时序次数按实测标定(每次写约50ns)：
 *   T0H(6次)≈300ns  T0L(16次)≈800ns
 *   T1H(16次)≈800ns T1L(6次)≈300ns
 * 换平台/换频率需重新标定次数。
 */
#define WRITE_FRAME_0()  do {                  \
    temp |= 1 << bit;   /* 拉高 */              \
    W4 W2               /* T0H: 6次 */          \
    temp &= (~(1 << bit)); /* 拉低 */           \
    W16                 /* T0L: 16次 */         \
} while(0)

#define WRITE_FRAME_1()  do {                  \
    temp |= 1 << bit;   /* 拉高 */              \
    W16                 /* T1H: 16次 */         \
    temp &= (~(1 << bit)); /* 拉低 */           \
    W4 W2               /* T1L: 6次 */          \
} while(0)

static void ws2812_write_byte(unsigned char byte)
{
	int i = 0;

	for(i = 0; i < 8; i++)
	{
		if((byte << i) & 0x80)
			WRITE_FRAME_1();
	  	else
			WRITE_FRAME_0();
	}
}

/*
 * GPIO配置函数：完成ioremap + 方向设置 + 初始电平设置
 * 注意：此处可能睡眠(ioremap)，不能在自旋锁内调用。
 */
static int ws2812_setup_gpio(unsigned int gpiochip, unsigned int gpionum)
{
	int step;

	if(gpiochip > 4)
	{
		printk(KERN_ERR"ws2812.gpiochip must >= 0 && <= 4\n");
		return -1;
	}
	if(gpionum > 31)
	{
		printk(KERN_ERR"ws2812.gpionum must >= 0 && <= 31\n");
		return -1;
	}

	/* step : 0-15使用GPIO_SWPORT_DR_L_OFFSET，16-31使用GPIO_SWPORT_DR_H_OFFSET */
	step = gpionum / 16;
	if(gpiochip == 0)
	{
		GPIO_DIR_REG	= ioremap(GPIO0_BASE_ADDR + GPIO_SWPORT_DDR_L_OFFSET+(step*(0x4)), 4);
		GPIO_LEVEL_REG 	= ioremap(GPIO0_BASE_ADDR + GPIO_SWPORT_DR_L_OFFSET+(step*(0x4)), 4);
	}
	else
	{
		GPIO_DIR_REG	= ioremap(GPIO1_BASE_ADDR + (0x10000*(gpiochip-1)) + GPIO_SWPORT_DDR_L_OFFSET+(step*(0x4)), 4);
		GPIO_LEVEL_REG	= ioremap(GPIO1_BASE_ADDR + (0x10000*(gpiochip-1)) + GPIO_SWPORT_DR_L_OFFSET+(step*(0x4)), 4);

#ifdef DEBUG
		printk("GPIO_DIR_REG : 0x%lX\n", GPIO1_BASE_ADDR + (0x10000*(gpiochip-1)) + GPIO_SWPORT_DDR_L_OFFSET+(step*(0x4)));
		printk("GPIO_LEVEL_REG : 0x%lX\n", GPIO1_BASE_ADDR + (0x10000*(gpiochip-1)) + GPIO_SWPORT_DR_L_OFFSET+(step*(0x4)));
#endif
	}

	if(GPIO_LEVEL_REG == NULL || GPIO_DIR_REG == NULL)
	{
		printk(KERN_ERR"GPIO_LEVEL_REG or GPIO_DIR_REG is NULL\n");
		return -1;
	}

	bit = gpionum % 16;

	/* 设置GPIO模式为Output */
	temp = *GPIO_DIR_REG;
	temp |= 1 << (16 + bit);
	temp |= 1 << bit;
	*GPIO_DIR_REG = temp;

	/* 设置GPIO初始电平为低电平 */
	temp = *GPIO_LEVEL_REG;
	temp |= 1 << (16 + bit);
	temp &= (~(1 << bit));
	*GPIO_LEVEL_REG = temp;

	return 0;
}

static ssize_t ws2812_drv_write(struct file *filp, const char __user * buf, size_t count, loff_t * ppos)
{
	int err;
	struct ws2812_mes ws2812_usr;
	int i = 1;
	unsigned long flags;

	err = copy_from_user(&ws2812_usr, buf, sizeof(ws2812_usr));
	if(err != 0)
	{
		printk(KERN_ERR"get ws2812 struct err!\n");
		return err;
	}

#ifdef DEBUG
	printk("ws2812_usr.gpiochip : %d\n", ws2812_usr.gpiochip);
	printk("ws2812_usr.gpionum : %d\n", ws2812_usr.gpionum);
	printk("ws2812_usr.lednum : %d\n", ws2812_usr.lednum);
	printk("ws2812_usr.ledcount : %d\n", ws2812_usr.ledcount);
	{
		int k;
		for(k = 0; k < ws2812_usr.ledcount; k++)
			printk("color[%d] : R=%d G=%d B=%d\n", k,
				ws2812_usr.color[k][0], ws2812_usr.color[k][1], ws2812_usr.color[k][2]);
	}
#endif

	if(ws2812_usr.lednum < 1 || ws2812_usr.lednum > LED_NUM_MAX)
	{
		printk(KERN_ERR"ws2812.lednum must >= 1 && <= %d\n", LED_NUM_MAX);
		return -1;
	}
	if(ws2812_usr.ledcount < 1 || ws2812_usr.ledcount > LED_NUM_MAX)
	{
		printk(KERN_ERR"ws2812.ledcount must >= 1 && <= %d\n", LED_NUM_MAX);
		return -1;
	}
	if(ws2812_usr.lednum + ws2812_usr.ledcount - 1 > LED_NUM_MAX)
	{
		printk(KERN_ERR"ws2812.lednum + ledcount must <= %d\n", LED_NUM_MAX);
		return -1;
	}

	err = ws2812_setup_gpio(ws2812_usr.gpiochip, ws2812_usr.gpionum);
	if(err != 0)
		return err;

	/* 使用spin_lock_irqsave同时关闭中断和抢占，保护整个数据帧发送过程 */
	spin_lock_irqsave(&ws2812_lock, flags);

	ws2812_reset();

	/*
	 * 第一个bit紧跟reset的udelay后立即拉高，不经过for循环判断/函数调用，
	 * 消除~440ns开销导致的第一个码高电平超标。
	 * 宏WRITE_FRAME_0/1编译时内联展开，无函数调用开销，所有bit开销一致。
	 * 第一个byte = (lednum>1) ? 0x00(填充) : color[0][G]
	 */
	{
		unsigned char first_byte = (ws2812_usr.lednum > 1) ? 0x00 : ws2812_usr.color[0][1];
		int j;

		/* 立即处理第一个bit，紧跟udelay，无循环/调用开销 */
		if((first_byte << 0) & 0x80)
			WRITE_FRAME_1();
		else
			WRITE_FRAME_0();

		/* 后续7个bit用for循环+宏展开 */
		for(j = 1; j < 8; j++)
		{
			if((first_byte << j) & 0x80)
				WRITE_FRAME_1();
			else
				WRITE_FRAME_0();
		}
	}

	/* 发送剩余bytes */
	if(ws2812_usr.lednum > 1)
	{
		/* 第一个填充LED剩余2字节(0x00, 0x00) */
		ws2812_write_byte(0x00);
		ws2812_write_byte(0x00);
		/* 中间填充LED全部填黑 (LED 2 ~ lednum-1) */
		for(i = 2; i < ws2812_usr.lednum; i++)
		{
			ws2812_write_byte(0x00);
			ws2812_write_byte(0x00);
			ws2812_write_byte(0x00);
		}
		/* 目标LED：依次发送每个LED的 G R B */
		for(i = 0; i < ws2812_usr.ledcount; i++)
		{
			ws2812_write_byte(ws2812_usr.color[i][1]);	// color G
			ws2812_write_byte(ws2812_usr.color[i][0]);	// color R
			ws2812_write_byte(ws2812_usr.color[i][2]);	// color B
		}
	}
	else
	{
		/* lednum==1，第一个目标LED只剩R和B两字节 */
		ws2812_write_byte(ws2812_usr.color[0][0]);		// color R
		ws2812_write_byte(ws2812_usr.color[0][2]);		// color B
		/* 剩余目标LED (LED 2 ~ ledcount) */
		for(i = 1; i < ws2812_usr.ledcount; i++)
		{
			ws2812_write_byte(ws2812_usr.color[i][1]);	// color G
			ws2812_write_byte(ws2812_usr.color[i][0]);	// color R
			ws2812_write_byte(ws2812_usr.color[i][2]);	// color B
		}
	}

	ws2812_reset();

	spin_unlock_irqrestore(&ws2812_lock, flags);

#ifdef DEBUG
	printk("ws2812 write over!\n");
#endif

	return 0;
}

static int ws2812_drv_close(struct inode *node, struct file *file)
{	
	iounmap(GPIO_LEVEL_REG);
	iounmap(GPIO_DIR_REG);

	GPIO_LEVEL_REG = NULL;
	GPIO_DIR_REG = NULL;

	return 0;
}

static struct file_operations ws2812_fops = {
	.owner = THIS_MODULE,
	.open = ws2812_drv_open,
	.release = ws2812_drv_close,
	.write = ws2812_drv_write,
};

static __init int ws2812_init(void)
{
	printk(KERN_INFO"Load the ws2812 module successfully!\n");

	major = register_chrdev(0, "rk_ws2812", &ws2812_fops);  

	ws2812_class = class_create(THIS_MODULE, "rk_ws2812_class");
	if (IS_ERR(ws2812_class)) {
		printk(KERN_ERR"%s %s line %d\n", __FILE__, __FUNCTION__, __LINE__);
		unregister_chrdev(major, "rk_ws2812");
		return PTR_ERR(ws2812_class);
	}

	device_create(ws2812_class, NULL, MKDEV(major, 0), NULL, "ws2812");

	return 0;
}
module_init(ws2812_init);

static __exit void ws2812_exit(void)
{
	printk(KERN_INFO"the ws2812 module has been remove!\n");

	device_destroy(ws2812_class, MKDEV(major, 0));
	class_destroy(ws2812_class);
	unregister_chrdev(major, "rk_ws2812");

	if(GPIO_LEVEL_REG != NULL)
		iounmap(GPIO_LEVEL_REG);
	if(GPIO_DIR_REG != NULL)
		iounmap(GPIO_DIR_REG);
	GPIO_LEVEL_REG = NULL;
	GPIO_DIR_REG = NULL;
}
module_exit(ws2812_exit);

MODULE_AUTHOR("embedfire");
MODULE_LICENSE("GPL");
