#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/slab.h>
#include <asm/uaccess.h>

#include <linux/time.h>
#include <linux/timer.h>

#include <linux/gpio.h>
#include <linux/moduleparam.h>

#include <linux/interrupt.h>
#include <linux/irq.h>

#include <linux/wait.h>
#include <linux/poll.h>

#include "ioctl_test.h"

#define KERNELTIMER_DEV_NAME "kerneltimer_dev"
#define KERNELTIMER_DEV_MAJOR 230

#define OFF 0
#define ON 1
#define GPIOCNT 8
#define DEBUG

static int timerVal = 100;	//f=100HZ, T=1/100 = 10ms, 100*10ms = 1Sec
static int ledVal = 0;
struct timer_list timerLed;

module_param(ledVal,int ,0);
module_param(timerVal,int ,0);

static int gpioLed[] = {6,7,8,9,10,11,12,13};
static int gpioKey[] = {16,17,18,19,20,21,22,23};
typedef struct {
	int keyNumber;
	int key_irq[GPIOCNT];
} keyDataStruct;

void kerneltimer_registertimer(unsigned long timeover);
void kerneltimer_func(struct timer_list *t );

static long gpioLedInit(void);
static void gpioLedSet(long);
static void gpioLedFree(void);
static long gpioKeyInit(void);
static void gpioKeyFree(void);

static int gpioKeyIrqInit(keyDataStruct * pKeyData);
static void gpioKeyIrqFree(keyDataStruct * pKeyData);
irqreturn_t key_isr(int irq, void * data);
DECLARE_WAIT_QUEUE_HEAD(WaitQueue_Read);


static long gpioLedInit(void)
{
	long ret = 0;
	int i;
	char gpioName[10];
	for(i=0;i<GPIOCNT;i++)
	{
		sprintf(gpioName,"gpio%d",gpioLed[i]);
		ret = gpio_request(gpioLed[i],gpioName);
		if(ret < 0) {
#ifdef DEBUG
			printk("Failed request gpio%d error\n",gpioLed[i]);
#endif
			return ret;
		}
	}
	for(i=0;i<GPIOCNT;i++)
	{
		ret = gpio_direction_output(gpioLed[i],OFF);
		if(ret < 0) {
#ifdef DEBUG
			printk("Failed direction_output gpio%d error\n",gpioLed[i]);
#endif
			return ret;
		}
	}
	return ret;
}

static void gpioLedSet(long val)
{
	int i;
	for(i=0;i<GPIOCNT;i++)
	{
		gpio_set_value(gpioLed[i],val & (0x1 << i));
	}

}

static void gpioLedFree(void)
{
	int i;
	for(i=0;i<GPIOCNT;i++)
	{
  		gpio_free(gpioLed[i]);
	}
#ifdef DEBUG
	printk("gpio_free\n");
#endif
}

static long gpioKeyInit(void)
{
	long ret = 0;
	int i;
	char gpioName[10];
	for(i=0;i<GPIOCNT;i++)
	{
		sprintf(gpioName,"gpio%d",gpioKey[i]);
		ret = gpio_request(gpioKey[i],gpioName);
		if(ret < 0) {
#ifdef DEBUG
			printk("Failed request gpio%d error\n",gpioKey[i]);
#endif
			return ret;
		}
	}
	for(i=0;i<GPIOCNT;i++)
	{
		ret = gpio_direction_input(gpioKey[i]);
		if(ret < 0) {
#ifdef DEBUG
			printk("Failed direction_output gpio%d error\n",gpioKey[i]);
#endif
			return ret;
		}
	}
	return ret;
}

static void gpioKeyFree(void)
{
	int i;
	for(i=0;i<GPIOCNT;i++)
	{
  		gpio_free(gpioKey[i]);
	}
}

// gpio 인터럽트
static int gpioKeyIrqInit(keyDataStruct * pKeyData)
{
	int i;
	for(i=0;i<GPIOCNT;i++)
	{
		pKeyData->key_irq[i] = gpio_to_irq(gpioKey[i]);
		if(pKeyData->key_irq[i] < 0){
			printk("Failed gpio_to_irq() gpio%d error\n", gpioKey[i]);
			return pKeyData->key_irq[i];
		}
	}
	return 0;
}

// gpio 인터럽트 해제
static void gpioKeyIrqFree(keyDataStruct * pKeyData)
{
	int i;
	for(i=0;i<GPIOCNT;i++)
	{
		free_irq(pKeyData->key_irq[i],pKeyData);
	}
}

static int kerneltimer_open(struct inode *inode, struct file *filp)
{
	int i;
	int result;
    int num0 = MAJOR(inode->i_rdev); 
    int num1 = MINOR(inode->i_rdev); 
	char * irqName[GPIOCNT] = {"irqKey0","irqKey1","irqKey2","irqKey3","ir    qKey4","irqKey5","irqKey6","irqKey7"};
	keyDataStruct * pKeyData;

    printk( "kerneltimer open -> major : %d\n", num0 );
    printk( "kerneltimer open -> minor : %d\n", num1 );
	try_module_get(THIS_MODULE);

	pKeyData = (keyDataStruct *)kmalloc(sizeof(keyDataStruct),GFP_KERNEL);
    if(!pKeyData)
        return -ENOMEM;
    pKeyData->keyNumber = 0;

    result = gpioKeyIrqInit(pKeyData);
    if(result < 0)
        return result;

    for(i=0;i<GPIOCNT;i++)
    {
        result = request_irq(pKeyData->key_irq[i],key_isr,IRQF_TRIGGER_RISING,irqName[i],pKeyData);
        if(result < 0)
        {
            printk("request_irq() failed irq %d\n",pKeyData->key_irq[i]);
            return result;
        }
    }

    filp->private_data = pKeyData;
    return 0;
}

static loff_t kerneltimer_llseek(struct file *filp, loff_t off, int whence)
{
	printk("kerneltimer llseek -> off: %08X, whence: %08X\n",(unsigned int)off, whence);
	return 0x23;
}

static ssize_t kerneltimer_read(struct file *filp, char *buf, size_t count, loff_t *_pos)
{
	int ret;
	keyDataStruct * pKeyData = (keyDataStruct *)filp->private_data;
#ifdef DEBUG
	printk("kerneltimer read -> buf : %08X, count : %08X\n",(unsigned int)buf, count);
#endif
	if(pKeyData->keyNumber == 0)
	{
		if(!(filp->f_flags & O_NONBLOCK))
		{
			wait_event_interruptible(WaitQueue_Read,pKeyData->keyNumber);
		}
	}
	ret = copy_to_user(buf,&(pKeyData->keyNumber),count);
	if(ret < 0)
		return -EFAULT;
	
	if(pKeyData->keyNumber != 0)
		pKeyData->keyNumber = 0;

	return count;
}

static ssize_t kerneltimer_write(struct file *filp, const char *buf, size_t count, loff_t *_pos)
{
	char kbuf;
	int ret;
	ret = copy_from_user(&kbuf,buf,count);
	if(ret < 0)
		return -EFAULT;
	ledVal = kbuf;
	return count;
}

static long kerneltimer_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{

	keyled_data ctrl_info;
	int err=0, size;
	if( _IOC_TYPE( cmd ) != IOCTLTEST_MAGIC ) return -EINVAL;
	if( _IOC_NR( cmd ) >= IOCTLTEST_MAXNR ) return -EINVAL;

	size = _IOC_SIZE( cmd );
	if( size )
	{
		if( _IOC_DIR( cmd ) & _IOC_READ )
			err = access_ok( (void *) arg, size );
		if( _IOC_DIR( cmd ) & _IOC_WRITE )
			err = access_ok( (void *) arg, size );
		if( !err ) return err;
	}
	switch( cmd )
	{
		case TIMER_START :
			if(timer_pending(&timerLed))
				break;
			kerneltimer_registertimer(timerVal);
			break;
		case TIMER_STOP :
			if(timer_pending(&timerLed))
				del_timer(&timerLed);
			break;
		case TIMER_VALUE :
			if(timer_pending(&timerLed))
				del_timer(&timerLed);
			err = copy_from_user((void *)&ctrl_info,(void *)arg,size);
			if (err != 0){
				return -EFAULT;
			}
			timerVal = ctrl_info.timer_val;
			kerneltimer_registertimer(timerVal);
			break;
		default:
			err =-E2BIG;
			break;
	}	
	return err;
}

static __poll_t kerneltimer_poll(struct file * filp, struct poll_table_struct * wait)
{

	unsigned int mask=0;
    keyDataStruct * pKeyData = (keyDataStruct *)filp->private_data;
#ifdef DEBUG
    printk("_key : %u\n",(wait->_key & POLLIN));
#endif
    if(wait->_key & POLLIN)
        poll_wait(filp, &WaitQueue_Read, wait);
    if(pKeyData->keyNumber > 0)
        mask = POLLIN;

    return mask;

}

static int kerneltimer_release(struct inode *inode, struct file *filp)
{
	module_put(THIS_MODULE);
	gpioKeyIrqFree(filp->private_data);
	if(filp->private_data)
		kfree(filp->private_data);
	if(timer_pending(&timerLed))
		del_timer(&timerLed);
	printk("kerneltimer release\n");
	return 0;
}

void kerneltimer_registertimer(unsigned long timeover)
{
	timerLed.expires = get_jiffies_64() + timeover;  //10ms(1/100) *100 = 1sec
	timer_setup( &timerLed,kerneltimer_func,0);
	add_timer( &timerLed );
}

void kerneltimer_func(struct timer_list *t)
{
#ifdef DEBUG
	printk("ledVal : %#04x\n",(unsigned int)(ledVal));
#endif
	gpioLedSet(ledVal);
	ledVal = ~ledVal & 0xff;
	mod_timer(t,get_jiffies_64() + timerVal);
}

struct file_operations kerneltimer_fops = {
	// .owner = THIS_MODULE,
	.open    = kerneltimer_open,
	.read    = kerneltimer_read,
	.write   = kerneltimer_write,
	.unlocked_ioctl = kerneltimer_ioctl,
	.llseek  = kerneltimer_llseek,
	.poll    = kerneltimer_poll,
	.release = kerneltimer_release,
};

irqreturn_t key_isr(int irq, void * data)
{
	int i;
	keyDataStruct * pKeyData = (keyDataStruct *)data;
    for(i=0;i<GPIOCNT;i++)
    {
        if(irq == pKeyData->key_irq[i])
        {
            pKeyData->keyNumber = i+1;
            break;
        }
    }
	printk("key_isr() irq : %d, keyNumber : %d\n",irq, pKeyData->keyNumber);
	wake_up_interruptible(&WaitQueue_Read);
	return IRQ_HANDLED;
}

int kerneltimer_init(void)
{
	int ret;
#ifdef DEBUG
	printk("kerneltimer_ledkey init\n");
	printk("timerVal : %d , sec : %d \n",timerVal,timerVal/HZ );
#endif
	ret = register_chrdev(KERNELTIMER_DEV_MAJOR,KERNELTIMER_DEV_NAME,&kerneltimer_fops);
	if(ret < 0 ) 
		return ret;

	ret = gpioLedInit();
	if(ret < 0)
		return ret;

	ret = gpioKeyInit();
	if(ret < 0)
		return ret;

	return 0;
}
void kerneltimer_exit(void)
{
	gpioLedSet(0x00);
	gpioLedFree();
	gpioKeyFree();
	printk("kerneltimer_ledkey exit\n");
	unregister_chrdev(KERNELTIMER_DEV_MAJOR, KERNELTIMER_DEV_NAME);
}

module_init(kerneltimer_init);
module_exit(kerneltimer_exit);

MODULE_AUTHOR("JDKim");
MODULE_DESCRIPTION("led key test module");
MODULE_LICENSE("Dual BSD/GPL");
