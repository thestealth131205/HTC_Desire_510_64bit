/* 
 * drivers/input/touchscreen/ft5x06_ex_fun.c
 *
 * FocalTech ft5x0x expand function for debug. 
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
 *Note:the error code of EIO is the general error in this file.
 */


#include "ft5x06_ex_fun.h"
#include <linux/input/ft5x06_ts.h>
#include <linux/mount.h>
#include <linux/netdevice.h>
#include <linux/i2c.h>

#include "scap_test_lib.h"
#define FTS_DBG
#ifdef FTS_DBG
#define DBG(fmt, args...) printk("[FTS]" fmt, ## args)
#else
#define DBG(fmt, args...) do{}while(0)
#endif


extern u8 fts_fw_version;
extern u8 fts_fw_id;


struct Upgrade_Info{
	u16		delay_aa;		
	u16		delay_55;		
	u8		upgrade_id_1;	
	u8		upgrade_id_2;	
	u16		delay_readid;	
};

int  fts_ctpm_fw_upgrade(struct i2c_client * client, u8* pbt_buf, u32 dw_lenth);


static u16 u_fw_size = 0; 
unsigned char * CTPM_FW = NULL;

static unsigned char CTPM_FW_YUSHUN[] =
{
};

static unsigned char CTPM_FW_BOYI[] =
{
};

static struct mutex g_device_mutex;

int ft5x0x_write_reg(struct i2c_client * client, u8 regaddr, u8 regvalue)
{
	unsigned char buf[2] = {0};
	buf[0] = regaddr;
	buf[1] = regvalue;
	
	return ft5x0x_i2c_Write(client, buf, sizeof(buf));
}

int ft5x0x_read_reg(struct i2c_client * client, u8 regaddr, u8 * regvalue)
{
	return ft5x0x_i2c_Read(client, &regaddr, 1, regvalue, 1);
}
int fts_ctpm_auto_clb(struct i2c_client * client)
{
	unsigned char uc_temp;
	unsigned char i ;

	
	msleep(200);
	ft5x0x_write_reg(client, 0, 0x40);  
	msleep(100);   
	ft5x0x_write_reg(client, 2, 0x4);  
	msleep(300);
	if (DEVICE_IC_TYPE == IC_FT5X36) {
		for(i=0;i<100;i++)
		{
			ft5x0x_read_reg(client, 0x02, &uc_temp);
			if (0x02 == uc_temp ||
				0xFF == uc_temp)
			{
				
			    break;
			}
			msleep(20);	    
		}
	} else {
		for(i=0;i<100;i++)
		{
			ft5x0x_read_reg(client, 0, &uc_temp);
			if (0x0 == ((uc_temp&0x70)>>4))  
			{
			    break;
			}
			msleep(20);	    
		}
	}
	
	ft5x0x_write_reg(client, 0, 0x40);  
	msleep(200);   
	ft5x0x_write_reg(client, 2, 0x5);  
	msleep(300);
	ft5x0x_write_reg(client, 0, 0x0);  
	msleep(300);
	
	
	return 0;
}

int fts_ctpm_fw_upgrade_with_i_file(struct i2c_client * client)
{
	u8 * pbt_buf = NULL;
	int i_ret;
	int fw_len = u_fw_size;

	if(fw_len<8 || fw_len>32*1024)
	{
		pr_err("FW length error\n");
		return -EIO;
	}	
	if((CTPM_FW[fw_len-8]^CTPM_FW[fw_len-6])==0xFF
		&& (CTPM_FW[fw_len-7]^CTPM_FW[fw_len-5])==0xFF
		&& (CTPM_FW[fw_len-3]^CTPM_FW[fw_len-4])==0xFF)
	{
		
		pbt_buf = CTPM_FW;
		
		i_ret =  fts_ctpm_fw_upgrade(client, pbt_buf, u_fw_size);
		if (i_ret)
		{
			printk("[FTS] upgrade failed. err=%d.\n", i_ret);
		}
		else
		{
#ifdef AUTO_CLB
			fts_ctpm_auto_clb(client);  
#endif
		}
	}
	else
	{
		printk("FW format error\n");
		return -EBADFD;
	}
	return i_ret;
}

u8 fts_ctpm_get_i_file_ver(void)
{
	printk("[FTS]fts_ctpm_get_i_file_ver fts_fw_id= 0x%x\n", fts_fw_id);
	
	if(fts_fw_id == 0x5F)		
	{
		CTPM_FW = CTPM_FW_YUSHUN;
		u_fw_size = sizeof(CTPM_FW_YUSHUN);
		if (u_fw_size > 2)
			return CTPM_FW_YUSHUN[u_fw_size - 2];
	}
	else if(fts_fw_id == 0xA6)		
	{
		CTPM_FW = CTPM_FW_BOYI;
		u_fw_size = sizeof(CTPM_FW_BOYI);
		if (u_fw_size > 2)
			return CTPM_FW_BOYI[u_fw_size - 2];
	}

    return 0x00; 
}

int fts_ctpm_auto_upgrade(struct i2c_client * client)
{
	u8 uc_host_fm_ver = FT5x0x_REG_FW_VER;
	u8 uc_tp_fm_ver;
	int i_ret;
	u8 uc_vendor_id;

	uc_tp_fm_ver = fts_fw_version;
    uc_vendor_id = fts_fw_id ;
	
	ft5x0x_read_reg(client, FT5x0x_REG_FW_VER, &uc_tp_fm_ver);
	uc_host_fm_ver = fts_ctpm_get_i_file_ver();

	if(uc_host_fm_ver == 0x00)
	{
	  	printk("[FTS] fts_ctpm_auto_upgrade firmware is not match the tp\n");
	  	return 1;
	}
	
	printk("[FTS] uc_tp_fm_ver = 0x%x, uc_host_fm_ver = 0x%x\n", uc_tp_fm_ver, uc_host_fm_ver);

#if 1
	if ( uc_tp_fm_ver == FT5x0x_REG_FW_VER		
	      || uc_tp_fm_ver < uc_host_fm_ver			
	    )
#else
	if (1)
#endif
	{
		msleep(100);
		i_ret = fts_ctpm_fw_upgrade_with_i_file(client);
		if (i_ret == 0)
		{
	    	msleep(300);
	    	uc_host_fm_ver = fts_ctpm_get_i_file_ver();
	    	printk("[FTS] upgrade to new version 0x%x\n", uc_host_fm_ver);
		}
		else
		{
	    	printk("[FTS] upgrade failed ret=%d.\n", i_ret);
			return -EIO;
		}
	}

	return 0;
}

static void fts_get_upgrade_info(struct Upgrade_Info * upgrade_info)
{
	switch(DEVICE_IC_TYPE)
	{
	case IC_FT5X06:
		upgrade_info->delay_55 = FT5X06_UPGRADE_55_DELAY;
		upgrade_info->delay_aa = FT5X06_UPGRADE_AA_DELAY;
		upgrade_info->upgrade_id_1 = FT5X06_UPGRADE_ID_1;
		upgrade_info->upgrade_id_2 = FT5X06_UPGRADE_ID_2;
		upgrade_info->delay_readid = FT5X06_UPGRADE_READID_DELAY;
		break;
	case IC_FT5606:
		upgrade_info->delay_55 = FT5606_UPGRADE_55_DELAY;
		upgrade_info->delay_aa = FT5606_UPGRADE_AA_DELAY;
		upgrade_info->upgrade_id_1 = FT5606_UPGRADE_ID_1;
		upgrade_info->upgrade_id_2 = FT5606_UPGRADE_ID_2;
		upgrade_info->delay_readid = FT5606_UPGRADE_READID_DELAY;
		break;
	case IC_FT5316:
		upgrade_info->delay_55 = FT5316_UPGRADE_55_DELAY;
		upgrade_info->delay_aa = FT5316_UPGRADE_AA_DELAY;
		upgrade_info->upgrade_id_1 = FT5316_UPGRADE_ID_1;
		upgrade_info->upgrade_id_2 = FT5316_UPGRADE_ID_2;
		upgrade_info->delay_readid = FT5316_UPGRADE_READID_DELAY;
		break;
	case IC_FT5X36:
		upgrade_info->delay_55 = FT5X36_UPGRADE_55_DELAY;
		upgrade_info->delay_aa = FT5X36_UPGRADE_AA_DELAY;
		upgrade_info->upgrade_id_1 = FT5X36_UPGRADE_ID_1;
		upgrade_info->upgrade_id_2 = FT5X36_UPGRADE_ID_2;
		upgrade_info->delay_readid = FT5X36_UPGRADE_READID_DELAY;
		break;
	case IC_FT6206:
		upgrade_info->delay_55 = FT6206_UPGRADE_55_DELAY;
		upgrade_info->delay_aa = FT6206_UPGRADE_AA_DELAY;
		upgrade_info->upgrade_id_1 = FT6206_UPGRADE_ID_1;
		upgrade_info->upgrade_id_2 = FT6206_UPGRADE_ID_2;
		upgrade_info->delay_readid = FT6206_UPGRADE_READID_DELAY;
		break;		
	default:
		break;
	}
}
#define FTS_UPGRADE_LOOP	30

int ft5x36_read_project_code(struct i2c_client * client, char * pProjectCode) {
	u8 reg_val[2] = {0};
	u32 i = 0;
	u32  j;
	u32  temp;
	u8 	packet_buf[4];
	u8  	auc_i2c_write_buf[10];
	int      i_ret;
	u8 is_5336_new_bootloader = 0;
	struct Upgrade_Info upgradeinfo;

	fts_get_upgrade_info(&upgradeinfo);

	for (i = 0; i < FTS_UPGRADE_LOOP; i++) {
    	
    	
	   	ft5x0x_write_reg(client, 0xfc, FT_UPGRADE_AA);
		msleep(upgradeinfo.delay_aa);
		
		 
		ft5x0x_write_reg(client, 0xfc, FT_UPGRADE_55);   
		msleep(upgradeinfo.delay_55);   

		
		auc_i2c_write_buf[0] = FT_UPGRADE_55;
		auc_i2c_write_buf[1] = FT_UPGRADE_AA;
		
	    i_ret = ft5x0x_i2c_Write(client, auc_i2c_write_buf, 2);
	  
	       
		msleep(upgradeinfo.delay_readid);
	   	auc_i2c_write_buf[0] = 0x90; 
		auc_i2c_write_buf[1] = auc_i2c_write_buf[2] = auc_i2c_write_buf[3] = 0x00;

		ft5x0x_i2c_Read(client, auc_i2c_write_buf, 4, reg_val, 2);
		
		if (reg_val[0] == upgradeinfo.upgrade_id_1 
			&& reg_val[1] == upgradeinfo.upgrade_id_2)
		{
	    	dev_dbg(&client->dev, "[FTS] Step 3: CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",reg_val[0],reg_val[1]);
	    	break;
		}
		else
		{
			dev_err(&client->dev, "[FTS] Step 3: CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",reg_val[0],reg_val[1]);
	    	continue;
		}
	}
	if (i >= FTS_UPGRADE_LOOP)
		return -EIO;

	auc_i2c_write_buf[0] = 0xcd;
	ft5x0x_i2c_Read(client, auc_i2c_write_buf, 1, reg_val, 1);
	if (reg_val[0] > 4)
		is_5336_new_bootloader = 1;

	pr_info("bootloader version:%d\n", reg_val[0]);

	

	packet_buf[0] = 0x03;
	packet_buf[1] = 0x00;
	for (j=0;j<33;j++)
	{
		if (is_5336_new_bootloader)
			temp = 0x07d0 + j;
		else
			temp = 0x7820 + j;
		packet_buf[2] = (u8)(temp>>8);
		packet_buf[3] = (u8)temp;

		ft5x0x_i2c_Read(client, packet_buf, sizeof(packet_buf), 
			pProjectCode+j, 1);
		if (*(pProjectCode+j) == '\0')
			break;
	}
	pr_info("project code = %s \n", pProjectCode);

	
	auc_i2c_write_buf[0] = 0x07;
	ft5x0x_i2c_Write(client, auc_i2c_write_buf, 1);
	msleep(300);  
	return 0;
}

int  fts_ctpm_fw_upgrade(struct i2c_client * client, u8* pbt_buf, u32 dw_lenth)
{
	
	u8 reg_val[2] = {0};
	u32 i = 0;
	u8 is_5336_new_bootloader = 0;
	u32  packet_number;
	u32  j = 0;
	u32  temp;
	u32  lenght;
	u8 	packet_buf[FTS_PACKET_LENGTH + 6];
	u8  	auc_i2c_write_buf[10];
	u8  	bt_ecc;
	int      i_ret;
	u8 is_5336_fwsize_30 = 0;
	struct Upgrade_Info upgradeinfo;
	
	fts_get_upgrade_info(&upgradeinfo);

	printk("%s\n", __func__);
	
	if(pbt_buf[dw_lenth-12] == 30)
	{
		is_5336_fwsize_30 = 1;
	}
	else 
	{
		is_5336_fwsize_30 = 0;
	}

	for (i = 0; i < FTS_UPGRADE_LOOP; i++) {
		
		
		DBG("GPG :fts_ctpm_fw_upgrade LOOP:%d(%d)\n", i, FTS_UPGRADE_LOOP);	
		if ( (DEVICE_IC_TYPE == IC_FT6208) || (DEVICE_IC_TYPE == IC_FT6206) )
			ft5x0x_write_reg(client, 0xbc, FT_UPGRADE_AA);
		else
			ft5x0x_write_reg(client, 0xfc, FT_UPGRADE_AA);
		msleep(upgradeinfo.delay_aa);

		
		if ( (DEVICE_IC_TYPE == IC_FT6208)  || (DEVICE_IC_TYPE == IC_FT6206) )
			ft5x0x_write_reg(client, 0xbc, FT_UPGRADE_55);
		else
			ft5x0x_write_reg(client, 0xfc, FT_UPGRADE_55);

		DBG("GPG 1:fts_ctpm_fw_upgrade LOOP:%d(%d)\n", i, FTS_UPGRADE_LOOP);	
		
		if(i%2)
		{
			DBG("GPG 1\n");
			msleep(30+i);
		}
		else
		{
			DBG("GPG 2\n");
			msleep(30-i);
		}
		
		auc_i2c_write_buf[0] = FT_UPGRADE_55;
		auc_i2c_write_buf[1] = FT_UPGRADE_AA;
		do {
			j++;
			i_ret = ft5x0x_i2c_Write(client, auc_i2c_write_buf, 2);
			msleep(5);
		} while (i_ret <= 0 && j < 5);


		
		msleep(upgradeinfo.delay_readid);
	   	auc_i2c_write_buf[0] = 0x90; 
		auc_i2c_write_buf[1] = auc_i2c_write_buf[2] = auc_i2c_write_buf[3] = 0x00;

		ft5x0x_i2c_Read(client, auc_i2c_write_buf, 4, reg_val, 2);
		
		if (reg_val[0] == upgradeinfo.upgrade_id_1 
			&& reg_val[1] == upgradeinfo.upgrade_id_2)
		{
	    	dev_dbg(&client->dev, "[FTS] Step 3: CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",reg_val[0],reg_val[1]);
	    	break;
		}
		else
		{
			dev_err(&client->dev, "[FTS] Step 3: CTPM ID,ID1 = 0x%x,ID2 = 0x%x\n",reg_val[0],reg_val[1]);
	    
		}
	}
	if (i >= FTS_UPGRADE_LOOP)
		return -EIO;
		
	auc_i2c_write_buf[0] = 0xcd;
	ft5x0x_i2c_Read(client, auc_i2c_write_buf, 1, reg_val, 1);

	if( DEVICE_IC_TYPE == IC_FT5X36 )
	{
		if (reg_val[0] > 4)
			is_5336_new_bootloader = 1;
	}
     
	if(1) 
	{
		auc_i2c_write_buf[0] = 0x61;
		ft5x0x_i2c_Write(client, auc_i2c_write_buf, 1); 	
	    msleep(FT_UPGRADE_EARSE_DELAY); 

		
		auc_i2c_write_buf[0] = 0x63;
		ft5x0x_i2c_Write(client, auc_i2c_write_buf, 1);
		msleep(100);
	}
	else
	{
		auc_i2c_write_buf[0] = 0x61;
		ft5x0x_i2c_Write(client, auc_i2c_write_buf, 1); 	
	    msleep(FT_UPGRADE_EARSE_DELAY); 
	}

	
	bt_ecc = 0;
	if(is_5336_new_bootloader)
	{
		dw_lenth = dw_lenth - 14;
	}
	else
	{
		dw_lenth = dw_lenth - 8;
	}
	packet_number = (dw_lenth) / FTS_PACKET_LENGTH;
	packet_buf[0] = 0xbf;
	packet_buf[1] = 0x00;
	for (j=0;j<packet_number;j++)
	{
		temp = j * FTS_PACKET_LENGTH;
		packet_buf[2] = (u8)(temp>>8);
		packet_buf[3] = (u8)temp;
		lenght = FTS_PACKET_LENGTH;
		packet_buf[4] = (u8)(lenght>>8);
		packet_buf[5] = (u8)lenght;

		for (i=0;i<FTS_PACKET_LENGTH;i++)
		{
		    packet_buf[6+i] = pbt_buf[j*FTS_PACKET_LENGTH + i]; 
		    bt_ecc ^= packet_buf[6+i];
		}

		ft5x0x_i2c_Write(client, packet_buf, FTS_PACKET_LENGTH+6);
		msleep(FTS_PACKET_LENGTH/6 + 1);
	}

	if ((dw_lenth) % FTS_PACKET_LENGTH > 0)
	{
		temp = packet_number * FTS_PACKET_LENGTH;
		packet_buf[2] = (u8)(temp>>8);
		packet_buf[3] = (u8)temp;

		temp = (dw_lenth) % FTS_PACKET_LENGTH;
		packet_buf[4] = (u8)(temp>>8);
		packet_buf[5] = (u8)temp;

		for (i=0;i<temp;i++)
		{
		    packet_buf[6+i] = pbt_buf[ packet_number*FTS_PACKET_LENGTH + i]; 
		    bt_ecc ^= packet_buf[6+i];
		}
  
		ft5x0x_i2c_Write(client, packet_buf, temp+6);
		msleep(20);
	}

	
	if(is_5336_new_bootloader )
	{
		for (i = 0; i<12; i++)
		{
			if (is_5336_fwsize_30 )
			{
				temp = 0x7ff4 + i;
			}
			else
			{
				temp = 0x7bf4 + i;
			}


			packet_buf[2] = (u8)(temp>>8);
			packet_buf[3] = (u8)temp;
			temp =1;
			packet_buf[4] = (u8)(temp>>8);
			packet_buf[5] = (u8)temp;
			packet_buf[6] = pbt_buf[ dw_lenth + i]; 
			bt_ecc ^= packet_buf[6];
			ft5x0x_i2c_Write(client, packet_buf, 7);
			msleep(20);
		}
	}
	else
	{
		for (i = 0; i<6; i++)
		{
			if (is_5336_new_bootloader && DEVICE_IC_TYPE==IC_FT5X36) 
				temp = 0x7bfa + i;
			else
				temp = 0x6ffa + i;
			packet_buf[2] = (u8)(temp>>8);
			packet_buf[3] = (u8)temp;
			temp =1;
			packet_buf[4] = (u8)(temp>>8);
			packet_buf[5] = (u8)temp;
			packet_buf[6] = pbt_buf[ dw_lenth + i]; 
			bt_ecc ^= packet_buf[6];
	  
			ft5x0x_i2c_Write(client, packet_buf, 7);
			msleep(2);
		}
	}

	
	
	auc_i2c_write_buf[0] = 0xcc;
	ft5x0x_i2c_Read(client, auc_i2c_write_buf, 1, reg_val, 1); 

	if(reg_val[0] != bt_ecc)
	{
		dev_err(&client->dev, "[FTS]--ecc error! FW=%02x bt_ecc=%02x\n", reg_val[0], bt_ecc);
	    	return -EIO;
	}

	
	auc_i2c_write_buf[0] = 0x07;
	ft5x0x_i2c_Write(client, auc_i2c_write_buf, 1);
	msleep(300);  

	printk("[FTS] update success\n");
	return 0;
}



static int ft5x0x_GetFirmwareSize(char * firmware_name)
{
	struct file* pfile = NULL;
	struct inode *inode;
	unsigned long magic; 
	off_t fsize = 0; 
	char filepath[128];
	memset(filepath, 0, sizeof(filepath));

	sprintf(filepath, "%s", firmware_name);

	if(NULL == pfile){
		pfile = filp_open(filepath, O_RDONLY, 0);
		}
	if(IS_ERR(pfile)){
		pr_err("error occured while opening file %s.\n", filepath);
		return -EIO;
		}
	inode=pfile->f_dentry->d_inode; 
	magic=inode->i_sb->s_magic;
	fsize=inode->i_size; 
	filp_close(pfile, NULL);
	
	return fsize;
}

static int ft5x0x_ReadFirmware(char * firmware_name, unsigned char * firmware_buf)
{
	struct file* pfile = NULL;
	struct inode *inode;
	unsigned long magic; 
	off_t fsize; 
	char filepath[128];
	loff_t pos;

	mm_segment_t old_fs;
	memset(filepath, 0, sizeof(filepath));
	sprintf(filepath, "%s", firmware_name);
	if(NULL == pfile){
		pfile = filp_open(filepath, O_RDONLY, 0);
		}
	if(IS_ERR(pfile)){
		pr_err("error occured while opening file %s.\n", filepath);
		return -EIO;
		}
	inode=pfile->f_dentry->d_inode; 
	magic=inode->i_sb->s_magic;
	fsize=inode->i_size; 
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	pos = 0;

	vfs_read(pfile, firmware_buf, fsize, &pos);

	filp_close(pfile, NULL);
	set_fs(old_fs);
	return 0;
}


int fts_ctpm_fw_upgrade_with_app_file(struct i2c_client * client, char * firmware_name)
{
  	u8*     pbt_buf = NULL;
   	int i_ret;
   	int fwsize = ft5x0x_GetFirmwareSize(firmware_name);
   	if(fwsize <= 0)
   	{
   		dev_err(&client->dev, "%s ERROR:Get firmware size failed\n", __FUNCTION__);
		return -EIO;
   	}
	if(fwsize<8 || fwsize>32*1024)
	{
		dev_err(&client->dev, "FW length error\n");
		return -EIO;
	}
	
    
  	 pbt_buf = (unsigned char *) kmalloc(fwsize+1,GFP_ATOMIC);
	if(ft5x0x_ReadFirmware(firmware_name, pbt_buf))
    	{
       	dev_err(&client->dev, "%s() - ERROR: request_firmware failed\n", __FUNCTION__);
        	kfree(pbt_buf);
		return -EIO;
    	}
	if((pbt_buf[fwsize-8]^pbt_buf[fwsize-6])==0xFF
		&& (pbt_buf[fwsize-7]^pbt_buf[fwsize-5])==0xFF
		&& (pbt_buf[fwsize-3]^pbt_buf[fwsize-4])==0xFF)
	{
		
		i_ret =  fts_ctpm_fw_upgrade(client, pbt_buf, fwsize);
   		if (i_ret != 0)
   		{
       		dev_err(&client->dev, "%s() - ERROR:[FTS] upgrade failed i_ret = %d.\n",__FUNCTION__,  i_ret);
   		}
  	 	else
   		{
		
      		fts_ctpm_auto_clb(client);  
   		}
		kfree(pbt_buf);
	}
	else
	{
		dev_dbg(&client->dev, "FW format error\n");
		kfree(pbt_buf);
		return -EIO;
	}
   	return i_ret;
}

static ssize_t ft5x0x_tpfwver_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	ssize_t num_read_chars = 0;
	u8	   fwver = 0;
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	
	mutex_lock(&g_device_mutex);
	if(ft5x0x_read_reg(client, FT5x0x_REG_FW_VER, &fwver) < 0)
		num_read_chars = snprintf(buf, PAGE_SIZE, "get tp fw version fail!\n");
	else
		num_read_chars = snprintf(buf, PAGE_SIZE, "%02X\n", fwver);

	mutex_unlock(&g_device_mutex);
	return num_read_chars;
}

static ssize_t ft5x0x_tpfwver_store(struct device *dev,
					struct device_attribute *attr,
						const char *buf, size_t count)
{
	
	return -EPERM;
}

static ssize_t ft5x0x_tprwreg_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	
	return -EPERM;
}

static ssize_t ft5x0x_tprwreg_store(struct device *dev,
					struct device_attribute *attr,
						const char *buf, size_t count)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);	
	ssize_t num_read_chars = 0;
	int retval;
	unsigned long wmreg=0;
	u8 regaddr=0xff,regvalue=0xff;
	u8 valbuf[5]={0};
	memset(valbuf, 0, sizeof(valbuf));
	
	mutex_lock(&g_device_mutex);
	num_read_chars = count - 1;

	if(num_read_chars!=2)
	{
		if(num_read_chars!=4)
		{
			dev_err(dev, "please input 2 or 4 character\n");
			goto error_return;
		}
	}
	
	memcpy(valbuf, buf, num_read_chars);
	retval = strict_strtoul(valbuf, 16, &wmreg);
	if (0 != retval)
	{
    	dev_err(dev, "%s() - ERROR: Could not convert the given input to a number. The given input was: \"%s\"\n", __FUNCTION__, buf);
    	goto error_return;
	}

	if(2 == num_read_chars)
	{
		
		regaddr = wmreg;
		if(ft5x0x_read_reg(client, regaddr, &regvalue) < 0)
			dev_err(dev, "Could not read the register(0x%02x)\n", regaddr);
		else
			dev_dbg(dev, "the register(0x%02x) is 0x%02x\n", regaddr, regvalue);
	}
	else
	{
		regaddr = wmreg>>8;
		regvalue = wmreg;
		if(ft5x0x_write_reg(client, regaddr, regvalue)<0)
			dev_err(dev, "Could not write the register(0x%02x)\n", regaddr);
		else
			dev_dbg(dev, "Write 0x%02x into register(0x%02x) successful\n", regvalue, regaddr);
	}
error_return:
	mutex_unlock(&g_device_mutex);

	return count;
}


static ssize_t ft5x0x_fwupdate_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	
    return -EPERM;
}

static ssize_t ft5x0x_fwupdate_store(struct device *dev,
					struct device_attribute *attr,
						const char *buf, size_t count)
{
	struct ft5x0x_ts_data *data = NULL;
	u8 uc_host_fm_ver;int i_ret;
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	data = (struct ft5x0x_ts_data *) i2c_get_clientdata( client );
	
	mutex_lock(&g_device_mutex);

	disable_irq(client->irq);
	i_ret = fts_ctpm_fw_upgrade_with_i_file(client);    
	if (i_ret == 0)
	{
	    msleep(300);
	    uc_host_fm_ver = fts_ctpm_get_i_file_ver();
	    dev_dbg(dev, "%s [FTS] upgrade to new version 0x%x\n", __FUNCTION__, uc_host_fm_ver);
	}
	else
	{
	    dev_err(dev, "%s ERROR:[FTS] upgrade failed ret=%d.\n", __FUNCTION__, i_ret);
	}
	enable_irq(client->irq);
	
	mutex_unlock(&g_device_mutex);

	return count;
}

static ssize_t ft5x0x_fwupgradeapp_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	
    return -EPERM;
}
static ssize_t ft5x0x_fwupgradeapp_store(struct device *dev,
					struct device_attribute *attr,
						const char *buf, size_t count)
{
	char fwname[128];
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	memset(fwname, 0, sizeof(fwname));
	sprintf(fwname, "%s", buf);
	fwname[count-1] = '\0';

	mutex_lock(&g_device_mutex);
	disable_irq(client->irq);
	
	fts_ctpm_fw_upgrade_with_app_file(client, fwname);
	
	enable_irq(client->irq);

	mutex_unlock(&g_device_mutex);

	return count;
}

static ssize_t ft5x0x_ftsgetprojectcode_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	ssize_t num_read_chars = 0;
	char projectcode[32]; 
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);

	memset(projectcode, 0, sizeof(projectcode));
	mutex_lock(&g_device_mutex);
	if(ft5x36_read_project_code(client, projectcode) < 0)
		num_read_chars = snprintf(buf, PAGE_SIZE, "get projcet code fail!\n");
	else
		num_read_chars = snprintf(buf, PAGE_SIZE, "projcet code = %s\n", projectcode);

	mutex_unlock(&g_device_mutex);
	return num_read_chars;

	
}
static ssize_t ft5x0x_ftsgetprojectcode_store(struct device *dev,
					struct device_attribute *attr,
						const char *buf, size_t count)
{
	
    return -EPERM;
}


#define FT5X0X_INI_FILEPATH "/system/etc/firmware/"
static int ft5x0x_GetInISize(char *config_name)
{
	struct file *pfile = NULL;
	struct inode *inode;
	unsigned long magic;
	off_t fsize = 0;
	char filepath[128];
	memset(filepath, 0, sizeof(filepath));

	sprintf(filepath, "%s%s", FT5X0X_INI_FILEPATH, config_name);

	if (NULL == pfile)
		pfile = filp_open(filepath, O_RDONLY, 0);

	if (IS_ERR(pfile)) {
		pr_err("error occured while opening file %s.\n", filepath);
		return -EIO;
	}

	inode = pfile->f_dentry->d_inode;
	magic = inode->i_sb->s_magic;
	fsize = inode->i_size;
	filp_close(pfile, NULL);
	return fsize;
}

static int ft5x0x_ReadInIData(char *config_name,
			      char *config_buf)
{
	struct file *pfile = NULL;
	struct inode *inode;
	unsigned long magic;
	off_t fsize;
	char filepath[128];
	loff_t pos;
	mm_segment_t old_fs;

	memset(filepath, 0, sizeof(filepath));
	sprintf(filepath, "%s%s", FT5X0X_INI_FILEPATH, config_name);
	if (NULL == pfile)
		pfile = filp_open(filepath, O_RDONLY, 0);
	if (IS_ERR(pfile)) {
		pr_err("error occured while opening file %s.\n", filepath);
		return -EIO;
	}

	inode = pfile->f_dentry->d_inode;
	magic = inode->i_sb->s_magic;
	fsize = inode->i_size;
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	pos = 0;
	vfs_read(pfile, config_buf, fsize, &pos);
	filp_close(pfile, NULL);
	set_fs(old_fs);

	return 0;
}
static int ft5x0x_get_testparam_from_ini(char *config_name)
{
	char *filedata = NULL;

	int inisize = ft5x0x_GetInISize(config_name);

	pr_info("inisize = %d \n ", inisize);
	if (inisize <= 0) {
		pr_err("%s ERROR:Get firmware size failed\n",
					__func__);
		return -EIO;
	}

	filedata = kmalloc(inisize + 1, GFP_ATOMIC);
		
	if (ft5x0x_ReadInIData(config_name, filedata)) {
		pr_err("%s() - ERROR: request_firmware failed\n",
					__func__);
		kfree(filedata);
		return -EIO;
	} else {
		pr_info("ft5x0x_ReadInIData successful\n");
	}

	SetParamData(filedata);
	return 0;
}

static ssize_t ft5x0x_ftsscaptest_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{	
	
	return -EPERM;
}
extern int focal_i2c_Read(unsigned char *writebuf, int writelen, unsigned char *readbuf, int readlen);
extern int focal_i2c_Write(unsigned char *writebuf, int writelen);
static ssize_t ft5x0x_ftsscaptest_store(struct device *dev,
					struct device_attribute *attr,
						const char *buf, size_t count)
{
	
    char cfgname[128];
	memset(cfgname, 0, sizeof(cfgname));
	sprintf(cfgname, "%s", buf);
	cfgname[count-1] = '\0';

	mutex_lock(&g_device_mutex);

	Init_I2C_Write_Func(focal_i2c_Write);
	Init_I2C_Read_Func(focal_i2c_Read);
	if(ft5x0x_get_testparam_from_ini(cfgname) <0)
		printk("get testparam from ini failure\n");
	else {
		#if 1
		if(true == StartTestTP())
			printk("tp test pass\n");
		else
			printk("tp test failure\n");
		#endif
		FreeTestParamData();
	}
	
	mutex_unlock(&g_device_mutex);

	return count;
}
static DEVICE_ATTR(ftstpfwver, S_IRUGO|S_IWUSR, ft5x0x_tpfwver_show, ft5x0x_tpfwver_store);

static DEVICE_ATTR(ftsfwupdate, S_IRUGO|S_IWUSR, ft5x0x_fwupdate_show, ft5x0x_fwupdate_store);

static DEVICE_ATTR(ftstprwreg, S_IRUGO|S_IWUSR, ft5x0x_tprwreg_show, ft5x0x_tprwreg_store);

static DEVICE_ATTR(ftsfwupgradeapp, S_IRUGO|S_IWUSR, ft5x0x_fwupgradeapp_show, ft5x0x_fwupgradeapp_store);

static DEVICE_ATTR(ftsgetprojectcode, S_IRUGO|S_IWUSR, ft5x0x_ftsgetprojectcode_show, ft5x0x_ftsgetprojectcode_store);


static DEVICE_ATTR(ftsscaptest, S_IRUGO|S_IWUSR, ft5x0x_ftsscaptest_show, ft5x0x_ftsscaptest_store);
static struct attribute *ft5x0x_attributes[] = {
	&dev_attr_ftstpfwver.attr,
	&dev_attr_ftsfwupdate.attr,
	&dev_attr_ftstprwreg.attr,
	&dev_attr_ftsfwupgradeapp.attr,
	&dev_attr_ftsgetprojectcode.attr,
	&dev_attr_ftsscaptest.attr,
	NULL
};

static struct attribute_group ft5x0x_attribute_group = {
	.attrs = ft5x0x_attributes
};

int ft5x0x_create_sysfs(struct i2c_client * client)
{
	int err;
	err = sysfs_create_group(&client->dev.kobj, &ft5x0x_attribute_group);
   	if (0 != err)
  	{
		dev_err(&client->dev, "%s() - ERROR: sysfs_create_group() failed.error code: %d\n", __FUNCTION__, err);
		sysfs_remove_group(&client->dev.kobj, &ft5x0x_attribute_group);
		return -EIO;
  	}
   	else
    	{		
		mutex_init(&g_device_mutex);
        	dev_dbg(&client->dev, "ft5x0x:%s() - sysfs_create_group() succeeded. \n", __FUNCTION__);
    	}
	return err;
}

int ft5x0x_remove_sysfs(struct i2c_client * client)
{
	sysfs_remove_group(&client->dev.kobj, &ft5x0x_attribute_group);
	mutex_destroy(&g_device_mutex);
	return 1;
}

#define PROC_UPGRADE			0
#define PROC_READ_REGISTER		1
#define PROC_WRITE_REGISTER		2
#define PROC_AUTOCLB			4
#define PROC_UPGRADE_INFO		5
#define PROC_WRITE_DATA			6
#define PROC_READ_DATA			7

#include "../../fs/proc/internal.h"

#define PROC_NAME	"ft5x0x-debug"
static unsigned char proc_operate_mode = PROC_UPGRADE;
static struct proc_dir_entry *ft5x0x_proc_entry;
static int ft5x0x_debug_write(struct file *file, const char __user *buf, size_t size, loff_t *ppos)
{
	struct i2c_client *client = (struct i2c_client *)ft5x0x_proc_entry->data;
	unsigned char writebuf[FTS_PACKET_LENGTH];
	int buflen = size;
	int writelen = 0;
	int ret = 0;
	
	if (copy_from_user(&writebuf, buf, buflen)) {
		dev_err(&client->dev, "%s:copy from user error\n", __func__);
		return -EFAULT;
	}
	proc_operate_mode = writebuf[0];

	switch (proc_operate_mode) {
	case PROC_UPGRADE:
		{
			char upgrade_file_path[128];
			memset(upgrade_file_path, 0, sizeof(upgrade_file_path));
			sprintf(upgrade_file_path, "%s", writebuf + 1);
			upgrade_file_path[buflen-1] = '\0';
			DBG("%s\n", upgrade_file_path);
			disable_irq(client->irq);

			ret = fts_ctpm_fw_upgrade_with_app_file(client, upgrade_file_path);

			enable_irq(client->irq);
			if (ret < 0) {
				dev_err(&client->dev, "%s:upgrade failed.\n", __func__);
				return ret;
			}
		}
		break;
	case PROC_READ_REGISTER:
		writelen = 1;
		DBG("%s:register addr=0x%02x\n", __func__, writebuf[1]);
		ret = ft5x0x_i2c_Write(client, writebuf + 1, writelen);
		if (ret < 0) {
			dev_err(&client->dev, "%s:write iic error\n", __func__);
			return ret;
		}
		break;
	case PROC_WRITE_REGISTER:
		writelen = 2;
		ret = ft5x0x_i2c_Write(client, writebuf + 1, writelen);
		if (ret < 0) {
			dev_err(&client->dev, "%s:write iic error\n", __func__);
			return ret;
		}
		break;
	case PROC_AUTOCLB:
		DBG("%s: autoclb\n", __func__);
		fts_ctpm_auto_clb(client);
		break;
	case PROC_READ_DATA:
	case PROC_WRITE_DATA:
		writelen = size - 1;
		ret = ft5x0x_i2c_Write(client, writebuf + 1, writelen);
		if (ret < 0) {
			dev_err(&client->dev, "%s:write iic error\n", __func__);
			return ret;
		}
		break;
	default:
		break;
	}
	

	return size;
}

static int ft5x0x_debug_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	#if 0
	struct i2c_client *client = (struct i2c_client *)ft5x0x_proc_entry->data;
	int ret = 0;
	
	
	unsigned char buf[PAGE_SIZE];
	int num_read_chars = 0;
	u8 readlen = 0;
	u8 regvalue = 0x00, regaddr = 0x00;
	
	switch (proc_operate_mode) {
	case PROC_UPGRADE:
		
		regaddr = 0xA6;
		ret = ft5x0x_read_reg(client, regaddr, &regvalue);
		if (ret < 0)
			num_read_chars = sprintf(buf, "%s", "get fw version failed.\n");
		else
			num_read_chars = sprintf(buf, "current fw version:0x%02x\n", regvalue);
		break;
	case PROC_READ_REGISTER:
		readlen = 1;
		ret = ft5x0x_i2c_Read(client, NULL, 0, buf, readlen);
		if (ret < 0) {
			dev_err(&client->dev, "%s:read iic error\n", __func__);
			return ret;
		} else
			DBG("%s:value=0x%02x\n", __func__, buf[0]);
		num_read_chars = 1;
		break;
	case PROC_READ_DATA:
		readlen = count;
		ret = ft5x0x_i2c_Read(client, NULL, 0, buf, readlen);
		if (ret < 0) {
			dev_err(&client->dev, "%s:read iic error\n", __func__);
			return ret;
		}
		
		num_read_chars = readlen;
		break;
	case PROC_WRITE_DATA:
		break;
	default:
		break;
	}
	
	memcpy(page, buf, num_read_chars);
	return num_read_chars;
	#endif
	return 1;
}

static const struct file_operations ft5x0x_proc_fops = {
	.read		= ft5x0x_debug_read,
	.write		= ft5x0x_debug_write,
};

int ft5x0x_create_apk_debug_channel(struct i2c_client * client)
{
#if 1		
	ft5x0x_proc_entry = proc_create_data(PROC_NAME, 0777, NULL, &ft5x0x_proc_fops, NULL);
	if (IS_ERR_OR_NULL(ft5x0x_proc_entry))
	{
		pr_err("add /proc/msg-ito-test/debug error \n");
	}
#else
	ft5x0x_proc_entry = create_proc_entry(PROC_NAME, 0777, NULL);
	if (NULL == ft5x0x_proc_entry) {
		dev_err(&client->dev, "Couldn't create proc entry!\n");
		return -ENOMEM;
	} else {
		dev_info(&client->dev, "Create proc entry success!\n");
		ft5x0x_proc_entry->data = client;
		ft5x0x_proc_entry->write_proc = ft5x0x_debug_write;
		ft5x0x_proc_entry->read_proc = ft5x0x_debug_read;
	}
#endif
	return 0;
}

void ft5x0x_release_apk_debug_channel(void)
{
	if (ft5x0x_proc_entry)
#if 1
		proc_remove(ft5x0x_proc_entry);
#else
		remove_proc_entry(PROC_NAME, NULL);
#endif
}

int ft5x06_self_test(void)
{
	int pf_value=0x00;
	char cfgname[]="ft5x06_selftest.ini";

	mutex_lock(&g_device_mutex);
	
	Init_I2C_Write_Func(focal_i2c_Write);
	Init_I2C_Read_Func(focal_i2c_Read);
	if(ft5x0x_get_testparam_from_ini(cfgname) <0)
		pr_info("get testparam from ini failure\n");
	else {
	#if 1
		if(true == StartTestTP())
		{
			printk("tp test pass\n");
			pf_value = 0x0;
		}
		else
		{
			pf_value = 0x1;
			printk("tp test failure\n");
		}
	#endif
		FreeTestParamData();
	}
	mutex_unlock(&g_device_mutex);
	return pf_value;	
}


