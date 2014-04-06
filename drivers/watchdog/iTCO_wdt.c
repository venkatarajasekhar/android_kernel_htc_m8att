/*
 *	intel TCO Watchdog Driver
 *
 *	(c) Copyright 2006-2011 Wim Van Sebroeck <wim@iguana.be>.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	Neither Wim Van Sebroeck nor Iguana vzw. admit liability nor
 *	provide warranty for any of this software. This material is
 *	provided "AS-IS" and at no charge.
 *
 *	The TCO watchdog is implemented in the following I/O controller hubs:
 *	(See the intel documentation on http://developer.intel.com.)
 *	document number 290655-003, 290677-014: 82801AA (ICH), 82801AB (ICHO)
 *	document number 290687-002, 298242-027: 82801BA (ICH2)
 *	document number 290733-003, 290739-013: 82801CA (ICH3-S)
 *	document number 290716-001, 290718-007: 82801CAM (ICH3-M)
 *	document number 290744-001, 290745-025: 82801DB (ICH4)
 *	document number 252337-001, 252663-008: 82801DBM (ICH4-M)
 *	document number 273599-001, 273645-002: 82801E (C-ICH)
 *	document number 252516-001, 252517-028: 82801EB (ICH5), 82801ER (ICH5R)
 *	document number 300641-004, 300884-013: 6300ESB
 *	document number 301473-002, 301474-026: 82801F (ICH6)
 *	document number 313082-001, 313075-006: 631xESB, 632xESB
 *	document number 307013-003, 307014-024: 82801G (ICH7)
 *	document number 322896-001, 322897-001: NM10
 *	document number 313056-003, 313057-017: 82801H (ICH8)
 *	document number 316972-004, 316973-012: 82801I (ICH9)
 *	document number 319973-002, 319974-002: 82801J (ICH10)
 *	document number 322169-001, 322170-003: 5 Series, 3400 Series (PCH)
 *	document number 320066-003, 320257-008: EP80597 (IICH)
 *	document number 324645-001, 324646-001: Cougar Point (CPT)
 *	document number TBD                   : Patsburg (PBG)
 *	document number TBD                   : DH89xxCC
 *	document number TBD                   : Panther Point
 *	document number TBD                   : Lynx Point
 */


#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#define DRV_NAME	"iTCO_wdt"
#define DRV_VERSION	"1.07"

#include <linux/module.h>		
#include <linux/moduleparam.h>		
#include <linux/types.h>		
#include <linux/errno.h>		
#include <linux/kernel.h>		
#include <linux/miscdevice.h>		
#include <linux/watchdog.h>		
#include <linux/init.h>			
#include <linux/fs.h>			
#include <linux/platform_device.h>	
#include <linux/pci.h>			
#include <linux/ioport.h>		
#include <linux/spinlock.h>		
#include <linux/uaccess.h>		
#include <linux/io.h>			

#include "iTCO_vendor.h"

enum iTCO_chipsets {
	TCO_ICH = 0,	
	TCO_ICH0,	
	TCO_ICH2,	
	TCO_ICH2M,	
	TCO_ICH3,	
	TCO_ICH3M,	
	TCO_ICH4,	
	TCO_ICH4M,	
	TCO_CICH,	
	TCO_ICH5,	
	TCO_6300ESB,	
	TCO_ICH6,	
	TCO_ICH6M,	
	TCO_ICH6W,	
	TCO_631XESB,	
	TCO_ICH7,	
	TCO_ICH7DH,	
	TCO_ICH7M,	
	TCO_ICH7MDH,	
	TCO_NM10,	
	TCO_ICH8,	
	TCO_ICH8DH,	
	TCO_ICH8DO,	
	TCO_ICH8M,	
	TCO_ICH8ME,	
	TCO_ICH9,	
	TCO_ICH9R,	
	TCO_ICH9DH,	
	TCO_ICH9DO,	
	TCO_ICH9M,	
	TCO_ICH9ME,	
	TCO_ICH10,	
	TCO_ICH10R,	
	TCO_ICH10D,	
	TCO_ICH10DO,	
	TCO_PCH,	
	TCO_PCHM,	
	TCO_P55,	
	TCO_PM55,	
	TCO_H55,	
	TCO_QM57,	
	TCO_H57,	
	TCO_HM55,	
	TCO_Q57,	
	TCO_HM57,	
	TCO_PCHMSFF,	
	TCO_QS57,	
	TCO_3400,	
	TCO_3420,	
	TCO_3450,	
	TCO_EP80579,	
	TCO_CPT,	
	TCO_CPTD,	
	TCO_CPTM,	
	TCO_PBG,	
	TCO_DH89XXCC,	
	TCO_PPT,	
	TCO_LPT,	
};

static struct {
	char *name;
	unsigned int iTCO_version;
} iTCO_chipset_info[] __devinitdata = {
	{"ICH", 1},
	{"ICH0", 1},
	{"ICH2", 1},
	{"ICH2-M", 1},
	{"ICH3-S", 1},
	{"ICH3-M", 1},
	{"ICH4", 1},
	{"ICH4-M", 1},
	{"C-ICH", 1},
	{"ICH5 or ICH5R", 1},
	{"6300ESB", 1},
	{"ICH6 or ICH6R", 2},
	{"ICH6-M", 2},
	{"ICH6W or ICH6RW", 2},
	{"631xESB/632xESB", 2},
	{"ICH7 or ICH7R", 2},
	{"ICH7DH", 2},
	{"ICH7-M or ICH7-U", 2},
	{"ICH7-M DH", 2},
	{"NM10", 2},
	{"ICH8 or ICH8R", 2},
	{"ICH8DH", 2},
	{"ICH8DO", 2},
	{"ICH8M", 2},
	{"ICH8M-E", 2},
	{"ICH9", 2},
	{"ICH9R", 2},
	{"ICH9DH", 2},
	{"ICH9DO", 2},
	{"ICH9M", 2},
	{"ICH9M-E", 2},
	{"ICH10", 2},
	{"ICH10R", 2},
	{"ICH10D", 2},
	{"ICH10DO", 2},
	{"PCH Desktop Full Featured", 2},
	{"PCH Mobile Full Featured", 2},
	{"P55", 2},
	{"PM55", 2},
	{"H55", 2},
	{"QM57", 2},
	{"H57", 2},
	{"HM55", 2},
	{"Q57", 2},
	{"HM57", 2},
	{"PCH Mobile SFF Full Featured", 2},
	{"QS57", 2},
	{"3400", 2},
	{"3420", 2},
	{"3450", 2},
	{"EP80579", 2},
	{"Cougar Point", 2},
	{"Cougar Point Desktop", 2},
	{"Cougar Point Mobile", 2},
	{"Patsburg", 2},
	{"DH89xxCC", 2},
	{"Panther Point", 2},
	{"Lynx Point", 2},
	{NULL, 0}
};

static DEFINE_PCI_DEVICE_TABLE(iTCO_wdt_pci_tbl) = {
	{ PCI_VDEVICE(INTEL, 0x2410), TCO_ICH},
	{ PCI_VDEVICE(INTEL, 0x2420), TCO_ICH0},
	{ PCI_VDEVICE(INTEL, 0x2440), TCO_ICH2},
	{ PCI_VDEVICE(INTEL, 0x244c), TCO_ICH2M},
	{ PCI_VDEVICE(INTEL, 0x2480), TCO_ICH3},
	{ PCI_VDEVICE(INTEL, 0x248c), TCO_ICH3M},
	{ PCI_VDEVICE(INTEL, 0x24c0), TCO_ICH4},
	{ PCI_VDEVICE(INTEL, 0x24cc), TCO_ICH4M},
	{ PCI_VDEVICE(INTEL, 0x2450), TCO_CICH},
	{ PCI_VDEVICE(INTEL, 0x24d0), TCO_ICH5},
	{ PCI_VDEVICE(INTEL, 0x25a1), TCO_6300ESB},
	{ PCI_VDEVICE(INTEL, 0x2640), TCO_ICH6},
	{ PCI_VDEVICE(INTEL, 0x2641), TCO_ICH6M},
	{ PCI_VDEVICE(INTEL, 0x2642), TCO_ICH6W},
	{ PCI_VDEVICE(INTEL, 0x2670), TCO_631XESB},
	{ PCI_VDEVICE(INTEL, 0x2671), TCO_631XESB},
	{ PCI_VDEVICE(INTEL, 0x2672), TCO_631XESB},
	{ PCI_VDEVICE(INTEL, 0x2673), TCO_631XESB},
	{ PCI_VDEVICE(INTEL, 0x2674), TCO_631XESB},
	{ PCI_VDEVICE(INTEL, 0x2675), TCO_631XESB},
	{ PCI_VDEVICE(INTEL, 0x2676), TCO_631XESB},
	{ PCI_VDEVICE(INTEL, 0x2677), TCO_631XESB},
	{ PCI_VDEVICE(INTEL, 0x2678), TCO_631XESB},
	{ PCI_VDEVICE(INTEL, 0x2679), TCO_631XESB},
	{ PCI_VDEVICE(INTEL, 0x267a), TCO_631XESB},
	{ PCI_VDEVICE(INTEL, 0x267b), TCO_631XESB},
	{ PCI_VDEVICE(INTEL, 0x267c), TCO_631XESB},
	{ PCI_VDEVICE(INTEL, 0x267d), TCO_631XESB},
	{ PCI_VDEVICE(INTEL, 0x267e), TCO_631XESB},
	{ PCI_VDEVICE(INTEL, 0x267f), TCO_631XESB},
	{ PCI_VDEVICE(INTEL, 0x27b8), TCO_ICH7},
	{ PCI_VDEVICE(INTEL, 0x27b0), TCO_ICH7DH},
	{ PCI_VDEVICE(INTEL, 0x27b9), TCO_ICH7M},
	{ PCI_VDEVICE(INTEL, 0x27bd), TCO_ICH7MDH},
	{ PCI_VDEVICE(INTEL, 0x27bc), TCO_NM10},
	{ PCI_VDEVICE(INTEL, 0x2810), TCO_ICH8},
	{ PCI_VDEVICE(INTEL, 0x2812), TCO_ICH8DH},
	{ PCI_VDEVICE(INTEL, 0x2814), TCO_ICH8DO},
	{ PCI_VDEVICE(INTEL, 0x2815), TCO_ICH8M},
	{ PCI_VDEVICE(INTEL, 0x2811), TCO_ICH8ME},
	{ PCI_VDEVICE(INTEL, 0x2918), TCO_ICH9},
	{ PCI_VDEVICE(INTEL, 0x2916), TCO_ICH9R},
	{ PCI_VDEVICE(INTEL, 0x2912), TCO_ICH9DH},
	{ PCI_VDEVICE(INTEL, 0x2914), TCO_ICH9DO},
	{ PCI_VDEVICE(INTEL, 0x2919), TCO_ICH9M},
	{ PCI_VDEVICE(INTEL, 0x2917), TCO_ICH9ME},
	{ PCI_VDEVICE(INTEL, 0x3a18), TCO_ICH10},
	{ PCI_VDEVICE(INTEL, 0x3a16), TCO_ICH10R},
	{ PCI_VDEVICE(INTEL, 0x3a1a), TCO_ICH10D},
	{ PCI_VDEVICE(INTEL, 0x3a14), TCO_ICH10DO},
	{ PCI_VDEVICE(INTEL, 0x3b00), TCO_PCH},
	{ PCI_VDEVICE(INTEL, 0x3b01), TCO_PCHM},
	{ PCI_VDEVICE(INTEL, 0x3b02), TCO_P55},
	{ PCI_VDEVICE(INTEL, 0x3b03), TCO_PM55},
	{ PCI_VDEVICE(INTEL, 0x3b06), TCO_H55},
	{ PCI_VDEVICE(INTEL, 0x3b07), TCO_QM57},
	{ PCI_VDEVICE(INTEL, 0x3b08), TCO_H57},
	{ PCI_VDEVICE(INTEL, 0x3b09), TCO_HM55},
	{ PCI_VDEVICE(INTEL, 0x3b0a), TCO_Q57},
	{ PCI_VDEVICE(INTEL, 0x3b0b), TCO_HM57},
	{ PCI_VDEVICE(INTEL, 0x3b0d), TCO_PCHMSFF},
	{ PCI_VDEVICE(INTEL, 0x3b0f), TCO_QS57},
	{ PCI_VDEVICE(INTEL, 0x3b12), TCO_3400},
	{ PCI_VDEVICE(INTEL, 0x3b14), TCO_3420},
	{ PCI_VDEVICE(INTEL, 0x3b16), TCO_3450},
	{ PCI_VDEVICE(INTEL, 0x5031), TCO_EP80579},
	{ PCI_VDEVICE(INTEL, 0x1c41), TCO_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c42), TCO_CPTD},
	{ PCI_VDEVICE(INTEL, 0x1c43), TCO_CPTM},
	{ PCI_VDEVICE(INTEL, 0x1c44), TCO_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c45), TCO_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c46), TCO_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c47), TCO_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c48), TCO_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c49), TCO_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c4a), TCO_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c4b), TCO_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c4c), TCO_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c4d), TCO_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c4e), TCO_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c4f), TCO_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c50), TCO_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c51), TCO_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c52), TCO_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c53), TCO_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c54), TCO_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c55), TCO_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c56), TCO_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c57), TCO_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c58), TCO_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c59), TCO_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c5a), TCO_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c5b), TCO_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c5c), TCO_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c5d), TCO_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c5e), TCO_CPT},
	{ PCI_VDEVICE(INTEL, 0x1c5f), TCO_CPT},
	{ PCI_VDEVICE(INTEL, 0x1d40), TCO_PBG},
	{ PCI_VDEVICE(INTEL, 0x1d41), TCO_PBG},
	{ PCI_VDEVICE(INTEL, 0x2310), TCO_DH89XXCC},
	{ PCI_VDEVICE(INTEL, 0x1e40), TCO_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e41), TCO_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e42), TCO_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e43), TCO_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e44), TCO_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e45), TCO_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e46), TCO_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e47), TCO_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e48), TCO_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e49), TCO_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e4a), TCO_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e4b), TCO_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e4c), TCO_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e4d), TCO_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e4e), TCO_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e4f), TCO_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e50), TCO_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e51), TCO_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e52), TCO_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e53), TCO_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e54), TCO_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e55), TCO_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e56), TCO_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e57), TCO_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e58), TCO_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e59), TCO_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e5a), TCO_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e5b), TCO_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e5c), TCO_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e5d), TCO_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e5e), TCO_PPT},
	{ PCI_VDEVICE(INTEL, 0x1e5f), TCO_PPT},
	{ PCI_VDEVICE(INTEL, 0x8c40), TCO_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c41), TCO_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c42), TCO_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c43), TCO_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c44), TCO_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c45), TCO_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c46), TCO_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c47), TCO_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c48), TCO_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c49), TCO_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c4a), TCO_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c4b), TCO_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c4c), TCO_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c4d), TCO_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c4e), TCO_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c4f), TCO_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c50), TCO_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c51), TCO_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c52), TCO_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c53), TCO_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c54), TCO_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c55), TCO_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c56), TCO_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c57), TCO_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c58), TCO_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c59), TCO_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c5a), TCO_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c5b), TCO_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c5c), TCO_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c5d), TCO_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c5e), TCO_LPT},
	{ PCI_VDEVICE(INTEL, 0x8c5f), TCO_LPT},
	{ 0, },			
};
MODULE_DEVICE_TABLE(pci, iTCO_wdt_pci_tbl);

#define TCOBASE		(iTCO_wdt_private.ACPIBASE + 0x60)
#define SMI_EN		(iTCO_wdt_private.ACPIBASE + 0x30)

#define TCO_RLD		(TCOBASE + 0x00) 
#define TCOv1_TMR	(TCOBASE + 0x01) 
#define TCO_DAT_IN	(TCOBASE + 0x02) 
#define TCO_DAT_OUT	(TCOBASE + 0x03) 
#define TCO1_STS	(TCOBASE + 0x04) 
#define TCO2_STS	(TCOBASE + 0x06) 
#define TCO1_CNT	(TCOBASE + 0x08) 
#define TCO2_CNT	(TCOBASE + 0x0a) 
#define TCOv2_TMR	(TCOBASE + 0x12) 

static unsigned long is_active;
static char expect_release;
static struct {		
	
	unsigned int iTCO_version;
	
	unsigned long ACPIBASE;
	
	unsigned long __iomem *gcs;
	
	spinlock_t io_lock;
	
	struct pci_dev *pdev;
} iTCO_wdt_private;

static struct platform_device *iTCO_wdt_platform_device;

#define WATCHDOG_HEARTBEAT 30	
static int heartbeat = WATCHDOG_HEARTBEAT;  
module_param(heartbeat, int, 0);
MODULE_PARM_DESC(heartbeat, "Watchdog timeout in seconds. "
	"5..76 (TCO v1) or 3..614 (TCO v2), default="
				__MODULE_STRING(WATCHDOG_HEARTBEAT) ")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
	"Watchdog cannot be stopped once started (default="
				__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static int turn_SMI_watchdog_clear_off = 1;
module_param(turn_SMI_watchdog_clear_off, int, 0);
MODULE_PARM_DESC(turn_SMI_watchdog_clear_off,
	"Turn off SMI clearing watchdog (depends on TCO-version)(default=1)");


static inline unsigned int seconds_to_ticks(int seconds)
{
	return (seconds * 10) / 6;
}

static void iTCO_wdt_set_NO_REBOOT_bit(void)
{
	u32 val32;

	
	if (iTCO_wdt_private.iTCO_version == 2) {
		val32 = readl(iTCO_wdt_private.gcs);
		val32 |= 0x00000020;
		writel(val32, iTCO_wdt_private.gcs);
	} else if (iTCO_wdt_private.iTCO_version == 1) {
		pci_read_config_dword(iTCO_wdt_private.pdev, 0xd4, &val32);
		val32 |= 0x00000002;
		pci_write_config_dword(iTCO_wdt_private.pdev, 0xd4, val32);
	}
}

static int iTCO_wdt_unset_NO_REBOOT_bit(void)
{
	int ret = 0;
	u32 val32;

	
	if (iTCO_wdt_private.iTCO_version == 2) {
		val32 = readl(iTCO_wdt_private.gcs);
		val32 &= 0xffffffdf;
		writel(val32, iTCO_wdt_private.gcs);

		val32 = readl(iTCO_wdt_private.gcs);
		if (val32 & 0x00000020)
			ret = -EIO;
	} else if (iTCO_wdt_private.iTCO_version == 1) {
		pci_read_config_dword(iTCO_wdt_private.pdev, 0xd4, &val32);
		val32 &= 0xfffffffd;
		pci_write_config_dword(iTCO_wdt_private.pdev, 0xd4, val32);

		pci_read_config_dword(iTCO_wdt_private.pdev, 0xd4, &val32);
		if (val32 & 0x00000002)
			ret = -EIO;
	}

	return ret; 
}

static int iTCO_wdt_start(void)
{
	unsigned int val;

	spin_lock(&iTCO_wdt_private.io_lock);

	iTCO_vendor_pre_start(iTCO_wdt_private.ACPIBASE, heartbeat);

	
	if (iTCO_wdt_unset_NO_REBOOT_bit()) {
		spin_unlock(&iTCO_wdt_private.io_lock);
		pr_err("failed to reset NO_REBOOT flag, reboot disabled by hardware/BIOS\n");
		return -EIO;
	}

	if (iTCO_wdt_private.iTCO_version == 2)
		outw(0x01, TCO_RLD);
	else if (iTCO_wdt_private.iTCO_version == 1)
		outb(0x01, TCO_RLD);

	
	val = inw(TCO1_CNT);
	val &= 0xf7ff;
	outw(val, TCO1_CNT);
	val = inw(TCO1_CNT);
	spin_unlock(&iTCO_wdt_private.io_lock);

	if (val & 0x0800)
		return -1;
	return 0;
}

static int iTCO_wdt_stop(void)
{
	unsigned int val;

	spin_lock(&iTCO_wdt_private.io_lock);

	iTCO_vendor_pre_stop(iTCO_wdt_private.ACPIBASE);

	
	val = inw(TCO1_CNT);
	val |= 0x0800;
	outw(val, TCO1_CNT);
	val = inw(TCO1_CNT);

	
	iTCO_wdt_set_NO_REBOOT_bit();

	spin_unlock(&iTCO_wdt_private.io_lock);

	if ((val & 0x0800) == 0)
		return -1;
	return 0;
}

static int iTCO_wdt_keepalive(void)
{
	spin_lock(&iTCO_wdt_private.io_lock);

	iTCO_vendor_pre_keepalive(iTCO_wdt_private.ACPIBASE, heartbeat);

	
	if (iTCO_wdt_private.iTCO_version == 2)
		outw(0x01, TCO_RLD);
	else if (iTCO_wdt_private.iTCO_version == 1) {
		outw(0x0008, TCO1_STS);	

		outb(0x01, TCO_RLD);
	}

	spin_unlock(&iTCO_wdt_private.io_lock);
	return 0;
}

static int iTCO_wdt_set_heartbeat(int t)
{
	unsigned int val16;
	unsigned char val8;
	unsigned int tmrval;

	tmrval = seconds_to_ticks(t);

	
	if (iTCO_wdt_private.iTCO_version == 1)
		tmrval /= 2;

	
	
	if (tmrval < 0x04)
		return -EINVAL;
	if (((iTCO_wdt_private.iTCO_version == 2) && (tmrval > 0x3ff)) ||
	    ((iTCO_wdt_private.iTCO_version == 1) && (tmrval > 0x03f)))
		return -EINVAL;

	iTCO_vendor_pre_set_heartbeat(tmrval);

	
	if (iTCO_wdt_private.iTCO_version == 2) {
		spin_lock(&iTCO_wdt_private.io_lock);
		val16 = inw(TCOv2_TMR);
		val16 &= 0xfc00;
		val16 |= tmrval;
		outw(val16, TCOv2_TMR);
		val16 = inw(TCOv2_TMR);
		spin_unlock(&iTCO_wdt_private.io_lock);

		if ((val16 & 0x3ff) != tmrval)
			return -EINVAL;
	} else if (iTCO_wdt_private.iTCO_version == 1) {
		spin_lock(&iTCO_wdt_private.io_lock);
		val8 = inb(TCOv1_TMR);
		val8 &= 0xc0;
		val8 |= (tmrval & 0xff);
		outb(val8, TCOv1_TMR);
		val8 = inb(TCOv1_TMR);
		spin_unlock(&iTCO_wdt_private.io_lock);

		if ((val8 & 0x3f) != tmrval)
			return -EINVAL;
	}

	heartbeat = t;
	return 0;
}

static int iTCO_wdt_get_timeleft(int *time_left)
{
	unsigned int val16;
	unsigned char val8;

	
	if (iTCO_wdt_private.iTCO_version == 2) {
		spin_lock(&iTCO_wdt_private.io_lock);
		val16 = inw(TCO_RLD);
		val16 &= 0x3ff;
		spin_unlock(&iTCO_wdt_private.io_lock);

		*time_left = (val16 * 6) / 10;
	} else if (iTCO_wdt_private.iTCO_version == 1) {
		spin_lock(&iTCO_wdt_private.io_lock);
		val8 = inb(TCO_RLD);
		val8 &= 0x3f;
		if (!(inw(TCO1_STS) & 0x0008))
			val8 += (inb(TCOv1_TMR) & 0x3f);
		spin_unlock(&iTCO_wdt_private.io_lock);

		*time_left = (val8 * 6) / 10;
	} else
		return -EINVAL;
	return 0;
}


static int iTCO_wdt_open(struct inode *inode, struct file *file)
{
	
	if (test_and_set_bit(0, &is_active))
		return -EBUSY;

	iTCO_wdt_start();
	return nonseekable_open(inode, file);
}

static int iTCO_wdt_release(struct inode *inode, struct file *file)
{
	if (expect_release == 42) {
		iTCO_wdt_stop();
	} else {
		pr_crit("Unexpected close, not stopping watchdog!\n");
		iTCO_wdt_keepalive();
	}
	clear_bit(0, &is_active);
	expect_release = 0;
	return 0;
}

static ssize_t iTCO_wdt_write(struct file *file, const char __user *data,
			      size_t len, loff_t *ppos)
{
	
	if (len) {
		if (!nowayout) {
			size_t i;

			expect_release = 0;

			for (i = 0; i != len; i++) {
				char c;
				if (get_user(c, data + i))
					return -EFAULT;
				if (c == 'V')
					expect_release = 42;
			}
		}

		
		iTCO_wdt_keepalive();
	}
	return len;
}

static long iTCO_wdt_ioctl(struct file *file, unsigned int cmd,
							unsigned long arg)
{
	int new_options, retval = -EINVAL;
	int new_heartbeat;
	void __user *argp = (void __user *)arg;
	int __user *p = argp;
	static const struct watchdog_info ident = {
		.options =		WDIOF_SETTIMEOUT |
					WDIOF_KEEPALIVEPING |
					WDIOF_MAGICCLOSE,
		.firmware_version =	0,
		.identity =		DRV_NAME,
	};

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		return copy_to_user(argp, &ident, sizeof(ident)) ? -EFAULT : 0;
	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, p);

	case WDIOC_SETOPTIONS:
	{
		if (get_user(new_options, p))
			return -EFAULT;

		if (new_options & WDIOS_DISABLECARD) {
			iTCO_wdt_stop();
			retval = 0;
		}
		if (new_options & WDIOS_ENABLECARD) {
			iTCO_wdt_keepalive();
			iTCO_wdt_start();
			retval = 0;
		}
		return retval;
	}
	case WDIOC_KEEPALIVE:
		iTCO_wdt_keepalive();
		return 0;

	case WDIOC_SETTIMEOUT:
	{
		if (get_user(new_heartbeat, p))
			return -EFAULT;
		if (iTCO_wdt_set_heartbeat(new_heartbeat))
			return -EINVAL;
		iTCO_wdt_keepalive();
		
	}
	case WDIOC_GETTIMEOUT:
		return put_user(heartbeat, p);
	case WDIOC_GETTIMELEFT:
	{
		int time_left;
		if (iTCO_wdt_get_timeleft(&time_left))
			return -EINVAL;
		return put_user(time_left, p);
	}
	default:
		return -ENOTTY;
	}
}


static const struct file_operations iTCO_wdt_fops = {
	.owner =		THIS_MODULE,
	.llseek =		no_llseek,
	.write =		iTCO_wdt_write,
	.unlocked_ioctl =	iTCO_wdt_ioctl,
	.open =			iTCO_wdt_open,
	.release =		iTCO_wdt_release,
};

static struct miscdevice iTCO_wdt_miscdev = {
	.minor =	WATCHDOG_MINOR,
	.name =		"watchdog",
	.fops =		&iTCO_wdt_fops,
};


static int __devinit iTCO_wdt_init(struct pci_dev *pdev,
		const struct pci_device_id *ent, struct platform_device *dev)
{
	int ret;
	u32 base_address;
	unsigned long RCBA;
	unsigned long val32;

	pci_read_config_dword(pdev, 0x40, &base_address);
	base_address &= 0x0000ff80;
	if (base_address == 0x00000000) {
		
		pr_err("failed to get TCOBASE address, device disabled by hardware/BIOS\n");
		return -ENODEV;
	}
	iTCO_wdt_private.iTCO_version =
			iTCO_chipset_info[ent->driver_data].iTCO_version;
	iTCO_wdt_private.ACPIBASE = base_address;
	iTCO_wdt_private.pdev = pdev;

	if (iTCO_wdt_private.iTCO_version == 2) {
		pci_read_config_dword(pdev, 0xf0, &base_address);
		if ((base_address & 1) == 0) {
			pr_err("RCBA is disabled by hardware/BIOS, device disabled\n");
			ret = -ENODEV;
			goto out;
		}
		RCBA = base_address & 0xffffc000;
		iTCO_wdt_private.gcs = ioremap((RCBA + 0x3410), 4);
	}

	
	if (iTCO_wdt_unset_NO_REBOOT_bit() && iTCO_vendor_check_noreboot_on()) {
		pr_info("unable to reset NO_REBOOT flag, device disabled by hardware/BIOS\n");
		ret = -ENODEV;	
		goto out_unmap;
	}

	
	iTCO_wdt_set_NO_REBOOT_bit();

	
	if (!request_region(SMI_EN, 4, "iTCO_wdt")) {
		pr_err("I/O address 0x%04lx already in use, device disabled\n",
		       SMI_EN);
		ret = -EIO;
		goto out_unmap;
	}
	if (turn_SMI_watchdog_clear_off >= iTCO_wdt_private.iTCO_version) {
		
		val32 = inl(SMI_EN);
		val32 &= 0xffffdfff;	
		outl(val32, SMI_EN);
	}

	if (!request_region(TCOBASE, 0x20, "iTCO_wdt")) {
		pr_err("I/O address 0x%04lx already in use, device disabled\n",
		       TCOBASE);
		ret = -EIO;
		goto unreg_smi_en;
	}

	pr_info("Found a %s TCO device (Version=%d, TCOBASE=0x%04lx)\n",
		iTCO_chipset_info[ent->driver_data].name,
		iTCO_chipset_info[ent->driver_data].iTCO_version,
		TCOBASE);

	
	outw(0x0008, TCO1_STS);	
	outw(0x0002, TCO2_STS);	
	outw(0x0004, TCO2_STS);	

	
	iTCO_wdt_stop();

	if (iTCO_wdt_set_heartbeat(heartbeat)) {
		iTCO_wdt_set_heartbeat(WATCHDOG_HEARTBEAT);
		pr_info("timeout value out of range, using %d\n", heartbeat);
	}

	ret = misc_register(&iTCO_wdt_miscdev);
	if (ret != 0) {
		pr_err("cannot register miscdev on minor=%d (err=%d)\n",
		       WATCHDOG_MINOR, ret);
		goto unreg_region;
	}

	pr_info("initialized. heartbeat=%d sec (nowayout=%d)\n",
		heartbeat, nowayout);

	return 0;

unreg_region:
	release_region(TCOBASE, 0x20);
unreg_smi_en:
	release_region(SMI_EN, 4);
out_unmap:
	if (iTCO_wdt_private.iTCO_version == 2)
		iounmap(iTCO_wdt_private.gcs);
out:
	iTCO_wdt_private.ACPIBASE = 0;
	return ret;
}

static void __devexit iTCO_wdt_cleanup(void)
{
	
	if (!nowayout)
		iTCO_wdt_stop();

	
	misc_deregister(&iTCO_wdt_miscdev);
	release_region(TCOBASE, 0x20);
	release_region(SMI_EN, 4);
	if (iTCO_wdt_private.iTCO_version == 2)
		iounmap(iTCO_wdt_private.gcs);
	pci_dev_put(iTCO_wdt_private.pdev);
	iTCO_wdt_private.ACPIBASE = 0;
}

static int __devinit iTCO_wdt_probe(struct platform_device *dev)
{
	int ret = -ENODEV;
	int found = 0;
	struct pci_dev *pdev = NULL;
	const struct pci_device_id *ent;

	spin_lock_init(&iTCO_wdt_private.io_lock);

	for_each_pci_dev(pdev) {
		ent = pci_match_id(iTCO_wdt_pci_tbl, pdev);
		if (ent) {
			found++;
			ret = iTCO_wdt_init(pdev, ent, dev);
			if (!ret)
				break;
		}
	}

	if (!found)
		pr_info("No device detected\n");

	return ret;
}

static int __devexit iTCO_wdt_remove(struct platform_device *dev)
{
	if (iTCO_wdt_private.ACPIBASE)
		iTCO_wdt_cleanup();

	return 0;
}

static void iTCO_wdt_shutdown(struct platform_device *dev)
{
	iTCO_wdt_stop();
}

static struct platform_driver iTCO_wdt_driver = {
	.probe          = iTCO_wdt_probe,
	.remove         = __devexit_p(iTCO_wdt_remove),
	.shutdown       = iTCO_wdt_shutdown,
	.driver         = {
		.owner  = THIS_MODULE,
		.name   = DRV_NAME,
	},
};

static int __init iTCO_wdt_init_module(void)
{
	int err;

	pr_info("Intel TCO WatchDog Timer Driver v%s\n", DRV_VERSION);

	err = platform_driver_register(&iTCO_wdt_driver);
	if (err)
		return err;

	iTCO_wdt_platform_device = platform_device_register_simple(DRV_NAME,
								-1, NULL, 0);
	if (IS_ERR(iTCO_wdt_platform_device)) {
		err = PTR_ERR(iTCO_wdt_platform_device);
		goto unreg_platform_driver;
	}

	return 0;

unreg_platform_driver:
	platform_driver_unregister(&iTCO_wdt_driver);
	return err;
}

static void __exit iTCO_wdt_cleanup_module(void)
{
	platform_device_unregister(iTCO_wdt_platform_device);
	platform_driver_unregister(&iTCO_wdt_driver);
	pr_info("Watchdog Module Unloaded\n");
}

module_init(iTCO_wdt_init_module);
module_exit(iTCO_wdt_cleanup_module);

MODULE_AUTHOR("Wim Van Sebroeck <wim@iguana.be>");
MODULE_DESCRIPTION("Intel TCO WatchDog Timer Driver");
MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);