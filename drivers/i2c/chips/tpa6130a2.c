/*
 * ALSA SoC Texas Instruments TPA6130A2 headset stereo amplifier driver
 *
 * Copyright (C) Nokia Corporation
 *
 * Author: Peter Ujfalusi <peter.ujfalusi@ti.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <sound/tpa6130a2-plat.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <linux/of_gpio.h>
#include "mach/tpa6130a2.h"

#include <mach/htc_acoustic_alsa.h>
#ifdef CONFIG_TI_TCA6418
#include <linux/i2c/tca6418_ioexpander.h>
#endif

#undef pr_info
#undef pr_err
#define pr_info(fmt, ...) pr_aud_info(fmt, ##__VA_ARGS__)
#define pr_err(fmt, ...) pr_aud_err(fmt, ##__VA_ARGS__)


static struct i2c_client *tpa6130a2_client;

struct tpa6130a2_data {
	
	unsigned char initRegs[TPA6130A2_CACHEREGNUM];
	struct regulator *supply;
	int power_gpio;
	unsigned char power_state;
	
};

struct tpa6130a2_reg_data {
	unsigned char addr;
	unsigned char val;
};
#define RETRY_CNT 5


static int tpa6130a2_opened;
static struct mutex hp_amp_lock;

#if 0
static int tpa6130a2_i2c_read(int reg)
{
	struct tpa6130a2_data *data;
	int val;

	BUG_ON(tpa6130a2_client == NULL);
	data = i2c_get_clientdata(tpa6130a2_client);

	
	if (data->power_state) {
		val = i2c_smbus_read_byte_data(tpa6130a2_client, reg);
		if (val < 0)
			pr_err("%s : Read failed\n", __func__);
		else
			data->initRegs[reg] = val;
	} else {
		val = data->initRegs[reg];
	}

	return val;
}
#endif


static int tpa6130a2_i2c_write(u8 reg, u8 value)
{
	int err;
	struct i2c_msg msg[1];
	unsigned char data[2];

	msg->addr = tpa6130a2_client->addr;
	msg->flags = 0; 
	msg->len = 2; 
	msg->buf = data;
	data[0] = reg;
	data[1] = value;

	pr_info("%s: write reg 0x%x val 0x%x\n",__func__,data[0],data[1]);

	err = i2c_transfer(tpa6130a2_client->adapter, msg, 1);

	if (err >= 0)
		return 0;
        else {

            pr_info("%s: write error error %d\n",__func__,err);
            return err;
        }

}

#if 0
static int tpa6130a2_i2c_write_addr(tpa6130a2_reg_data *txData, int length)
{
	int i, retry, pass = 0;
	char buf[2];
	struct i2c_msg msg[] = {
		{
		 .addr = tpa6130a2_client->addr,
		 .flags = 0, 
		 .len = 2,
		 .buf = buf,
		},
	};
	for (i = 0; i < length; i++) {
		
		
		buf[0] = txData[i].addr;
		buf[1] = txData[i].val;

#if 0
		pr_info("%s:i2c_write addr 0x%x val 0x%x\n", __func__,buf[0], buf[1]);
#endif
		msg->buf = buf;
		retry = RETRY_CNT;
		pass = 0;
		while (retry--) {
			if (i2c_transfer(tpa6130a2_client->adapter, msg, 1) < 0) {
				pr_err("%s: I2C transfer error %d retry %d\n",
						__func__, i, retry);
				msleep(20);
			} else {
				pass = 1;
				break;
			}
		}
		if (pass == 0) {
			pr_err("I2C transfer error, retry fail\n");
			return -EIO;
		}
	}
	return 0;
}
#endif

static int tpa6130a2_read(unsigned char *rxData, unsigned char addr)
{
	int rc;
	struct i2c_msg msgs[] = {
		{
		 .addr = tpa6130a2_client->addr,
		 .flags = 0, 
		 .len = 1,
		 .buf = rxData,
		},
		{
		 .addr = tpa6130a2_client->addr,
		 .flags = I2C_M_RD, 
		 .len = 1,
		 .buf = rxData,
		},
	};

	if(!rxData)
		return -1;

	*rxData = addr;

	rc = i2c_transfer(tpa6130a2_client->adapter, msgs, 2);
	if (rc < 0) {
		pr_err("%s:[1] transfer error %d\n", __func__, rc);
		return rc;
	}

	pr_info("%s:i2c_read addr 0x%x value = 0x%x\n", __func__, addr, *rxData);
	return 0;
}


static int tpa6130a2_initialize(void)
{
	
	
	int ret = 0;

	pr_info("%s :", __func__);

	
	ret = tpa6130a2_i2c_write(TPA6130A2_REG_CONTROL, 0);
	ret = tpa6130a2_i2c_write(TPA6130A2_REG_VOL_MUTE, TPA6130A2_MUTE_R \
		                                    |TPA6130A2_MUTE_L | TPA6130A2_VOLUME(63));


	return ret;
}


static int tpa6130a2_power(int power)
{
	struct	tpa6130a2_data *data=NULL;
	unsigned char val;
	int	ret = 0;

	pr_info("%s : %d", __func__, power);

	

	data = i2c_get_clientdata(tpa6130a2_client);

	mutex_lock(&hp_amp_lock);

	if (power == data->power_state)
		goto exit;


	if (power) {
		
		ret = regulator_enable(data->supply);
		if (ret != 0) {
			pr_err("%s : Failed to enable supply: %d\n", __func__, ret);
			goto exit;
		}

		
		

		
		
		
             ioexp_gpio_set_value(2,1);

		data->power_state = 1;
		
		ret = tpa6130a2_initialize();

		if (ret < 0) {
			pr_err("%s : Failed to initialize chip\n", __func__);
			
				
#if 0
			regulator_disable(data->supply);
#endif
			data->power_state = 0;
			goto exit;
		}
	}
	else
	{
		
		
		ret = tpa6130a2_read(&val, TPA6130A2_REG_CONTROL);
		val |= TPA6130A2_SWS;
		ret = tpa6130a2_i2c_write(TPA6130A2_REG_CONTROL, val);

		

		

		
		
		
             ioexp_gpio_set_value(2,0);

		
#if 0
		ret = regulator_disable(data->supply);
		if (ret != 0) {
			pr_err("%s: Failed to disable supply: %d\n", __func__, ret);
			goto exit;
		}
#endif
		data->power_state = 0;
	}

exit:
	mutex_unlock(&hp_amp_lock);
	return ret;
}

#if 0
static int tpa6130a2_get_volsw(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct tpa6130a2_data *data;
	unsigned int reg = mc->reg;
	unsigned int shift = mc->shift;
	int max = mc->max;
	unsigned int mask = (1 << fls(max)) - 1;
	unsigned int invert = mc->invert;

	BUG_ON(tpa6130a2_client == NULL);
	data = i2c_get_clientdata(tpa6130a2_client);

	mutex_lock(&data->mutex);

	ucontrol->value.integer.value[0] =
		(tpa6130a2_read(reg) >> shift) & mask;

	if (invert)
		ucontrol->value.integer.value[0] =
			max - ucontrol->value.integer.value[0];

	mutex_unlock(&data->mutex);
	return 0;
}

static int tpa6130a2_put_volsw(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct tpa6130a2_data *data;
	unsigned int reg = mc->reg;
	unsigned int shift = mc->shift;
	int max = mc->max;
	unsigned int mask = (1 << fls(max)) - 1;
	unsigned int invert = mc->invert;
	unsigned int val = (ucontrol->value.integer.value[0] & mask);
	unsigned int val_reg;

	BUG_ON(tpa6130a2_client == NULL);
	data = i2c_get_clientdata(tpa6130a2_client);

	if (invert)
		val = max - val;

	mutex_lock(&data->mutex);

	val_reg = tpa6130a2_read(reg);
	if (((val_reg >> shift) & mask) == val) {
		mutex_unlock(&data->mutex);
		return 0;
	}

	val_reg &= ~(mask << shift);
	val_reg |= val << shift;
	tpa6130a2_i2c_write(reg, val_reg);

	mutex_unlock(&data->mutex);

	return 1;
}

static const unsigned int tpa6130_tlv[] = {
	TLV_DB_RANGE_HEAD(10),
	0, 1, TLV_DB_SCALE_ITEM(-5950, 600, 0),
	2, 3, TLV_DB_SCALE_ITEM(-5000, 250, 0),
	4, 5, TLV_DB_SCALE_ITEM(-4550, 160, 0),
	6, 7, TLV_DB_SCALE_ITEM(-4140, 190, 0),
	8, 9, TLV_DB_SCALE_ITEM(-3650, 120, 0),
	10, 11, TLV_DB_SCALE_ITEM(-3330, 160, 0),
	12, 13, TLV_DB_SCALE_ITEM(-3040, 180, 0),
	14, 20, TLV_DB_SCALE_ITEM(-2710, 110, 0),
	21, 37, TLV_DB_SCALE_ITEM(-1960, 74, 0),
	38, 63, TLV_DB_SCALE_ITEM(-720, 45, 0),
};

static const struct snd_kcontrol_new tpa6130a2_controls[] = {
	SOC_SINGLE_EXT_TLV("TPA6130A2 Headphone Playback Volume",
		       TPA6130A2_REG_VOL_MUTE, 0, 0x3f, 0,
		       tpa6130a2_get_volsw, tpa6130a2_put_volsw,
		       tpa6130_tlv),
};

static const unsigned int tpa6140_tlv[] = {
	TLV_DB_RANGE_HEAD(3),
	0, 8, TLV_DB_SCALE_ITEM(-5900, 400, 0),
	9, 16, TLV_DB_SCALE_ITEM(-2500, 200, 0),
	17, 31, TLV_DB_SCALE_ITEM(-1000, 100, 0),
};

static const struct snd_kcontrol_new tpa6140a2_controls[] = {
	SOC_SINGLE_EXT_TLV("TPA6140A2 Headphone Playback Volume",
		       TPA6130A2_REG_VOL_MUTE, 1, 0x1f, 0,
		       tpa6130a2_get_volsw, tpa6130a2_put_volsw,
		       tpa6140_tlv),
};
#endif

static void tpa6130a2_channel_enable(int channel, int enable)
{
	unsigned char val;
	int ret=0;

	if (enable) {
		
		
		ret = tpa6130a2_read(&val,TPA6130A2_REG_CONTROL);
		val |= channel;
		val &= ~TPA6130A2_SWS; 
		tpa6130a2_i2c_write(TPA6130A2_REG_CONTROL, val);

		
		ret = tpa6130a2_read(&val,TPA6130A2_REG_VOL_MUTE);
		val &= ~channel;
		tpa6130a2_i2c_write(TPA6130A2_REG_VOL_MUTE, val);
	} else {
		
		
		ret = tpa6130a2_read(&val,TPA6130A2_REG_VOL_MUTE);
		val |= channel;
		tpa6130a2_i2c_write(TPA6130A2_REG_VOL_MUTE, val);

		
		ret = tpa6130a2_read(&val,TPA6130A2_REG_CONTROL);
		val &= ~channel;
		tpa6130a2_i2c_write(TPA6130A2_REG_CONTROL, val);
	}
}

#if 0

static void tpa6130a2_SWS(int enable)
{
	unsigned char	val;

	if (enable) {
		val = tpa6130a2_read(TPA6130A2_REG_CONTROL);
		val |= TPA6130A2_SWS;
		tpa6130a2_i2c_write(TPA6130A2_REG_CONTROL, val);

	} else {
		val = tpa6130a2_read(TPA6130A2_REG_CONTROL);
		val &= ~TPA6130A2_SWS;
		tpa6130a2_i2c_write(TPA6130A2_REG_CONTROL, val);
	}
}
#endif

int tpa6130a2_stereo_enable(int enable)
{
	int ret = 0;


	if (enable) {
		ret = tpa6130a2_power(1);
		
			
		tpa6130a2_channel_enable(TPA6130A2_HP_EN_R | TPA6130A2_HP_EN_L,
					 1);
	} else {
		tpa6130a2_channel_enable(TPA6130A2_HP_EN_R | TPA6130A2_HP_EN_L,
					 0);
		ret = tpa6130a2_power(0);
	}

	return ret;
}
//EXPORT_SYMBOL_GPL(tpa6130a2_stereo_enable);

#if 0
int tpa6130a2_add_controls(struct snd_soc_codec *codec)
{
	struct	tpa6130a2_data *data;

	if (tpa6130a2_client == NULL)
		return -ENODEV;

	data = i2c_get_clientdata(tpa6130a2_client);

	if (data->id == TPA6140A2)
		return snd_soc_add_codec_controls(codec, tpa6140a2_controls,
						ARRAY_SIZE(tpa6140a2_controls));
	else
		return snd_soc_add_codec_controls(codec, tpa6130a2_controls,
						ARRAY_SIZE(tpa6130a2_controls));
}
EXPORT_SYMBOL_GPL(tpa6130a2_add_controls);
#endif

static int set_tpa6130a2_amp(int on, int dsp)
{
	pr_info("%s: %d", __func__, on);

	if(on) {
		tpa6130a2_stereo_enable(1);
	}
	else {
		tpa6130a2_stereo_enable(0);
	}

	return 0;
}
static int tpa6130a2_open(struct inode *inode, struct file *file)
{
	int rc = 0;

	mutex_lock(&hp_amp_lock);

	if (tpa6130a2_opened) {
		pr_err("%s: busy\n", __func__);
		rc = -EBUSY;
		goto done;
	}
	tpa6130a2_opened = 1;
done:
	mutex_unlock(&hp_amp_lock);
	return rc;
}

static int tpa6130a2_release(struct inode *inode, struct file *file)
{
	mutex_lock(&hp_amp_lock);
	tpa6130a2_opened = 0;
	mutex_unlock(&hp_amp_lock);

	return 0;
}

static long tpa6130a2_ioctl(struct file *file, unsigned int cmd,
	   unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int rc = 0, modeid = 0;

	switch (cmd) {
	case AMP_SET_MODE:
		if (copy_from_user(&modeid, argp, sizeof(modeid)))
			return -EFAULT;
		break;

	case AMP_SET_PARAM:
		break;

	default:
		pr_err("%s: Invalid command\n", __func__);
		rc = -EINVAL;
		break;
	}
	return rc;
}
static struct file_operations tpa6130a2_fops = {
	.owner = THIS_MODULE,
	.open = tpa6130a2_open,
	.release = tpa6130a2_release,
	.unlocked_ioctl = tpa6130a2_ioctl,
};

static struct miscdevice tpa6130a2_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "tpa6130a2",
	.fops = &tpa6130a2_fops,
};


static int tpa6130a2_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	struct device *dev;
	struct tpa6130a2_data *data;
	const char *regulator;
	int ret;
	


	dev = &client->dev;

	pr_info("tpa6130a2_probe ++");


	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (data == NULL) {
		pr_err("%s : Can not allocate memory\n", __func__);
		return -ENOMEM;
	}

	tpa6130a2_client = client;

	i2c_set_clientdata(tpa6130a2_client, data);

	
	
	


	ret = misc_register(&tpa6130a2_device);
	htc_acoustic_register_hs_amp(set_tpa6130a2_amp, &tpa6130a2_fops);

	
	

	
	mutex_init(&hp_amp_lock);

	
	
	
		

       data->power_state =0;

	


	regulator = "Vdd";
	data->supply = regulator_get(dev, regulator);

	if (IS_ERR(data->supply)) {
		
		pr_err("%s : Failed to request supply: %d\n", __func__, ret);
		
	}

	

	ret = tpa6130a2_power(1);

#if 0
       ioexp_gpio_set_value(2,1);


     
      tpa6130a2_i2c_write(TPA6130A2_REG_VOL_MUTE, 0xcf);
      tpa6130a2_stereo_enable(1);
#endif

	
#ifdef CONFIG_TI_TCA6418
	
#else
#if 0
	if (data->power_gpio >= 0) {
		ret = gpio_request(data->power_gpio, "tpa6130a2 enable");
		if (ret < 0) {
			pr_err(dev, "Failed to request power GPIO (%d)\n",
				data->power_gpio);
			goto err_gpio;
		}
		gpio_direction_output(data->power_gpio, 0);
	}
#endif
#endif


	
	
#if 0
	ret = tpa6130a2_power(1);
	if (ret != 0)
		goto err_power;
#endif


#if 0
	
	
		

	ret = tpa6130a2_read(&ver,TPA6130A2_REG_VERSION);
	ver = ver & TPA6130A2_VERSION_MASK;

	pr_info("%s TPA6130A2 version: %d", __func__, ver);
	if ((ver != 1) && (ver != 2))
		dev_warn(dev, "UNTESTED version detected (%d)\n", ver);
#endif

	
	ret = tpa6130a2_power(0);

	
	

	pr_info("tpa6130a2_probe --");

	return 0;

#if 0
err_power:
	regulator_put(data->supply);
err_regulator:
	if (data->power_gpio >= 0)
		gpio_free(data->power_gpio);
err_gpio:
	tpa6130a2_client = NULL;

	return ret;
#endif

}

static int tpa6130a2_remove(struct i2c_client *client)
{
#if 1
	tpa6130a2_power(0);
#endif
	tpa6130a2_client = NULL;

	return 0;
}
static int tpa6130a2_suspend(struct i2c_client *client, pm_message_t mesg)
{
	return 0;
}

static int tpa6130a2_resume(struct i2c_client *client)
{
	return 0;
}

static struct of_device_id tpa6130a2_match_table[] = {
        { .compatible = "tpa6130a2",},
        { },
};

static const struct i2c_device_id tpa6130a2_id[] = {
	{ TPA6130A2_I2C_NAME, 0 },
	{ }
};


static struct i2c_driver tpa6130a2_i2c_driver = {
	.probe = tpa6130a2_probe,
	.remove = tpa6130a2_remove,
	.suspend = tpa6130a2_suspend,
	.resume = tpa6130a2_resume,
	.id_table = tpa6130a2_id,
	.driver = {
		.owner = THIS_MODULE,
		.name = TPA6130A2_I2C_NAME,
		.of_match_table = tpa6130a2_match_table,
	},
};

static int __init tpa6130a2_init(void)
{
	pr_info("%s\n", __func__);
	return i2c_add_driver(&tpa6130a2_i2c_driver);
}

static void __exit tpa6130a2_exit(void)
{
	i2c_del_driver(&tpa6130a2_i2c_driver);
}

module_init(tpa6130a2_init);
module_exit(tpa6130a2_exit);

MODULE_DESCRIPTION("TPA6130A2 Headphone amplifier driver");
MODULE_LICENSE("GPL");
