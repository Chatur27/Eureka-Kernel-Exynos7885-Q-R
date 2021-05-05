/*
 * Copyright (C) 2010 Trusted Logic S.A.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define PN547_MAGIC	0xE9

/*
 * PN544 power control via ioctl
 * PN544_SET_PWR(0): power off
 * PN544_SET_PWR(1): power on
 * PN544_SET_PWR(>1): power on with firmware download enabled
 */
#define PN547_SET_PWR	_IOW(PN547_MAGIC, 0x01, long)

#if 1 //ese
/*
 * SPI Request NFCC to enable p61 power, only in param
 * Only for SPI
 * level 1 = Enable power
 * level 0 = Disable power
 */
#define P61_SET_SPI_PWR    _IOW(PN547_MAGIC, 0x02, long)

/* SPI or DWP can call this ioctl to get the current
 * power state of P61
 *
*/
#define P61_GET_PWR_STATUS    _IOR(PN547_MAGIC, 0x03, long)

/* DWP side this ioctl will be called
 * level 1 = Wired access is enabled/ongoing
 * level 0 = Wired access is disalbed/stopped
*/
#define P61_SET_WIRED_ACCESS _IOW(PN547_MAGIC, 0x04, long)
#define P547_SET_NFC_SERVICE_PID _IOW(PN547_MAGIC, 0x05, long)

#define P547_GET_ESE_ACCESS _IOW(PN547_MAGIC, 0x06, long)
#define P547_REL_SVDD_WAIT _IOW(PN547_MAGIC, 0x07, long)

#define NFC_I2C_LDO_ON	1
#define NFC_I2C_LDO_OFF	0

/* SFR bit mask */
#define PN547_NFC_CLKCTRL_PD              0x01
#define PN547_NFC_CLKCTRL_REQ_POLA        0x10
#define PN547_NFC_CLKCTRL_PD_POLA         0x20
#define PN547_NFC_CLKCTRL_CLK_ENABLE    0x80000000

enum pn547_nfc_power {
	PN547_NFC_PW_OFF = 0,
	PN547_NFC_PW_ON,
};


typedef enum p61_access_state{
	P61_STATE_INVALID = 0x0000,
	P61_STATE_IDLE = 0x0100, /* p61 is free to use */
	P61_STATE_WIRED = 0x0200,  /* p61 is being accessed by DWP (NFCC)*/
	P61_STATE_SPI = 0x0400, /* P61 is being accessed by SPI */
	P61_STATE_DWNLD = 0x0800, /* NFCC fw download is in progress */
	P61_STATE_SPI_PRIO = 0x1000, /*Start of p61 access by SPI on priority*/
	P61_STATE_SPI_PRIO_END = 0x2000, /*End of p61 access by SPI on priority*/
	P61_STATE_SPI_END = 0x4000,
	P61_STATE_SPI_SVDD_SYNC_START = 0x0001, /*ESE_VDD Low req by SPI*/
	P61_STATE_SPI_SVDD_SYNC_END = 0x0002, /*ESE_VDD is Low by SPI*/
	P61_STATE_DWP_SVDD_SYNC_START = 0x0003, /*ESE_VDD  Low req by Nfc*/
	P61_STATE_DWP_SVDD_SYNC_END = 0x0004, /*ESE_VDD is Low by Nfc*/
}p61_access_state_t;
#endif

struct pn547_i2c_platform_data {
	void (*conf_gpio) (void);
	int irq_gpio;
	int ven_gpio;
	int firm_gpio;
	int ese_pwr_req;
	int clk_req_gpio;
	int clk_req_irq;
	phys_addr_t clkctrl_addr; /*use NFC block of AP*/

	u32 irq_gpio_flags;
	u32 ven_gpio_flags;
	u32 firm_gpio_flags;
	u32 ese_pwr_req_flags;

	const char *i2c_1p8;
	struct   clk *clk;
};

extern int poweroff_charging;

extern unsigned int lpcharge;
