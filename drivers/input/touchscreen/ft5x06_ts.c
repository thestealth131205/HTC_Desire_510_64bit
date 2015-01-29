/* drivers/input/touchscreen/ft5x06_ts.c
 *
 * FocalTech ft5x0x TouchScreen driver.
 *
 * Copyright (c) 2010  Focal tech Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/i2c.h>
#include <linux/input.h>
#if defined(CONFIG_FB)
#include <linux/notifier.h>
#include <linux/fb.h>
#elif defined(CONFIG_HAS_EARLYSUSPEND)
#include <linux/earlysuspend.h>
#endif
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <mach/irqs.h>
#include <linux/kernel.h>
#include <linux/semaphore.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/unistd.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/input/mt.h>
#include <linux/input/ft5x06_ts.h>
#include <linux/timer.h>
#include <mach/vreg.h>
#include <linux/regulator/consumer.h>
#include <mach/gpio.h>
#include <linux/gpio.h>
#include <linux/irq.h> 
#include <linux/of_gpio.h>
#include <linux/regulator/driver.h>

#ifdef CONFIG_TOUCHPANEL_PROXIMITY_SENSOR
#include <linux/mutex.h>
#include <linux/input-polldev.h>
#include <linux/wakelock.h>
#endif

#include "scap_test_lib.h"

#define FTS_SCAP_TEST

#ifdef FTS_SCAP_TEST
struct i2c_client *g_focalclient = NULL;
#endif

#define SYSFS_DEBUG
#define FTS_APK_DEBUG
#define FTS_CTL_IIC

#define FTS_REPORT_WITH_OLD_PROTOCAL       0
#define FTS_ENABLE_FW_AUTO_UPDATE      0

#define FT_COORDS_ARR_SIZE	4
#define MAX_BUTTONS		4

#define FT_VTG_MIN_UV		2850000
#define FT_VTG_MAX_UV		2850000
#define FT_I2C_VTG_MIN_UV	1800000
#define FT_I2C_VTG_MAX_UV	1800000

#if FTS_ENABLE_FW_AUTO_UPDATE
extern int fts_ctpm_auto_upgrade(struct i2c_client * client);
#endif

#ifdef FTS_CTL_IIC
#include "focaltech_ctl.h"
#endif

static struct i2c_client *g_i2c_client = NULL;
static struct ft5x0x_platform_data *p_platform_data = NULL;

#ifdef FT5336_DOWNLOAD
#include "ft5336_download_lib.h"
static unsigned char CTPM_MAIN_FW[]=
{
	#include "ft5336_all.i"
};
#endif

#ifdef SYSFS_DEBUG
#include "ft5x06_ex_fun.h"
#endif


struct ts_event {
	u16 au16_x[CFG_MAX_TOUCH_POINTS];	
	u16 au16_y[CFG_MAX_TOUCH_POINTS];	
	u8 au8_touch_event[CFG_MAX_TOUCH_POINTS];	
	u8 au8_finger_id[CFG_MAX_TOUCH_POINTS];	
	u16 pressure;
	u8 touch_point;
	u8 point_num;
};

struct ft5x0x_ts_data {
	struct regulator *vdd;
	struct regulator *vcc_i2c;
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct ts_event event;
	struct ft5x0x_platform_data *pdata;
	struct work_struct 	pen_event_work;
	struct workqueue_struct *ts_workqueue;
#ifdef CONFIG_TOUCHPANEL_PROXIMITY_SENSOR
	struct input_dev  *input_prox_dev;
	struct input_polled_dev *input_poll_dev;
#endif
#if defined(CONFIG_FB)
	struct notifier_block fb_notif;
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	struct early_suspend early_suspend;
#endif
	struct pinctrl *ts_pinctrl;
	struct pinctrl_state *gpio_state_active;
	struct pinctrl_state *gpio_state_suspend;
	char tp_version[64];
};

#define ANDROID_INPUT_PROTOCOL_B

#define LOG_TAG "FTS"


#if defined(TPD_DEBUG)
#undef TPD_DEBUG
#define TPD_DEBUG(a,arg...) printk(LOG_TAG ": " a,##arg)
#define TPD_DMESG(a,arg...) printk(LOG_TAG ": " a,##arg)
#else
#define TPD_DEBUG(arg...) 
#define TPD_DMESG(a,arg...)	
#endif


u8 fts_fw_version = 0;
u8 fts_fw_id = 0;


#ifdef CONFIG_TOUCHPANEL_PROXIMITY_SENSOR
#define PROXIMITY_INPUT_DEV_NAME 	"proximity"

#define SENSOR_PROX_TP_USE_WAKELOCK
static struct i2c_client * i2c_prox_client = NULL;
#ifdef SENSOR_PROX_TP_USE_WAKELOCK
static struct wake_lock sensor_prox_tp_wake_lock;
#endif
static DEFINE_MUTEX(tp_prox_sensor_mutex);

static int tp_prox_sensor_opened;
static char tp_prox_sensor_data = 0; 
static char tp_prox_sensor_data_changed = 0;
static char tp_prox_pre_sensor_data = 1;

static int is_suspend = 0;
static int is_need_report_pointer = 1;

static void tp_prox_sensor_enable(int enable);
#endif


#if defined(CONFIG_FB)
static int fb_notifier_callback(struct notifier_block *self,
				 unsigned long event, void *data);
#elif defined(CONFIG_HAS_EARLYSUSPEND)
static void ft5x0x_ts_earlysuspend(struct early_suspend *handler);
static void ft5x0x_ts_lateresume(struct early_suspend *handler);
#endif

/*
*ft5x0x_i2c_Read-read data and write data by i2c
*@client: handle of i2c
*@writebuf: Data that will be written to the slave
*@writelen: How many bytes to write
*@readbuf: Where to store data read from slave
*@readlen: How many bytes to read
*
*Returns negative errno, else the number of messages executed
*
*
*/
int ft5x0x_i2c_Read(struct i2c_client *client, char *writebuf,
		    int writelen, char *readbuf, int readlen)
{
	int ret;

	if (writelen > 0) {
		struct i2c_msg msgs[] = {
			{
			 .addr = client->addr,
			 .flags = 0,
			 .len = writelen,
			 .buf = writebuf,
			 },
			{
			 .addr = client->addr,
			 .flags = I2C_M_RD,
			 .len = readlen,
			 .buf = readbuf,
			 },
		};
		ret = i2c_transfer(client->adapter, msgs, 2);
		if (ret < 0)
			dev_err(&client->dev, "f%s: i2c read error.\n",
				__func__);
	} else {
		struct i2c_msg msgs[] = {
			{
			 .addr = client->addr,
			 .flags = I2C_M_RD,
			 .len = readlen,
			 .buf = readbuf,
			 },
		};
		ret = i2c_transfer(client->adapter, msgs, 1);
		if (ret < 0)
			dev_err(&client->dev, "%s:i2c read error.\n", __func__);
	}
	return ret;
}
int ft5x0x_i2c_Write(struct i2c_client *client, char *writebuf, int writelen)
{
	int ret;

	struct i2c_msg msg[] = {
		{
		 .addr = client->addr,
		 .flags = 0,
		 .len = writelen,
		 .buf = writebuf,
		 },
	};

	ret = i2c_transfer(client->adapter, msg, 1);
	if (ret < 0)
		dev_err(&client->dev, "%s i2c write error.\n", __func__);

	return ret;
}
#ifdef FTS_SCAP_TEST
int focal_i2c_Read(unsigned char *writebuf,
			int writelen, unsigned char *readbuf, int readlen)
{
	int ret;

	if (writelen > 0) {
		struct i2c_msg msgs[] = {
			{
			 .addr = g_focalclient->addr,
			 .flags = 0,
			 .len = writelen,
			 .buf = writebuf,
			 },
			{
			 .addr = g_focalclient->addr,
			 .flags = I2C_M_RD,
			 .len = readlen,
			 .buf = readbuf,
			 },
		};
		ret = i2c_transfer(g_focalclient->adapter, msgs, 2);
		if (ret < 0)
			dev_err(&g_focalclient->dev, "f%s: i2c read error.\n",
				__func__);
	} else {
		struct i2c_msg msgs[] = {
			{
			 .addr = g_focalclient->addr,
			 .flags = I2C_M_RD,
			 .len = readlen,
			 .buf = readbuf,
			 },
		};
		ret = i2c_transfer(g_focalclient->adapter, msgs, 1);
		if (ret < 0)
			dev_err(&g_focalclient->dev, "%s:i2c read error.\n", __func__);
	}
	return ret;
}
int focal_i2c_Write(unsigned char *writebuf, int writelen)
{
	int ret;

	struct i2c_msg msg[] = {
		{
		 .addr = g_focalclient->addr,
		 .flags = 0,
		 .len = writelen,
		 .buf = writebuf,
		 },
	};

	ret = i2c_transfer(g_focalclient->adapter, msg, 1);
	if (ret < 0)
		dev_err(&g_focalclient->dev, "%s i2c write error.\n", __func__);

	return ret;
}
#endif
#ifdef FT5336_DOWNLOAD
int ft5x0x_download_i2c_Read(unsigned char *writebuf,
		    int writelen, unsigned char *readbuf, int readlen)
{
	int ret;

	if (writelen > 0) {
		struct i2c_msg msgs[] = {
			{
			 .addr = 0x38,
			 .flags = 0,
			 .len = writelen,
			 .buf = writebuf,
			 },
			{
			 .addr = 0x38,
			 .flags = I2C_M_RD,
			 .len = readlen,
			 .buf = readbuf,
			 },
		};
		ret = i2c_transfer(g_i2c_client->adapter, msgs, 2);
		if (ret < 0)
			dev_err(&g_i2c_client->dev, "f%s: i2c read error.\n",
				__func__);
	} else {
		struct i2c_msg msgs[] = {
			{
			 .addr = 0x38,
			 .flags = I2C_M_RD,
			 .len = readlen,
			 .buf = readbuf,
			 },
		};
		ret = i2c_transfer(g_i2c_client->adapter, msgs, 1);
		if (ret < 0)
			dev_err(&g_i2c_client->dev, "%s:i2c read error.\n", __func__);
	}
	return ret;
}
int ft5x0x_download_i2c_Write(unsigned char *writebuf, int writelen)
{
	int ret;

	struct i2c_msg msg[] = {
		{
		 .addr = 0x38,
		 .flags = 0,
		 .len = writelen,
		 .buf = writebuf,
		 },
	};

	ret = i2c_transfer(g_i2c_client->adapter, msg, 1);
	if (ret < 0)
		dev_err(&g_i2c_client->dev, "%s i2c write error.\n", __func__);

	return ret;
}

#endif
static int ft5x0x_read_Touchdata(struct ft5x0x_ts_data *data)
{
	struct ts_event *event = &data->event;
	u8 buf[POINT_READ_BUF] = { 0 };
	int ret = -1;
	int i = 0;
	u8 pointid = FT_MAX_ID;
	int checkzero = 0;	
	
	#ifdef CONFIG_TOUCHPANEL_PROXIMITY_SENSOR
	u8 state;
	u8 proximity_status;
	#endif

	#ifdef CONFIG_TOUCHPANEL_PROXIMITY_SENSOR
	if (tp_prox_sensor_opened == 1)
	{
		i2c_smbus_read_i2c_block_data(data->client, 0xB0, 1, &state);
		TPD_DEBUG("proxi_5206 0xB0 state value is 1131 0x%02X\n", state);
		if(!(state&0x01))
		{
			tp_prox_sensor_enable(1);
		}
		i2c_smbus_read_i2c_block_data(data->client, 0x01, 1, &proximity_status);
		TPD_DEBUG("proxi_5206 0x01 value is 1139 0x%02X\n", proximity_status);
		tp_prox_pre_sensor_data = tp_prox_sensor_data;
		if (proximity_status == 0xC0)  
		{
			tp_prox_sensor_data = 0;	
		}
		else if(proximity_status == 0xE0)  
		{
			tp_prox_sensor_data = 1;
		}
		 TPD_DEBUG( "%s tp_pre_sensor_data=%d,tp_prox_sensor_data=%d\n", __func__,tp_prox_pre_sensor_data,tp_prox_sensor_data);
	 	if( tp_prox_pre_sensor_data != tp_prox_sensor_data)
	 	{  
		    TPD_DMESG( "%s ensor data changed\n", __func__);
			mutex_lock(&tp_prox_sensor_mutex);
	        tp_prox_sensor_data_changed = 1;
	        mutex_unlock(&tp_prox_sensor_mutex);
			return 1;
	 	}
		if(is_need_report_pointer == 0)
		{
			TPD_DMESG( ":%s:  we don not report pointer when sleep in call\n", __func__);
			return 1;
		}
		
	}  
	#endif


	ret = ft5x0x_i2c_Read(data->client, buf, 1, buf, POINT_READ_BUF);
	if (ret < 0) {
		dev_err(&data->client->dev, "%s read touchdata failed.\n",
			__func__);
		return ret;
	}
	memset(event, 0, sizeof(struct ts_event));

	event->touch_point = 0;
	event->point_num = buf[2] & 0x0f;
	for (i = 0; i < CFG_MAX_TOUCH_POINTS; i++) {
		pointid = (buf[FT_TOUCH_ID_POS + FT_TOUCH_STEP * i]) >> 4;
				event->au8_touch_event[i] =
		    buf[FT_TOUCH_EVENT_POS + FT_TOUCH_STEP * i] >> 6;
		if (pointid >= FT_MAX_ID && (3==event->au8_touch_event[i]))
			break;
		else
			event->touch_point++;
		event->au16_x[i] =
		    (s16) (buf[FT_TOUCH_X_H_POS + FT_TOUCH_STEP * i] & 0x0F) <<
		    8 | (s16) buf[FT_TOUCH_X_L_POS + FT_TOUCH_STEP * i];
		event->au16_y[i] =
		    (s16) (buf[FT_TOUCH_Y_H_POS + FT_TOUCH_STEP * i] & 0x0F) <<
		    8 | (s16) buf[FT_TOUCH_Y_L_POS + FT_TOUCH_STEP * i];
		event->au8_touch_event[i] =
		    buf[FT_TOUCH_EVENT_POS + FT_TOUCH_STEP * i] >> 6;
		event->au8_finger_id[i] =
		    (buf[FT_TOUCH_ID_POS + FT_TOUCH_STEP * i]) >> 4;
		#if 1
		TPD_DEBUG("id=%d event=%d x=%d y=%d\n", event->au8_finger_id[i],
			event->au8_touch_event[i], event->au16_x[i], event->au16_y[i]);
		#endif
	}
	for(i = 0; i < event->touch_point; i++ )
	{
		checkzero |= event->au16_x[i] | event->au16_y[i] | event->au8_touch_event[i] | event->au8_finger_id[i];		
		if(checkzero >= 0)
		{
			break;
		}
	}
	if(!checkzero)
	{
		event->touch_point = 0; 
		return 0;
	}

	event->pressure = FT_PRESS;

	return 0;
}

static void ft5x0x_report_value(struct ft5x0x_ts_data *data)
{
	struct ts_event *event = &data->event;
	int i;
#if !FTS_REPORT_WITH_OLD_PROTOCAL
	int uppoint = 0;
#endif

#if FTS_REPORT_WITH_OLD_PROTOCAL
	TPD_DEBUG(" ft5x0x_report_value event->touch_point=%d,event->point_num=%d\n",event->touch_point,event->point_num);
	if(event->point_num > 0)
	{
		for (i = 0; i < event->point_num; i++)
		{
			input_report_key(data->input_dev, BTN_TOUCH, 1);
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR, event->pressure);
			input_report_abs(data->input_dev, ABS_MT_POSITION_X, event->au16_x[i]);
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y, event->au16_y[i]);
			input_report_abs(data->input_dev, ABS_MT_TRACKING_ID, event->au8_finger_id[i]); 
			input_mt_sync(data->input_dev);
		}
		input_sync(data->input_dev);
	}
	else
	{
		input_report_key(data->input_dev, BTN_TOUCH, 0);
		input_mt_sync(data->input_dev);
		input_sync(data->input_dev);
	}
#else
		
	for (i = 0; i < event->touch_point; i++)
	{
		input_mt_slot(data->input_dev, event->au8_finger_id[i]);
		
		if (event->au8_touch_event[i]== 0 || event->au8_touch_event[i] == 2)
		{
			input_mt_report_slot_state(data->input_dev, MT_TOOL_FINGER,
				true);
			input_report_abs(data->input_dev, ABS_MT_TOUCH_MAJOR,
					event->pressure);
			input_report_abs(data->input_dev, ABS_MT_POSITION_X,
					event->au16_x[i]);
			input_report_abs(data->input_dev, ABS_MT_POSITION_Y,
					event->au16_y[i]);
		}
		else
		{
			uppoint++;
			input_mt_report_slot_state(data->input_dev, MT_TOOL_FINGER,
				false);
		}
	}
	if(event->touch_point == uppoint)
		input_report_key(data->input_dev, BTN_TOUCH, 0);
	else
		input_report_key(data->input_dev, BTN_TOUCH, event->touch_point > 0);
	input_sync(data->input_dev);
#endif

}


static void ft5x0x_ts_pen_irq_work(struct work_struct *work)
{
	struct ft5x0x_ts_data *ft5x0x_ts = container_of(work, struct ft5x0x_ts_data, pen_event_work);
	struct irq_desc *desc; 
	int ret = -1;

	ret = ft5x0x_read_Touchdata(ft5x0x_ts);
	if (ret == 0)
		ft5x0x_report_value(ft5x0x_ts);
	else if((ret == -ETIMEDOUT) || (ret == -ENOTCONN))
	{
		TPD_DMESG(" tp timeout, reset\n");
		
		gpio_set_value(ft5x0x_ts->pdata->reset_gpio, 0);
		msleep(ft5x0x_ts->pdata->hard_rst_dly);
		gpio_set_value(ft5x0x_ts->pdata->reset_gpio, 1);
	
	}
	enable_irq(ft5x0x_ts->client->irq);  

	desc = irq_to_desc(ft5x0x_ts->pdata->irq_gpio);
	if ((desc != NULL) && (desc->depth > 0))  
	{  
		TPD_DMESG(" there is something wrong for irq enable and disable\n");
		enable_irq(ft5x0x_ts->client->irq);  
	}
}


static irqreturn_t ft5x0x_ts_interrupt(int irq, void *dev_id)
{
	struct ft5x0x_ts_data *ft5x0x_ts = dev_id;

	disable_irq_nosync(ft5x0x_ts->client->irq);
	if (!work_pending(&ft5x0x_ts->pen_event_work))
	{
		queue_work(ft5x0x_ts->ts_workqueue, &ft5x0x_ts->pen_event_work);
	}
	else
	{
		TPD_DMESG("ft5x0x_ts_interrupt work in pending \n");
		enable_irq(ft5x0x_ts->client->irq);
	}
	return IRQ_HANDLED;
}

void ft5x0x_Enable_IRQ(struct i2c_client *client, int enable)
{
	if (1 == enable)
		enable_irq(client->irq);
	else
		disable_irq_nosync(client->irq);
}

#if 0
static int fts_init_gpio_hw(struct ft5x0x_ts_data *ft5x0x_ts)
{

	int ret = 0;
	gpio_direction_output(FT5X0X_RESET_GPIO, 1);
	gpio_set_value(FT5X0X_RESET_GPIO, 1);
	msleep(100);
	gpio_set_value(FT5X0X_RESET_GPIO, 0); 
    msleep(100);
    gpio_set_value(FT5X0X_RESET_GPIO, 1);  
    msleep(200);
	return ret;
}
#endif

static void fts_un_init_gpio_hw(struct ft5x0x_ts_data *ft5x0x_ts)
{

}

#ifdef FT5336_DOWNLOAD

FTS_I2c_Read_Function fun_i2c_read = ft5x0x_download_i2c_Read;
FTS_I2c_Write_Function fun_i2c_write = ft5x0x_download_i2c_Write;

int ft5336_Enter_Debug(void)
{
	ft5x0x_reset_tp(0);
	msleep(4);
	ft5x0x_reset_tp(1);
	return ft5336_Lib_Enter_Download_Mode();
}
int ft5336_IsDownloadMain(void)
{
	
	return -1;
}
int ft5336_DownloadMain(void)
{
	unsigned short fwlen = 0;
	
	if (ft5336_Enter_Debug() < 0) {
		pr_err("-----enter debug mode failed\n");
		return -1;
	}
	fwlen = sizeof(CTPM_MAIN_FW);
	pr_info("----fwlen=%d\n", fwlen);

	
	return ft5336_Lib_DownloadMain(CTPM_MAIN_FW, fwlen);
}
#endif

static int ft5x0x_read_version(struct i2c_client *client,u8 *module,u8 *fwversion)
{
	int ret = -1;
	ret = ft5x0x_read_reg(client,FT5x0x_REG_FW_VER,fwversion);
	if(ret < 0)
	{
		printk("wxun: %s: read FT5x0x_REG_FW_VER failed with ret=%d\n", __func__, ret);
		return ret;
	}
	ret = ft5x0x_read_reg(client,FT6x06_REG_VENDOR_ID,module);
	if(ret < 0)
	{
		printk("wxun: %s: read FT6x06_REG_VENDOR_ID failed with ret=%d\n", __func__, ret);
		return ret;
	}
	
	return ret;
}


#ifdef CONFIG_TOUCHPANEL_PROXIMITY_SENSOR

static void tp_prox_sensor_enable(int enable)
{
	u8 state;
	int ret = -1;

    if(i2c_prox_client==NULL)
    	return;


	i2c_smbus_read_i2c_block_data(i2c_prox_client, 0xB0, 1, &state);
	TPD_DMESG("[proxi_5206]read: 999 0xb0's value is 0x%02X\n", state);
	if (enable){
		state |= 0x01;
	}else{
		state &= 0x00;	
	}
	ret = i2c_smbus_write_i2c_block_data(i2c_prox_client, 0xB0, 1, &state);
	if(ret < 0)
	{
		TPD_DMESG("[proxi_5206]write psensor switch command failed\n");
	}
	return;
}

static ssize_t tp_prox_enable_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return snprintf(buf, 4, "%d\n", tp_prox_sensor_opened);
}

static ssize_t tp_prox_enable_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ft5x0x_ts_data *prox = i2c_get_clientdata(client);
	struct input_dev *input_dev = prox->input_prox_dev;
	unsigned long data;
	int error;

	error = kstrtoul(buf, 10, &data);
	if (error)
		return error;
	TPD_DMESG("%s, data=%ld\n",__func__,data);
	mutex_lock(&input_dev->mutex);
	disable_irq(client->irq);

	mutex_lock(&tp_prox_sensor_mutex);
	tp_prox_sensor_enable((int)data);
	if(data){
		#ifdef SENSOR_PROX_TP_USE_WAKELOCK
		wake_lock(&sensor_prox_tp_wake_lock);
		#endif
		tp_prox_sensor_opened = 1;
		tp_prox_sensor_data = 1;
		tp_prox_sensor_data_changed = 1;
	}else{
		tp_prox_sensor_opened = 0;
		#ifdef SENSOR_PROX_TP_USE_WAKELOCK
		wake_unlock(&sensor_prox_tp_wake_lock);
		#endif
	}
	mutex_unlock(&tp_prox_sensor_mutex);

	enable_irq(client->irq);
	mutex_unlock(&input_dev->mutex);

	return count;
}

static DEVICE_ATTR(enable, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
			tp_prox_enable_show, tp_prox_enable_store);

static ssize_t tp_prox_get_poll_delay(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ft5x0x_ts_data *ps = i2c_get_clientdata(client);

	return sprintf(buf, "%d\n", ps->input_poll_dev->poll_interval);
}

static ssize_t tp_prox_set_poll_delay(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ft5x0x_ts_data *ps = i2c_get_clientdata(client);
	struct input_dev *input_dev = ps->input_prox_dev;
	unsigned int interval;
	int error;

	error = kstrtouint(buf, 10, &interval);
	if (error < 0)
		return error;

	
	mutex_lock(&input_dev->mutex);

	disable_irq(client->irq);

	ps->input_poll_dev->poll_interval = max((int)interval,(int)ps->input_poll_dev->poll_interval_min);

	enable_irq(client->irq);
	mutex_unlock(&input_dev->mutex);

	return count;
}

static DEVICE_ATTR(poll_delay, S_IRUGO|S_IWUSR|S_IWGRP|S_IWOTH,
			tp_prox_get_poll_delay, tp_prox_set_poll_delay);

static struct attribute *tp_prox_attributes[] = {
	&dev_attr_enable.attr,
	&dev_attr_poll_delay.attr,
	NULL
};

static struct attribute_group tp_prox_attribute_group = {
	.attrs = tp_prox_attributes
};

static void __init tp_prox_init_input_device(struct ft5x0x_ts_data *prox,
					      struct input_dev *input_dev)
{
	__set_bit(EV_ABS, input_dev->evbit);
	input_set_abs_params(input_dev, ABS_DISTANCE, 0, 1, 0, 0);

	input_dev->name = PROXIMITY_INPUT_DEV_NAME;
	input_dev->id.bustype = BUS_I2C;
	input_dev->dev.parent = &prox->client->dev;
}

static void tp_prox_poll(struct input_polled_dev *dev)
{
	struct ft5x0x_ts_data *prox = dev->private;

	if (tp_prox_sensor_data_changed){
		mutex_lock(&tp_prox_sensor_mutex);
		tp_prox_sensor_data_changed = 0;
		mutex_unlock(&tp_prox_sensor_mutex);
		TPD_DMESG("%s poll tp_prox_sensor_data=%d\n",__func__,tp_prox_sensor_data);
		input_report_abs(prox->input_prox_dev, ABS_DISTANCE, tp_prox_sensor_data);
		input_sync(prox->input_prox_dev);
	}
}

static int __init tp_prox_setup_polled_device(struct ft5x0x_ts_data *ps)
{
	int err;
	struct input_polled_dev *poll_dev;

	poll_dev = input_allocate_polled_device();
	if (!poll_dev) {
		TPD_DMESG("Failed to allocate polled device\n");
		return -ENOMEM;
	}

	ps->input_poll_dev = poll_dev;
	ps->input_prox_dev = poll_dev->input;

	poll_dev->private = ps;
	poll_dev->poll = tp_prox_poll;
	
	
	poll_dev->poll_interval = 100;
	poll_dev->poll_interval_min= 0;

	tp_prox_init_input_device(ps, poll_dev->input);
	err = input_register_polled_device(poll_dev);
	TPD_DMESG("%s, err=%d, poll-interval=%d\n",__func__,err,poll_dev->poll_interval);
	if (err) {
		TPD_DMESG("Unable to register polled device, err=%d\n", err);
		input_free_polled_device(poll_dev);
		return -ENOMEM;
	}

	return 0;
}

static void tp_prox_teardown_polled_device(struct ft5x0x_ts_data *ps)
{
	input_unregister_polled_device(ps->input_poll_dev);
	input_free_polled_device(ps->input_poll_dev);
}
#endif

extern int  fts_ctpm_fw_upgrade(struct i2c_client * client, u8* pbt_buf, u32 dw_lenth);
extern int is_tp_driver_loaded;
extern int ft5x06_self_test(void);
static int ft5x06_power_init(struct ft5x0x_ts_data *data, bool on)
{
	int rc;

	TPD_DMESG("%s: on=%d\n", __func__, on);
	
	if (!on)
		goto pwr_deinit;

	if (0)
	{
		TPD_DMESG("%s, power_ldo_gpio\n", __func__);
		rc = gpio_request(data->pdata->power_ldo_gpio, "msg21xx_ldo_gpio");
		if (rc)
		{
			printk("irq gpio request failed\n");
			return rc;
		}
		
		rc = gpio_direction_output(data->pdata->power_ldo_gpio, 1);
		if (rc)
		{
			printk("set_direction for irq gpio failed\n");
			goto free_ldo_gpio;
		}
	}
	else
	{
		TPD_DMESG("%s, regulator vdd\n", __func__);
		data->vdd = regulator_get(&data->client->dev, "vdd");
		if (IS_ERR(data->vdd))
		{
			rc = PTR_ERR(data->vdd);
			dev_err(&data->client->dev, "Regulator get failed vdd rc=%d\n", rc);
			return rc;
		}

		if (regulator_count_voltages(data->vdd) > 0)
		{
			rc = regulator_set_voltage(data->vdd, FT_VTG_MIN_UV, FT_VTG_MAX_UV);
			if (rc)
			{
				dev_err(&data->client->dev, "Regulator set_vtg failed vdd rc=%d\n", rc);
				goto reg_vdd_put;
			}
		}
	}
	
	data->vcc_i2c = regulator_get(&data->client->dev, "vcc_i2c");
	if (IS_ERR(data->vcc_i2c))
	{
		rc = PTR_ERR(data->vcc_i2c);
		dev_err(&data->client->dev, "Regulator get failed vcc_i2c rc=%d\n", rc);
		goto reg_vdd_set_vtg;
	}

	if (regulator_count_voltages(data->vcc_i2c) > 0)
	{
		rc = regulator_set_voltage(data->vcc_i2c, FT_I2C_VTG_MIN_UV, FT_I2C_VTG_MAX_UV);
		if (rc)
		{
			dev_err(&data->client->dev, "Regulator set_vtg failed vcc_i2c rc=%d\n", rc);
			goto reg_vcc_i2c_put;
		}
	}
	TPD_DMESG("%s: done\n", __func__);
	return 0;

reg_vcc_i2c_put:
	regulator_put(data->vcc_i2c);
reg_vdd_set_vtg:
	if (regulator_count_voltages(data->vdd) > 0)
		regulator_set_voltage(data->vdd, 0, FT_VTG_MAX_UV);
reg_vdd_put:
free_ldo_gpio:
	if (0)
		gpio_free(data->pdata->power_ldo_gpio);
	else
		regulator_put(data->vdd);
	
	TPD_DMESG("%s: err, rc=%d\n", __func__, rc);
	return rc;

pwr_deinit:
	if (0)
		gpio_free(data->pdata->power_ldo_gpio);
	else
	{
		if (regulator_count_voltages(data->vdd) > 0)
			regulator_set_voltage(data->vdd, 0, FT_VTG_MAX_UV);

		regulator_put(data->vdd);
	}
	
	if (regulator_count_voltages(data->vcc_i2c) > 0)
		regulator_set_voltage(data->vcc_i2c, 0, FT_I2C_VTG_MAX_UV);

	regulator_put(data->vcc_i2c);
	TPD_DMESG("%s: pwr_deinit done\n", __func__);
	return 0;
}

static int ft5x06_power_on(struct ft5x0x_ts_data *data, bool on)
{
	int rc;
	int voltage_vdd;

	TPD_DMESG("%s: on=%d\n", __func__, on);
	if (!on)
		goto power_off;
	
	if (0)
	{
		TPD_DMESG("%s, power_ldo_gpio\n", __func__);
		gpio_set_value(data->pdata->power_ldo_gpio, 1);
	}
	else
	{
		TPD_DMESG("%s, regulator vdd\n", __func__);
		rc = regulator_enable(data->vdd);
		voltage_vdd = regulator_get_voltage(data->vdd);
		if (rc)
		{
			dev_err(&data->client->dev, "Regulator vdd enable failed rc=%d\n", rc);
			return rc;
		}
	}
	
	rc = regulator_enable(data->vcc_i2c);
	if (rc)
	{
		dev_err(&data->client->dev, "Regulator vcc_i2c enable failed rc=%d\n", rc);
		regulator_disable(data->vdd);
	}

	TPD_DMESG("%s: done, rc=%d\n", __func__, rc);
	return rc;

power_off:
	if (0)
	{
		gpio_set_value(data->pdata->power_ldo_gpio, 0);
	}
	else
	{
		rc = regulator_disable(data->vdd);
		if (rc)
		{
			dev_err(&data->client->dev, "Regulator vdd disable failed rc=%d\n", rc);
			return rc;
		}
	}
	
	rc = regulator_disable(data->vcc_i2c);
	if (rc)
	{
		dev_err(&data->client->dev, "Regulator vcc_i2c disable failed rc=%d\n", rc);
	}
	
	TPD_DMESG("%s: power_off, rc=%d\n", __func__, rc);
	return rc;
}

void ft5x0x_reset_tp(int HighOrLow)
{
	pr_info("set tp reset pin to %d\n", HighOrLow);
	gpio_set_value(p_platform_data->reset_gpio, HighOrLow);
}

static int ft5x06_ts_pinctrl_init(struct ft5x0x_ts_data *ft5x06_data)
{
	int retval;

	
	ft5x06_data->ts_pinctrl = devm_pinctrl_get(&(ft5x06_data->client->dev));
	if (IS_ERR_OR_NULL(ft5x06_data->ts_pinctrl))
	{
		dev_dbg(&ft5x06_data->client->dev, "Target does not use pinctrl\n");
		retval = PTR_ERR(ft5x06_data->ts_pinctrl);
		ft5x06_data->ts_pinctrl = NULL;
		return retval;
	}

	ft5x06_data->gpio_state_active = pinctrl_lookup_state(ft5x06_data->ts_pinctrl, "pmx_ts_active");
	if (IS_ERR_OR_NULL(ft5x06_data->gpio_state_active))
	{
		dev_dbg(&ft5x06_data->client->dev, "Can not get ts default pinstate\n");
		retval = PTR_ERR(ft5x06_data->gpio_state_active);
		ft5x06_data->ts_pinctrl = NULL;
		return retval;
	}

	ft5x06_data->gpio_state_suspend	= pinctrl_lookup_state(ft5x06_data->ts_pinctrl, "pmx_ts_suspend");
	if (IS_ERR_OR_NULL(ft5x06_data->gpio_state_suspend)) {
		dev_err(&ft5x06_data->client->dev, "Can not get ts sleep pinstate\n");
		retval = PTR_ERR(ft5x06_data->gpio_state_suspend);
		ft5x06_data->ts_pinctrl = NULL;
		return retval;
	}

	return 0;
}

static int ft5x06_ts_pinctrl_select(struct ft5x0x_ts_data *ft5x06_data, bool on)
{
	struct pinctrl_state *pins_state;
	int ret;

	pins_state = on ? ft5x06_data->gpio_state_active : ft5x06_data->gpio_state_suspend;
	if (!IS_ERR_OR_NULL(pins_state))
	{
		ret = pinctrl_select_state(ft5x06_data->ts_pinctrl, pins_state);
		if (ret)
		{
			dev_err(&ft5x06_data->client->dev, "can not set %s pins\n",
				on ? "pmx_ts_active" : "pmx_ts_suspend");
			return ret;
		}
	}
	else
	{
		dev_err(&ft5x06_data->client->dev, "not a valid '%s' pinstate\n",
			on ? "pmx_ts_active" : "pmx_ts_suspend");
	}

	return 0;
}

#ifdef CONFIG_OF
static int ft5x06_get_dt_coords(struct device *dev, char *name, struct ft5x0x_platform_data *pdata)
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
	if (coords_size != FT_COORDS_ARR_SIZE)
	{
		dev_err(dev, "invalid %s\n", name);
		return -EINVAL;
	}

	rc = of_property_read_u32_array(np, name, coords, coords_size);
	if (rc && (rc != -EINVAL))
	{
		dev_err(dev, "Unable to read %s\n", name);
		return rc;
	}

	if (!strcmp(name, "focaltech,panel-coords"))
	{
		pdata->panel_minx = coords[0];
		pdata->panel_miny = coords[1];
		pdata->panel_maxx = coords[2];
		pdata->panel_maxy = coords[3];
	}
	else if (!strcmp(name, "focaltech,display-coords"))
	{
		pdata->x_min = coords[0];
		pdata->y_min = coords[1];
		pdata->x_max = coords[2];
		pdata->y_max = coords[3];
	}
	else
	{
		dev_err(dev, "unsupported property %s\n", name);
		return -EINVAL;
	}

	return 0;
}

static int ft5x06_parse_dt(struct device *dev,
			struct ft5x0x_platform_data *pdata)
{
	int rc;
	struct device_node *np = dev->of_node;
	struct property *prop;
	u32 temp_val, num_buttons;
	u32 button_map[MAX_BUTTONS];

	pdata->name = "focaltech";
	rc = of_property_read_string(np, "focaltech,name", &pdata->name);
	if (rc && (rc != -EINVAL))
		dev_err(dev, "Unable to read name\n");

	rc = ft5x06_get_dt_coords(dev, "focaltech,panel-coords", pdata);
	if (rc && (rc != -EINVAL))
		dev_err(dev, "Unable to read panel-coords\n");

	rc = ft5x06_get_dt_coords(dev, "focaltech,display-coords", pdata);
	if (rc)
		dev_err(dev, "Unable to read display-coords\n");

	pdata->i2c_pull_up = of_property_read_bool(np, "focaltech,i2c-pull-up");
	pdata->no_force_update = of_property_read_bool(np, "focaltech,no-force-update");
	
	pdata->reset_gpio = of_get_named_gpio_flags(np, "focaltech,reset-gpio", 0, &pdata->reset_gpio_flags);
	if (pdata->reset_gpio < 0)
		dev_err(dev, "%s, Unable to read reset_gpio\n", __func__);

	pdata->irq_gpio = of_get_named_gpio_flags(np, "focaltech,irq-gpio", 0, &pdata->irq_gpio_flags);
	if (pdata->irq_gpio < 0)
		dev_err(dev, "%s, Unable to read irq_gpio\n", __func__);

	
 	pdata->power_ldo_gpio = of_get_named_gpio_flags(np, "focaltech,power_ldo-gpio", 0, &pdata->power_ldo_gpio_flags);
	if (pdata->power_ldo_gpio < 0)
		dev_err(dev, "%s, Unable to read power_ldo_gpio\n", __func__);

	rc = of_property_read_string(np, "focaltech,fw-name", &pdata->fw_name);
	if (rc && (rc != -EINVAL))
		dev_err(dev, "%s, Unable to read fw-name\n", __func__);

	rc = of_property_read_u32(np, "focaltech,group-id", &temp_val);
	if (!rc)
		pdata->group_id = temp_val;
	else
		dev_err(dev, "%s, Unable to read group-id\n", __func__);

	rc = of_property_read_u32(np, "focaltech,hard-reset-delay-ms", &temp_val);
	if (!rc)
		pdata->hard_rst_dly = temp_val;
	else
		dev_err(dev, "%s, Unable to read hard-reset-delay-ms\n", __func__);

	rc = of_property_read_u32(np, "focaltech,soft-reset-delay-ms", &temp_val);
	if (!rc)
		pdata->soft_rst_dly = temp_val;
	else
		dev_err(dev, "%s, Unable to read soft-reset-delay-ms\n", __func__);

	rc = of_property_read_u32(np, "focaltech,num-max-touches", &temp_val);
	if (!rc)
		pdata->num_max_touches = temp_val;
	else
		dev_err(dev, "%s, Unable to read num-max-touches\n", __func__);

	rc = of_property_read_u32(np, "focaltech,fw-delay-aa-ms", &temp_val);
	if (rc && (rc != -EINVAL))
		dev_err(dev, "%s, Unable to read fw-delay-aa-ms\n", __func__);
	else if (rc != -EINVAL)
		pdata->info.delay_aa =  temp_val;

	rc = of_property_read_u32(np, "focaltech,fw-delay-55-ms", &temp_val);
	if (rc && (rc != -EINVAL))
		dev_err(dev, "%s, Unable to read fw-delay-55-ms\n", __func__);
	else if (rc != -EINVAL)
		pdata->info.delay_55 =  temp_val;

	rc = of_property_read_u32(np, "focaltech,fw-upgrade-id1", &temp_val);
	if (rc && (rc != -EINVAL))
		dev_err(dev, "%s, Unable to read fw-upgrade-id1\n", __func__);
	else if (rc != -EINVAL)
		pdata->info.upgrade_id_1 =  temp_val;

	rc = of_property_read_u32(np, "focaltech,fw-upgrade-id2", &temp_val);
	if (rc && (rc != -EINVAL))
		dev_err(dev, "%s, Unable to read fw-upgrade-id2\n", __func__);
	else if (rc != -EINVAL)
		pdata->info.upgrade_id_2 =  temp_val;

	rc = of_property_read_u32(np, "focaltech,fw-delay-readid-ms", &temp_val);
	if (rc && (rc != -EINVAL))
		dev_err(dev, "%s, Unable to read fw-delay-readid-ms\n", __func__);
	else if (rc != -EINVAL)
		pdata->info.delay_readid =  temp_val;

	rc = of_property_read_u32(np, "focaltech,fw-delay-era-flsh-ms", &temp_val);
	if (rc && (rc != -EINVAL))
		dev_err(dev, "%s, Unable to read fw-delay-era-flsh-ms\n", __func__);
	else if (rc != -EINVAL)
		pdata->info.delay_erase_flash =  temp_val;

	pdata->info.auto_cal = of_property_read_bool(np, "focaltech,fw-auto-cal");
	pdata->fw_vkey_support = of_property_read_bool(np, "focaltech,fw-vkey-support");
	pdata->ignore_id_check = of_property_read_bool(np, "focaltech,ignore-id-check");

	rc = of_property_read_u32(np, "focaltech,family-id", &temp_val);
	if (!rc)
		pdata->family_id = temp_val;
	else
		dev_err(dev, "%s, Unable to read family_id\n", __func__);

	prop = of_find_property(np, "focaltech,button-map", NULL);
	if (prop)
	{
		num_buttons = prop->length / sizeof(temp_val);
		if (num_buttons > MAX_BUTTONS)
			return -EINVAL;

		rc = of_property_read_u32_array(np, "focaltech,button-map", button_map, num_buttons);
		if (rc)
			dev_err(dev, "%s, Unable to read button-map\n", __func__);
	}

	return 0;
}
#else
static int ft5x06_parse_dt(struct device *dev, struct ft5x06_ts_platform_data *pdata)
{
	return -ENODEV;
}
#endif

static int ft5x0x_ts_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct ft5x0x_platform_data *pdata ;
	struct ft5x0x_ts_data *ft5x0x_ts;
	struct input_dev *input_dev;
	int err = 0;
	unsigned char uc_reg_value;
	unsigned char uc_reg_addr;
	char tp_vendor[16];

	printk("%s, is_tp_driver_loaded=%d\n", __func__, is_tp_driver_loaded);
	if (is_tp_driver_loaded)
	{
		TPD_DMESG("other driver has been loaded\n");
		return -ENODEV;
	}

	if (client->dev.of_node)
	{
		pdata = devm_kzalloc(&client->dev, sizeof(struct ft5x0x_platform_data), GFP_KERNEL);
		if (!pdata)
		{
			err = -ENOMEM;
			goto Err_devm_kzalloc_data;
		}
		
		err = ft5x06_parse_dt(&client->dev, pdata);
		if (err)
		{
			dev_err(&client->dev, "DT parsing failed\n");
			goto Err_parse_dt;
		}
	}
	else
		pdata = client->dev.platform_data;

	ft5x0x_ts = kzalloc(sizeof(struct ft5x0x_ts_data), GFP_KERNEL);
	if (!ft5x0x_ts)
	{
		err = -ENOMEM;
		goto Err_kzalloc_data;
	}
	
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
	{
		err = -ENODEV;
		goto Err_check_functionality;
	}

	ft5x0x_ts->client = client;
	ft5x0x_ts->pdata = pdata;
	i2c_set_clientdata(client, ft5x0x_ts);
	
#ifdef FTS_SCAP_TEST
	g_focalclient = client; 
#endif
	
	err = ft5x06_ts_pinctrl_init(ft5x0x_ts);
	if (!err && ft5x0x_ts->ts_pinctrl)
	{
		err = ft5x06_ts_pinctrl_select(ft5x0x_ts, true);
		if (err < 0)
			goto Err_pinctrl_select;
	}
	else
		goto Err_pinctrl_init;
	
	
	err = ft5x06_power_init(ft5x0x_ts, true);
	if (err)
	{
		dev_err(&client->dev, "power init failed");
		goto Err_power_init;
	}
	
	err = ft5x06_power_on(ft5x0x_ts, true);
	if (err)
	{
		dev_err(&client->dev, "power on failed");
		goto Err_power_on;
	}

	
	if (gpio_is_valid(pdata->irq_gpio))
	{
		err = gpio_request(pdata->irq_gpio, "ft5x06_irq_gpio");
		if (err)
		{
			dev_err(&client->dev, "irq gpio request failed");
			goto Err_gpio_request_irq;
		}
		err = gpio_direction_input(pdata->irq_gpio);
		if (err)
		{
			dev_err(&client->dev, "set_direction for irq gpio failed\n");
			goto Err_gpio_direction_input_irq;
		}
	}
	
	if (gpio_is_valid(pdata->reset_gpio))
	{
		err = gpio_request(pdata->reset_gpio, "ft5x06_reset_gpio");
		if (err)
		{
			dev_err(&client->dev, "reset gpio request failed");
			goto Err_gpio_request_reset;
		}
	
		err = gpio_direction_output(pdata->reset_gpio, 1);
		if (err)
		{
			dev_err(&client->dev, "set_direction for reset gpio failed\n");
			goto Err_gpio_direction_input_reset;
		}
		
		gpio_set_value_cansleep(pdata->reset_gpio, 1);
	}
	
	msleep(pdata->soft_rst_dly);		

	ft5x0x_ts->client = client;
	ft5x0x_ts->client->irq = gpio_to_irq(pdata->irq_gpio);
	ft5x0x_ts->pdata = pdata;
	TPD_DMESG("irq=%d\n", client->irq);

	if (ft5x0x_read_version(client, &fts_fw_id, &fts_fw_version) < 0)
	{
		TPD_DMESG("ft5x0x_ts_probe read verion failed\n");
		goto Err_read_version;
	}

	INIT_WORK(&ft5x0x_ts->pen_event_work, ft5x0x_ts_pen_irq_work);
	ft5x0x_ts->ts_workqueue = create_singlethread_workqueue(dev_name(&client->dev));
	if (!ft5x0x_ts->ts_workqueue)
	{
		err = -ESRCH;
		TPD_DMESG("fail to create wq\n");
		goto Err_ts_workqueue;
	}
	
	printk("%s: fts_fw_id=0x%x, fts_fw_version=0x%x\n", __func__, fts_fw_id, fts_fw_version);
	
	if(fts_fw_id == 0x5F)		
		strlcpy(tp_vendor, "yushun", sizeof(tp_vendor));
	else if(fts_fw_id == 0xA6)		
		strlcpy(tp_vendor, "boyi", sizeof(tp_vendor));
	else
		strlcpy(tp_vendor, "unknown", sizeof(tp_vendor));
	
	snprintf(ft5x0x_ts->tp_version, sizeof(ft5x0x_ts->tp_version), "vid-%s,fw-%x", tp_vendor, fts_fw_version);

#if FTS_ENABLE_FW_AUTO_UPDATE
	TPD_DMESG(" start check fw add update if needed\n");
	fts_ctpm_auto_upgrade(client);
#endif
	
	err = request_irq(client->irq, ft5x0x_ts_interrupt, IRQF_TRIGGER_FALLING, "ft5x0x", ft5x0x_ts);
	if (err < 0)
	{
		TPD_DMESG(" request irq failed\n");
		goto Err_request_threaded_irq;
	}
	
	disable_irq(client->irq);

	input_dev = input_allocate_device();
	if (!input_dev)
	{
		err = -ENOMEM;
		TPD_DMESG(" failed to allocate input device\n");
		goto Err_input_allocate_device;
	}

	ft5x0x_ts->input_dev = input_dev;

	__set_bit(EV_ABS, input_dev->evbit);
	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(BTN_TOUCH, input_dev->keybit);
	__set_bit(INPUT_PROP_DIRECT, input_dev->propbit);
	
#if FTS_REPORT_WITH_OLD_PROTOCAL
	input_set_abs_params(input_dev, ABS_MT_TRACKING_ID, 0, CFG_MAX_TOUCH_POINTS, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_X, 0, pdata->x_max, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0, pdata->y_max, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, PRESS_MAX, 0, 0); 
	input_set_abs_params(input_dev, ABS_MT_WIDTH_MAJOR, 0, PRESS_MAX, 0, 0); 
	input_set_abs_params(input_dev, ABS_MT_PRESSURE, 0, 0xFF, 0, 0);
#else
	input_mt_init_slots(input_dev, CFG_MAX_TOUCH_POINTS, 0);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, PRESS_MAX, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_X, 0, pdata->x_max, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0, pdata->y_max, 0, 0);
#endif
	input_dev->name = FT5X0X_NAME;
	err = input_register_device(input_dev);
	if (err)
	{
		TPD_DMESG(" failed to register input device\n");
		goto Err_input_register_device;
	}
	
	
	msleep(pdata->soft_rst_dly);
	
#ifdef SYSFS_DEBUG
	ft5x0x_create_sysfs(client);
#endif
#ifdef FTS_CTL_IIC
	if (ft_rw_iic_drv_init(client) < 0)
		TPD_DMESG(" create fts control iic driver failed\n");
#endif
#ifdef FTS_APK_DEBUG
	ft5x0x_create_apk_debug_channel(client);
#endif

	g_i2c_client = client;
	p_platform_data = pdata;

#ifdef FT5336_DOWNLOAD
	Init_I2C_Read_Func(fun_i2c_read);
	Init_I2C_Write_Func(fun_i2c_write);
	if (ft5336_IsDownloadMain() < 0)
	{
		pr_info("--------FTS---------download main\n");
		if (ft5336_DownloadMain() < 0)
			pr_err("---------FTS---------Download main failed\n");
	}
	else
		pr_info("--------FTS---------no download main\n");
#endif

	
	uc_reg_addr = FT5x0x_REG_FW_VER;
	ft5x0x_i2c_Read(client, &uc_reg_addr, 1, &uc_reg_value, 1);
	TPD_DMESG( " Firmware version = 0x%x\n", uc_reg_value);

	uc_reg_addr = FT5x0x_REG_POINT_RATE;
	ft5x0x_i2c_Read(client, &uc_reg_addr, 1, &uc_reg_value, 1);
	TPD_DMESG(" report rate is %dHz.\n", uc_reg_value * 10);

	uc_reg_addr = FT5X0X_REG_THGROUP;
	ft5x0x_i2c_Read(client, &uc_reg_addr, 1, &uc_reg_value, 1);
	TPD_DMESG(" touch threshold is %d.\n", uc_reg_value * 4);

#if defined(CONFIG_FB)
	ft5x0x_ts->fb_notif.notifier_call = fb_notifier_callback;
	err = fb_register_client(&ft5x0x_ts->fb_notif);
	if (err)
	{
		TPD_DMESG("Unable to register fb_notifier: %d\n", err);
		goto Err_fb_notif;
	}
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	ft5x0x_ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ft5x0x_ts->early_suspend.suspend = ft5x0x_ts_earlysuspend;
	ft5x0x_ts->early_suspend.resume = ft5x0x_ts_lateresume;
	register_early_suspend(&ft5x0x_ts->early_suspend);
#endif

#ifdef CONFIG_TOUCHPANEL_PROXIMITY_SENSOR
	i2c_prox_client = client;
#ifdef SENSOR_PROX_TP_USE_WAKELOCK
	wake_lock_init(&sensor_prox_tp_wake_lock, WAKE_LOCK_SUSPEND, "prox_tp");
#endif

	err = sysfs_create_group(&client->dev.kobj, &tp_prox_attribute_group);
	if (err)
	{
		TPD_DMESG( "sysfs create failed: %d\n", err);
		goto Err_attribute_group;
	}

	tp_prox_setup_polled_device(ft5x0x_ts);
#endif

	enable_irq(client->irq);


	is_tp_driver_loaded = 1;
	return 0;
	
#ifdef CONFIG_TOUCHPANEL_PROXIMITY_SENSOR
Err_attribute_group:
	wake_lock_destroy(&sensor_prox_tp_wake_lock);
#if defined(CONFIG_FB)
	fb_unregister_client(&ft5x0x_ts->fb_notif);
#endif
#endif
#if defined(CONFIG_FB)
Err_fb_notif:
	input_unregister_device(input_dev);
#endif
Err_input_register_device:
	input_free_device(input_dev);
Err_input_allocate_device:
	free_irq(client->irq, ft5x0x_ts);
Err_request_threaded_irq:
	destroy_workqueue(ft5x0x_ts->ts_workqueue);
Err_ts_workqueue:
Err_read_version:
Err_gpio_direction_input_reset:
	gpio_free(pdata->reset_gpio);
Err_gpio_request_reset:
Err_gpio_direction_input_irq:
	gpio_free(pdata->irq_gpio);
Err_gpio_request_irq:
	ft5x06_power_on(ft5x0x_ts, false);
Err_power_on:
	ft5x06_power_init(ft5x0x_ts, false);
Err_power_init:
Err_pinctrl_select:
	if (ft5x0x_ts->ts_pinctrl)
		pinctrl_put(ft5x0x_ts->ts_pinctrl);
Err_pinctrl_init:
	i2c_set_clientdata(client, NULL);
Err_check_functionality:
	kfree(ft5x0x_ts);
Err_kzalloc_data:
Err_parse_dt:
	if (client->dev.of_node && (pdata != NULL))
		devm_kfree(&client->dev, pdata);
Err_devm_kzalloc_data:
	printk("%s: err=%d\n", __func__, err);
	return err;
}


#if defined(CONFIG_FB)
static void ft5x0x_ts_suspend(struct ft5x0x_ts_data *ts)
{
	int retval = 0;
	
#ifdef CONFIG_TOUCHPANEL_PROXIMITY_SENSOR
	if(tp_prox_sensor_opened)
	{
		TPD_DMESG("tp can not sleep in call\n");
		is_need_report_pointer = 0;
		return;
	}
#endif
	TPD_DMESG("%s\n", __func__);
	disable_irq(ts->client->irq);
	retval = cancel_work_sync(&ts->pen_event_work);  
	if (retval)
	{
		TPD_DMESG("cancel work sync\n");
		enable_irq(ts->client->irq);  
	}
	
	flush_workqueue(ts->ts_workqueue);
	ft5x0x_write_reg(ts->client,0xa5, 0x03);
#ifdef CONFIG_TOUCHPANEL_PROXIMITY_SENSOR
	is_suspend = 1;
#endif
}

static void ft5x0x_ts_resume(struct ft5x0x_ts_data *ts)
{
	int count =10;
	struct irq_desc *desc;
	struct ts_event *event = &ts->event;

#ifdef CONFIG_TOUCHPANEL_PROXIMITY_SENSOR
	is_need_report_pointer = 1;
	if (tp_prox_sensor_opened && !is_suspend)
	{
		TPD_DMESG("%s tp no need to wake up in call\n", __func__);
		return;
	}
#endif

	TPD_DMESG("ft5x0x resume.\n");
	input_mt_slot(ts->input_dev, event->au8_finger_id[0]);
	input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, false);
	input_mt_slot(ts->input_dev, event->au8_finger_id[1]);
	input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, false);
	input_report_key(ts->input_dev, BTN_TOUCH, 0);
	input_sync(ts->input_dev);

	gpio_set_value(ts->pdata->reset_gpio, 0);
	msleep(ts->pdata->hard_rst_dly);
	gpio_set_value(ts->pdata->reset_gpio, 1);
	
	enable_irq(ts->client->irq);
	desc = irq_to_desc(ts->client->irq); 
	while (desc && (desc->depth>0) && (count>0))
	{     
		TPD_DMESG("there is something wrong for irq enable and disable in resume count=%d\n", count);
		enable_irq(ts->client->irq);
		count--;
	}  
#ifdef CONFIG_TOUCHPANEL_PROXIMITY_SENSOR
	is_suspend = 0;
#endif
}

static int fb_notifier_callback(struct notifier_block *self, unsigned long event, void *data)
{
	struct ft5x0x_ts_data *ts = container_of(self, struct ft5x0x_ts_data, fb_notif);
	struct fb_event *evdata = data;
	int *blank;

	if (evdata && evdata->data && event==FB_EVENT_BLANK && ts && ts->client)
	{
		blank = evdata->data;
		if (*blank == FB_BLANK_UNBLANK)
			ft5x0x_ts_resume(ts);
		else if (*blank == FB_BLANK_POWERDOWN)
			ft5x0x_ts_suspend(ts);
	}

	return 0;
}

#elif defined(CONFIG_HAS_EARLYSUSPEND)

static void ft5x0x_ts_earlysuspend(struct early_suspend *handler)
{
	int retval = 0; 
	struct ft5x0x_ts_data *ts = container_of(handler, struct ft5x0x_ts_data,early_suspend);

	TPD_DMESG("ft5x0x suspend\n");
	disable_irq(ts->client->irq);
	retval = cancel_work_sync(&ts->pen_event_work);  
	if (retval)
	{ 
		TPD_DMESG("cancel work sync\n");
		enable_irq(ts->client->irq);  
	}  

	flush_workqueue(ts->ts_workqueue);
	ft5x0x_write_reg(ts->client, 0xa5, 0x03);
}

static void ft5x0x_ts_lateresume(struct early_suspend *handler)
{
	int count =10;
	struct ft5x0x_ts_data *ts = container_of(handler, struct ft5x0x_ts_data, early_suspend);
	struct irq_desc *desc;
	
	TPD_DMESG("ft5x0x resume.\n");

	gpio_set_value(ts->pdata->reset_gpio, 0);
	msleep(ts->pdata->hard_rst_dly);
	gpio_set_value(ts->pdata->reset_gpio, 1);
	enable_irq(ts->client->irq);
	desc = irq_to_desc(ts->pdata->irq); 
	while (desc && (desc->depth>0) && (count>0))
	{     
		TPD_DMESG("there is something wrong for irq enable and disable in resume count=%d\n",count);
		enable_irq(ts->client->irq);
		count--;
	}  
}
#endif

static int __exit ft5x0x_ts_remove(struct i2c_client *client)
{
	struct ft5x0x_ts_data *ft5x0x_ts = i2c_get_clientdata(client);
	
	input_unregister_device(ft5x0x_ts->input_dev);
	input_free_device(ft5x0x_ts->input_dev);
	cancel_work_sync(&ft5x0x_ts->pen_event_work);
	destroy_workqueue(ft5x0x_ts->ts_workqueue);
#ifdef CONFIG_TOUCHPANEL_PROXIMITY_SENSOR
    sysfs_remove_group(&client->dev.kobj, &tp_prox_attribute_group);
    tp_prox_teardown_polled_device(ft5x0x_ts);
#ifdef SENSOR_PROX_TP_USE_WAKELOCK
    wake_lock_destroy(&sensor_prox_tp_wake_lock);
#endif
#endif
#if defined(CONFIG_FB)
	if (fb_unregister_client(&ft5x0x_ts->fb_notif))
		pr_info("Error occurred while unregistering fb_notifier.\n");
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	unregister_early_suspend(&ft5x0x_ts->early_suspend);
#endif
#ifdef FTS_CTL_IIC
	ft_rw_iic_drv_exit();
#endif
#ifdef SYSFS_DEBUG
	ft5x0x_remove_sysfs(client);
#endif
#ifdef FTS_APK_DEBUG
	ft5x0x_release_apk_debug_channel();
#endif
	fts_un_init_gpio_hw(ft5x0x_ts);
	free_irq(client->irq, ft5x0x_ts);
	kfree(ft5x0x_ts);
	i2c_set_clientdata(client, NULL);
	gpio_free(ft5x0x_ts->pdata->reset_gpio);
	gpio_free(ft5x0x_ts->pdata->irq_gpio);
	ft5x06_power_on(ft5x0x_ts, false);
	ft5x06_power_init(ft5x0x_ts, false);
	if (ft5x0x_ts->ts_pinctrl)
		pinctrl_put(ft5x0x_ts->ts_pinctrl);
	
	i2c_set_clientdata(client, NULL);
	kfree(ft5x0x_ts);
	if (client->dev.of_node && (ft5x0x_ts->pdata != NULL))
		devm_kfree(&client->dev, ft5x0x_ts->pdata);
	
	return 0;
}


static struct of_device_id ft5x0x_match_table[] = {
	{.compatible = "focaltech,5x06",},
	{},
};

static const struct i2c_device_id ft5x0x_ts_id[] = {
	{FT5X0X_NAME, 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, ft5x0x_ts_id);

static struct i2c_driver ft5x0x_ts_driver = {
	.probe = ft5x0x_ts_probe,
	.remove = __exit_p(ft5x0x_ts_remove),
	.id_table = ft5x0x_ts_id,
	.driver = {
		.name = FT5X0X_NAME,
		.owner = THIS_MODULE,
		.of_match_table = ft5x0x_match_table,
	},
};

static int __init ft5x0x_ts_init(void)
{
	int ret;
	
	ret = i2c_add_driver(&ft5x0x_ts_driver);
	if (ret) {
		printk("%s: failed, ret=%d\n", __func__, ret);
	} else {
		pr_info("%s: init ok, driver_name=%s\n", __func__, ft5x0x_ts_driver.driver.name);
	}
	return ret;
}

static void __exit ft5x0x_ts_exit(void)
{
	i2c_del_driver(&ft5x0x_ts_driver);
}

module_init(ft5x0x_ts_init);
module_exit(ft5x0x_ts_exit);

MODULE_AUTHOR("<luowj>");
MODULE_DESCRIPTION("FocalTech ft5x0x TouchScreen driver");
MODULE_LICENSE("GPL");
