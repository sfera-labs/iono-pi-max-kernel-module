/*
 * ionopimax
 *
 *     Copyright (C) 2020-2023 Sfera Labs S.r.l.
 *
 *     For information, visit https://www.sferalabs.cc
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * LICENSE.txt file for more details.
 *
 */

#include "commons/commons.h"
#include "gpio/gpio.h"
#include "wiegand/wiegand.h"
#include "atecc/atecc.h"
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/version.h>

#define I2C_ADDR_LOCAL 0x35

#define LOG_TAG "ionopimax: "

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sfera Labs - http://sferalabs.cc");
MODULE_DESCRIPTION("Iono Pi Max driver module");
MODULE_VERSION("1.18");

struct DeviceAttrRegSpecs {
	uint16_t reg;
	uint8_t len;
	bool maskedReg;
	uint32_t mask;
	uint8_t shift;
	bool sign;
	const char *vals;
};

struct DeviceAttrBean {
	struct device_attribute devAttr;
	struct DeviceAttrRegSpecs regSpecs;
	struct DeviceAttrRegSpecs regSpecsStore;
	struct GpioBean *gpio;
};

struct DeviceBean {
	char *name;
	struct device *pDevice;
	struct DeviceAttrBean *devAttrBeans;
};

static struct class *pDeviceClass;

static ssize_t devAttrI2c_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static ssize_t devAttrI2c_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t devAttrAxMode_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static ssize_t devAttrAxMode_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t devAttrUpsBatteryV_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static ssize_t devAttrUpsBatteryV_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t devAttrSdEnabled_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static ssize_t devAttrSdEnabled_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t devAttrMcuFwVersion_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t devAttrMcuConfig_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static ssize_t mcuI2cRead_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t mcuI2cRead_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static ssize_t mcuI2cWrite_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static ssize_t devAttrSerialRs232Rs485Inv_show(struct device *dev,
		struct device_attribute *attr, char *buf);

static ssize_t devAttrSerialRs232Rs485Inv_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);

static const char VALS_DIGITAL_OUTS_STATUS[] = { 4, '0', '1', 'F', 'S' };
static const char VALS_WATCHDOG_ENABLE_MODE[] = { 2, 'D', 'A' };
static const char VALS_POWER_DOWN_ENABLE_MODE[] = { 2, 'I', 'A' };
static const char VALS_POWER_UP_MODE[] = { 2, 'M', 'A' };
static const char VALS_SD_SDX_ROUTING[] = { 2, 'A', 'B' };
static const char VALS_ANALOG_OUTS_MODE[] = { 2, 'I', 'V' };

static uint8_t fwVerMajor;
static uint8_t fwVerMinor;

enum digInEnum {
	DI1 = 0,
	DI2,
	DI3,
	DI4,
	DI_SIZE,
};

enum dtEnum {
	DT1 = 0,
	DT2,
	DT3,
	DT4,
	DT_SIZE,
};

static struct DebouncedGpioBean gpioDI[] = {
	[DI1] = {
		.gpio = {
			.name = "ionopimax_di1",
			.flags = GPIOD_IN,
		},
	},
	[DI2] = {
		.gpio = {
			.name = "ionopimax_di2",
			.flags = GPIOD_IN,
		},
	},
	[DI3] = {
		.gpio = {
			.name = "ionopimax_di3",
			.flags = GPIOD_IN,
		},
	},
	[DI4] = {
		.gpio = {
			.name = "ionopimax_di4",
			.flags = GPIOD_IN,
		},
	},
};

static struct GpioBean gpioDT[] = {
	[DT1] = {
		.name = "ionopimax_dt1",
	},
	[DT2] = {
		.name = "ionopimax_dt2",
	},
	[DT3] = {
		.name = "ionopimax_dt3",
	},
	[DT4] = {
		.name = "ionopimax_dt4",
	},
};

static struct GpioBean gpioBuzzer = {
	.name = "ionopimax_buzzer",
	.flags = GPIOD_OUT_LOW,
};

static struct DebouncedGpioBean gpioButton = {
	.gpio = {
		.name = "ionopimax_button",
		.flags = GPIOD_IN,
		.invert = true,
	},
};

static struct GpioBean gpioWdEn = {
	.name = "ionopimax_wd_en",
	.flags = GPIOD_OUT_LOW,
};

static struct GpioBean gpioWdHeartbeat = {
	.name = "ionopimax_wd_hb",
	.flags = GPIOD_OUT_LOW,
};

static struct DebouncedGpioBean gpioWdExpired = {
	.gpio = {
		.name = "ionopimax_wd_ex",
		.flags = GPIOD_IN,
	},
};

static struct GpioBean gpioPwrDnwEn = {
	.name = "ionopimax_pwr_dwn",
	.flags = GPIOD_OUT_LOW,
};

static struct GpioBean gpioUsb1En = {
	.name = "ionopimax_usb1_en",
	.flags = GPIOD_OUT_LOW,
	.invert = true,
};

static struct GpioBean gpioUsb1Err = {
	.name = "ionopimax_usb1_err",
	.flags = GPIOD_IN,
	.invert = true,
};

static struct GpioBean gpioUsb2En = {
	.name = "ionopimax_usb2_en",
	.flags = GPIOD_OUT_LOW,
	.invert = true,
};

static struct GpioBean gpioUsb2Err = {
	.name = "ionopimax_usb2_err",
	.flags = GPIOD_IN,
	.invert = true,
};

static struct GpioBean gpioSwEn = {
	.name = "ionopimax_sw_en",
	.flags = GPIOD_OUT_LOW,
};

static struct GpioBean gpioSwReset = {
	.name = "ionopimax_sw_rst",
	.flags = GPIOD_OUT_LOW,
};

static struct WiegandBean w1 = {
	.d0 = {
		.gpio = &gpioDT[DT1],
	},
	.d1 = {
		.gpio = &gpioDT[DT2],
	},
};

static struct WiegandBean w2 = {
	.d0 = {
		.gpio = &gpioDT[DT3],
	},
	.d1 = {
		.gpio = &gpioDT[DT4],
	},
};

static struct DeviceAttrBean devAttrBeansBuzzer[] = {
	{
		.devAttr = {
			.attr = {
				.name = "status",
				.mode = 0660,
			},
			.show = devAttrGpio_show,
			.store = devAttrGpio_store,
		},
		.gpio = &gpioBuzzer,
	},

	{
		.devAttr = {
			.attr = {
				.name = "beep",
				.mode = 0220,
			},
			.show = NULL,
			.store = devAttrGpioBlink_store,
		},
		.gpio = &gpioBuzzer,
	},

	{ }
};

static struct DeviceAttrBean devAttrBeansButton[] = {
	{
		.devAttr = {
			.attr = {
				.name = "status",
				.mode = 0440,
			},
			.show = devAttrGpio_show,
			.store = NULL,
		},
		.gpio = &gpioButton.gpio,
	},

	{
		.devAttr = {
			.attr = {
				.name = "status_deb",
				.mode = 0440,
			},
			.show = devAttrGpioDeb_show,
			.store = NULL,
		},
		.gpio = &gpioButton.gpio,
	},

	{
		.devAttr = {
			.attr = {
				.name = "status_deb_ms",
				.mode = 0660,
			},
			.show = devAttrGpioDebMsOn_show,
			.store = devAttrGpioDebMsOn_store,
		},
		.gpio = &gpioButton.gpio,
	},

	{
		.devAttr = {
			.attr = {
				.name = "status_deb_cnt",
				.mode = 0440,
			},
			.show = devAttrGpioDebOnCnt_show,
			.store = NULL,
		},
		.gpio = &gpioButton.gpio,
	},

	{ }
};

static struct DeviceAttrBean devAttrBeansLed[] = {
	{
		.devAttr = {
			.attr = {
				.name = "l1_r",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 105,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "l1_g",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 106,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "l1_b",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 107,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "l1_br",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 108,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "l2_r",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 110,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "l2_g",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 111,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "l2_b",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 112,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "l2_br",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 113,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "l3_r",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 115,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "l3_g",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 116,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "l3_b",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 117,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "l3_br",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 118,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "l4_r",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 120,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "l4_g",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 121,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "l4_b",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 122,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "l4_br",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 123,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "l5_r",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 125,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "l5_g",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 126,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "l5_b",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 127,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "l5_br",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 128,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{ }
};

static struct DeviceAttrBean devAttrBeansAnalogIn[] = {
	{
		.devAttr = {
			.attr = {
				.name = "enabled",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 69,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "hsf",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 69,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 1,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "av1_mode",
				.mode = 0660,
			},
			.show = devAttrAxMode_show,
			.store = devAttrAxMode_store,
		},
		.regSpecs = {
			.reg = 69,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 4,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "av2_mode",
				.mode = 0660,
			},
			.show = devAttrAxMode_show,
			.store = devAttrAxMode_store,
		},
		.regSpecs = {
			.reg = 69,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 5,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "av3_mode",
				.mode = 0660,
			},
			.show = devAttrAxMode_show,
			.store = devAttrAxMode_store,
		},
		.regSpecs = {
			.reg = 69,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 6,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "av4_mode",
				.mode = 0660,
			},
			.show = devAttrAxMode_show,
			.store = devAttrAxMode_store,
		},
		.regSpecs = {
			.reg = 69,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 7,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "ai1_mode",
				.mode = 0660,
			},
			.show = devAttrAxMode_show,
			.store = devAttrAxMode_store,
		},
		.regSpecs = {
			.reg = 69,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 8,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "ai2_mode",
				.mode = 0660,
			},
			.show = devAttrAxMode_show,
			.store = devAttrAxMode_store,
		},
		.regSpecs = {
			.reg = 69,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 9,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "ai3_mode",
				.mode = 0660,
			},
			.show = devAttrAxMode_show,
			.store = devAttrAxMode_store,
		},
		.regSpecs = {
			.reg = 69,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 10,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "ai4_mode",
				.mode = 0660,
			},
			.show = devAttrAxMode_show,
			.store = devAttrAxMode_store,
		},
		.regSpecs = {
			.reg = 69,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 11,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "at1_mode",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 69,
			.len = 2,
			.maskedReg = false,
			.mask = 0b11,
			.shift = 12,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "at2_mode",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 69,
			.len = 2,
			.maskedReg = false,
			.mask = 0b11,
			.shift = 14,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "av1",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 71,
			.len = 3,
			.maskedReg = false,
			.mask = 0xffffff,
			.shift = 0,
			.sign = true,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "av2",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 72,
			.len = 3,
			.maskedReg = false,
			.mask = 0xffffff,
			.shift = 0,
			.sign = true,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "av3",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 73,
			.len = 3,
			.maskedReg = false,
			.mask = 0xffffff,
			.shift = 0,
			.sign = true,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "av4",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 74,
			.len = 3,
			.maskedReg = false,
			.mask = 0xffffff,
			.shift = 0,
			.sign = true,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "ai1",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 75,
			.len = 3,
			.maskedReg = false,
			.mask = 0xffffff,
			.shift = 0,
			.sign = true,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "ai2",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 76,
			.len = 3,
			.maskedReg = false,
			.mask = 0xffffff,
			.shift = 0,
			.sign = true,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "ai3",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 77,
			.len = 3,
			.maskedReg = false,
			.mask = 0xffffff,
			.shift = 0,
			.sign = true,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "ai4",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 78,
			.len = 3,
			.maskedReg = false,
			.mask = 0xffffff,
			.shift = 0,
			.sign = true,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "at1",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 79,
			.len = 3,
			.maskedReg = false,
			.mask = 0xffffff,
			.shift = 0,
			.sign = true,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "at2",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 80,
			.len = 3,
			.maskedReg = false,
			.mask = 0xffffff,
			.shift = 0,
			.sign = true,
			.vals = NULL,
		},
	},

	{ }
};

static struct DeviceAttrBean devAttrBeansDigitalIO[] = {
	{
		.devAttr = {
			.attr = {
				.name = "dt1_mode",
				.mode = 0660,
			},
			.show = devAttrGpioMode_show,
			.store = devAttrGpioMode_store,
		},
		.gpio = &gpioDT[DT1],
	},

	{
		.devAttr = {
			.attr = {
				.name = "dt2_mode",
				.mode = 0660,
			},
			.show = devAttrGpioMode_show,
			.store = devAttrGpioMode_store,
		},
		.gpio = &gpioDT[DT2],
	},

	{
		.devAttr = {
			.attr = {
				.name = "dt3_mode",
				.mode = 0660,
			},
			.show = devAttrGpioMode_show,
			.store = devAttrGpioMode_store,
		},
		.gpio = &gpioDT[DT3],
	},

	{
		.devAttr = {
			.attr = {
				.name = "dt4_mode",
				.mode = 0660,
			},
			.show = devAttrGpioMode_show,
			.store = devAttrGpioMode_store,
		},
		.gpio = &gpioDT[DT4],
	},

	{
		.devAttr = {
			.attr = {
				.name = "dt1",
				.mode = 0660,
			},
			.show = devAttrGpio_show,
			.store = devAttrGpio_store,
		},
		.gpio = &gpioDT[DT1],
	},

	{
		.devAttr = {
			.attr = {
				.name = "dt2",
				.mode = 0660,
			},
			.show = devAttrGpio_show,
			.store = devAttrGpio_store,
		},
		.gpio = &gpioDT[DT2],
	},

	{
		.devAttr = {
			.attr = {
				.name = "dt3",
				.mode = 0660,
			},
			.show = devAttrGpio_show,
			.store = devAttrGpio_store,
		},
		.gpio = &gpioDT[DT3],
	},

	{
		.devAttr = {
			.attr = {
				.name = "dt4",
				.mode = 0660,
			},
			.show = devAttrGpio_show,
			.store = devAttrGpio_store,
		},
		.gpio = &gpioDT[DT4],
	},

	{ }
};

static struct DeviceAttrBean devAttrBeansDigitalIn[] = {
	{
		.devAttr = {
			.attr = {
				.name = "di1",
				.mode = 0440,
			},
			.show = devAttrGpio_show,
			.store = NULL,
		},
		.gpio = &gpioDI[DI1].gpio,
	},

	{
		.devAttr = {
			.attr = {
				.name = "di2",
				.mode = 0440,
			},
			.show = devAttrGpio_show,
			.store = NULL,
		},
		.gpio = &gpioDI[DI2].gpio,
	},

	{
		.devAttr = {
			.attr = {
				.name = "di3",
				.mode = 0440,
			},
			.show = devAttrGpio_show,
			.store = NULL,
		},
		.gpio = &gpioDI[DI3].gpio,
	},

	{
		.devAttr = {
			.attr = {
				.name = "di4",
				.mode = 0440,
			},
			.show = devAttrGpio_show,
			.store = NULL,
		},
		.gpio = &gpioDI[DI4].gpio,
	},

	{
		.devAttr = {
			.attr = {
				.name = "di1_deb",
				.mode = 0440,
			},
			.show = devAttrGpioDeb_show,
			.store = NULL,
		},
		.gpio = &gpioDI[DI1].gpio,
	},

	{
		.devAttr = {
			.attr = {
				.name = "di2_deb",
				.mode = 0440,
			},
			.show = devAttrGpioDeb_show,
			.store = NULL,
		},
		.gpio = &gpioDI[DI2].gpio,
	},

	{
		.devAttr = {
			.attr = {
				.name = "di3_deb",
				.mode = 0440,
			},
			.show = devAttrGpioDeb_show,
			.store = NULL,
		},
		.gpio = &gpioDI[DI3].gpio,
	},

	{
		.devAttr = {
			.attr = {
				.name = "di4_deb",
				.mode = 0440,
			},
			.show = devAttrGpioDeb_show,
			.store = NULL,
		},
		.gpio = &gpioDI[DI4].gpio,
	},

	{
		.devAttr = {
			.attr = {
				.name = "di1_deb_on_ms",
				.mode = 0660,
			},
			.show = devAttrGpioDebMsOn_show,
			.store = devAttrGpioDebMsOn_store,
		},
		.gpio = &gpioDI[DI1].gpio,
	},

	{
		.devAttr = {
			.attr = {
				.name = "di1_deb_off_ms",
				.mode = 0660,
			},
			.show = devAttrGpioDebMsOff_show,
			.store = devAttrGpioDebMsOff_store,
		},
		.gpio = &gpioDI[DI1].gpio,
	},

	{
		.devAttr = {
			.attr = {
				.name = "di2_deb_on_ms",
				.mode = 0660,
			},
			.show = devAttrGpioDebMsOn_show,
			.store = devAttrGpioDebMsOn_store,
		},
		.gpio = &gpioDI[DI2].gpio,
	},

	{
		.devAttr = {
			.attr = {
				.name = "di2_deb_off_ms",
				.mode = 0660,
			},
			.show = devAttrGpioDebMsOff_show,
			.store = devAttrGpioDebMsOff_store,
		},
		.gpio = &gpioDI[DI2].gpio,
	},

	{
		.devAttr = {
			.attr = {
				.name = "di3_deb_on_ms",
				.mode = 0660,
			},
			.show = devAttrGpioDebMsOn_show,
			.store = devAttrGpioDebMsOn_store,
		},
		.gpio = &gpioDI[DI3].gpio,
	},

	{
		.devAttr = {
			.attr = {
				.name = "di3_deb_off_ms",
				.mode = 0660,
			},
			.show = devAttrGpioDebMsOff_show,
			.store = devAttrGpioDebMsOff_store,
		},
		.gpio = &gpioDI[DI3].gpio,
	},

	{
		.devAttr = {
			.attr = {
				.name = "di4_deb_on_ms",
				.mode = 0660,
			},
			.show = devAttrGpioDebMsOn_show,
			.store = devAttrGpioDebMsOn_store,
		},
		.gpio = &gpioDI[DI4].gpio,
	},

	{
		.devAttr = {
			.attr = {
				.name = "di4_deb_off_ms",
				.mode = 0660,
			},
			.show = devAttrGpioDebMsOff_show,
			.store = devAttrGpioDebMsOff_store,
		},
		.gpio = &gpioDI[DI4].gpio,
	},

	{
		.devAttr = {
			.attr = {
				.name = "di1_deb_on_cnt",
				.mode = 0440,
			},
			.show = devAttrGpioDebOnCnt_show,
			.store = NULL,
		},
		.gpio = &gpioDI[DI1].gpio,
	},

	{
		.devAttr = {
			.attr = {
				.name = "di1_deb_off_cnt",
				.mode = 0440,
			},
			.show = devAttrGpioDebOffCnt_show,
			.store = NULL,
		},
		.gpio = &gpioDI[DI1].gpio,
	},

	{
		.devAttr = {
			.attr = {
				.name = "di2_deb_on_cnt",
				.mode = 0440,
			},
			.show = devAttrGpioDebOnCnt_show,
			.store = NULL,
		},
		.gpio = &gpioDI[DI2].gpio,
	},

	{
		.devAttr = {
			.attr = {
				.name = "di2_deb_off_cnt",
				.mode = 0440,
			},
			.show = devAttrGpioDebOffCnt_show,
			.store = NULL,
		},
		.gpio = &gpioDI[DI2].gpio,
	},

	{
		.devAttr = {
			.attr = {
				.name = "di3_deb_on_cnt",
				.mode = 0440,
			},
			.show = devAttrGpioDebOnCnt_show,
			.store = NULL,
		},
		.gpio = &gpioDI[DI3].gpio,
	},

	{
		.devAttr = {
			.attr = {
				.name = "di3_deb_off_cnt",
				.mode = 0440,
			},
			.show = devAttrGpioDebOffCnt_show,
			.store = NULL,
		},
		.gpio = &gpioDI[DI3].gpio,
	},

	{
		.devAttr = {
			.attr = {
				.name = "di4_deb_on_cnt",
				.mode = 0440,
			},
			.show = devAttrGpioDebOnCnt_show,
			.store = NULL,
		},
		.gpio = &gpioDI[DI4].gpio,
	},

	{
		.devAttr = {
			.attr = {
				.name = "di4_deb_off_cnt",
				.mode = 0440,
			},
			.show = devAttrGpioDebOffCnt_show,
			.store = NULL,
		},
		.gpio = &gpioDI[DI4].gpio,
	},

	{ }
};

static struct DeviceAttrBean devAttrBeansDigitalOut[] = {
	{
		.devAttr = {
			.attr = {
				.name = "pdc",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 137,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 4,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "o1",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 85,
			.len = 2,
			.maskedReg = false,
			.mask = 0b11,
			.shift = 0,
			.sign = false,
			.vals = VALS_DIGITAL_OUTS_STATUS,
		},
		.regSpecsStore = {
			.reg = 84,
			.len = 2,
			.maskedReg = true,
			.mask = 0b1,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "o2",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 85,
			.len = 2,
			.maskedReg = false,
			.mask = 0b11,
			.shift = 2,
			.sign = false,
			.vals = VALS_DIGITAL_OUTS_STATUS,
		},
		.regSpecsStore = {
			.reg = 84,
			.len = 2,
			.maskedReg = true,
			.mask = 0b1,
			.shift = 1,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "o3",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 85,
			.len = 2,
			.maskedReg = false,
			.mask = 0b11,
			.shift = 4,
			.sign = false,
			.vals = VALS_DIGITAL_OUTS_STATUS,
		},
		.regSpecsStore = {
			.reg = 84,
			.len = 2,
			.maskedReg = true,
			.mask = 0b1,
			.shift = 2,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "o4",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 85,
			.len = 2,
			.maskedReg = false,
			.mask = 0b11,
			.shift = 6,
			.sign = false,
			.vals = VALS_DIGITAL_OUTS_STATUS,
		},
		.regSpecsStore = {
			.reg = 84,
			.len = 2,
			.maskedReg = true,
			.mask = 0b1,
			.shift = 3,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "oc1",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 90,
			.len = 2,
			.maskedReg = false,
			.mask = 0b11,
			.shift = 0,
			.sign = false,
			.vals = VALS_DIGITAL_OUTS_STATUS,
		},
		.regSpecsStore = {
			.reg = 89,
			.len = 2,
			.maskedReg = true,
			.mask = 0b1,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "oc2",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 90,
			.len = 2,
			.maskedReg = false,
			.mask = 0b11,
			.shift = 2,
			.sign = false,
			.vals = VALS_DIGITAL_OUTS_STATUS,
		},
		.regSpecsStore = {
			.reg = 89,
			.len = 2,
			.maskedReg = true,
			.mask = 0b1,
			.shift = 1,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "oc3",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 90,
			.len = 2,
			.maskedReg = false,
			.mask = 0b11,
			.shift = 4,
			.sign = false,
			.vals = VALS_DIGITAL_OUTS_STATUS,
		},
		.regSpecsStore = {
			.reg = 89,
			.len = 2,
			.maskedReg = true,
			.mask = 0b1,
			.shift = 2,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "oc4",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 90,
			.len = 2,
			.maskedReg = false,
			.mask = 0b11,
			.shift = 6,
			.sign = false,
			.vals = VALS_DIGITAL_OUTS_STATUS,
		},
		.regSpecsStore = {
			.reg = 89,
			.len = 2,
			.maskedReg = true,
			.mask = 0b1,
			.shift = 3,
			.sign = false,
			.vals = NULL,
		},
	},

	{ }
};

static struct DeviceAttrBean devAttrBeansPwrIn[] = {
	{
		.devAttr = {
			.attr = {
				.name = "mon_v",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 145,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "mon_i",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 146,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{ }
};

static struct DeviceAttrBean devAttrBeansSysTemp[] = {
	{
		.devAttr = {
			.attr = {
				.name = "top",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 155,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = true,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "bottom",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 156,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = true,
			.vals = NULL,
		},
	},

	{ }
};

static struct DeviceAttrBean devAttrBeansWatchdog[] = {
	{
		.devAttr = {
			.attr = {
				.name = "enabled",
				.mode = 0660,
			},
			.show = devAttrGpio_show,
			.store = devAttrGpio_store,
		},
		.gpio = &gpioWdEn,
	},

	{
		.devAttr = {
			.attr = {
				.name = "heartbeat",
				.mode = 0660,
			},
			.show = devAttrGpio_show,
			.store = devAttrGpio_store,
		},
		.gpio = &gpioWdHeartbeat,
	},

	{
		.devAttr = {
			.attr = {
				.name = "expired",
				.mode = 0440,
			},
			.show = devAttrGpioDeb_show,
			.store = NULL,
		},
		.gpio = &gpioWdExpired.gpio,
	},

	{
		.devAttr = {
			.attr = {
				.name = "enable_mode",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 29,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = VALS_WATCHDOG_ENABLE_MODE,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "timeout",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 30,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "down_delay",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 31,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "sd_switch",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 32,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{ }
};

static struct DeviceAttrBean devAttrBeansPower[] = {
	{
		.devAttr = {
			.attr = {
				.name = "down_enabled",
				.mode = 0660,
			},
			.show = devAttrGpio_show,
			.store = devAttrGpio_store,
		},
		.gpio = &gpioPwrDnwEn,
	},

	{
		.devAttr = {
			.attr = {
				.name = "down_enable_mode",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 36,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 0,
			.sign = false,
			.vals = VALS_POWER_DOWN_ENABLE_MODE,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "up_mode",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 36,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 1,
			.sign = false,
			.vals = VALS_POWER_UP_MODE,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "sd_switch",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 36,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 2,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "down_delay",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 37,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "off_time",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 38,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "up_delay",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 39,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{ },
};

static struct DeviceAttrBean devAttrBeansUps[] = {
	{
		.devAttr = {
			.attr = {
				.name = "enabled",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 43,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "battery_v",
				.mode = 0660,
			},
			.show = devAttrUpsBatteryV_show,
			.store = devAttrUpsBatteryV_store,
		},
		.regSpecs = {
			.reg = 43,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 1,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "battery_capacity",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 44,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "battery_i_max",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 45,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "power_delay",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 46,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "battery",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 48,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 7,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "battery_charge",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 48,
			.len = 2,
			.maskedReg = false,
			.mask = 0xff,
			.shift = 8,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "status",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 48,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1111,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "charger_mon_v",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 147,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "charger_mon_i",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 148,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{ }
};

static struct DeviceAttrBean devAttrBeansSd[] = {
	{
		.devAttr = {
			.attr = {
				.name = "sdx_enabled",
				.mode = 0660,
			},
			.show = devAttrSdEnabled_show,
			.store = devAttrSdEnabled_store,
		},
		.regSpecs = {
			.reg = 52,
			.len = 2,
			.maskedReg = false,
			.mask = 0x0101,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "sd1_enabled",
				.mode = 0660,
			},
			.show = devAttrSdEnabled_show,
			.store = devAttrSdEnabled_store,
		},
		.regSpecs = {
			.reg = 52,
			.len = 2,
			.maskedReg = false,
			.mask = 0x0101,
			.shift = 1,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "sdx_default",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 52,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 2,
			.sign = false,
			.vals = VALS_SD_SDX_ROUTING,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "sdx_routing",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 52,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 3,
			.sign = false,
			.vals = VALS_SD_SDX_ROUTING,
		},
	},

	{ }
};

static struct DeviceAttrBean devAttrBeansUsb[] = {
	{
		.devAttr = {
			.attr = {
				.name = "usb1_enabled",
				.mode = 0660,
			},
			.show = devAttrGpio_show,
			.store = devAttrGpio_store,
		},
		.gpio = &gpioUsb1En,
	},

	{
		.devAttr = {
			.attr = {
				.name = "usb1_err",
				.mode = 0440,
			},
			.show = devAttrGpio_show,
			.store = NULL,
		},
		.gpio = &gpioUsb1Err,
	},

	{
		.devAttr = {
			.attr = {
				.name = "usb2_enabled",
				.mode = 0660,
			},
			.show = devAttrGpio_show,
			.store = devAttrGpio_store,
		},
		.gpio = &gpioUsb2En,
	},

	{
		.devAttr = {
			.attr = {
				.name = "usb2_err",
				.mode = 0440,
			},
			.show = devAttrGpio_show,
			.store = NULL,
		},
		.gpio = &gpioUsb2Err,
	},

	{ }
};

static struct DeviceAttrBean devAttrBeansAnalogOut[] = {
	{
		.devAttr = {
			.attr = {
				.name = "ao1_enabled",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 94,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "ao1_mode",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 94,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 1,
			.sign = false,
			.vals = VALS_ANALOG_OUTS_MODE,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "ao1",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 95,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "ao1_err",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 96,
			.len = 2,
			.maskedReg = false,
			.mask = 0b111,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "ao2_enabled",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 99,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "ao2_mode",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 99,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 1,
			.sign = false,
			.vals = VALS_ANALOG_OUTS_MODE,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "ao2",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 100,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "ao2_err",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 101,
			.len = 2,
			.maskedReg = false,
			.mask = 0b111,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{ }
};

static struct DeviceAttrBean devAttrBeansPowerOut[] = {
	{
		.devAttr = {
			.attr = {
				.name = "vso_enabled",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 132,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "vso",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 133,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "vso_mon_v",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 149,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "vso_mon_i",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 150,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "5vo_enabled",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 137,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 1,
			.sign = false,
			.vals = NULL,
		},
	},

	{ }
};

static struct DeviceAttrBean devAttrBeansFan[] = {
	{
		.devAttr = {
			.attr = {
				.name = "always_on",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 137,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "status",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 140,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{ }
};

static struct DeviceAttrBean devAttrBeansExpBus[] = {
	{
		.devAttr = {
			.attr = {
				.name = "enabled",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 137,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 3,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "aux",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 140,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 3,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "5vx",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 137,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 2,
			.sign = false,
			.vals = NULL,
		},
	},

	{ }
};

static struct DeviceAttrBean devAttrBeansSysState[] = {
	{
		.devAttr = {
			.attr = {
				.name = "status_all",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 140,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "fan_status",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 140,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "5vo_prot",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 140,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 1,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "5vx_prot",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 140,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 2,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "expbus_aux",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 140,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 3,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "vso_prot",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 140,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 4,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "ao1_prot",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 140,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 5,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "ao2_prot",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 140,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 6,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "vso_err",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 140,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 7,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "ad4112_err",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 140,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 8,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "ups_err",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 140,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 9,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "led_err",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 140,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 10,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "sys_temp_err",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 140,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 11,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "rs232_err",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 140,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 12,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "rs485_err",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecs = {
			.reg = 140,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 13,
			.sign = false,
			.vals = NULL,
		},
	},

	{ }
};

static struct DeviceAttrBean devAttrBeansWiegand[] = {
	{
		.devAttr = {
			.attr = {
				.name = "w1_enabled",
				.mode = 0660,
			},
			.show = devAttrWiegandEnabled_show,
			.store = devAttrWiegandEnabled_store,
		}
	},

	{
		.devAttr = {
			.attr = {
				.name = "w1_data",
				.mode = 0440,
			},
			.show = devAttrWiegandData_show,
			.store = NULL,
		}
	},

	{
		.devAttr = {
			.attr = {
				.name = "w1_noise",
				.mode = 0440,
			},
			.show = devAttrWiegandNoise_show,
			.store = NULL,
		}
	},

	{
		.devAttr = {
			.attr = {
				.name = "w1_pulse_itvl_min",
				.mode = 0660,
			},
			.show = devAttrWiegandPulseIntervalMin_show,
			.store = devAttrWiegandPulseIntervalMin_store,
		}
	},

	{
		.devAttr = {
			.attr = {
				.name = "w1_pulse_itvl_max",
				.mode = 0660,
			},
			.show = devAttrWiegandPulseIntervalMax_show,
			.store = devAttrWiegandPulseIntervalMax_store,
		}
	},

	{
		.devAttr = {
			.attr = {
				.name = "w1_pulse_width_min",
				.mode = 0660,
			},
			.show = devAttrWiegandPulseWidthMin_show,
			.store = devAttrWiegandPulseWidthMin_store,
		}
	},

	{
		.devAttr = {
			.attr = {
				.name = "w1_pulse_width_max",
				.mode = 0660,
			},
			.show = devAttrWiegandPulseWidthMax_show,
			.store = devAttrWiegandPulseWidthMax_store,
		}
	},

	{
		.devAttr = {
			.attr = {
				.name = "w2_enabled",
				.mode = 0660,
			},
			.show = devAttrWiegandEnabled_show,
			.store = devAttrWiegandEnabled_store,
		}
	},

	{
		.devAttr = {
			.attr = {
				.name = "w2_data",
				.mode = 0440,
			},
			.show = devAttrWiegandData_show,
			.store = NULL,
		}
	},

	{
		.devAttr = {
			.attr = {
				.name = "w2_noise",
				.mode = 0440,
			},
			.show = devAttrWiegandNoise_show,
			.store = NULL,
		}
	},

	{
		.devAttr = {
			.attr = {
				.name = "w2_pulse_itvl_min",
				.mode = 0660,
			},
			.show = devAttrWiegandPulseIntervalMin_show,
			.store = devAttrWiegandPulseIntervalMin_store,
		}
	},

	{
		.devAttr = {
			.attr = {
				.name = "w2_pulse_itvl_max",
				.mode = 0660,
			},
			.show = devAttrWiegandPulseIntervalMax_show,
			.store = devAttrWiegandPulseIntervalMax_store,
		}
	},

	{
		.devAttr = {
			.attr = {
				.name = "w2_pulse_width_min",
				.mode = 0660,
			},
			.show = devAttrWiegandPulseWidthMin_show,
			.store = devAttrWiegandPulseWidthMin_store,
		}
	},

	{
		.devAttr = {
			.attr = {
				.name = "w2_pulse_width_max",
				.mode = 0660,
			},
			.show = devAttrWiegandPulseWidthMax_show,
			.store = devAttrWiegandPulseWidthMax_store,
		}
	},

	{ }
};

static struct DeviceAttrBean devAttrBeansMcu[] = {
	{
		.devAttr = {
			.attr = {
				.name = "fw_version",
				.mode = 0440,
			},
			.show = devAttrMcuFwVersion_show,
			.store = NULL,
		}
	},

	{
		.devAttr = {
			.attr = {
				.name = "config",
				.mode = 0220,
			},
			.show = NULL,
			.store = devAttrMcuConfig_store,
		}
	},

	{
		.devAttr = {
			.attr = {
				.name = "sw_en",
				.mode = 0660,
			},
			.show = devAttrGpio_show,
			.store = devAttrGpio_store,
		},
		.gpio = &gpioSwEn,
	},

	{
		.devAttr = {
			.attr = {
				.name = "sw_reset",
				.mode = 0660,
			},
			.show = devAttrGpio_show,
			.store = devAttrGpio_store,
		},
		.gpio = &gpioSwReset,
	},

	{
		.devAttr = {
			.attr = {
				.name = "i2c_read",
				.mode = 0660,
			},
			.show = mcuI2cRead_show,
			.store = mcuI2cRead_store,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "i2c_write",
				.mode = 0220,
			},
			.show = NULL,
			.store = mcuI2cWrite_store,
		},
	},

	{ }
};

static struct DeviceAttrBean devAttrBeansAtec[] = {
	{
		.devAttr = {
			.attr = {
				.name = "serial_num",
				.mode = 0440,
			},
			.show = devAttrAteccSerial_show,
			.store = NULL,
		},
	},

	{ }
};

static struct DeviceAttrBean devAttrBeansSerial[] = {
	{
		.devAttr = {
			.attr = {
				.name = "rs232_rs485_inv",
				.mode = 0660,
			},
			.show = devAttrSerialRs232Rs485Inv_show,
			.store = devAttrSerialRs232Rs485Inv_store,
		},
		.regSpecs = {
			.reg = 26,
			.len = 2,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "rs485_txe",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecs = {
			.reg = 27,
			.len = 2,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
	},

	{ }
};

static struct DeviceBean devices[] = {
	{
		.name = "buzzer",
		.devAttrBeans = devAttrBeansBuzzer,
	},

	{
		.name = "button",
		.devAttrBeans = devAttrBeansButton,
	},

	{
		.name = "led",
		.devAttrBeans = devAttrBeansLed,
	},

	{
		.name = "analog_in",
		.devAttrBeans = devAttrBeansAnalogIn,
	},

	{
		.name = "analog_out",
		.devAttrBeans = devAttrBeansAnalogOut,
	},

	{
		.name = "digital_out",
		.devAttrBeans = devAttrBeansDigitalOut,
	},

	{
		.name = "digital_in",
		.devAttrBeans = devAttrBeansDigitalIn,
	},

	{
		.name = "digital_io",
		.devAttrBeans = devAttrBeansDigitalIO,
	},

	{
		.name = "power_in",
		.devAttrBeans = devAttrBeansPwrIn,
	},

	{
		.name = "sys_temp",
		.devAttrBeans = devAttrBeansSysTemp,
	},

	{
		.name = "watchdog",
		.devAttrBeans = devAttrBeansWatchdog,
	},

	{
		.name = "power",
		.devAttrBeans = devAttrBeansPower,
	},

	{
		.name = "ups",
		.devAttrBeans = devAttrBeansUps,
	},

	{
		.name = "sd",
		.devAttrBeans = devAttrBeansSd,
	},

	{
		.name = "usb",
		.devAttrBeans = devAttrBeansUsb,
	},

	{
		.name = "fan",
		.devAttrBeans = devAttrBeansFan,
	},

	{
		.name = "expbus",
		.devAttrBeans = devAttrBeansExpBus,
	},

	{
		.name = "power_out",
		.devAttrBeans = devAttrBeansPowerOut,
	},

	{
		.name = "sys_state",
		.devAttrBeans = devAttrBeansSysState,
	},

	{
		.name = "wiegand",
		.devAttrBeans = devAttrBeansWiegand,
	},

	{
		.name = "mcu",
		.devAttrBeans = devAttrBeansMcu,
	},

	{
		.name = "sec_elem",
		.devAttrBeans = devAttrBeansAtec,
	},

	{
		.name = "serial",
		.devAttrBeans = devAttrBeansSerial,
	},

	{ }
};

struct i2c_client *ionopimax_i2c_client = NULL;

struct ionopimax_i2c_data {
	struct mutex update_lock;
};

struct GpioBean* gpioGetBean(struct device *dev, struct device_attribute *attr) {
	struct DeviceAttrBean *dab;
	dab = container_of(attr, struct DeviceAttrBean, devAttr);
	if (dab == NULL) {
		return NULL;
	}
	return dab->gpio;
}

struct WiegandBean* wiegandGetBean(struct device *dev,
		struct device_attribute *attr) {
	if (attr->attr.name[1] == '1') {
		return &w1;
	} else {
		return &w2;
	}
}

static bool ionopimax_i2c_lock(void) {
	uint8_t i;
	struct ionopimax_i2c_data *data;
	if (!ionopimax_i2c_client) {
		return false;
	}
	data = i2c_get_clientdata(ionopimax_i2c_client);
	for (i = 0; i < 20; i++) {
		if (mutex_trylock(&data->update_lock)) {
			return true;
		}
		msleep(1);
	}
	return false;
}

static void ionopimax_i2c_unlock(void) {
	struct ionopimax_i2c_data *data;
	if (ionopimax_i2c_client) {
		data = i2c_get_clientdata(ionopimax_i2c_client);
		mutex_unlock(&data->update_lock);
	}
}

static uint8_t ionopimax_i2c_crc_process(uint8_t crc, uint8_t dByte) {
	uint8_t k;
	crc ^= dByte;
	for (k = 0; k < 8; k++)
		crc = crc & 0x80 ? (crc << 1) ^ 0x2f : crc << 1;
	return crc;
}

static void ionopimax_i2c_add_crc(int reg, char *data, uint8_t len) {
	uint8_t i;
	uint8_t crc;

	crc = ionopimax_i2c_crc_process(0xff, reg);
	for (i = 0; i < len; i++) {
		crc = ionopimax_i2c_crc_process(crc, data[i]);
	}
	data[len] = crc;
}

static int32_t ionopimax_i2c_read_no_lock(uint8_t reg, uint8_t len) {
	int32_t res;
	char buf[4];
	uint8_t i;
	uint8_t crc;

	if (!ionopimax_i2c_client) {
		return -EIO;
	}

	if (fwVerMajor > 1 || fwVerMinor >= 4) {
		len++;
	}

	for (i = 0; i < 3; i++) {
		res = i2c_smbus_read_i2c_block_data(ionopimax_i2c_client, reg, len,
				buf);
		if (res == len) {
			if (fwVerMajor > 1 || fwVerMinor >= 4) {
				crc = buf[len - 1];
				ionopimax_i2c_add_crc(reg, buf, len - 1);
				if (crc == buf[len - 1]) {
					break;
				} else {
					res = -1;
				}
			} else {
				break;
			}
		}
	}

	if (res != len) {
		return -EIO;
	}

	if (fwVerMajor > 1 || fwVerMinor >= 4) {
		len--;
	}

	res = 0;
	for (i = 0; i < len; i++) {
		res |= (buf[i] & 0xff) << (i * 8);
	}

	return res;
}

static int32_t ionopimax_i2c_write_no_lock(uint8_t reg, uint8_t len,
		uint32_t val) {
	char buf[4];
	uint8_t i;

	if (!ionopimax_i2c_client) {
		return -EIO;
	}

	for (i = 0; i < len; i++) {
		buf[i] = val >> (8 * i);
	}
	if (fwVerMajor > 1 || fwVerMinor >= 4) {
		ionopimax_i2c_add_crc(reg, buf, len);
		len++;
	}
	for (i = 0; i < 3; i++) {
		if (!i2c_smbus_write_i2c_block_data(ionopimax_i2c_client, reg, len,
				buf)) {
			return len;
		}
	}
	return -EIO;
}

static int32_t ionopimax_i2c_read(uint8_t reg, uint8_t len) {
	int32_t res;

	if (len < 2) {
		return -EINVAL;
	}

	if (!ionopimax_i2c_lock()) {
		return -EBUSY;
	}

	res = ionopimax_i2c_read_no_lock(reg, len);

	ionopimax_i2c_unlock();

	if (res < 0) {
		return -EIO;
	}
	return res;
}

static int32_t ionopimax_i2c_write(uint8_t reg, uint8_t len, uint32_t val) {
	int32_t res;

	if (!ionopimax_i2c_lock()) {
		return -EBUSY;
	}

	res = ionopimax_i2c_write_no_lock(reg, len, val);

	ionopimax_i2c_unlock();

	if (res < 0) {
		return -EIO;
	}
	return res;
}

static int32_t ionopimax_i2c_read_segment(uint8_t reg, uint8_t len,
		uint32_t mask, uint8_t shift) {
	int32_t res;
	res = ionopimax_i2c_read(reg, len);
	if (res < 0) {
		return res;
	}
	return (res >> shift) & mask;
}

static int32_t ionopimax_i2c_write_segment(uint8_t reg, bool maskedReg,
		uint32_t mask, uint8_t shift, uint32_t val) {
	int32_t res = 0;

	if (!ionopimax_i2c_lock()) {
		return -EBUSY;
	}

	val = (val & mask) << shift;
	mask <<= shift;

	if (maskedReg) {
		val = (mask << 8) | val;
	} else if (mask != 0xffff) {
		res = ionopimax_i2c_read_no_lock(reg, 2);
		val = (res & ~mask) | val;
	}

	if (res >= 0) {
		res = ionopimax_i2c_write_no_lock(reg, 2, val);
		if (res >= 0) {
			res = ionopimax_i2c_read_no_lock(reg, 2);
			if (res >= 0) {
				if (maskedReg) {
					res &= mask;
					val &= mask;
				}
				if (res != val) {
					res = -EPERM;
				}
			}
		}
	}

	ionopimax_i2c_unlock();

	return res;
}

static ssize_t devAttrI2c_show(struct device *dev,
		struct device_attribute *attr, char *buf) {
	int32_t res;
	struct DeviceAttrRegSpecs *specs;
	struct DeviceAttrBean *dab;
	dab = container_of(attr, struct DeviceAttrBean, devAttr);
	if (dab == NULL) {
		return -EFAULT;
	}
	specs = &dab->regSpecs;
	if (specs->reg == 0) {
		return -EFAULT;
	}

	res = ionopimax_i2c_read_segment((uint8_t) specs->reg, specs->len,
			specs->mask, specs->shift);

	if (res < 0) {
		return res;
	}

	if (specs->vals == NULL) {
		if (specs->sign) {
			if (specs->len == 3) {
				if ((res & 0x800000) == 0x800000) {
					// negative => add sign bits
					res |= 0xff000000;
				}
				return sprintf(buf, "%d\n", (int32_t) res);
			} else {
				return sprintf(buf, "%d\n", (int16_t) res);
			}
		} else {
			return sprintf(buf, "%d\n", res);
		}
	} else {
		if (res > specs->vals[0] - 1) {
			return -EFAULT;
		}

		return sprintf(buf, "%c\n", specs->vals[res + 1]);
	}
}

static ssize_t devAttrI2c_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count) {
	long val;
	int ret;
	uint16_t i;
	int32_t res;
	char valC;
	struct DeviceAttrRegSpecs *specs;
	struct DeviceAttrBean *dab;
	dab = container_of(attr, struct DeviceAttrBean, devAttr);
	if (dab == NULL) {
		return -EFAULT;
	}
	specs = &dab->regSpecsStore;
	if (specs->reg == 0) {
		specs = &dab->regSpecs;
		if (specs->reg == 0) {
			return -EFAULT;
		}
	}

	if (specs->vals == NULL) {
		ret = kstrtol(buf, 10, &val);
		if (ret < 0) {
			return -EINVAL;
		}
	} else {
		val = -1;
		valC = toUpper(buf[0]);
		for (i = 0; i < specs->vals[0]; i++) {
			if (specs->vals[i + 1] == valC) {
				val = i;
				break;
			}
		}
		if (val == -1) {
			return -EINVAL;
		}
	}

	if (!specs->sign && val < 0) {
		return -EINVAL;
	}

	res = ionopimax_i2c_write_segment((uint8_t) specs->reg,
			specs->maskedReg,
			specs->mask, specs->shift, (uint16_t) val);

	if (res < 0) {
		return res;
	}

	return count;
}

static ssize_t devAttrAxMode_show(struct device *dev,
		struct device_attribute *attr, char *buf) {
	int32_t res;
	struct DeviceAttrRegSpecs *specs;
	struct DeviceAttrBean *dab;
	dab = container_of(attr, struct DeviceAttrBean, devAttr);
	if (dab == NULL) {
		return -EFAULT;
	}
	specs = &dab->regSpecs;
	if (specs->reg == 0) {
		return -EFAULT;
	}

	if (fwVerMajor > 1 || fwVerMinor >= 3) {
		res = ionopimax_i2c_read_segment((uint8_t) specs->reg + 1,
				specs->len,
				specs->mask, specs->shift - 4);

		if (res < 0) {
			return res;
		}

		if (res == 0) {
			return sprintf(buf, "0\n");
		}
	}

	res = ionopimax_i2c_read_segment((uint8_t) specs->reg, specs->len,
			specs->mask, specs->shift);

	if (res < 0) {
		return res;
	}

	if (res == 0) {
		return sprintf(buf, "U\n");
	}
	return sprintf(buf, "B\n");
}

static ssize_t devAttrAxMode_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count) {
	int32_t res;
	char valC;
	uint16_t en, mode;
	struct DeviceAttrRegSpecs *specs;
	struct DeviceAttrBean *dab;
	dab = container_of(attr, struct DeviceAttrBean, devAttr);
	if (dab == NULL) {
		return -EFAULT;
	}
	specs = &dab->regSpecs;
	if (specs->reg == 0) {
		return -EFAULT;
	}

	valC = toUpper(buf[0]);
	if (valC == '0') {
		if (fwVerMajor > 1 || fwVerMinor >= 3) {
			en = 0;
			mode = 0xff;
		} else {
			return -EINVAL;
		}
	} else {
		en = 1;
		if (valC == 'U') {
			mode = 0;
		} else if (valC == 'B') {
			mode = 1;
		} else {
			return -EINVAL;
		}
	}

	if (fwVerMajor > 1 || fwVerMinor >= 3) {
		res = ionopimax_i2c_write_segment((uint8_t) specs->reg + 1,
				specs->maskedReg, specs->mask, specs->shift - 4,
				(uint16_t) en);

		if (res < 0) {
			return res;
		}
	}

	if (mode != 0xff) {
		res = ionopimax_i2c_write_segment((uint8_t) specs->reg,
				specs->maskedReg, specs->mask, specs->shift,
				(uint16_t) mode);

		if (res < 0) {
			return res;
		}
	}

	return count;
}

static ssize_t devAttrUpsBatteryV_show(struct device *dev,
		struct device_attribute *attr, char *buf) {
	int32_t res;
	res = devAttrI2c_show(dev, attr, buf);
	if (res < 0) {
		return res;
	}
	if (buf[0] == '1') {
		return sprintf(buf, "24000\n");
	} else {
		return sprintf(buf, "12000\n");
	}
}

static ssize_t devAttrUpsBatteryV_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count) {
	int ret;
	long val;
	char buf2[2];
	ret = kstrtol(buf, 10, &val);
	if (ret < 0) {
		return -EINVAL;
	}
	if (val == 12000) {
		buf2[0] = '0';
	} else if (val == 24000) {
		buf2[0] = '1';
	} else {
		return -EINVAL;
	}

	buf2[1] = '\0';
	ret = devAttrI2c_store(dev, attr, buf2, 1);
	if (ret < 0) {
		return ret;
	}
	return count;
}

static ssize_t devAttrSdEnabled_show(struct device *dev,
		struct device_attribute *attr, char *buf) {
	int32_t res;
	struct DeviceAttrRegSpecs *specs;
	struct DeviceAttrBean *dab;
	dab = container_of(attr, struct DeviceAttrBean, devAttr);
	if (dab == NULL) {
		return -EFAULT;
	}
	specs = &dab->regSpecs;
	if (specs->reg == 0) {
		return -EFAULT;
	}

	res = ionopimax_i2c_read_segment((uint8_t) specs->reg, specs->len,
			specs->mask, specs->shift);

	if (res < 0) {
		return res;
	}

	if (res < 2) {
		return sprintf(buf, "%d\n", res);
	}

	if (specs->shift == 0) { // SDX
		if ((res & 1) == 0) {
			return sprintf(buf, "2\n");
		} else {
			return sprintf(buf, "1\n");
		}
	} else { // SD1
		if ((res & 1) == 1) {
			return sprintf(buf, "2\n");
		} else {
			return sprintf(buf, "0\n");
		}
	}
}

static ssize_t devAttrSdEnabled_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count) {
	long val;
	int ret;
	int32_t res;
	struct DeviceAttrRegSpecs *specs;
	struct DeviceAttrBean *dab;
	dab = container_of(attr, struct DeviceAttrBean, devAttr);
	if (dab == NULL) {
		return -EFAULT;
	}
	specs = &dab->regSpecsStore;
	if (specs->reg == 0) {
		specs = &dab->regSpecs;
		if (specs->reg == 0) {
			return -EFAULT;
		}
	}

	ret = kstrtol(buf, 10, &val);
	if (ret < 0) {
		return -EINVAL;
	}

	if (val < 0 || val > 2) {
		return -EINVAL;
	}

	if (val == 2) {
		if (specs->shift == 0) { // SDX
			val = 0x0100;
		} else { // SD1
			val = 0x0101;
		}
	}

	res = ionopimax_i2c_write_segment((uint8_t) specs->reg,
			specs->maskedReg,
			specs->mask, specs->shift, (uint16_t) val);

	if (res < 0) {
		return res;
	}

	return count;
}

static ssize_t getFwVersion(void) {
	int32_t val;
	val = ionopimax_i2c_read(1, 2);

	if (val < 0) {
		return val;
	}

	fwVerMajor = (val >> 8) & 0xf;
	fwVerMinor = val & 0xf;

	return 0;
}

static ssize_t devAttrMcuFwVersion_show(struct device *dev,
		struct device_attribute *attr, char *buf) {
	int32_t res;

	res = getFwVersion();
	if (res < 0) {
		return res;
	}

	return sprintf(buf, "%d.%d\n", fwVerMajor, fwVerMinor);
}

static ssize_t devAttrMcuConfig_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count) {
	int32_t res;
	uint32_t val;
	uint16_t i;
	char cmd;
	cmd = toUpper(buf[0]);
	val = 0x2a00;
	if (cmd == 'R') {
		val |= 0x02;
	} else if (cmd == 'S') {
		val |= 0x03;
	} else {
		return -EINVAL;
	}

	res = ionopimax_i2c_write(5, 2, val);
	if (res < 0) {
		return res;
	}

	for (i = 0; i < 10; i++) {
		msleep(50);
		res = ionopimax_i2c_read(6, 2);
		if (res >= 0) {
			if (((res >> 10) & 1) == 0) {
				if (((res >> 8) & 1) == 1) {
					return count;
				} else {
					return -EFAULT;
				}
			} else {
				res = -EBUSY;
			}
		}
	}

	return res;
}

static int32_t mcuI2cReadVal;

static ssize_t mcuI2cRead_show(struct device *dev,
		struct device_attribute *attr, char *buf) {
	return sprintf(buf, "0x%04x\n", mcuI2cReadVal);
}

static ssize_t mcuI2cRead_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count) {
	int ret;
	long reg;
	ret = kstrtol(buf, 10, &reg);
	if (ret < 0) {
		return ret;
	}

	mcuI2cReadVal = ionopimax_i2c_read((uint8_t) reg, 2);

	if (mcuI2cReadVal < 0) {
		return mcuI2cReadVal;
	}

	return count;
}

static ssize_t mcuI2cWrite_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count) {
	long reg = 0;
	long val = 0;
	char *end = NULL;

	reg = simple_strtol(buf, &end, 10);
	val = simple_strtol(end + 1, NULL, 16);

	if (ionopimax_i2c_write((uint8_t) reg, 2, (uint32_t) val) < 0) {
		return -EIO;
	}

	return count;
}

static ssize_t devAttrSerialRs232Rs485Inv_show(struct device *dev,
		struct device_attribute *attr, char *buf) {
	int32_t res;
	long val;
	res = devAttrI2c_show(dev, attr, buf);
	if (res < 0) {
		return res;
	}
	if (kstrtol(buf, 10, &val) < 0) {
		return -EINVAL;
	}

	if ((val & 1) == 0) {
		return sprintf(buf, "0\n");
	}

	if ((val & 2) == 0) {
		return sprintf(buf, "1\n");
	}

	switch ((val >> 8) & 0xf) {
	case 2:
		res = sprintf(buf, "1200");
		break;
	case 3:
		res = sprintf(buf, "2400");
		break;
	case 4:
		res = sprintf(buf, "4800");
		break;
	case 5:
		res = sprintf(buf, "9600");
		break;
	case 6:
		res = sprintf(buf, "19200");
		break;
	case 7:
		res = sprintf(buf, "38400");
		break;
	case 8:
		res = sprintf(buf, "57600");
		break;
	case 9:
		res = sprintf(buf, "115200");
		break;
	default:
		return -EINVAL;
	}

	res += sprintf(buf + res, " ");

	res += sprintf(buf + res, (((val >> 14) & 1) == 0) ? "7" : "8");

	switch ((val >> 12) & 0x3) {
	case 0:
		res += sprintf(buf + res, "N");
		break;
	case 1:
		res += sprintf(buf + res, "O");
		break;
	case 2:
		res += sprintf(buf + res, "E");
		break;
	default:
		return -EINVAL;
	}

	res += sprintf(buf + res, (((val >> 15) & 1) == 0) ? "1" : "2");

	res += sprintf(buf + res, "\n");

	return res;
}

static ssize_t devAttrSerialRs232Rs485Inv_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count) {
	int ret;
	char *end = NULL;
	long baud;
	uint16_t regVal;
	char regStr[6];

	baud = simple_strtol(buf, &end, 10);
	if (buf == end) {
		return -EINVAL;
	}

	switch (baud) {
	case 0:
		case 1:
		regVal = baud;
		break;
	case 1200:
		regVal = 2;
		break;
	case 2400:
		regVal = 3;
		break;
	case 4800:
		regVal = 4;
		break;
	case 9600:
		regVal = 5;
		break;
	case 19200:
		regVal = 6;
		break;
	case 38400:
		regVal = 7;
		break;
	case 57600:
		regVal = 8;
		break;
	case 115200:
		regVal = 9;
		break;
	default:
		return -EINVAL;
	}

	if (regVal > 1) {
		if (end + 4 > buf + count) {
			return -EINVAL;
		}

		regVal <<= 8;
		regVal |= 0x3;

		switch (end[1]) {
		case '7':
			break;
		case '8':
			regVal |= 0x4000;
			break;
		default:
			return -EINVAL;
		}

		switch (end[2]) {
		case 'n':
			case 'N':
			break;
		case 'o':
			case 'O':
			regVal |= 0x1000;
			break;
		case 'e':
			case 'E':
			regVal |= 0x2000;
			break;
		default:
			return -EINVAL;
		}

		switch (end[3]) {
		case '1':
			break;
		case '2':
			regVal |= 0x8000;
			break;
		default:
			return -EINVAL;
		}
	}

	ret = sprintf(regStr, "%d", regVal);
	if (ret < 1) {
		return ret;
	}

	ret = devAttrI2c_store(dev, attr, regStr, ret);
	if (ret < 0) {
		return ret;
	}

	return count;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,5,0)
static int ionopimax_i2c_probe(struct i2c_client *client) {
#else
static int ionopimax_i2c_probe(struct i2c_client *client,
		const struct i2c_device_id *id) {
#endif
	struct ionopimax_i2c_data *data;

	data = devm_kzalloc(&client->dev, sizeof(struct ionopimax_i2c_data),
	GFP_KERNEL);
	if (!data) {
		return -ENOMEM;
	}

	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);

	ionopimax_i2c_client = client;

	pr_info(LOG_TAG "i2c probe addr=0x%02hx\n", client->addr);

	return 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,0,0)
static int ionopimax_i2c_remove(struct i2c_client *client) {
#else
static void ionopimax_i2c_remove(struct i2c_client *client) {
#endif
	struct ionopimax_i2c_data *data = i2c_get_clientdata(client);
	mutex_destroy(&data->update_lock);

	pr_info(LOG_TAG "i2c remove addr=0x%02hx\n", client->addr);

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,0,0)
	return 0;
#endif
}

const struct of_device_id ionopimax_of_match[] = {
	{ .compatible = "sferalabs,ionopimax", },
	{ },
};
MODULE_DEVICE_TABLE( of, ionopimax_of_match);

static const struct i2c_device_id ionopimax_i2c_id[] = {
	{ "ionopimax", 0 },
	{ },
};
MODULE_DEVICE_TABLE( i2c, ionopimax_i2c_id);

static struct i2c_driver ionopimax_i2c_driver = {
	.driver = {
		.name = "ionopimax",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(ionopimax_of_match),
	},
	.probe = ionopimax_i2c_probe,
	.remove = ionopimax_i2c_remove,
	.id_table = ionopimax_i2c_id,
};

static void cleanup(void) {
	struct DeviceBean *db;
	struct DeviceAttrBean *dab;
	int i, di, ai;

	i2c_del_driver(&ionopimax_i2c_driver);

	di = 0;
	while (devices[di].name != NULL) {
		if (devices[di].pDevice && !IS_ERR(devices[di].pDevice)) {
			db = &devices[di];
			ai = 0;
			while (db->devAttrBeans[ai].devAttr.attr.name != NULL) {
				dab = &db->devAttrBeans[ai];
				device_remove_file(db->pDevice, &dab->devAttr);
				ai++;
			}
		}
		device_destroy(pDeviceClass, 0);
		di++;
	}

	if (!IS_ERR(pDeviceClass)) {
		class_destroy(pDeviceClass);
	}

	wiegandDisable(&w1);
	wiegandDisable(&w2);

	for (i = 0; i < DI_SIZE; i++) {
		gpioFreeDebounce(&gpioDI[i]);
	}
	for (i = 0; i < DT_SIZE; i++) {
		gpioFree(&gpioDT[i]);
	}
	gpioFree(&gpioBuzzer);
	gpioFreeDebounce(&gpioButton);
	gpioFree(&gpioWdEn);
	gpioFree(&gpioWdHeartbeat);
	gpioFreeDebounce(&gpioWdExpired);
	gpioFree(&gpioPwrDnwEn);
	gpioFree(&gpioUsb1En);
	gpioFree(&gpioUsb1Err);
	gpioFree(&gpioUsb2En);
	gpioFree(&gpioUsb2Err);
	gpioFree(&gpioSwEn);
	gpioFree(&gpioSwReset);
}

static int ionopimax_init(struct platform_device *pdev) {
	struct DeviceBean *db;
	struct DeviceAttrBean *dab;
	int i, di, ai;

	pr_info(LOG_TAG "init\n");

	i2c_add_driver(&ionopimax_i2c_driver);

	ateccAddDriver();

	gpioSetPlatformDev(pdev);

	for (i = 0; i < DI_SIZE; i++) {
		if (gpioInitDebounce(&gpioDI[i])) {
			pr_err(LOG_TAG "error setting up GPIO %s\n", gpioDI[i].gpio.name);
			goto fail;
		}
	}
	if (gpioInit(&gpioBuzzer)) {
		pr_err(LOG_TAG "error setting up GPIO %s\n", gpioBuzzer.name);
		goto fail;
	}
	if (gpioInitDebounce(&gpioButton)) {
		pr_err(LOG_TAG "error setting up GPIO %s\n", gpioButton.gpio.name);
		goto fail;
	}
	if (gpioInit(&gpioWdEn)) {
		pr_err(LOG_TAG "error setting up GPIO %s\n", gpioWdEn.name);
		goto fail;
	}
	if (gpioInit(&gpioWdHeartbeat)) {
		pr_err(LOG_TAG "error setting up GPIO %s\n", gpioWdHeartbeat.name);
		goto fail;
	}
	if (gpioInitDebounce(&gpioWdExpired)) {
		pr_err(LOG_TAG "error setting up GPIO %s\n", gpioWdExpired.gpio.name);
		goto fail;
	}
	if (gpioInit(&gpioPwrDnwEn)) {
		pr_err(LOG_TAG "error setting up GPIO %s\n", gpioPwrDnwEn.name);
		goto fail;
	}
	if (gpioInit(&gpioUsb1En)) {
		pr_err(LOG_TAG "error setting up GPIO %s\n", gpioUsb1En.name);
		goto fail;
	}
	if (gpioInit(&gpioUsb1Err)) {
		pr_err(LOG_TAG "error setting up GPIO %s\n", gpioUsb1Err.name);
		goto fail;
	}
	if (gpioInit(&gpioUsb2En)) {
		pr_err(LOG_TAG "error setting up GPIO %s\n", gpioUsb2En.name);
		goto fail;
	}
	if (gpioInit(&gpioUsb2Err)) {
		pr_err(LOG_TAG "error setting up GPIO %s\n", gpioUsb2Err.name);
		goto fail;
	}
	if (gpioInit(&gpioSwEn)) {
		pr_err(LOG_TAG "error setting up GPIO %s\n", gpioSwEn.name);
		goto fail;
	}
	if (gpioInit(&gpioSwReset)) {
		pr_err(LOG_TAG "error setting up GPIO %s\n", gpioSwReset.name);
		goto fail;
	}

	gpioSetVal(&gpioSwEn, 0);
	gpioSetVal(&gpioSwReset, 1);

	wiegandInit(&w1);
	wiegandInit(&w2);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,5,0)
	pDeviceClass = class_create("ionopimax");
#else
	pDeviceClass = class_create(THIS_MODULE, "ionopimax");
#endif
	if (IS_ERR(pDeviceClass)) {
		pr_err(LOG_TAG "failed to create device class\n");
		goto fail;
	}

	di = 0;
	while (devices[di].name != NULL) {
		db = &devices[di];
		db->pDevice = device_create(pDeviceClass, NULL, 0, NULL, db->name);
		if (IS_ERR(db->pDevice)) {
			pr_err(LOG_TAG "failed to create device '%s'\n", db->name);
			goto fail;
		}

		ai = 0;
		while (db->devAttrBeans[ai].devAttr.attr.name != NULL) {
			dab = &db->devAttrBeans[ai];
			if (device_create_file(db->pDevice, &dab->devAttr)) {
				pr_alert(LOG_TAG "failed to create device file '%s/%s'\n",
						db->name, dab->devAttr.attr.name);
				goto fail;
			}
			ai++;
		}
		di++;
	}

	if (getFwVersion() < 0) {
		pr_err(LOG_TAG "failed to read FW version\n");
		goto fail;
	}

	pr_info(LOG_TAG "ready FW%d.%d\n", fwVerMajor, fwVerMinor);
	return 0;

	fail:
	pr_err(LOG_TAG "init failed\n");
	cleanup();
	return -1;
}

static int ionopimax_exit(struct platform_device *pdev) {
	cleanup();
	pr_info(LOG_TAG "exit\n");
	return 0;
}

static struct platform_driver ionopimax_driver = {
	.probe = ionopimax_init,
	.remove = ionopimax_exit,
	.driver = {
		.name = "ionopimax",
		.owner = THIS_MODULE,
		.of_match_table = ionopimax_of_match,
	}
};

module_platform_driver(ionopimax_driver);
