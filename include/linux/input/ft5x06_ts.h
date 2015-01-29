#ifndef __LINUX_FT5X0X_TS_H__
#define __LINUX_FT5X0X_TS_H__

#define CFG_MAX_TOUCH_POINTS	5

#define PRESS_MAX	0xFF
#define FT_PRESS	0x08

#define FT5X0X_NAME	"ft5x06_ts"


#define FT_MAX_ID	0x0F
#define FT_TOUCH_STEP	6
#define FT_TOUCH_X_H_POS		3
#define FT_TOUCH_X_L_POS		4
#define FT_TOUCH_Y_H_POS		5
#define FT_TOUCH_Y_L_POS		6
#define FT_TOUCH_EVENT_POS		3
#define FT_TOUCH_ID_POS			5

#define POINT_READ_BUF	(3 + FT_TOUCH_STEP * CFG_MAX_TOUCH_POINTS)

#define FT5x0x_REG_FW_VER		0xA6
#define FT6x06_REG_VENDOR_ID	0xA8
#define FT5x0x_REG_POINT_RATE	0x88
#define FT5X0X_REG_THGROUP	0x80





int ft5x0x_i2c_Read(struct i2c_client *client, char *writebuf, int writelen,
		    char *readbuf, int readlen);
int ft5x0x_i2c_Write(struct i2c_client *client, char *writebuf, int writelen);

void ft5x0x_reset_tp(int HighOrLow);

struct fw_upgrade_info {
	bool auto_cal;
	u16 delay_aa;
	u16 delay_55;
	u8 upgrade_id_1;
	u8 upgrade_id_2;
	u16 delay_readid;
	u16 delay_erase_flash;
};

struct ft5x0x_platform_data {
	const char *name;
	const char *fw_name;
	unsigned int panel_minx;
	unsigned int panel_miny;
	unsigned int panel_maxx;
	unsigned int panel_maxy;
	unsigned int x_min;
	unsigned int y_min;
	unsigned int x_max;
	unsigned int y_max;
	unsigned long irqflags;	
	unsigned int irq_gpio;
	unsigned int irq_gpio_flags;
	unsigned int reset_gpio;
	unsigned int reset_gpio_flags;
	unsigned int  power_ldo_gpio;
	unsigned int  power_ldo_gpio_flags;
	bool i2c_pull_up;
	bool no_force_update;
	bool fw_vkey_support;
	bool ignore_id_check;
	u32 group_id;
	u32 hard_rst_dly;
	u32 soft_rst_dly;
	u32 num_max_touches;
	u32 family_id;
	struct fw_upgrade_info info;
};

#endif
