/*
 * $QNXLicenseC:
 * Copyright 2013-2014, QNX Software Systems.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"). You
 * may not reproduce, modify or distribute this software except in
 * compliance with the License. You may obtain a copy of the License
 * at: http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTIES OF ANY KIND, either express or implied.
 *
 * This file may contain contributions from others, either as
 * contributors under the License or as licensors under other terms.
 * Please review this entire file for other proprietary rights or license
 * notices, as well as the QNX Development Suite License Guide at
 * http://licensing.qnx.com/license-guide/ for other information.
 * $
 */

#include "dra74x_startup.h"
#include "board.h"

extern int pmic_revision;

/* TLC59108 registers */
#define SUBADR1_ADDR		0x0E
#define SUBADR2_ADDR		0x0F
#define SUBADR3_ADDR		0x10
#define SUBADR1_DEFAULT		0x092
#define SUBADR2_DEFAULT		0x094
#define SUBADR3_DEFAULT		0x098

/* Amount of time to leave WiFi chip in reset */
#define WIFI_RESET_DELAY	200000

/***************************************************************************************************
 * Crossbar interrupts
 ***************************************************************************************************/
const struct crossbar_entry mpu_irq_map[] = {
	/* MPU_IRQ, CROSSBAR_IRQ */
	{49, 361},		/* EDMA_TPPC_IRQ_REGION0 */
	{145, 232},		/* PCIe_SS1_IRQ_INT0 mapped to MPU_IRQ_145 */
	{146, 233},		/* PCIe_SS1_IRQ_INT1 mapped to MPU_IRQ_146 */
	{147, 355},		/* PCIe_SS2_IRQ_INT0 mapped to MPU_IRQ_147 */
	{148, 356},		/* PCIe_SS2_IRQ_INT1 mapped to MPU_IRQ_148 */

	/*
	 * McASP1 interrupts are mapped to MPU_IRQ directly by default
	 *		McASP1_IRQ_AREVT -> MPU_IRQ_108
	 *		McASP1_IRQ_AXEVT -> MPU_IRQ_109
	 */
	{0, 148},			/* MCASP2_IRQ_AREVT mapped to MPU_IRQ_155 */
	{0, 149},			/* MCASP2_IRQ_AXEVT mapped to MPU_IRQ_156 */
	{158, 150},		/* MCASP3_IRQ_AREVT mapped to MPU_IRQ_158 */
	{157, 151},		/* MCASP3_IRQ_AXEVT mapped to MPU_IRQ_157 */

	{155, 154},		/* MCASP5_IRQ_AREVT mapped to MPU_IRQ_155 */
	{156, 155},		/* MCASP5_IRQ_AXEVT mapped to MPU_IRQ_156 */
	
	{151, 156},		/* MCASP6_IRQ_AREVT mapped to MPU_IRQ_151 */
	{152, 157},		/* MCASP6_IRQ_AXEVT mapped to MPU_IRQ_152 */
	{153, 158},		/* MCASP7_IRQ_AREVT mapped to MPU_IRQ_153 */
	{154, 159},		/* MCASP7_IRQ_AXEVT mapped to MPU_IRQ_154 */

	{115, 335},		/* GMAC_SW_IRQ_RX_PULSE */
	{116, 336},		/* GMAC_SW_IRQ_TX_PULSE */
	{117, 337},		/* GMAC_SW_IRQ_MISC_PULSE */

	{VIP1_IRQ, 351},	/* VIP1_IRQ_1 */
	{VIP2_IRQ, 352},	/* VIP2_IRQ_1 */
	{VIP3_IRQ, 353},	/* VIP3_IRQ_1 */
	{VPE_IRQ, 354},		/* VPE_IRQ */

	{CRYPTO_AES_IRQ, 110},	/* Crypto: AES */
	{CRYPTO_MD5_IRQ, 44},	/* Crypto: MD5 */
};

const struct crossbar_entry edma_dreq_map[] = {
	/* EDMA_DREQ, CROSSBAR_DMA */
	{9, 128},		/* MCASP1_DREQ_RX */
	{8, 129},		/* MCASP1_DREQ_TX */
	{0, 130},		/* MCASP2_DREQ_RX */
	{0, 131},		/* MCASP2_DREQ_TX */
	{59, 132},		/* MCASP3_DREQ_RX */
	{58, 133},		/* MCASP3_DREQ_TX */
	
	{11, 136},		/* MCASP5_DREQ_RX */
	{10, 137},		/* MCASP5_DREQ_TX */
	
	{17, 138},		/* MCASP6_DREQ_RX */
	{16, 139},		/* MCASP6_DREQ_TX */
	{19, 140},		/* MCASP7_DREQ_RX */
	{18, 141},		/* MCASP7_DREQ_TX */
};

static void set_crossbar_reg(void)
{
	dra74x_set_mpu_crossbar(DRA74X_CTRL_CORE_BASE + CTRL_CORE_MPU_IRQ_4_5, mpu_irq_map,
		sizeof(mpu_irq_map)/sizeof(struct crossbar_entry));

	dra74x_set_edma_crossbar(DRA74X_CTRL_CORE_BASE + CTRL_CORE_DMA_EDMA_DREQ_0_1, edma_dreq_map,
		sizeof(edma_dreq_map)/sizeof(struct crossbar_entry));
}

/***************************************************************************************************
 * COM8 module
 ***************************************************************************************************/
static void init_com8()
{
	uint8_t buf[2];
	omap_i2c_dev_t i2c_dev = {
		.base = DRA74X_I2C1_BASE,
		.clock = I2C_SPEED,
		.slave = IO_EXPANDER_2_I2C_ADDR
	};

	/* IO Expander PCF8575, U57 */
	omap_i2c_init(&i2c_dev);

	memset(buf, NULL, sizeof (buf));
	if (0 != omap_i2c_read(&i2c_dev, 0, 1, buf, 2)) {
		kprintf("failed to read from PCF8575\n");
	}

	/* Clear P15 (McASP1_ENn) to enable COM8
	 * Clear P16 (SEL_UART3_SPI2) to select UART3 over SPI2
	 * Note: Switches 6 (McASP1_ENn) on SW5 (USERCONFIG) must
	 *		also be in the OFF position.
	 */
	buf[1] &= ~(PCF8575_P15 | PCF8575_P16);

	if (gpmc_vout3) {
		/* Clear P0 (SEL_GPMC_AD_VID_S0) to enable FPD-III */
		buf[0] &= ~(PCF8575_P0);
	} else {
		/* Set P0 (SEL_GPMC_AD_VID_S0) to enable GPMC interface for either NAND for NOR Flash */
		buf[0] |= PCF8575_P0;
	}

	if (0 != omap_i2c_write(&i2c_dev, buf, 2)) {
		kprintf("failed to write to PCF8575\n");
	}

	/* IO Expander PCF8575, U119 */
	i2c_dev.base = DRA74X_I2C2_BASE,
	i2c_dev.slave = IO_EXPANDER_3_I2C_ADDR;
	omap_i2c_init(&i2c_dev);

	memset(buf, NULL, sizeof (buf));
	if (0 != omap_i2c_read(&i2c_dev, 0, 1, buf, 2)) {
		kprintf("failed to read from PCF8575\n");
	}

	/* Clear P1 (VIN6_SEL_S0) to select McASP1,2,3, & 7 */
	buf[0] &= ~(PCF8575_P1);
	if (0 != omap_i2c_write(&i2c_dev, buf, 2)) {
		kprintf("failed to write to PCF8575\n");
	}

	/* PMIC configuration */
	i2c_dev.base = DRA74X_I2C1_BASE;
	i2c_dev.slave = TPS659038_I2C_SLAVE_ADDR;
	omap_i2c_init(&i2c_dev);

	/* select CLK32KGA as GPIO_5 output */
	buf[0] = PMIC_PAD2A;
	buf[1] = 0x2b;
	if (omap_i2c_write (&i2c_dev, buf, 2) != 0) {
		kprintf ("PMIC write PAD2 failed\n");
	}

	/* enable CLK32K */
	buf[0] = PMIC_CLK32AUDIO_CTRLA;
	buf[1] = 0x01;
	if (omap_i2c_write (&i2c_dev, buf, 2) != 0) {
		kprintf ("PMIC write CTRLA failed\n");
	}

	omap_i2c_deinit(&i2c_dev);
}

/***************************************************************************************************
 * JAMR3 board init
 ***************************************************************************************************/
static void init_jamr3()
{
	uint8_t buf[2];

	/*
	 * This is for IO Expander PCF8575, U0005 on JAMR3 application board
	 * Please note that on JAMR3, SW2 should be set to OFF-OFF,
	 * so that I2C4 on CPU board can connect to JAMR3
	 */
	omap_i2c_dev_t i2c_dev = {
		.base = DRA74X_I2C4_BASE,
		.clock = I2C_SPEED,
		.slave = IO_EXPANDER_2_I2C_ADDR
	};

	omap_i2c_init(&i2c_dev);

	memset(buf, NULL, sizeof (buf));
	if (0 != omap_i2c_read(&i2c_dev, 0, 1, buf, 2)) {
		kprintf("failed to read from PCF8575 on JAMR3 board\n");
	}

	/* Clear P10 (SEL_TVP_FPD) to enable the rearview camera */
	buf[1] &= ~PCF8575_P10;
	if (0 != omap_i2c_write(&i2c_dev, buf, 2)) {
		kprintf("failed to write to PCF8575 on JAMR3 board\n");
	}

	omap_i2c_deinit(&i2c_dev);
}

/***************************************************************************************************
 * Ethernet
 ***************************************************************************************************/
/* Pin Mux Registers and the Corresponding Values */
static const uint32_t pin_data_ether[] = {
	VIN2A_D12, (M3),
	VIN2A_D13, (M3),
	VIN2A_D14, (M3),
	VIN2A_D15, (M3),
	VIN2A_D16, (M3),
	VIN2A_D17, (M3),
	VIN2A_D18, (IEN | PDIS | M3),
	VIN2A_D19, (IEN | PDIS | M3),
	VIN2A_D20, (IEN | PDIS | M3),
	VIN2A_D21, (IEN | PDIS | M3),
	VIN2A_D22, (IEN | PDIS | M3),
	VIN2A_D23, (IEN | PDIS | M3),
};

/* This code is required to setup the second ethernet interface */
static void init_ethernet()
{
	int		i, size;

	// configure PINMUX
	size = sizeof(pin_data_ether) / sizeof(uint32_t);
	for (i = 0; i < size; i += 2)
		out32(DRA74X_CONTROL_PADCONF_CORE_BASE + pin_data_ether[i], pin_data_ether[i + 1]);
}

/***************************************************************************************************
 * LCD configuration
 * 1 Old code enable "all call", SD does not	isend -n /dev/i2c0 -a 0x40 0x00 0x00
 * 2 STBYB=1, LCD_AVDD_EN=1, LCD_ENBKL=0		isend -n /dev/i2c0 -a 0x40 0x0c 0x21
 * 3 SHLR=1, MODE3=1, DITH=0, UPDN=0			isend -n /dev/i2c0 -a 0x40 0x0d 0x05
 * 4 Brightness = 75% (192/256)					isend -n /dev/i2c0 -a 0x40 0x04 0xC0
 * 5 Config second LED expander					isend -n /dev/i2c0 -a 0x41 0x00 0x00
 * 6 Default test points to "high"				isend -n /dev/i2c0 -a 0x41 0x0c 0x00
 * 7 Default test points to "high", drive captouch reset "low"	isend -n /dev/i2c0 -a 0x41 0x0d 0x01
 * 8 Default test points to "high", drive captouch reset "high" isend -n /dev/i2c0 -a 0x41 0x0d 0x00
 ***************************************************************************************************/
 static int configure_tlc59108(omap_i2c_dev_t *i2c_dev, int slave)
 {
	int idx;
	uint8_t buf = 0, count = 0;
	uint8_t i2c_data[][2] = {{0x00, 0x00},
							{0x0c, 0x21},
							{0x0d, 0x05},
							{0x04, 0xc0}};

	i2c_dev->slave = slave;

	/*
	 * Polling TLC59108 until it's ready
	 * Disable kprintf() from i2c driver while polling
	 */
	omap_i2c_set_debug(0);
	for (;;) {
		if (!omap_i2c_read(i2c_dev, SUBADR1_ADDR, 1, &buf, 1) && (buf == SUBADR1_DEFAULT)) {
			break;
		}

		if ( count++ > 10) {
			kprintf("%s: TLC59108 is not accessible!\n", __func__);
			omap_i2c_set_debug(1);
			return -1;
		}
	}
	omap_i2c_set_debug(1);

	/* Now TLC59108 is accessible, configure it */
	for (idx = 0; idx < sizeof(i2c_data) / 2; idx++) {
		if (0 != omap_i2c_write(i2c_dev, &i2c_data[idx][0], 2)) {
			kprintf("%s: Failed to init TLC59108. I2C slave_addr = 0%x, data[0] = %x, data[1] = %x\n",
					__func__, slave, i2c_data[idx][0], i2c_data[idx][1]);
			return -1;
		}
	}

	return 0;
}

/*
This function is used to initialise FPDLink-III serialiser chip
*/
static void init_ub925q()
{
	uint8_t buf[2];

	omap_i2c_dev_t i2c_dev = {
		.base = DRA74X_I2C2_BASE,
		.clock = I2C_SPEED,
		.slave = 0x1B
	};

	omap_i2c_init(&i2c_dev);

	/* Set ub925q to 18 bit mode */
	buf[0] = 18;
	buf[1] = 0x04;
	if (0 != omap_i2c_write(&i2c_dev, buf, 2)) {
		kprintf("failed to write to ub925q\n");
	}

	buf[0] = 4;
	buf[1] = 0x8A;
	if (0 != omap_i2c_write(&i2c_dev, buf, 2)) {
		kprintf("failed to write to ub925q\n");
	}

	omap_i2c_deinit(&i2c_dev);
}


static void init_7393()
{
	uint8_t buf[2];

	omap_i2c_dev_t i2c_dev = {
		.base = DRA74X_I2C3_BASE,
		.clock = I2C_SPEED,
		.slave = 0x57 //ALSB=1
	};

	kprintf("######## init_7393 ########\n");

	omap_i2c_init(&i2c_dev);

	memset(buf, NULL, sizeof (buf));
	if (0 != omap_i2c_read(&i2c_dev, 0x00, 1, buf, 2))
		kprintf("failed to read from 7393\n");
	else
		kprintf("read from 7393: [0x00]: %02x\n", buf);

	omap_i2c_deinit(&i2c_dev);
}

static void init_7611()
{
	uint8_t buf[2];

	omap_i2c_dev_t i2c_dev = {
		.base = DRA74X_I2C3_BASE,
		.clock = I2C_SPEED,
		.slave = 0x98
	};

	kprintf("######## init_7611 ########\n");
	
	omap_i2c_init(&i2c_dev);

	memset(buf, NULL, sizeof (buf));
	if (0 != omap_i2c_read(&i2c_dev, 0xEA, 1, buf, 2)) 
		kprintf("failed to read from 7611\n");
	else
		kprintf("read from 7611: [0xEA]: %02x\n", buf);

	omap_i2c_deinit(&i2c_dev);
}

static void init_sis9252()
{
	uint8_t buf[2];

	omap_i2c_dev_t i2c_dev = {
		.base = DRA74X_I2C1_BASE,
		.clock = I2C_SPEED,
		.slave = 0x08 
	};

	kprintf("######## init_sis9252 ########\n");

	omap_i2c_init(&i2c_dev);

	memset(buf, NULL, sizeof (buf));
	if (0 != omap_i2c_read(&i2c_dev, 0x00, 1, buf, 2))
		kprintf("failed to read from sis9252\n");
	else
		kprintf("read from sis9252: [0x00]: %02x\n", buf);

	omap_i2c_deinit(&i2c_dev);
}


static void init_lcd()
{
	uint8_t buf[2];

	omap_i2c_dev_t i2c_dev = {
		.base = DRA74X_I2C1_BASE,
		.clock = I2C_SPEED,
		.slave = IO_EXPANDER_1_I2C_ADDR
	};

	omap_i2c_init(&i2c_dev);

	memset(buf, NULL, sizeof (buf));
	if (0 != omap_i2c_read(&i2c_dev, 0, 1, buf, 2)) {
		kprintf("failed to read from PCF8575\n");
		omap_i2c_deinit(&i2c_dev);
		return;
	}

	/* Clear P15 (CON_LCD_PWR_DN) to power up LCD */
	buf[1] &= ~PCF8575_P15;
	if (0 != omap_i2c_write(&i2c_dev, buf, 2)) {
		kprintf("failed to write to PCF8575\n");
		omap_i2c_deinit(&i2c_dev);
		return;
	}

	configure_tlc59108(&i2c_dev, TLC59108IPW_1_I2C_ADDR);

	/*
	 * There are 2 TLC59108 devices on the Spectrum 7" display which is used for Rev E or earlier boards,
	 * while there is only 1 TLC59108 on the 10" display.
	 * Technically Rev F (or newer hardware) can mount the 7" display, but this is the case in reality
	 */
	if (pmic_revision == TPS659038_ES1_0) {
		configure_tlc59108(&i2c_dev, TLC59108IPW_2_I2C_ADDR);
	}

	omap_i2c_deinit(&i2c_dev);
}

/***************************************************************************************************
 * Touch screen
 ***************************************************************************************************/
static void init_ts()
{
#define MXT224_IRQ	(1 << 2)	/* Touch screen interrupt GPIO1[2]*/
	uintptr_t base = DRA74X_GPIO1_BASE;

	/* Falling edge trigger */
	out32(base + GPIO_OE, in32(base + GPIO_OE) | MXT224_IRQ);
	out32(base + GPIO_LEVELDETECT0, in32(base + GPIO_LEVELDETECT0) & ~MXT224_IRQ);
	out32(base + GPIO_LEVELDETECT1, in32(base + GPIO_LEVELDETECT1) & ~MXT224_IRQ);
	out32(base + GPIO_RISINGDETECT, in32(base + GPIO_RISINGDETECT) & ~MXT224_IRQ);
	out32(base + GPIO_FALLINGDETECT, in32(base + GPIO_FALLINGDETECT) | MXT224_IRQ);
}

/***************************************************************************************************
 * Wifi
 ***************************************************************************************************/
/* Pin Mux Registers and the Corresponding Values */
static unsigned pin_data_dp[] = {
	UART1_CTSN, (IEN | PTU | PDIS | M3),
	UART1_RTSN, (IEN | PTU | PDIS | M3),
	UART2_RXD, (IEN | PTU | PDIS | M3),
	UART2_TXD, (IEN | PTU | PDIS | M3),
	UART2_CTSN, (IEN | PTU | PDIS | M3),
	UART2_RTSN, (IEN | PTU | PDIS | M3),
	MCASP1_AXR5, (IEN | PTU | WKEN | M14),
	MCASP1_AXR6, (PTU | M14),
	MMC3_DAT0, (IEN | PTU | PDIS | M2),		/* uart5_rxd for MCU*/
	MMC3_DAT1, (IEN | PTU | PDIS | M2),		/* uart5_txd for MCU */
	//MCASP1_FSR, (PTU | M14),					/* 7611/7393 reset - GPIO5[1] */
	//MCASP1_AXR0, (IEN | PTU | PDIS | M3),		/* uart6_rxd for GPS*/
	//MCASP1_AXR1, (IEN | PTU | PDIS | M3),		/* uart6_txd for GPS*/
	//MCASP4_AXR0, (IEN | PTU | PDIS | M4),		/* uart4_rxd for GPS*/
	//MCASP4_AXR1, (IEN | PTU | PDIS | M4),		/* uart4_txd for GPS*/
	//MCASP4_ACLKX, (IEN | PTU | PEN | M0),
	//MCASP4_FSX, (IEN | PTU | PEN | M0),
	//MCASP4_AXR0, (IEN | PDIS | M0),
	//MCASP4_AXR1, (IEN | PDIS | M0),
	//MCASP3_ACLKX, (IEN | PDIS | M0),
	//MCASP3_FSX, (FSC | IEN | PDIS | M0),
	//MCASP3_AXR0, (FSC | IEN | PDIS | M0),
	//MCASP3_AXR1, (FSC | IEN | PDIS | M0),
	MCASP5_ACLKX, (IEN | PDIS | M0),
	MCASP5_FSX, (FSC | IEN | PDIS | M0),
	MCASP5_AXR0, (FSC | IEN | PDIS | M0),
	MCASP5_AXR1, (FSC | IEN | PDIS | M0),
};

/* Initialize the Pin Muxing */
static void init_mux(void)
{
	int i, size;

	size = sizeof(pin_data_dp) / sizeof(unsigned);
	for (i = 0; i < size; i += 2)
		out32(DRA74X_CONTROL_PADCONF_CORE_BASE + pin_data_dp[i], pin_data_dp[i + 1]);
}

/* Init WiFi interface (COMQ) */
static void init_wifi (void)
{
	uintptr_t prcm_base = DRA74X_PRCM_BASE;

	//init_mux();

	/* Set 48Mhz clock */
	out32(prcm_base + CM_L4PER_MMC4_CLKCTRL, (in32(prcm_base + CM_L4PER_MMC4_CLKCTRL) & ~0x01000000));

	/* Configure WiFi chip reset signal as output GPIO */
	sr32(DRA74X_GPIO5_BASE + GPIO_OE, 8, 1, 0);

	/* Reset WiFi chip */
	sr32(DRA74X_GPIO5_BASE + GPIO_DATAOUT, 8, 1, 0);
	usec_delay(WIFI_RESET_DELAY);
	sr32(DRA74X_GPIO5_BASE + GPIO_DATAOUT, 8, 1, 1);
}

/* hardware reset ADI 7611/7393 */
static void hw_reset_adi(void)
{
	kprintf("######## hw_reset_adi start ########\n");

	usec_delay(1000000);
	
	/* Configure ADI chipset reset signal as output GPIO */
	sr32(DRA74X_GPIO5_BASE + GPIO_OE, 5, 1, 0);
	
	/* Reset adi chip */
	sr32(DRA74X_GPIO5_BASE + GPIO_DATAOUT, 5, 1, 1);
	sr32(DRA74X_GPIO5_BASE + GPIO_DATAOUT, 5, 1, 0);
	usec_delay(10000);
	sr32(DRA74X_GPIO5_BASE + GPIO_DATAOUT, 5, 1, 1);
	
	kprintf("######## hw_reset_adi done ########\n");
} 

/***************************************************************************************************
 * MMCSD
 ***************************************************************************************************/
#define J6EVM_GPIO_SDCD (1 << 27)

void init_mmcsd(void)
{
	uintptr_t	base = DRA74X_PRCM_BASE;

	/* indicate stable voltages */
	out32(DRA74X_CTRL_CORE_CONTROL_PBIAS, in32(DRA74X_CTRL_CORE_CONTROL_PBIAS) |
					CTRL_CORE_CONTROL_PBIAS_SDCARD_BIAS_3V |
					CTRL_CORE_CONTROL_PBIAS_SDCARD_IO_PWRDNZ |
					CTRL_CORE_CONTROL_PBIAS_SDCARD_BIAS_PWRDNZ);

	/* PAD for eMMC, SD card, already configured by boot loader */
	base = DRA74X_GPIO6_BASE;
	out32(base + GPIO_OE, in32(base + GPIO_OE) | J6EVM_GPIO_SDCD);
	out32(base + GPIO_LEVELDETECT0, in32(base + GPIO_LEVELDETECT0) & ~J6EVM_GPIO_SDCD);
	out32(base + GPIO_LEVELDETECT1, in32(base + GPIO_LEVELDETECT1) & ~J6EVM_GPIO_SDCD);
	out32(base + GPIO_RISINGDETECT, in32(base + GPIO_RISINGDETECT) | J6EVM_GPIO_SDCD);
	out32(base + GPIO_FALLINGDETECT, in32(base + GPIO_FALLINGDETECT) | J6EVM_GPIO_SDCD);
	out32(base + GPIO_DEBOUNCEENABLE, in32(base + GPIO_DEBOUNCEENABLE) | J6EVM_GPIO_SDCD);
	out32(base + GPIO_DEBOUNCINGTIME, 31);
}
/***************************************************************************************************
 * Audio
 ***************************************************************************************************/
static void init_audio()
{
	if (abe_dpll_196m) {
		/*
		 * In this path, McASP3 can work at 48Khz sample rate, also McASP7 can work in clock master mode
		 * No proper clock to satisfy the 44.1Khz sample rate required by radio
		 * AUX_CLK of all McASP instances comes from PER_ABE_X1_GFCLK (DPLL_ABE_M2) 196.608MHz / 16 = 12.288Mhz
		 * AHCLKX is derived from AUX_CLK, so that ahxclk should be set to output direction
		 */
		 return;
	}

	/*
	 * IPL configures ABE DPLL at 361Mhz, which ultimately feeds all McASP devices (exclude McASP7) and radio
	 * In order to have McASP7 and radio co-existing without stomping on each other's clock, McASP3 gets its AHCLKX clock from ATL2
	 * Please note that in this configuration, the sample rate of both radio and McASP3 should be 44.1Khz
	 */

	/* ABE M2 CLK (PER_ABE_X1_CLK) is MCLK: 180.6336MHz */
	sr32(DRA74X_PRCM_BASE + CM_DIV_M2_DPLL_ABE, 0, 5, 0x1);

	/* Configure ATL clocks, select PER_ABE_X1_CLK as CLKSEL_SOURCE2 */
	init_atl_clock(-1, 1);

	/*
	 * There is a single ATLPCLK mux for all ATL instances and
	 * is controlled by PCLKMUX0. The rest of PCLKMUXs don't have
	 * any effect of clock selection
	 */
	out32(ATL_BASE + ATL_PCLKMUX(ATL_INST(0)), ATL_PCLKMUX_ATLPCLK);

	/* Disable ATL while reparenting and changing ATLPCLK input */
	out32(ATL_BASE + ATL_SWEN(ATL_INST(2)), ATL_SWEN_DISABLE);

	/* ATL divider (ATL_INTERNAL_DIVIDER): ATLPCLK-to-ATCLK ratio - 1 */
	/* 180.6336MHz / 16 = 11.2896MHz */
	out32(ATL_BASE + ATL_ATLCR(ATL_INST(2)), 0xF);

	/* Baseband word select - McASP2 FSX - don't care currently */
	out32(ATL_BASE + ATL_BWSMUX(ATL_INST(2)), 0x3);

	/* Audio word select - McASP3 FSX */
	out32(ATL_BASE + ATL_AWSMUX(ATL_INST(2)), 0x4);

	/* PPMR to 0 */
	out32(ATL_BASE + ATL_PPMR(ATL_INST(2)), 0x0);

	/* Enable ATL */
	out32(ATL_BASE + ATL_SWEN(ATL_INST(2)), ATL_SWEN_ENABLE);

	/* McASP3 high clock from ATL_CLK2*/
	sr32(DRA74X_PRCM_BASE + CM_L4PER2_MCASP3_CLKCTRL, CM_MCASP_CLKSEL_AHCLKX_OFF, 4, CM_MCASP_CLKSEL_AHCLKX_ATL2);
}

static void init_audio_6638_mcasp5()
{

	/* ABE M2 CLK (PER_ABE_X1_CLK) is MCLK: 180.6336MHz */
	sr32(DRA74X_PRCM_BASE + CM_DIV_M2_DPLL_ABE, 0, 5, 0x1);

	/* Configure ATL clocks, select PER_ABE_X1_CLK as CLKSEL_SOURCE2 */
	init_atl_clock(-1, 1);

	/*
	 * There is a single ATLPCLK mux for all ATL instances and
	 * is controlled by PCLKMUX0. The rest of PCLKMUXs don't have
	 * any effect of clock selection
	 */
	out32(ATL_BASE + ATL_PCLKMUX(ATL_INST(0)), ATL_PCLKMUX_ATLPCLK);

	/* Disable ATL while reparenting and changing ATLPCLK input */
	out32(ATL_BASE + ATL_SWEN(ATL_INST(2)), ATL_SWEN_DISABLE);

	/* ATL divider (ATL_INTERNAL_DIVIDER): ATLPCLK-to-ATCLK ratio - 1 */
	/* 180.6336MHz / 16 = 11.2896MHz */
	out32(ATL_BASE + ATL_ATLCR(ATL_INST(2)), 0xF);

	/* Audio word select - McASP5 FSX */
	out32(ATL_BASE + ATL_AWSMUX(ATL_INST(2)), 0x6);

	/* PPMR to 0 */
	out32(ATL_BASE + ATL_PPMR(ATL_INST(2)), 0x0);

	/* Enable ATL */
	out32(ATL_BASE + ATL_SWEN(ATL_INST(2)), ATL_SWEN_ENABLE);

	/* McASP5 high clock from ATL_CLK2*/
	sr32(DRA74X_PRCM_BASE + CM_L4PER2_MCASP5_CLKCTRL, CM_MCASP_CLKSEL_AHCLKX_OFF, 4, CM_MCASP_CLKSEL_AHCLKX_ATL2);
}


static void init_usb_extra()
{
	/*
	 * Drive USBx_DRVVBUS high to keep vbus high
	 * this is needed by iap2 driver
	 * Another alternative would be change the USB host driver to dynamically drive vbus level
	 */

	/* USB1_DRVVBUS */
	sr32(DRA74X_GPIO6_BASE + GPIO_OE, 12, 1, 0);
	sr32(DRA74X_GPIO6_BASE + GPIO_DATAOUT, 12, 1, 1);

	/* USB2_DRVVBUS */
	sr32(DRA74X_GPIO6_BASE + GPIO_OE, 13, 1, 0);
	sr32(DRA74X_GPIO6_BASE + GPIO_DATAOUT, 13, 1, 1);
}

void set_io_delay()
{
	const dra7xx_io_delay_t io_dly[] = {
		{CFG_RGMII0_TXCTL, RGMII0_TXCTL_DLY_VAL},
		{CFG_RGMII0_TXD0, RGMII0_TXD0_DLY_VAL},
		{CFG_RGMII0_TXD1, RGMII0_TXD1_DLY_VAL},
		{CFG_RGMII0_TXD2, RGMII0_TXD2_DLY_VAL},
		{CFG_RGMII0_TXD3, RGMII0_TXD3_DLY_VAL},
		{CFG_VIN2A_D13, VIN2A_D13_DLY_VAL},
		{CFG_VIN2A_D17, VIN2A_D17_DLY_VAL},
		{CFG_VIN2A_D16, VIN2A_D16_DLY_VAL},
		{CFG_VIN2A_D15, VIN2A_D15_DLY_VAL},
		{CFG_VIN2A_D14, VIN2A_D14_DLY_VAL},
		{0}
	};

	/* Adjust IO delay for RGMII tx path */
	dra7xx_adj_io_delay (io_dly);
}

uint32_t get_sys_clk2_freq()
{
	return SYS_CLK2_FREQ;
}

/*
 This function is used to initialized any HW components that are required to
 * bring the kernel up and running. In normal mode or when using
 * power-management only the strict minimum should be initialize and other
 * components should be hold in their lowest power mode.
 */
void hw_init()
{
	kprintf ("hw_init\n");
	if (abe_dpll_196m) {
		setup_abe_dpll(FUNC_32K_CLK_INPUT);
	}

	/* SYS_CLK_32K_CLK as source */
	init_gptimer_clock(1, 1 << CLKCTRL_CLKSEL_OFFSET);

	/*
	 * We don't enable all timers to save power
	 * IPU clock is disabled, so TIMER5 - TIMER8 can not be enabled
	 */
	init_gptimer_clock(2, 0);
	init_gptimer_clock(3, 0);
	init_gptimer_clock(4, 0);

	init_mmcsd();
	init_edma();
	init_usb1_otg();
	init_usb2_otg();
	init_usb3_phy();
	init_usb_extra();

	/* Need to put init_ocp2scp3() before init_sata() and init_pci() */
	init_ocp2scp3();
	//init_sata();
	set_crossbar_reg();
	read_pmic_revision();
	init_lcd();
	init_gpmc();
	init_audio();
	init_com8();
	set_io_delay();
	init_ethernet();
	init_ts();

	kprintf("gpmc_vout3:%d\n", gpmc_vout3);
	if (gpmc_vout3) {
		kprintf ("init_ub925q\n");
		init_ub925q();
	}

/*
	if (jamr3) {
		init_jamr3();
	}
*/

	init_mux();

	if (wifi) {
		init_wifi ();
	}

	kprintf ("hw_init done\n");

	kprintf ("init_audio_6638_mcasp5\n");
	init_audio_6638_mcasp5();

	//changan demo
	//usec_delay(1000000);
	//hw_reset_adi();   //now reset by MCU
	//init_7611();
	//init_7393();
	//init_sis9252();
	
}

/*
 * This function is used to initialize and pieces of HW startup could have a
 * dependency on. If startup do not needs a piece of HW to be initialized in
 * order to perform its normal task then the initialization code should be in
 * the hw_init function.
 */
void init_clocks(void)
{
	init_basic_clocks();

	/* Init IPU and timers needed by IPC */
	init_ipu();
	init_ipu1();
	init_ipu2();
	init_gptimer_clock(5, 0);
	init_gptimer_clock(6, 0);
	init_gptimer_clock(7, 0);
	init_gptimer_clock(8, 0);
	init_gptimer_clock(11, 0);

	/* Init timers needed by Radio */
	init_gptimer_clock(9, 0);
	init_gptimer_clock(10, 0);

	init_i2c_clock(2);
	init_i2c_clock(3);
	init_i2c_clock(4);

	/* UART1 is supposed to be enabled in first boot loader */
	/* UART3 for Bluetooth */
	init_uart_clock(3);
	init_uart_clock(5);/* UART5 for mcu for changan */

	init_mcspi_clock(1);

	init_qspi_clock();

	/*
	 * MCASP clocks
	 * Only enable devices that are needed
	 * By default on all devices:
	 *		select ABE_24M_GFCLK as the CLKSEL_AHCLKX source
	 *		select PER_ABE_X1_GFCLK as the CLKSEL_AUX_CLK source
	 */
	init_mcasp_clock(3);
	init_mcasp_clock(6);
	init_mcasp_clock(7);
	//init_mcasp_clock(4);//for changan
	init_mcasp_clock(5);//for changan

	/*
	 * MMC1: micso SD
	 * MMC2: eMMC
	 * MMC3: SD on the expander board
	 * MMC4: MMC interface for WiFi
	 * 192MHz clock source, divided by 1
	 */
	init_mmc_clock(1, (1 << CLKCTRL_CLKSEL_OFFSET));
	init_mmc_clock(2, (1 << CLKCTRL_CLKSEL_OFFSET));
	//init_mmc_clock(3, (1 << CLKCTRL_CLKSEL_OFFSET));
	init_mmc_clock(4, (1 << CLKCTRL_CLKSEL_OFFSET));

	/* GPIO clocks with debouncing clock enabled (except GPIO1) */
	init_gpio_clock(1, 0);
	init_gpio_clock(2, CLKCTRL_OPTCLKEN_ENABLED);
	init_gpio_clock(5, CLKCTRL_OPTCLKEN_ENABLED);
	init_gpio_clock(6, CLKCTRL_OPTCLKEN_ENABLED);

	init_gpmc_clock();

	/* Camera clocks */
	init_vin_clocks(VIP_DEVICE_1 | VIP_DEVICE_2 | VIP_DEVICE_3);

	/* Video Processing Engine (deinterlacing etc.) */
	init_vpe_clocks();

	init_graphics_clocks();

	/* We are expecting 532Mhz on M2 GPU_DPLL */
	if (calc_dpll_freq(DPLL_GPU) != 2128000000)
		setup_gpu_dpll_2128mhz();
	init_gpu_clock(CM_GPU_GPU_CLKCTRL_FCLK_GPU);

	init_dcan_clock(1);
	init_dcan_clock(2);

	init_gmac_clock();
}


#if defined(__QNXNTO__) && defined(__USESRCVERSION)
#include <sys/srcversion.h>
__SRCVERSION("$URL: http://svn/product/branches/6.6.0/trunk/hardware/startup/boards/dra74x/vayu-evm/hw_init.c $ $Rev: 768097 $")
#endif
