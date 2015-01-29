#include <linux/regulator/consumer.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/timer.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>

#include <linux/sysfs.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <mach/gpio.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include <linux/syscalls.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/string.h>
#include <asm/unistd.h>
#include <linux/cdev.h>
#include <asm/uaccess.h>
#if defined(CONFIG_FB)
#include <linux/notifier.h>
#include <linux/fb.h>
#endif

#include <mach/vreg.h>

#include <linux/miscdevice.h>
#include <asm/uaccess.h>

#include <linux/poll.h>
#include <linux/irq.h>

#include <linux/wakelock.h>
#include <linux/input-polldev.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>


#ifdef CONFIG_LCT_AE515
extern int light_lcd_leve;
#endif 

#define LOG_TAG "msg2133"
#ifdef DEBUG
#define TP_DEBUG(format, args...) printk(KERN_INFO "TP_:%s( )_%d_: " format, \
	__FUNCTION__ , __LINE__, ## args);
#define DBG() printk("[%s]:%d => \n",__FUNCTION__,__LINE__);
#else
#define TP_DEBUG(format, args...);
#define DBG()
#define TPD_DMESG(a,arg...) printk(LOG_TAG ": " a,##arg)

#endif

int is_tp_driver_loaded = 0;



#define TP_OF_YUSHUN		 (1)
#define TP_OF_BOYI		 (2)


#define VTG_MIN_UV		2850000
#define VTG_MAX_UV		2850000
#define I2C_VTG_MIN_UV	1800000
#define I2C_VTG_MAX_UV	1800000

void lct_qup_i2c_use_custom_clk(struct i2c_adapter *adap, u32 iicRate);
#if defined(MSG_GESTURE_FUNCTION)
extern struct device_attribute dev_attr_doubleclick;
extern struct device_attribute dev_attr_updirect;
extern struct device_attribute dev_attr_downdirect;
extern struct device_attribute dev_attr_leftdirect;
extern struct device_attribute dev_attr_rightdirect;

extern int msg_GetGestureFlag(void);
extern void msg_SetGestureFlag(int val);
extern void msg_SetDoubleClickModeFlage(int val);
extern void msg_SetUpDirectModeFlage(int val);
extern void msg_SetDownDirectModeFlage(int val);
extern void msg_SetLeftDirectModeFlage(int val);
extern void msg_SetRightDirectModeFlage(int val);
extern int msg_CloseGestureFunction( void );
extern u8 msg_GetGestureModeValue( void );
extern int msg_OpenGestureFunction( int g_Mode );
#endif

static int is_suspend = 0;

#define u8 unsigned char
#define U8 unsigned char
#define u32 unsigned int
#define U32 unsigned int
#define s32 signed int
#define U16 ushort
#define REPORT_PACKET_LENGTH 8
#define MSG21XX_INT_GPIO 13
#define MSG21XX_RESET_GPIO 12
#define FLAG_GPIO 122
#define ST_KEY_SEARCH 	217
#define MSG21XX_RETRY_COUNT 3
#define MS_TS_MSG20XX_X_MIN 0
#define MS_TS_MSG20XX_Y_MIN 0
#define MS_TS_MSG21XX_X_MAX 540
#define MS_TS_MSG21XX_Y_MAX 960
#define FT_I2C_VTG_MIN_UV	1800000
#define FT_I2C_VTG_MAX_UV	1800000
#define FT_COORDS_ARR_SIZE	4
#define MAX_BUTTONS		4
static int msg21xx_irq = 0;
static struct i2c_client *msg21xx_i2c_client;
static struct work_struct msg21xx_wq;

#if defined(CONFIG_FB)
static	struct notifier_block fb_notif;
#endif
static struct mutex msg21xx_mutex;
struct msg21xx_ts_data *msg2133_ts = NULL;
struct msg21xx_platform_data *pdata = NULL;
#if 0
static struct input_dev *simulate_key_input = NULL;
#endif
static struct input_dev *input=NULL;
static int after0 = 0;


const U16 tpd_key_array[] = { KEY_MENU, KEY_HOME, KEY_BACK, KEY_SEARCH };
#define MAX_KEY_NUM ( sizeof( tpd_key_array )/sizeof( tpd_key_array[0] ) )


typedef struct {
int id;
char * module_name;
}tp_info_t;

tp_info_t tp_info[] = {
{0x04,"Funa"},
{0x01,"CS"},
};



#ifdef MSTAR_USE_VIRTUALKEY
int virtualkey_x;
int virtualkey_y;
#endif


static u16 mstar_module_name = 0;
static u16 mstar_firmware_version = 0;

static int is_msg2133A = 1;



#define TOUCH_ADDR_MSG21XX	0x26
#define FW_ADDR_MSG20XX		0x62

struct class *firmware_class;
struct device *firmware_cmd_dev;


struct msg21xx_platform_data {
	const char *name;
	const char *fw_name;
	u32 irqflags;
	u32 irq_gpio;
	u32 irq_gpio_flags;
	u32 reset_gpio;
	u32 reset_gpio_flags;
	u32 power_ldo_gpio;
	u32 power_ldo_gpio_flags;
	u32 family_id;
	u32 x_max;
	u32 y_max;
	u32 x_min;
	u32 y_min;
	u32 panel_minx;
	u32 panel_miny;
	u32 panel_maxx;
	u32 panel_maxy;
	u32 group_id;
	u32 hard_rst_dly;
	u32 soft_rst_dly;
	u32 num_max_touches;
	bool fw_vkey_support;
	bool no_force_update;
	bool i2c_pull_up;
	bool ignore_id_check;
	int (*power_init) (bool);
	int (*power_on) (bool);
};

struct msg21xx_ts_data {
	uint16_t addr;
	struct i2c_client *client;
	struct input_dev *input_dev;
	const struct msg21xx_platform_data *pdata;
	struct regulator *vdd;
	struct regulator *vcc_i2c;
	int use_irq;
	struct hrtimer timer;
	struct work_struct work;
	int (*power)(int on);
	int (*get_int_status)(void);
	void (*reset_ic)(void);

	struct pinctrl *ts_pinctrl;
	struct pinctrl_state *gpio_state_active;
	struct pinctrl_state *gpio_state_suspend;
};

#define MAX_TOUCH_FINGER 2
typedef struct
{
	u16 X;
	u16 Y;
} TouchPoint_t;

typedef struct
{
	u8 nTouchKeyMode;
	u8 nTouchKeyCode;
	u8 nFingerNum;
	TouchPoint_t Point[MAX_TOUCH_FINGER];
} TouchScreenInfo_t;


#if defined(CONFIG_FB)
static int fb_notifier_callback(struct notifier_block *self,
				 unsigned long event, void *data);
#endif


void HalTscrCDevWriteI2CSeq(u8 addr, u8* data, u16 size)
{
	
	int rc;

	struct i2c_msg msgs[] = {
		{
			.addr = addr,
			.flags = 0,
			.len = size,
			.buf = data,
		},
	};
	rc = i2c_transfer(msg21xx_i2c_client->adapter, msgs, 1);
	if(rc < 0){
		printk("HalTscrCDevWriteI2CSeq error %d\n", rc);
	}
}


static void _HalTscrHWReset(void)
{
	gpio_direction_output(pdata->reset_gpio, 1);
	gpio_set_value(pdata->reset_gpio, 1);
	gpio_set_value(pdata->reset_gpio, 0);

	mdelay(10);
	gpio_set_value(pdata->reset_gpio, 1);
	
	mdelay(50);
}

int msg21xx_i2c_rx_data(char *buf, int len)
{
	uint8_t i;
	struct i2c_msg msg[] = {
		{
			.addr	= msg21xx_i2c_client->addr,
			.flags	= I2C_M_RD,
			.len	= len,
			.buf	= buf,
		}
	};

	for (i = 0; i < MSG21XX_RETRY_COUNT; i++) {
		if (i2c_transfer(msg21xx_i2c_client->adapter, msg, 1) >= 0) {
			break;
		}
		mdelay(10);
	}

	if (i >= MSG21XX_RETRY_COUNT) {
		pr_err("%s: retry over %d\n", __FUNCTION__, MSG21XX_RETRY_COUNT);
		return -EIO;
	}
	return 0;
}

void msg21xx_i2c_wr_data(u8 addr, u8* data, u16 size)
{
	
	int rc;

	struct i2c_msg msgs[] = {
		{
			.addr = addr,
			.flags = 0,
			.len = size,
			.buf = data,
		},
	};
	rc = i2c_transfer(msg21xx_i2c_client->adapter, msgs, 1);
	if(rc < 0){
		printk("HalTscrCDevWriteI2CSeq error %d\n", rc);
	}
}

int msg21xx_i2c_wr_data_ext(u8 addr, u8* data, u16 size)
{
	
	int rc;

	struct i2c_msg msgs[] = {
		{
			.addr = addr,
			.flags = 0,
			.len = size,
			.buf = data,
		},
	};
	rc = i2c_transfer(msg21xx_i2c_client->adapter, msgs, 1);
	if(rc < 0){
		printk("HalTscrCDevWriteI2CSeq error %d\n", rc);
		return rc;
	}
	return 0;
}

#if 0
static void msg21xx_chip_init(void)
{
	
	
	
	gpio_direction_output(pdata->reset_gpio, 1);
	printk(" msg21xx_chip_init before set gpio\n");
	gpio_set_value(pdata->reset_gpio, 0);
	printk(" msg21xx_chip_init after set gpio\n");
	mdelay(200);
	gpio_set_value(pdata->reset_gpio, 1);
	mdelay(500);
	
	
	

}

static void msg21xx_ts_early_suspend(struct early_suspend *h)
{
	
	printk("%s: enter\n", __func__);
	#ifdef CONFIG_LCT_AE515
	pmapp_disp_backlight_set_brightness(0);
	printk(KERN_ERR"pmapp_disp_backlight_set_brightness");
	#endif 

	gpio_set_value(MSG21XX_RESET_GPIO, 0);
	disable_irq_nosync(msg21xx_irq);


}

static void msg21xx_ts_late_resume(struct early_suspend *h)
{
	struct irq_desc *desc = irq_to_desc(msg21xx_irq);
	
	
	printk("%s: enter\n", __func__);


	printk("%s: desc->depth:%d\n", __func__,desc->depth);



	
	if(1) 
	{
		gpio_set_value(MSG21XX_RESET_GPIO, 1);
		msleep(300);
	}

	enable_irq(msg21xx_irq);
	


}


#endif





u8 Calculate_8BitsChecksum( u8 *msg, s32 s32Length )
{
	s32 s32Checksum = 0;
	s32 i;

	for ( i = 0 ; i < s32Length; i++ )
	{
		s32Checksum += msg[i];
	}

	return (u8)( ( -s32Checksum ) & 0xFF );
}


static void msg21xx_do_work(struct work_struct *work)
{
	u8 val[8] = {0};
	u8 Checksum = 0;
	u8 i;
	u32 delta_x = 0, delta_y = 0;
	u32 u32X = 0;
	u32 u32Y = 0;
	u8 touchkeycode = 0;
	TouchScreenInfo_t touchData;
	static u32 preKeyStatus=0;
	
#ifdef SWAP_X_Y
	int tempx;
	int tempy;
#endif
#ifdef MSG_GESTURE_FUNCTION
	int closeGesturnRetval = 0;
#endif

	mutex_lock(&msg21xx_mutex);
	msg21xx_i2c_rx_data( &val[0], REPORT_PACKET_LENGTH );
	Checksum = Calculate_8BitsChecksum( val, (REPORT_PACKET_LENGTH-1) );

#ifdef MSG_GESTURE_FUNCTION
	if (msg_GetGestureFlag())
	{
		if(( val[0] == 0x52 ) && ( val[1] == 0xFF ) && ( val[2] == 0xFF ) && ( val[3] == 0xFF ) && ( val[4] == 0xFF ) && ( val[6] == 0xFF ) && ( Checksum == val[7] ))
		{
			printk("tpd_touchinfo gesture function mode read data--%0x \n",val[5]);
			if(val[5] == 0x58)
			{
				msg_SetDoubleClickModeFlage(1);
			}
			else if(val[5] == 0x60)
			{
				msg_SetUpDirectModeFlage(1);
			}
			else if(val[5] == 0x61)
			{
				msg_SetDownDirectModeFlage(1);
			}
			else if(val[5] == 0x62)
			{
				msg_SetLeftDirectModeFlage(1);
			}
			else if(val[5] == 0x63)
			{
				msg_SetRightDirectModeFlage(1);
			}
			 if((val[5] == 0x58)||(val[5] == 0x60)||(val[5] == 0x61)||
				(val[5] == 0x62)||(val[5] == 0x63))
			{
				while(closeGesturnRetval == 0){
					closeGesturnRetval = msg_CloseGestureFunction();
				}
			}
			
		 }
	}
#endif

	if ((Checksum == val[7]) && (val[0] == 0x52))		
	{
		u32X = (((val[1] & 0xF0) << 4) | val[2]);	
		u32Y = (((val[1] & 0x0F) << 8) | val[3]);

		delta_x = (((val[4] & 0xF0) << 4) | val[5]);
		delta_y = (((val[4] & 0x0F) << 8) | val[6]);

#ifdef SWAP_X_Y
		tempy = u32X;
		tempx = u32Y;
		u32X = tempx;
		u32Y = tempy;

		tempy = delta_x;
		tempx = delta_y;
		delta_x = tempx;
		delta_y = tempy;
#endif
#ifdef REVERSE_X
		u32X = 2047 - u32X;
		delta_x = 4095 - delta_x;
#endif
#ifdef REVERSE_Y
		u32Y = 2047 - u32Y;
		delta_y = 4095 - delta_y;
#endif
		
		

		if ((val[1] == 0xFF) && (val[2] == 0xFF) && (val[3] == 0xFF) && (val[4] == 0xFF) && (val[6] == 0xFF))
		{
			touchData.Point[0].X = 0; 
			touchData.Point[0].Y = 0; 

			if((val[5]==0x0)||(val[5]==0xFF))
			{
				touchData.nFingerNum = 0; 
				touchData.nTouchKeyCode = 0; 
				touchData.nTouchKeyMode = 0; 
			}
			else
			{
				touchData.nTouchKeyMode = 1; 
				touchData.nTouchKeyCode = val[5]; 
				touchData.nFingerNum = 1;
			}
		}
		else
		{
			touchData.nTouchKeyMode = 0; 

			if(
#ifdef REVERSE_X
			(delta_x == 4095)
#else
			(delta_x == 0)
#endif
			&&
#ifdef REVERSE_Y
			(delta_y == 4095)
#else
			(delta_y == 0)
#endif
			)
			{
				if(u32X >=0&&u32X <=2047&&u32Y >=0&&u32Y<=2047)
				{
					touchData.nFingerNum = 1; 
					touchData.Point[0].X = (u32X * pdata->x_max) / 2048;
					touchData.Point[0].Y = (u32Y * pdata->y_max) / 2048;
				}
				else
				{
					enable_irq(msg21xx_irq);
					mutex_unlock(&msg21xx_mutex);
					return;
				}
			}
			else
			{
				u32 x2, y2;
				
				if (delta_x > 2048)	 
				{
				delta_x -= 4096;
				}
				if (delta_y > 2048)
				{
				delta_y -= 4096;
				}

				x2 = (u32)(u32X + delta_x);
				y2 = (u32)(u32Y + delta_y);

				if(u32X >=0&&u32X <=2047&&u32Y >=0&&u32Y<=2047
				&&x2 >=0&&x2 <=2047&&y2 >=0&&y2<=2047)
				{
					touchData.nFingerNum = 2; 
					
					touchData.Point[0].X = (u32X * pdata->x_max) / 2048;
					touchData.Point[0].Y = (u32Y * pdata->y_max) / 2048;

					touchData.Point[1].X = (x2 * pdata->x_max) / 2048;
					touchData.Point[1].Y = (y2 * pdata->y_max) / 2048;
				}
				else
				{
					enable_irq(msg21xx_irq);
					mutex_unlock(&msg21xx_mutex);
					return;
				}
			}
		}

		
		if(touchData.nTouchKeyMode)
		{
			if(is_msg2133A == 1)
			{
				if (touchData.nTouchKeyCode == 4)
				{
					touchkeycode = KEY_BACK;
#ifdef MSTAR_USE_VIRTUALKEY
					virtualkey_x = 540;
					virtualkey_y = 1060;
#endif
				}
				if (touchData.nTouchKeyCode == 2)
				{
					touchkeycode = KEY_HOME;
#ifdef MSTAR_USE_VIRTUALKEY
					virtualkey_x = 270;
					virtualkey_y = 1060;
#endif
				}
				if (touchData.nTouchKeyCode == 1)
				{
					touchkeycode = KEY_MENU;
#ifdef MSTAR_USE_VIRTUALKEY
					virtualkey_x = 90;
					virtualkey_y = 1060;
#endif
				}
			}
			else
			{
				if (touchData.nTouchKeyCode == 1)
				{
					touchkeycode = KEY_MENU;
#ifdef MSTAR_USE_VIRTUALKEY
					virtualkey_x = 270;
					virtualkey_y = 496;
#endif
				}
				if (touchData.nTouchKeyCode == 4)
				{
					touchkeycode = KEY_BACK;
#ifdef MSTAR_USE_VIRTUALKEY
					virtualkey_x = 60;
					virtualkey_y = 496;
#endif
				}
				if (touchData.nTouchKeyCode == 2)
				{
					touchkeycode = KEY_HOME;
#ifdef MSTAR_USE_VIRTUALKEY
					virtualkey_x = 160;
					virtualkey_y = 496;
#endif
				}
			}
			
			

			if(preKeyStatus!=touchkeycode)
			{
				after0 = 0;
				preKeyStatus=touchkeycode;

#ifdef MSTAR_USE_VIRTUALKEY
				input_report_abs(input, ABS_MT_TOUCH_MAJOR, 1);
				input_report_abs(input, ABS_MT_WIDTH_MAJOR, 1);
				input_report_abs(input, ABS_MT_POSITION_X, virtualkey_x);
				input_report_abs(input, ABS_MT_POSITION_Y, virtualkey_y);
				input_mt_sync(input);
				input_report_key(input, BTN_TOUCH, 1);
				input_sync(input);
#else
				
				
				input_report_key(input, BTN_TOUCH, 1);
				input_report_key(input, touchkeycode, 1);
				input_sync(input);
#endif
				
			}
		}
		else
		{
			preKeyStatus=0; 

			if((touchData.nFingerNum) == 0&&after0==0)	
			{
				after0 = 1;
				
				input_report_key(input, KEY_MENU, 0);
				input_report_key(input, KEY_HOME, 0);
				input_report_key(input, KEY_BACK, 0);
				input_report_key(input, KEY_SEARCH, 0);

				input_report_abs(input, ABS_MT_TOUCH_MAJOR, 0);
				input_mt_sync(input);
				input_report_key(input, BTN_TOUCH, 0);
				input_sync(input);
			}
			else 
			{
				after0 = 0;

				for(i = 0;i < (touchData.nFingerNum);i++)
				{
					input_report_abs(input, ABS_MT_PRESSURE, 15);
					input_report_abs(input, ABS_MT_TOUCH_MAJOR, 15);
					input_report_abs(input, ABS_MT_POSITION_X, touchData.Point[i].X);
					input_report_abs(input, ABS_MT_POSITION_Y, touchData.Point[i].Y);
					input_mt_sync(input);
					input_report_key(input, BTN_TOUCH, 1);
				}

				input_sync(input);
			}
		}
	}
	else
	{
		
		
		
		printk(KERN_ERR "err status in tp\n");
	}

	enable_irq(msg21xx_irq);
	mutex_unlock(&msg21xx_mutex);
}

static int msg21xx_ts_open(struct input_dev *dev)
{
	return 0;
}

static void msg21xx_ts_close(struct input_dev *dev)
{
	printk("msg21xx_ts_close\n");

}


static int msg21xx_init_input(void)
{


	
	int err;

	

	printk("%s: msg21xx_i2c_client->name:%s\n", __func__,msg21xx_i2c_client->name);
	input = input_allocate_device();
	input->name = msg21xx_i2c_client->name;
	input->phys = "I2C";
	input->id.bustype = BUS_I2C;
	input->dev.parent = &msg21xx_i2c_client->dev;
	input->open = msg21xx_ts_open;
	input->close = msg21xx_ts_close;

	set_bit(EV_ABS, input->evbit);
	set_bit(EV_SYN, input->evbit);
	set_bit(BTN_TOUCH, input->keybit);	
	set_bit(INPUT_PROP_DIRECT, input->propbit);
	#ifdef MSTAR_USE_VIRTUALKEY
	set_bit(EV_KEY, input->evbit);
	set_bit(BTN_MISC, input->keybit);
	set_bit(KEY_OK, input->keybit);
	set_bit(KEY_MENU, input->keybit);
	set_bit(KEY_BACK, input->keybit);
	#else
	{
		int i;
		for(i = 0; i < MAX_KEY_NUM; i++)
		{
			input_set_capability(input, EV_KEY, tpd_key_array[i]);
		}
	}
	#endif



	input_set_abs_params(input, ABS_MT_TRACKING_ID, 0, 2, 0, 0);
	input_set_abs_params(input, ABS_MT_TOUCH_MAJOR, 0, 2, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_X, 0, pdata->x_max, 0, 0);
	input_set_abs_params(input, ABS_MT_POSITION_Y, 0, pdata->y_max, 0, 0);

	err = input_register_device(input);
	if (err)
		goto fail_alloc_input;

	
	
	#if 0
	simulate_key_input = input_allocate_device();
	if (simulate_key_input == NULL) {
		printk("%s: not enough memory for input device\n", __func__);
		err = -ENOMEM;
		goto fail_alloc_input;
	}
	simulate_key_input->name = "7x27a_kp";
	__set_bit(EV_KEY, simulate_key_input->evbit);
	__set_bit(KEY_BACK, simulate_key_input->keybit);
	__set_bit(KEY_MENU, simulate_key_input->keybit);
	__set_bit(KEY_HOME, simulate_key_input->keybit);
	__set_bit(ST_KEY_SEARCH, simulate_key_input->keybit);
	err = input_register_device(simulate_key_input);
	if (err != 0) {
		printk("%s: failed to register input device\n", __func__);
		goto fail_alloc_input;
	}
	#endif




fail_alloc_input:

	return 0;
}
static irqreturn_t msg21xx_interrupt(int irq, void *dev_id)
{
	
	

	disable_irq_nosync(msg21xx_irq);
	schedule_work(&msg21xx_wq);
	return IRQ_HANDLED;
}



int SMBus_Read(U16 addr, U32 numOfBytes, U8 *pDataToRead)
{
	int ret = -1;
	U8 dbbus_tx_data[4];

	dbbus_tx_data[0] = 0x53;
	dbbus_tx_data[1] = (addr>>8)&0xFF;
	dbbus_tx_data[2] = addr&0xFF;

	msg21xx_i2c_wr_data(TOUCH_ADDR_MSG21XX, dbbus_tx_data,3 );
	ret = msg21xx_i2c_rx_data((char*)pDataToRead,4);
	return ret;
}

int Read_FW_Version(void)
{
	int ret = -1;
	U8 dbbus_rx_data[4] = {0};
	U16 Major,Minor;

	printk("xuke: %s, is_msg2133A=%d\n", __func__, is_msg2133A);
	if(is_msg2133A == 1)
	{
		ret = SMBus_Read(0x002a, 4, &dbbus_rx_data[0]);
	}
	else
	{
		ret = SMBus_Read(0x0074, 4, &dbbus_rx_data[0]);
	}

	if(ret == 0)
	{
		Major = ((U16)dbbus_rx_data[1]<<8)| (dbbus_rx_data[0]); 
		Minor = ((U16)dbbus_rx_data[3]<<8)| (dbbus_rx_data[2]); 
		mstar_module_name = Major;
		mstar_firmware_version = Minor;
		printk("%s: mstar_module_name:%d,mstar_firmware_version:%d\n", __func__,mstar_module_name,mstar_firmware_version);
	}
	return ret;
}

#ifdef CYTTSP_SUPPORT_READ_TP_VERSION

static int tp_version_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int tp_version_release(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t tp_version_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
{

	int i;

	char tp_version[30] = {0};
	char ic_type[10] = {0};
	printk("%s Enter,mstar_vendor_id=%d,mstar_firmware_version=%d\n",__func__,mstar_module_name,mstar_firmware_version);
	if(mstar_module_name < 0)
	{
		strcpy(tp_version,"no tp");
	}
	else
	{
		for (i=0;i< sizeof(tp_info)/sizeof(tp_info_t);i++)
		{
			if (mstar_module_name == tp_info[i].id)
				break;
		}
		if(is_msg2133A == 1)
		{
			strcpy(ic_type,"msg2133A");
		}
		else
		{
			strcpy(ic_type,"msg2133");
		}

		if(i == sizeof(tp_info)/sizeof(tp_info_t))
			snprintf(tp_version, 30,"M%xV%x-%s-%s\n",mstar_module_name,mstar_firmware_version,"unknown",ic_type);
		else
		{
			snprintf(tp_version, 30,"M%xV%x-%s-%s\n", mstar_module_name,mstar_firmware_version,tp_info[i].module_name,ic_type);
		}
	}
	if(copy_to_user(buf, tp_version, strlen(tp_version)))
		return -EFAULT;

	return strlen(tp_version);
}


static struct file_operations tp_version_fops = {
	.owner = THIS_MODULE,
	.open = tp_version_open,
	.release = tp_version_release,
	.read = tp_version_read,
};


static struct miscdevice tp_version_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "tp_version",
	.fops = &tp_version_fops,
};
#endif

static int msg21xx_power_on(struct msg21xx_ts_data *data, bool on)
{
	int rc;

	if (!on)
		goto power_off;

#if 0
	if (gpio_is_valid(data->pdata->power_ldo_gpio)) {
		gpio_set_value(data->pdata->power_ldo_gpio, 1);
	}
#else
	rc = regulator_enable(data->vdd);
	if (rc) {
		dev_err(&data->client->dev,
			"Regulator vdd enable failed rc=%d\n", rc);

		return rc;
	}
#endif

	rc = regulator_enable(data->vcc_i2c);
	if (rc) {
		dev_err(&data->client->dev,
			"Regulator vcc_i2c enable failed rc=%d\n", rc);
		regulator_disable(data->vdd);
	}

	return rc;

power_off:
	#if 0
	if (gpio_is_valid(data->pdata->power_ldo_gpio)) {
		gpio_free(data->pdata->power_ldo_gpio);
	}
	#else
	rc = regulator_disable(data->vdd);
	if (rc) {
		dev_err(&data->client->dev,
			"Regulator vdd disable failed rc=%d\n", rc);
		return rc;
	}
	#endif
	rc = regulator_disable(data->vcc_i2c);
	if (rc) {
		dev_err(&data->client->dev,
			"Regulator vcc_i2c disable failed rc=%d\n", rc);
		#if 0
		rc = regulator_enable(data->vdd);
		if (rc) {
			dev_err(&data->client->dev,
				"Regulator vdd enable failed rc=%d\n", rc);
		}
		#endif
	}

	return rc;
}


static int msg21xx_power_init(struct msg21xx_ts_data *data, bool on)
{
	int rc;

	if (!on)
		goto pwr_deinit;
#if 0
	if (gpio_is_valid(data->pdata->power_ldo_gpio)) {
			rc = gpio_request(data->pdata->power_ldo_gpio, "msg21xx_ldo_gpio");
			if (rc) {
				printk("irq gpio request failed\n");
				return rc;
			}
			rc = gpio_direction_output(data->pdata->power_ldo_gpio, 1);
			if (rc) {
				printk("set_direction for irq gpio failed\n");
				goto free_ldo_gpio;
			}
		}
#else
	data->vdd = regulator_get(&data->client->dev, "vdd");
	if (IS_ERR(data->vdd)) {
		rc = PTR_ERR(data->vdd);
		dev_err(&data->client->dev,
			"Regulator get failed vdd rc=%d\n", rc);
		return rc;
	}

	if (regulator_count_voltages(data->vdd) > 0) {
		rc = regulator_set_voltage(data->vdd, VTG_MIN_UV, VTG_MAX_UV);
		if (rc) {
			dev_err(&data->client->dev,
				"Regulator set_vtg failed vdd rc=%d\n", rc);
			goto reg_vdd_put;
		}
	}
#endif
	data->vcc_i2c = regulator_get(&data->client->dev, "vcc_i2c");
	if (IS_ERR(data->vcc_i2c)) {
		rc = PTR_ERR(data->vcc_i2c);
		dev_err(&data->client->dev,
			"Regulator get failed vcc_i2c rc=%d\n", rc);
		goto reg_vdd_set_vtg;
	}

	if (regulator_count_voltages(data->vcc_i2c) > 0) {
		rc = regulator_set_voltage(data->vcc_i2c, I2C_VTG_MIN_UV, I2C_VTG_MAX_UV);
		if (rc) {
			dev_err(&data->client->dev,
			"Regulator set_vtg failed vcc_i2c rc=%d\n", rc);
			goto reg_vcc_i2c_put;
		}
	}

	return 0;

reg_vcc_i2c_put:
	regulator_put(data->vcc_i2c);
#if 0
free_ldo_gpio:
	if (gpio_is_valid(data->pdata->power_ldo_gpio))
		gpio_free(data->pdata->power_ldo_gpio);

#else
reg_vdd_set_vtg:
	if (regulator_count_voltages(data->vdd) > 0)
		regulator_set_voltage(data->vdd, 0, VTG_MAX_UV);

reg_vdd_put:
	regulator_put(data->vdd);
#endif
	return rc;

pwr_deinit:
	#if 1
	if (regulator_count_voltages(data->vdd) > 0)
		regulator_set_voltage(data->vdd, 0, VTG_MAX_UV);

	regulator_put(data->vdd);
	#endif
	if (regulator_count_voltages(data->vcc_i2c) > 0)
		regulator_set_voltage(data->vcc_i2c, 0, I2C_VTG_MAX_UV);

	regulator_put(data->vcc_i2c);
	return 0;
}
static int msg21xx_pinctrl_init(struct msg21xx_ts_data *data)
{
	int retval;
	
	data->ts_pinctrl = devm_pinctrl_get(&(data->client->dev));
	if (IS_ERR_OR_NULL(data->ts_pinctrl)) {
		dev_dbg(&data->client->dev,
			"Target does not use pinctrl\n");
		retval = PTR_ERR(data->ts_pinctrl);
		data->ts_pinctrl = NULL;
		return retval;
	}

	data->gpio_state_active
		= pinctrl_lookup_state(data->ts_pinctrl,
			"pmx_ts_active");
	if (IS_ERR_OR_NULL(data->gpio_state_active)) {
		printk("%s Can not get ts default pinstate\n", __func__);
		retval = PTR_ERR(data->gpio_state_active);
		data->ts_pinctrl = NULL;
		return retval;
	}

	data->gpio_state_suspend
		= pinctrl_lookup_state(data->ts_pinctrl,
			"pmx_ts_suspend");
	if (IS_ERR_OR_NULL(data->gpio_state_suspend)) {
		dev_err(&data->client->dev,
			"Can not get ts sleep pinstate\n");
		retval = PTR_ERR(data->gpio_state_suspend);
		data->ts_pinctrl = NULL;
		return retval;
	}

	return 0;
}

static int msg21xx_pinctrl_select(struct msg21xx_ts_data *data, bool on)
{
	struct pinctrl_state *pins_state;
	int ret;
	pins_state = on ? data->gpio_state_active
		: data->gpio_state_suspend;
	if (!IS_ERR_OR_NULL(pins_state)) {
		ret = pinctrl_select_state(data->ts_pinctrl, pins_state);
		if (ret) {
			dev_err(&data->client->dev,
				"can not set %s pins\n",
				on ? "pmx_ts_active" : "pmx_ts_suspend");
			return ret;
		}
	} else {
		dev_err(&data->client->dev,
			"not a valid '%s' pinstate\n",
				on ? "pmx_ts_active" : "pmx_ts_suspend");
	}

	return 0;
}


#ifdef CONFIG_OF
static int msg21xx_get_dt_coords(struct device *dev, char *name,
				struct msg21xx_platform_data *pdata)
{
	u32 coords[FT_COORDS_ARR_SIZE];
	struct property *prop;
	struct device_node *np = dev->of_node;
	int coords_size, rc;

	prop = of_find_property(np, name, NULL);
	if (!prop)
		return -EINVAL;
	if (!prop->value)
		return -ENODATA;

	coords_size = prop->length / sizeof(u32);
	if (coords_size != FT_COORDS_ARR_SIZE) {
		dev_err(dev, "invalid %s\n", name);
		return -EINVAL;
	}

	rc = of_property_read_u32_array(np, name, coords, coords_size);
	if (rc && (rc != -EINVAL)) {
		dev_err(dev, "Unable to read %s\n", name);
		return rc;
	}

	if (!strcmp(name, "mstar,panel-coords")) {
		pdata->panel_minx = coords[0];
		pdata->panel_miny = coords[1];
		pdata->panel_maxx = coords[2];
		pdata->panel_maxy = coords[3];
	} else if (!strcmp(name, "mstar,display-coords")) {
		pdata->x_min = coords[0];
		pdata->y_min = coords[1];
		pdata->x_max = coords[2];
		pdata->y_max = coords[3];
	} else {
		dev_err(dev, "unsupported property %s\n", name);
		return -EINVAL;
	}

	return 0;
}

static int msg21xx_parse_dt(struct device *dev,
			struct msg21xx_platform_data *pdata)
{
	int rc;
	struct device_node *np = dev->of_node;
	struct property *prop;
	u32 temp_val, num_buttons;
	u32 button_map[MAX_BUTTONS];

	pdata->name = "mstar";
	rc = of_property_read_string(np, "mstar,name", &pdata->name);
	if (rc && (rc != -EINVAL)) {
		dev_err(dev, "Unable to read name\n");
		return rc;
	}

	rc = msg21xx_get_dt_coords(dev, "mstar,panel-coords", pdata);
	if (rc && (rc != -EINVAL))
		return rc;

	rc = msg21xx_get_dt_coords(dev, "mstar,display-coords", pdata);
	if (rc)
		return rc;

	pdata->i2c_pull_up = of_property_read_bool(np,
						"mstar,i2c-pull-up");

	pdata->no_force_update = of_property_read_bool(np,
						"mstar,no-force-update");
	
	pdata->reset_gpio = of_get_named_gpio_flags(np, "mstar,reset-gpio",
				0, &pdata->reset_gpio_flags);
	if (pdata->reset_gpio < 0)
		return pdata->reset_gpio;

	pdata->irq_gpio = of_get_named_gpio_flags(np, "mstar,irq-gpio",
				0, &pdata->irq_gpio_flags);
	if (pdata->irq_gpio < 0)
		return pdata->irq_gpio;

	
	pdata->power_ldo_gpio = of_get_named_gpio_flags(np, "mstar,power_ldo-gpio",
				0, &pdata->power_ldo_gpio_flags);
	if (pdata->power_ldo_gpio < 0)
		return pdata->power_ldo_gpio;


	rc = of_property_read_u32(np, "mstar,family-id", &temp_val);
	if (!rc)
		pdata->family_id = temp_val;
	else
		return rc;

	prop = of_find_property(np, "mstar,button-map", NULL);
	if (prop) {
		num_buttons = prop->length / sizeof(temp_val);
		if (num_buttons > MAX_BUTTONS)
			return -EINVAL;

		rc = of_property_read_u32_array(np,
			"mstar,button-map", button_map,
			num_buttons);
		if (rc) {
			dev_err(dev, "Unable to read key codes\n");
			return rc;
		}
	}

	return 0;
}
#else
static int msg21xx_parse_dt(struct device *dev,
			struct msg21xx_platform_data *pdata)
{
	return -ENODEV;
}
#endif


static int msg21xx_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	unsigned int irq;
	int err = 0;
	int retval = 0;
	int nRetryCount = 0;
#ifdef SUPPORT_READ_TP_VERSION
	char tp_version[60] = {0};
#endif

	printk("%s, is_tp_driver_loaded=%d\n", __func__, is_tp_driver_loaded);
	if (is_tp_driver_loaded)
	{
		TPD_DMESG("other driver has been loaded\n");
		return -ENODEV;
	}

	if (client->dev.of_node) {
		pdata = devm_kzalloc(&client->dev,
			sizeof(struct msg21xx_platform_data), GFP_KERNEL);
		if (!pdata) {
			dev_err(&client->dev, "Failed to allocate memory\n");
			return -ENOMEM;
		}

		err = msg21xx_parse_dt(&client->dev, pdata);
		if (err) {
			dev_err(&client->dev, "DT parsing failed\n");
			return err;
		}
	} else
		pdata = client->dev.platform_data;

	if (!pdata) {
		dev_err(&client->dev, "Invalid pdata\n");
		return -EINVAL;
	}


	msg21xx_i2c_client = client;
	msg2133_ts = devm_kzalloc(&client->dev,
			sizeof(struct msg21xx_ts_data), GFP_KERNEL);
	if (!msg2133_ts) {
		dev_err(&client->dev, "Not enough memory\n");
		return -ENOMEM;
	}

	msg2133_ts->client = client;
	msg2133_ts->pdata = pdata;
	i2c_set_clientdata(client, msg2133_ts);

	err = msg21xx_pinctrl_init(msg2133_ts);
	if (!err && msg2133_ts->ts_pinctrl) {
		err = msg21xx_pinctrl_select(msg2133_ts, true);
		if (err < 0)
			goto free_pinctrl;
	}

	err = msg21xx_power_init(msg2133_ts, true);
	if (err) {
		dev_err(&client->dev, "power init failed");
		goto exit_irq_request_failed;
	}

	err = msg21xx_power_on(msg2133_ts, true);
	if (err) {
		dev_err(&client->dev, "power on failed");
		goto pwr_deinit;
	}
	if (gpio_is_valid(pdata->irq_gpio)) {
		err = gpio_request(pdata->irq_gpio, "msg21xx_irq_gpio");
		if (err) {
			dev_err(&client->dev, "irq gpio request failed");
			goto pwr_off;
		}
		err = gpio_direction_input(pdata->irq_gpio);
		if (err) {
			dev_err(&client->dev,
				"set_direction for irq gpio failed\n");
			goto free_irq_gpio;
		}
	}
	irq = gpio_to_irq(pdata->irq_gpio);
	msg21xx_irq = irq;
	printk("[ PORTING MSG]%s irq =%d\n",__func__, irq);


	if (gpio_is_valid(pdata->reset_gpio)) {
		err = gpio_request(pdata->reset_gpio, "msg21xx_reset_gpio");
		if (err) {
			dev_err(&client->dev, "reset gpio request failed");
			goto free_irq_gpio;
		}

		err = gpio_direction_output(pdata->reset_gpio, 1);
		if (err) {
			dev_err(&client->dev,
				"set_direction for reset gpio failed\n");
			goto free_reset_gpio;
		}
		gpio_set_value(pdata->reset_gpio, 1);
	}

	
	msleep(100);


#if 1
	_HalTscrHWReset();
	do{
		msleep(20);
		retval = Read_FW_Version();
	
	}while((retval != 0) && (nRetryCount++ < 5));

	if(retval != 0)
	{
		return -1;
	}
#endif

#ifdef SUPPORT_READ_TP_VERSION
	memset(tp_version, 0, sizeof(tp_version));
	sprintf(tp_version, "vid:0x%04x,fw:0x%04x,ic:%s\n",mstar_module_name,mstar_firmware_version,"msg2133");
	init_tp_fm_info(0, tp_version, 0);
#endif

	INIT_WORK(&msg21xx_wq, msg21xx_do_work);

#ifdef MSTAR_USE_VIRTUALKEY
	 
#endif
	msg21xx_init_input();

err = request_irq(irq, msg21xx_interrupt,
	IRQF_TRIGGER_FALLING, "msg21xx", NULL);
if (err != 0) {
	printk("%s: cannot register irq\n", __func__);
	goto exit;
}

#ifdef MSG_GESTURE_FUNCTION
	
	if (device_create_file(firmware_cmd_dev, &dev_attr_doubleclick) < 0)
		pr_err("Failed to create device file(%s)!\n", dev_attr_doubleclick.attr.name);

	
	if (device_create_file(firmware_cmd_dev, &dev_attr_updirect) < 0)
		pr_err("Failed to create device file(%s)!\n", dev_attr_updirect.attr.name);

	
	if (device_create_file(firmware_cmd_dev, &dev_attr_downdirect) < 0)
		pr_err("Failed to create device file(%s)!\n", dev_attr_downdirect.attr.name);

	
	if (device_create_file(firmware_cmd_dev, &dev_attr_leftdirect) < 0)
		pr_err("Failed to create device file(%s)!\n", dev_attr_leftdirect.attr.name);

	
	if (device_create_file(firmware_cmd_dev, &dev_attr_rightdirect) < 0)
		pr_err("Failed to create device file(%s)!\n", dev_attr_rightdirect.attr.name);
#endif

#if defined(CONFIG_FB)
	fb_notif.notifier_call = fb_notifier_callback;
	err = fb_register_client(&fb_notif);
	if (err)
		TP_DEBUG("Unable to register fb_notifier: %d\n",err);
#endif

	
	
	
	


	#ifdef CYTTSP_SUPPORT_READ_TP_VERSION
	misc_register(&tp_version_device);
	#endif



#if 0
	gpio_set_value(pdata->reset_gpio, 0);
	pr_err(" MSG21XX_RESET_GPIO after set gpio");
	msleep(200);
	gpio_set_value(pdata->reset_gpio, 1);
	msleep(500);
#else
	_HalTscrHWReset();
#endif

	is_tp_driver_loaded = 1;


	return 0;
free_reset_gpio:
	if (gpio_is_valid(pdata->reset_gpio))
		gpio_free(pdata->reset_gpio);
	if (msg2133_ts->ts_pinctrl) {
		err = msg21xx_pinctrl_select(msg2133_ts, false);
		if (err < 0)
			pr_err("Cannot get idle pinctrl state\n");
	}
free_irq_gpio:
	if (gpio_is_valid(pdata->irq_gpio))
		gpio_free(pdata->irq_gpio);
	if (msg2133_ts->ts_pinctrl) {
		err = msg21xx_pinctrl_select(msg2133_ts, false);
		if (err < 0)
			pr_err("Cannot get idle pinctrl state\n");
	}
pwr_off:
	msg21xx_power_on(msg2133_ts, false);
pwr_deinit:
	msg21xx_power_init(msg2133_ts, false);
free_pinctrl:
	if (msg2133_ts->ts_pinctrl)
		pinctrl_put(msg2133_ts->ts_pinctrl);
exit_irq_request_failed:
	i2c_set_clientdata(client, NULL);
	kfree(msg2133_ts);
exit:
	return err;
}

#if defined(CONFIG_FB)
static void msg2133_ts_suspend(void)
{
#ifdef MSG_GESTURE_FUNCTION
	 int msg_gesturemoderetval = 0, gesture_mode = 0, timeout = 5;
#endif

#ifdef MSG_GESTURE_FUNCTION
	gesture_mode = msg_GetGestureModeValue();
	if(gesture_mode)
	{

		if((gesture_mode >= 0x01)&&(gesture_mode <= 0x1F))
		{
			while((!msg_gesturemoderetval) && (--timeout))
			{
				msg_gesturemoderetval = msg_OpenGestureFunction(gesture_mode);
			} 

			if (!timeout && !msg_gesturemoderetval)
			{
				printk("xuke: %s, failed to enter gesture mode!\n", __func__);
			}
			else
			{
				printk("xuke: %s, enter gesture mode 0x%x\n", __func__, gesture_mode);
			}
			return;
		}
		else
		{
			
			printk("%s, gesture_mode wrong!\n", __func__);
		}
	}
#endif

	gpio_set_value(pdata->reset_gpio, 0);
	disable_irq_nosync(msg21xx_irq);
	is_suspend = 1;
}

static void msg2133_ts_resume(void)
{
	
#ifdef MSG_GESTURE_FUNCTION
	msg_SetGestureFlag(0);
#endif
	
	if(1) 
	{
		gpio_set_value(pdata->reset_gpio, 1);
		msleep(30);
	}
	enable_irq(msg21xx_irq);
	
	is_suspend = 0;

}
static int fb_notifier_callback(struct notifier_block *self,
				 unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	int *blank;

	if (evdata && evdata->data && event == FB_EVENT_BLANK ){
		blank = evdata->data;
		TP_DEBUG("fb_notifier_callback blank=%d\n",*blank);
		if (*blank == FB_BLANK_UNBLANK)
			msg2133_ts_resume();
		else if (*blank == FB_BLANK_POWERDOWN)
			msg2133_ts_suspend();
	}

	return 0;
}

#endif


static int msg21xx_ts_remove(struct i2c_client *client)
{
	#ifdef CYTTSP_SUPPORT_READ_TP_VERSION
	misc_deregister(&tp_version_device);
	#endif

	#ifdef MSTAR_TP_USE_WAKELOCK
		wake_lock_destroy(&mstar_tp_wake_lock);
	#endif
	#if defined(CONFIG_FB)
		if (fb_unregister_client(&fb_notif))
			TP_DEBUG("Error occurred while unregistering fb_notifier.\n");
	#endif

	return 0;
}

static struct of_device_id msg2133_match_table[] = {
	{ .compatible = "mstar,msg2133",},
	{ },
};


static const struct i2c_device_id msg21xx_ts_id[] = {
	{ "ms-msg20xx", TOUCH_ADDR_MSG21XX },
	{ }
};
MODULE_DEVICE_TABLE(i2c, msg21xx_ts_id);


static struct i2c_driver msg21xx_ts_driver = {
	.driver = {
		.name = "ms-msg20xx",
		.owner = THIS_MODULE,
		.of_match_table = msg2133_match_table,
	},
	.probe = msg21xx_ts_probe,
	.remove = msg21xx_ts_remove,
	
	
	.id_table = msg21xx_ts_id,
};





static int __init msg21xx_init(void)
{
#ifdef TP_DIFF
	int err;
	int ret;
	gpio_request(FLAG_GPIO, "flag_msg21xx");
	gpio_direction_input(FLAG_GPIO);
	udelay(10);
	ret = gpio_get_value(FLAG_GPIO);
	
	if(0 == ret)
	{
		udelay(10);
		ret = gpio_get_value(FLAG_GPIO);
		ret = 1;
		
		if (0 == ret)
		{
			gpio_free(FLAG_GPIO);

			mutex_init(&msg21xx_mutex);
			err = i2c_add_driver(&msg21xx_ts_driver);
			if (err) {
				printk(KERN_WARNING "msg21xx driver failed "
					"(errno = %d)\n", err);
			} else {
				printk( "Successfully added driver %s\n",
						msg21xx_ts_driver.driver.name);
			}
			return err;
		}
		else
		{
			gpio_free(FLAG_GPIO);
		}
	}
	else
	{
		gpio_free(FLAG_GPIO);
		return -1;
	}
#else
	int err;
	mutex_init(&msg21xx_mutex);
	err = i2c_add_driver(&msg21xx_ts_driver);
	if (err) {
		printk(KERN_WARNING "msg21xx driver failed "
			 "(errno = %d)\n", err);
	} else {
		printk( "Successfully added driver %s\n",
				msg21xx_ts_driver.driver.name);
	}


	return err;
#endif
}

static void __exit msg21xx_cleanup(void)
{
	i2c_del_driver(&msg21xx_ts_driver);
}

module_init(msg21xx_init);
module_exit(msg21xx_cleanup);

MODULE_AUTHOR("wax.wang cellon");
MODULE_DESCRIPTION("Driver for msg21xx Touchscreen Controller");
MODULE_LICENSE("GPL");
