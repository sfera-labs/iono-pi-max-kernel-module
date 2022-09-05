/*
 * ionopimax
 *
 *     Copyright (C) 2020-2022 Sfera Labs S.r.l.
 *
 *     For information, visit https://www.sferalabs.cc
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * LICENSE.txt file for more details.
 *
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include "atecc/atecc.h"

#define I2C_ADDR_LOCAL 0x35

#define WIEGAND_MAX_BITS 64

#define GPIO_MODE_IN 1
#define GPIO_MODE_OUT 2

#define GPIO_DI1 16
#define GPIO_DI2 19
#define GPIO_DI3 20
#define GPIO_DI4 21

#define GPIO_DT1 29
#define GPIO_DT2 34
#define GPIO_DT3 35
#define GPIO_DT4 36

#define GPIO_SW_EN 41
#define GPIO_SW_RESET 45

#define DEBOUNCE_DEFAULT_TIME_USEC 50000ul
#define DEBOUNCE_STATE_NOT_DEFINED -1

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sfera Labs - http://sferalabs.cc");
MODULE_DESCRIPTION("Iono Pi Max driver module");
MODULE_VERSION("1.8");

struct DeviceAttrRegSpecs {
	uint16_t reg;
	uint8_t len;
	bool maskedReg;
	uint32_t mask;
	uint8_t shift;
	bool sign;
	const char *vals;
};

struct DebounceBean {
	int gpio;
	const char* debIrqDevName;
	int debValue;
	int debPastValue;
	int debIrqNum;
	struct timespec64 lastDebIrqTs;
	unsigned long debOnMinTime_usec;
	unsigned long debOffMinTime_usec;
	unsigned long debOnStateCnt;
	unsigned long debOffStateCnt;
};

struct DeviceAttrBean {
	struct device_attribute devAttr;
	struct DeviceAttrRegSpecs regSpecs;
	struct DeviceAttrRegSpecs regSpecsStore;
	int gpioMode;
	int gpio;
	bool invert;
	struct DebounceBean *debBean;
};

struct DeviceBean {
	char *name;
	struct device *pDevice;
	struct DeviceAttrBean *devAttrBeans;
};

struct WiegandLine {
	int gpio;
	unsigned int irq;
	bool irqRequested;
	bool wasLow;
};

struct WiegandBean {
	struct WiegandLine d0;
	struct WiegandLine d1;
	struct WiegandLine *activeLine;
	unsigned long pulseIntervalMin_usec;
	unsigned long pulseIntervalMax_usec;
	unsigned long pulseWidthMin_usec;
	unsigned long pulseWidthMax_usec;
	bool enabled;
	uint64_t data;
	int bitCount;
	int noise;
	struct timespec64 lastBitTs;
};

static struct class *pDeviceClass;

static ssize_t devAttrGpio_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrGpio_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);

static ssize_t devAttrGpioDeb_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrGpioDebMsOn_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrGpioDebMsOn_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);

static ssize_t devAttrGpioDebMsOff_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrGpioDebMsOff_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);

static ssize_t devAttrGpioDebOnCnt_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrGpioDebOffCnt_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrGpioBlink_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);

static ssize_t devAttrDtMode_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrDtMode_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);

static ssize_t devAttrI2c_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);

static ssize_t devAttrI2c_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrAxMode_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);

static ssize_t devAttrAxMode_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrUpsBatteryV_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);

static ssize_t devAttrUpsBatteryV_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrSdEnabled_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);

static ssize_t devAttrSdEnabled_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrMcuFwVersion_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrMcuConfig_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);

static ssize_t devAttrWiegandEnabled_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrWiegandEnabled_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);

static ssize_t devAttrWiegandData_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrWiegandNoise_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrWiegandPulseIntervalMin_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrWiegandPulseIntervalMin_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);

static ssize_t devAttrWiegandPulseIntervalMax_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrWiegandPulseIntervalMax_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);

static ssize_t devAttrWiegandPulseWidthMin_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrWiegandPulseWidthMin_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);

static ssize_t devAttrWiegandPulseWidthMax_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrWiegandPulseWidthMax_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);

static ssize_t mcuI2cRead_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t mcuI2cRead_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);

static ssize_t mcuI2cWrite_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);

static ssize_t devAttrSerialRs232Rs485Inv_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrSerialRs232Rs485Inv_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);

static const char VALS_DIGITAL_OUTS_STATUS[] = { 4, '0', '1', 'F', 'S' };
static const char VALS_WATCHDOG_ENABLE_MODE[] = { 2, 'D', 'A' };
static const char VALS_POWER_DOWN_ENABLE_MODE[] = { 2, 'I', 'A' };
static const char VALS_POWER_UP_MODE[] = { 2, 'M', 'A' };
static const char VALS_SD_SDX_ROUTING[] = { 2, 'A', 'B' };
static const char VALS_ANALOG_OUTS_MODE[] = { 2, 'I', 'V' };

static bool dt1enabled = false;
static bool dt2enabled = false;
static bool dt3enabled = false;
static bool dt4enabled = false;

static uint8_t fwVerMajor;
static uint8_t fwVerMinor;

static struct WiegandBean w1 = {
	.d0 = {
		.gpio = GPIO_DT1,
		.irqRequested = false,
	},
	.d1 = {
		.gpio = GPIO_DT2,
		.irqRequested = false,
	},
	.enabled = false,
	.pulseWidthMin_usec = 10,
	.pulseWidthMax_usec = 150,
	.pulseIntervalMin_usec = 1200,
	.pulseIntervalMax_usec = 2700,
	.noise = 0,
};

static struct WiegandBean w2 = {
	.d0 = {
		.gpio = GPIO_DT3,
		.irqRequested = false,
	},
	.d1 = {
		.gpio = GPIO_DT4,
		.irqRequested = false,
	},
	.enabled = false,
	.pulseWidthMin_usec = 10,
	.pulseWidthMax_usec = 150,
	.pulseIntervalMin_usec = 1200,
	.pulseIntervalMax_usec = 2700,
	.noise = 0,
};

enum digital_in {
	DI1 = 0,
	DI2,
	DI3,
	DI4,
};

static struct DebounceBean debounceBeans[] = {
	[DI1] = {
		.gpio = GPIO_DI1,
		.debIrqDevName = "ionopimax_di1_deb",
		.debOnMinTime_usec = DEBOUNCE_DEFAULT_TIME_USEC,
		.debOffMinTime_usec = DEBOUNCE_DEFAULT_TIME_USEC,
		.debOnStateCnt = 0,
		.debOffStateCnt = 0,
	},

	[DI2] = {
		.gpio = GPIO_DI2,
		.debIrqDevName = "ionopimax_di2_deb",
		.debOnMinTime_usec = DEBOUNCE_DEFAULT_TIME_USEC,
		.debOffMinTime_usec = DEBOUNCE_DEFAULT_TIME_USEC,
		.debOnStateCnt = 0,
		.debOffStateCnt = 0,
	},

	[DI3] = {
		.gpio = GPIO_DI3,
		.debIrqDevName = "ionopimax_di3_deb",
		.debOnMinTime_usec = DEBOUNCE_DEFAULT_TIME_USEC,
		.debOffMinTime_usec = DEBOUNCE_DEFAULT_TIME_USEC,
		.debOnStateCnt = 0,
		.debOffStateCnt = 0,
	},

	[DI4] = {
		.gpio = GPIO_DI4,
		.debIrqDevName = "ionopimax_di4_deb",
		.debOnMinTime_usec = DEBOUNCE_DEFAULT_TIME_USEC,
		.debOffMinTime_usec = DEBOUNCE_DEFAULT_TIME_USEC,
		.debOnStateCnt = 0,
		.debOffStateCnt = 0,
	},

	{ }
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
		.gpioMode = GPIO_MODE_OUT,
		.gpio = 40,
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
		.gpioMode = GPIO_MODE_OUT,
		.gpio = 40,
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
		.gpioMode = GPIO_MODE_IN,
		.gpio = 38,
		.invert = true,
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
			.show = devAttrDtMode_show,
			.store = devAttrDtMode_store,
		},
		.gpio = GPIO_DT1,
	},

	{
		.devAttr = {
			.attr = {
				.name = "dt2_mode",
				.mode = 0660,
			},
			.show = devAttrDtMode_show,
			.store = devAttrDtMode_store,
		},
		.gpio = GPIO_DT2,
	},

	{
		.devAttr = {
			.attr = {
				.name = "dt3_mode",
				.mode = 0660,
			},
			.show = devAttrDtMode_show,
			.store = devAttrDtMode_store,
		},
		.gpio = GPIO_DT3,
	},

	{
		.devAttr = {
			.attr = {
				.name = "dt4_mode",
				.mode = 0660,
			},
			.show = devAttrDtMode_show,
			.store = devAttrDtMode_store,
		},
		.gpio = GPIO_DT4,
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
		.gpioMode = 0,
		.gpio = GPIO_DT1,
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
		.gpioMode = 0,
		.gpio = GPIO_DT2,
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
		.gpioMode = 0,
		.gpio = GPIO_DT3,
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
		.gpioMode = 0,
		.gpio = GPIO_DT4,
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
		.gpioMode = GPIO_MODE_IN,
		.gpio = GPIO_DI1,
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
		.gpioMode = GPIO_MODE_IN,
		.gpio = GPIO_DI2,
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
		.gpioMode = GPIO_MODE_IN,
		.gpio = GPIO_DI3,
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
		.gpioMode = GPIO_MODE_IN,
		.gpio = GPIO_DI4,
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
		.debBean = &debounceBeans[DI1],
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
		.debBean = &debounceBeans[DI2],
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
		.debBean = &debounceBeans[DI3],
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
		.debBean = &debounceBeans[DI4],
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
		.debBean = &debounceBeans[DI1],
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
		.debBean = &debounceBeans[DI1],
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
		.debBean = &debounceBeans[DI2],
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
		.debBean = &debounceBeans[DI2],
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
		.debBean = &debounceBeans[DI3],
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
		.debBean = &debounceBeans[DI3],
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
		.debBean = &debounceBeans[DI4],
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
		.debBean = &debounceBeans[DI4],
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
		.debBean = &debounceBeans[DI1],
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
		.debBean = &debounceBeans[DI1],
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
		.debBean = &debounceBeans[DI2],
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
		.debBean = &debounceBeans[DI2],
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
		.debBean = &debounceBeans[DI3],
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
		.debBean = &debounceBeans[DI3],
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
		.debBean = &debounceBeans[DI4],
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
		.debBean = &debounceBeans[DI4],
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
		.gpioMode = GPIO_MODE_OUT,
		.gpio = 39,
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
		.gpioMode = GPIO_MODE_OUT,
		.gpio = 32,
	},

	{
		.devAttr = {
			.attr = {
				.name = "expired",
				.mode = 0440,
			},
			.show = devAttrGpio_show,
			.store = NULL,
		},
		.gpioMode = GPIO_MODE_IN,
		.gpio = 17,
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
		.gpioMode = GPIO_MODE_OUT,
		.gpio = 18,
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
		.gpioMode = GPIO_MODE_OUT,
		.gpio = 30,
		.invert = true,
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
		.gpioMode = GPIO_MODE_IN,
		.gpio = 0,
		.invert = true,
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
		.gpioMode = GPIO_MODE_OUT,
		.gpio = 31,
		.invert = true,
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
		.gpioMode = GPIO_MODE_IN,
		.gpio = 1,
		.invert = true,
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
		.gpioMode = GPIO_MODE_OUT,
		.gpio = GPIO_SW_EN,
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
		.gpioMode = GPIO_MODE_OUT,
		.gpio = GPIO_SW_RESET,
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

static char toUpper(char c) {
	if (c >= 97 && c <= 122) {
		return c - 32;
	}
	return c;
}

static int gpioSetup(struct DeviceBean* db, struct DeviceAttrBean* dab) {
	int result = 0;
	char gpioReqName[128];
	char *gpioReqNamePart;

	strcpy(gpioReqName, "ionopimax_");
	gpioReqNamePart = gpioReqName + strlen("ionopimax_");

	strcpy(gpioReqNamePart, db->name);
	gpioReqNamePart[strlen(db->name)] = '_';

	strcpy(gpioReqNamePart + strlen(db->name) + 1, dab->devAttr.attr.name);

	gpio_request(dab->gpio, gpioReqName);
	if (dab->gpioMode == GPIO_MODE_OUT) {
		result = gpio_direction_output(dab->gpio, false);
	} else if (dab->gpioMode == GPIO_MODE_IN) {
		result = gpio_direction_input(dab->gpio);
	}

	return result;
}

static struct DeviceBean* devGetBean(struct device* dev) {
	int di;
	di = 0;
	while (devices[di].name != NULL) {
		if (dev == devices[di].pDevice) {
			return &devices[di];
		}
		di++;
	}
	return NULL;
}

static struct DeviceAttrBean* devAttrGetBean(struct DeviceBean* devBean,
		struct device_attribute* attr) {
	int ai;
	if (devBean == NULL) {
		return NULL;
	}
	ai = 0;
	while (devBean->devAttrBeans[ai].devAttr.attr.name != NULL) {
		if (attr == &devBean->devAttrBeans[ai].devAttr) {
			return &devBean->devAttrBeans[ai];
		}
		ai++;
	}
	return NULL;
}

static unsigned long long to_usec(struct timespec64 *t) {
	return (t->tv_sec * 1000000) + (t->tv_nsec / 1000);
}

static unsigned long long diff_usec(struct timespec64 *t1, struct timespec64 *t2) {
	struct timespec64 diff;
	diff = timespec64_sub(*t2, *t1);
	return to_usec(&diff);
}

static ssize_t devAttrGpio_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	int val;
	struct DeviceAttrBean* dab;
	dab = devAttrGetBean(devGetBean(dev), attr);
	if (dab == NULL || dab->gpio < 0) {
		return -EFAULT;
	}
	if (dab->gpioMode != GPIO_MODE_IN && dab->gpioMode != GPIO_MODE_OUT) {
		return -EPERM;
	}
	val = gpio_get_value(dab->gpio);
	if (dab->invert) {
		val = val == 0 ? 1 : 0;
	}
	return sprintf(buf, "%d\n", val);
}

static ssize_t devAttrGpio_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
	bool val;
	struct DeviceAttrBean* dab;
	dab = devAttrGetBean(devGetBean(dev), attr);
	if (dab == NULL || dab->gpio < 0) {
		return -EFAULT;
	}
	if (dab->gpioMode != GPIO_MODE_OUT) {
		return -EPERM;
	}
	if (kstrtobool(buf, &val) < 0) {
		if (toUpper(buf[0]) == 'E') { // Enable
			val = true;
		} else if (toUpper(buf[0]) == 'D') { // Disable
			val = false;
		} else if (toUpper(buf[0]) == 'F' || toUpper(buf[0]) == 'T') { // Flip/Toggle
			val = gpio_get_value(dab->gpio) ==
					(dab->invert ? 0 : 1) ? false : true;
		} else {
			return -EINVAL;
		}
	}
	if (dab->invert) {
		val = !val;
	}
	gpio_set_value(dab->gpio, val ? 1 : 0);
	return count;
}

static ssize_t devAttrGpioDeb_show(struct device *dev,
		struct device_attribute *attr, char *buf) {
	struct timespec64 now;
	unsigned long long diff;
	int actualGPIOStatus;
	struct DeviceAttrBean *dab;
	int res;

	ktime_get_raw_ts64(&now);
	dab = container_of(attr, struct DeviceAttrBean, devAttr);
	diff = diff_usec((struct timespec64*) &dab->debBean->lastDebIrqTs,
			&now);
	actualGPIOStatus = gpio_get_value(dab->debBean->gpio);
	if (actualGPIOStatus) {
		if (diff >= dab->debBean->debOnMinTime_usec) {
			res = actualGPIOStatus;
		} else {
			res = dab->debBean->debValue;
		}
	} else {
		if (diff >= dab->debBean->debOffMinTime_usec) {
			res = actualGPIOStatus;
		} else {
			res = dab->debBean->debValue;
		}
	}
	return sprintf(buf, "%d\n", res);
}

static ssize_t devAttrGpioDebMsOn_show(struct device *dev,
		struct device_attribute *attr, char *buf) {
	struct DeviceAttrBean* dab = container_of(attr, struct DeviceAttrBean,
			devAttr);

	return sprintf(buf, "%lu\n",
			dab->debBean->debOnMinTime_usec / 1000);
}

static ssize_t devAttrGpioDebMsOff_show(struct device *dev,
		struct device_attribute *attr, char *buf) {
	struct DeviceAttrBean* dab = container_of(attr, struct DeviceAttrBean,
			devAttr);

	return sprintf(buf, "%lu\n",
			dab->debBean->debOffMinTime_usec / 1000);
}

static ssize_t devAttrGpioDebMsOn_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count) {
	struct DeviceAttrBean* dab = container_of(attr, struct DeviceAttrBean,
			devAttr);
	unsigned int val;

	int ret = kstrtouint(buf, 10, &val);
	if (ret < 0) {
		return ret;
	}
	dab->debBean->debOnMinTime_usec = val * 1000;
	dab->debBean->debOnStateCnt = 0;
	dab->debBean->debOffStateCnt = 0;
	dab->debBean->debValue = DEBOUNCE_STATE_NOT_DEFINED;
	return count;
}

static ssize_t devAttrGpioDebMsOff_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count) {
	struct DeviceAttrBean* dab = container_of(attr, struct DeviceAttrBean,
			devAttr);
	unsigned int val;

	int ret = kstrtouint(buf, 10, &val);
	if (ret < 0) {
		return ret;
	}
	dab->debBean->debOffMinTime_usec = val * 1000;
	dab->debBean->debOnStateCnt = 0;
	dab->debBean->debOffStateCnt = 0;
	dab->debBean->debValue = DEBOUNCE_STATE_NOT_DEFINED;
	return count;
}

static ssize_t devAttrGpioDebOnCnt_show(struct device *dev,
		struct device_attribute *attr, char *buf) {
	struct DeviceAttrBean* dab = container_of(attr, struct DeviceAttrBean,
			devAttr);
	struct timespec64 now;
	unsigned long long diff;
	int actualGPIOStatus;
	unsigned long res;

	ktime_get_raw_ts64(&now);
	diff = diff_usec(
			(struct timespec64*) &dab->debBean->lastDebIrqTs, &now);

	actualGPIOStatus = gpio_get_value(dab->debBean->gpio);
	if (dab->debBean->debPastValue == actualGPIOStatus
			&& actualGPIOStatus
			&& diff >= dab->debBean->debOnMinTime_usec
			&& actualGPIOStatus != dab->debBean->debValue) {
		res = dab->debBean->debOnStateCnt + 1;
	} else {
		res = dab->debBean->debOnStateCnt;
	}

	return sprintf(buf, "%lu\n", res);
}

static ssize_t devAttrGpioDebOffCnt_show(struct device *dev,
		struct device_attribute *attr, char *buf) {
	struct DeviceAttrBean* dab = container_of(attr, struct DeviceAttrBean,
			devAttr);
	struct timespec64 now;
	unsigned long long diff;
	int actualGPIOStatus;
	unsigned long res;

	ktime_get_raw_ts64(&now);
	diff = diff_usec(
			(struct timespec64*) &dab->debBean->lastDebIrqTs, &now);

	actualGPIOStatus = gpio_get_value(dab->debBean->gpio);
	if (dab->debBean->debPastValue == actualGPIOStatus
			&& !actualGPIOStatus
			&& diff >= dab->debBean->debOffMinTime_usec
			&& actualGPIOStatus != dab->debBean->debValue) {
		res = dab->debBean->debOffStateCnt + 1;
	} else {
		res = dab->debBean->debOffStateCnt;
	}

	return sprintf(buf, "%lu\n", res);
}

static ssize_t devAttrGpioBlink_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
	int i;
	struct DeviceAttrBean* dab;
	long on = 0;
	long off = 0;
	long rep = 1;
	char *end = NULL;
	dab = devAttrGetBean(devGetBean(dev), attr);
	if (dab == NULL || dab->gpioMode == 0) {
		return -EFAULT;
	}
	if (dab->gpio < 0) {
		return -EFAULT;
	}
	on = simple_strtol(buf, &end, 10);
	if (++end < buf + count) {
		off = simple_strtol(end, &end, 10);
		if (++end < buf + count) {
			rep = simple_strtol(end, NULL, 10);
		}
	}
	if (rep < 1) {
		rep = 1;
	}
	if (on > 0) {
		for (i = 0; i < rep; i++) {
			gpio_set_value(dab->gpio, dab->invert ? 0 : 1);
			msleep(on);
			gpio_set_value(dab->gpio, dab->invert ? 1 : 0);
			if (i < rep - 1) {
				msleep(off);
			}
		}
	}
	return count;
}

static ssize_t devAttrDtMode_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	struct DeviceAttrBean* dab;
	dab = devAttrGetBean(devGetBean(dev), attr);
	if (dab == NULL || dab->gpio < 0) {
		return -EFAULT;
	}
	if (dab->gpioMode == GPIO_MODE_IN) {
		return sprintf(buf, "in\n");
	}
	if (dab->gpioMode == GPIO_MODE_OUT) {
		return sprintf(buf, "out\n");
	}
	return sprintf(buf, "x\n");
}

static ssize_t devAttrDtMode_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
	size_t ret;
	int ai;
	bool* enabled;
	struct DeviceBean* db;
	struct DeviceAttrBean* dab;
	db = devGetBean(dev);
	dab = devAttrGetBean(db, attr);
	if (dab == NULL) {
		return -EFAULT;
	}

	if (dab->gpio == GPIO_DT1) {
		if (w1.enabled) {
			return -EBUSY;
		}
		enabled = &dt1enabled;
	} else if (dab->gpio == GPIO_DT2) {
		if (w1.enabled) {
			return -EBUSY;
		}
		enabled = &dt2enabled;
	} else if (dab->gpio == GPIO_DT3) {
		if (w2.enabled) {
			return -EBUSY;
		}
		enabled = &dt3enabled;
	} else if (dab->gpio == GPIO_DT4) {
		if (w2.enabled) {
			return -EBUSY;
		}
		enabled = &dt4enabled;
	} else {
		return -EFAULT;
	}

	if (toUpper(buf[0]) == 'I') {
		dab->gpioMode = GPIO_MODE_IN;
	} else if (toUpper(buf[0]) == 'O') {
		dab->gpioMode = GPIO_MODE_OUT;
	} else {
		dab->gpioMode = 0;
	}

	ret = count;
	gpio_free(dab->gpio);
	if (dab->gpioMode != 0) {
		(*enabled) = true;
		if (gpioSetup(db, dab)) {
			dab->gpioMode = 0;
			gpio_free(dab->gpio);
			ret = -EFAULT;
		}
	} else {
		(*enabled) = false;
	}

	ai = 0;
	while (db->devAttrBeans[ai].devAttr.attr.name != NULL) {
		if (db->devAttrBeans[ai].gpio == dab->gpio) {
			db->devAttrBeans[ai].gpioMode = dab->gpioMode;
		}
		ai++;
	}

	return ret;
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

static ssize_t devAttrI2c_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	int32_t res;
	struct DeviceAttrRegSpecs *specs;
	struct DeviceAttrBean* dab = devAttrGetBean(devGetBean(dev), attr);
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

static ssize_t devAttrI2c_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
	long val;
	int ret;
	uint16_t i;
	int32_t res;
	char valC;
	struct DeviceAttrRegSpecs *specs;
	struct DeviceAttrBean* dab = devAttrGetBean(devGetBean(dev), attr);
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

	res = ionopimax_i2c_write_segment((uint8_t) specs->reg, specs->maskedReg,
			specs->mask, specs->shift, (uint16_t) val);

	if (res < 0) {
		return res;
	}

	return count;
}

static ssize_t devAttrAxMode_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	int32_t res;
	struct DeviceAttrRegSpecs *specs;
	struct DeviceAttrBean* dab = devAttrGetBean(devGetBean(dev), attr);
	if (dab == NULL) {
		return -EFAULT;
	}
	specs = &dab->regSpecs;
	if (specs->reg == 0) {
		return -EFAULT;
	}

	if (fwVerMajor > 1 || fwVerMinor >= 3) {
		res = ionopimax_i2c_read_segment((uint8_t) specs->reg + 1, specs->len,
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

static ssize_t devAttrAxMode_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
	int32_t res;
	char valC;
	uint16_t en, mode;
	struct DeviceAttrRegSpecs *specs;
	struct DeviceAttrBean* dab = devAttrGetBean(devGetBean(dev), attr);
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
				specs->maskedReg, specs->mask, specs->shift - 4, (uint16_t) en);

		if (res < 0) {
			return res;
		}
	}

	if (mode != 0xff) {
		res = ionopimax_i2c_write_segment((uint8_t) specs->reg,
				specs->maskedReg, specs->mask, specs->shift, (uint16_t) mode);

		if (res < 0) {
			return res;
		}
	}

	return count;
}

static ssize_t devAttrUpsBatteryV_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
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

static ssize_t devAttrUpsBatteryV_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
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

static ssize_t devAttrSdEnabled_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	int32_t res;
	struct DeviceAttrRegSpecs *specs;
	struct DeviceAttrBean* dab = devAttrGetBean(devGetBean(dev), attr);
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

static ssize_t devAttrSdEnabled_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
	long val;
	int ret;
	int32_t res;
	struct DeviceAttrRegSpecs *specs;
	struct DeviceAttrBean* dab = devAttrGetBean(devGetBean(dev), attr);
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

	res = ionopimax_i2c_write_segment((uint8_t) specs->reg, specs->maskedReg,
			specs->mask, specs->shift, (uint16_t) val);

	if (res < 0) {
		return res;
	}

	return count;
}

static struct WiegandBean* getWiegandBean(struct device* dev,
		struct device_attribute* attr) {
	struct DeviceAttrBean* dab;
	dab = devAttrGetBean(devGetBean(dev), attr);
	if (dab->devAttr.attr.name[1] == '1') {
		return &w1;
	} else {
		return &w2;
	}
}

static ssize_t devAttrWiegandEnabled_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	struct WiegandBean* w;
	w = getWiegandBean(dev, attr);
	return sprintf(buf, w->enabled ? "1\n" : "0\n");
}

static void wiegandReset(struct WiegandBean* w) {
	w->enabled = true;
	w->data = 0;
	w->bitCount = 0;
	w->activeLine = NULL;
	w->d0.wasLow = false;
	w->d1.wasLow = false;
}

static irq_handler_t wiegandDataIrqHandler(unsigned int irq, void *dev_id,
		struct pt_regs *regs) {
	bool isLow;
	struct timespec64 now;
	unsigned long long diff;
	struct WiegandBean* w;
	struct WiegandLine* l = NULL;

	if (w1.enabled) {
		if (irq == w1.d0.irq) {
			w = &w1;
			l = &w1.d0;
		} else if (irq == w1.d1.irq) {
			w = &w1;
			l = &w1.d1;
		}
	}

	if (w2.enabled) {
		if (irq == w2.d0.irq) {
			w = &w2;
			l = &w2.d0;
		} else if (irq == w2.d1.irq) {
			w = &w2;
			l = &w2.d1;
		}
	}

	if (l == NULL) {
		return (irq_handler_t) IRQ_HANDLED;
	}

	isLow = gpio_get_value(l->gpio) == 0;

	ktime_get_raw_ts64(&now);

	if (l->wasLow == isLow) {
		// got the interrupt but didn't change state. Maybe a fast pulse
		if (w->noise == 0) {
			w->noise = 10;
		}
		return (irq_handler_t) IRQ_HANDLED;
	}

	l->wasLow = isLow;

	if (isLow) {
		if (w->bitCount != 0) {
			diff = diff_usec((struct timespec64 *) &(w->lastBitTs), &now);

			if (diff < w->pulseIntervalMin_usec) {
				// pulse too early
				w->noise = 11;
				goto noise;
			}

			if (diff > w->pulseIntervalMax_usec) {
				w->data = 0;
				w->bitCount = 0;
			}
		}

		if (w->activeLine != NULL) {
			// there's movement on both lines
			w->noise = 12;
			goto noise;
		}

		w->activeLine = l;

		w->lastBitTs.tv_sec = now.tv_sec;
		w->lastBitTs.tv_nsec = now.tv_nsec;

	} else {
		if (w->activeLine != l) {
			// there's movement on both lines or previous noise
			w->noise = 13;
			goto noise;
		}

		w->activeLine = NULL;

		if (w->bitCount >= WIEGAND_MAX_BITS) {
			return (irq_handler_t) IRQ_HANDLED;
		}

		diff = diff_usec((struct timespec64 *) &(w->lastBitTs), &now);
		if (diff < w->pulseWidthMin_usec) {
			// pulse too short
			w->noise = 14;
			goto noise;
		}
		if (diff > w->pulseWidthMax_usec) {
			// pulse too long
			w->noise = 15;
			goto noise;
		}

		w->data <<= 1;
		if (l == &w->d1) {
			w->data |= 1;
		}
		w->bitCount++;
	}

	return (irq_handler_t) IRQ_HANDLED;

	noise:
	wiegandReset(w);
	return (irq_handler_t) IRQ_HANDLED;
}

static void wiegandDisable(struct WiegandBean* w) {
	if (w->enabled) {
		gpio_free(w->d0.gpio);
		gpio_free(w->d1.gpio);

		if (w->d0.irqRequested) {
			free_irq(w->d0.irq, NULL);
			w->d0.irqRequested = false;
		}

		if (w->d1.irqRequested) {
			free_irq(w->d1.irq, NULL);
			w->d1.irqRequested = false;
		}

		w->enabled = false;
	}
}

static ssize_t devAttrWiegandEnabled_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
	struct WiegandBean* w;
	bool enable;
	bool isW1;
	int result = 0;
	char reqName[] = "ionopimax_wN_dN";

	w = getWiegandBean(dev, attr);

	if (buf[0] == '0') {
		enable = false;
	} else if (buf[0] == '1') {
		enable = true;
	} else {
		return -EINVAL;
	}

	if (enable) {
		isW1 = w == &w1;
		if (isW1) {
			if (dt1enabled || dt2enabled) {
				return -EBUSY;
			}
			reqName[11] = '1';
		} else {
			if (dt3enabled || dt4enabled) {
				return -EBUSY;
			}
			reqName[11] = '2';
		}

		reqName[14] = '0';
		gpio_request(w->d0.gpio, reqName);
		reqName[14] = '1';
		gpio_request(w->d1.gpio, reqName);

		result = gpio_direction_input(w->d0.gpio);
		if (!result) {
			result = gpio_direction_input(w->d1.gpio);
		}

		if (result) {
			printk(
			KERN_ALERT "ionopimax: * | error setting up wiegand GPIOs\n");
			enable = false;
		} else {
			gpio_set_debounce(w->d0.gpio, 0);
			gpio_set_debounce(w->d1.gpio, 0);

			w->d0.irq = gpio_to_irq(w->d0.gpio);
			w->d1.irq = gpio_to_irq(w->d1.gpio);

			reqName[14] = '0';
			result = request_irq(w->d0.irq,
					(irq_handler_t) wiegandDataIrqHandler,
					IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
					reqName, NULL);

			if (result) {
				printk(
						KERN_ALERT "ionopimax: * | error registering wiegand D0 irq handler\n");
				enable = false;
			} else {
				w->d0.irqRequested = true;

				reqName[14] = '1';
				result = request_irq(w->d1.irq,
						(irq_handler_t) wiegandDataIrqHandler,
						IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
						reqName, NULL);

				if (result) {
					printk(
							KERN_ALERT "ionopimax: * | error registering wiegand D1 irq handler\n");
					enable = false;
				} else {
					w->d1.irqRequested = true;
				}
			}
		}
	}

	if (enable) {
		w->noise = 0;
		wiegandReset(w);
	} else {
		wiegandDisable(w);
	}

	if (result) {
		return result;
	}
	return count;
}

static ssize_t devAttrWiegandData_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	struct timespec64 now;
	unsigned long long diff;
	struct WiegandBean* w;
	w = getWiegandBean(dev, attr);

	if (!w->enabled) {
		return -ENODEV;
	}

	ktime_get_raw_ts64(&now);
	diff = diff_usec((struct timespec64 *) &(w->lastBitTs), &now);
	if (diff <= w->pulseIntervalMax_usec) {
		return -EBUSY;
	}

	return sprintf(buf, "%llu %d %llu\n", to_usec(&w->lastBitTs), w->bitCount,
			w->data);
}

static ssize_t devAttrWiegandNoise_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	struct WiegandBean* w;
	int noise;
	w = getWiegandBean(dev, attr);
	noise = w->noise;

	w->noise = 0;

	return sprintf(buf, "%d\n", noise);
}

static ssize_t devAttrWiegandPulseIntervalMin_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	struct WiegandBean* w;
	w = getWiegandBean(dev, attr);

	return sprintf(buf, "%lu\n", w->pulseIntervalMin_usec);
}

static ssize_t devAttrWiegandPulseIntervalMin_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
	int ret;
	unsigned long val;
	struct WiegandBean* w;
	w = getWiegandBean(dev, attr);

	ret = kstrtol(buf, 10, &val);
	if (ret < 0) {
		return ret;
	}

	w->pulseIntervalMin_usec = val;

	return count;
}

static ssize_t devAttrWiegandPulseIntervalMax_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	struct WiegandBean* w;
	w = getWiegandBean(dev, attr);

	return sprintf(buf, "%lu\n", w->pulseIntervalMax_usec);
}

static ssize_t devAttrWiegandPulseIntervalMax_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
	int ret;
	unsigned long val;
	struct WiegandBean* w;
	w = getWiegandBean(dev, attr);

	ret = kstrtol(buf, 10, &val);
	if (ret < 0) {
		return ret;
	}

	w->pulseIntervalMax_usec = val;

	return count;
}

static ssize_t devAttrWiegandPulseWidthMin_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	struct WiegandBean* w;
	w = getWiegandBean(dev, attr);

	return sprintf(buf, "%lu\n", w->pulseWidthMin_usec);
}

static ssize_t devAttrWiegandPulseWidthMin_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
	int ret;
	unsigned long val;
	struct WiegandBean* w;
	w = getWiegandBean(dev, attr);

	ret = kstrtol(buf, 10, &val);
	if (ret < 0) {
		return ret;
	}

	w->pulseWidthMin_usec = val;

	return count;
}

static ssize_t devAttrWiegandPulseWidthMax_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	struct WiegandBean* w;
	w = getWiegandBean(dev, attr);

	return sprintf(buf, "%lu\n", w->pulseWidthMax_usec);
}

static ssize_t devAttrWiegandPulseWidthMax_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
	int ret;
	unsigned long val;
	struct WiegandBean* w;
	w = getWiegandBean(dev, attr);

	ret = kstrtol(buf, 10, &val);
	if (ret < 0) {
		return ret;
	}

	w->pulseWidthMax_usec = val;

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

static ssize_t devAttrMcuFwVersion_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	int32_t res;

	res = getFwVersion();
	if (res < 0) {
		return res;
	}

	return sprintf(buf, "%d.%d\n", fwVerMajor, fwVerMinor);
}

static ssize_t devAttrMcuConfig_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
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

static ssize_t mcuI2cRead_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	return sprintf(buf, "0x%04x\n", mcuI2cReadVal);
}

static ssize_t mcuI2cRead_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
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

static ssize_t mcuI2cWrite_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
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

static int ionopimax_i2c_probe(struct i2c_client *client,
		const struct i2c_device_id *id) {
	struct ionopimax_i2c_data *data;

	data = devm_kzalloc(&client->dev, sizeof(struct ionopimax_i2c_data),
	GFP_KERNEL);
	if (!data) {
		return -ENOMEM;
	}

	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);

	ionopimax_i2c_client = client;

	printk(KERN_INFO "ionopimax: - | i2c probe addr=0x%02hx\n", client->addr);

	return 0;
}

static int ionopimax_i2c_remove(struct i2c_client *client) {
	struct ionopimax_i2c_data *data = i2c_get_clientdata(client);
	mutex_destroy(&data->update_lock);

	printk(KERN_INFO "ionopimax: - | i2c remove addr=0x%02hx\n", client->addr);

	return 0;
}

const struct of_device_id ionopimax_of_match[] = {
	{ .compatible = "sferalabs,ionopimax", },
	{ },
};
MODULE_DEVICE_TABLE(of, ionopimax_of_match);

static const struct i2c_device_id ionopimax_i2c_id[] = {
	{ "ionopimax", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, ionopimax_i2c_id);

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

static irqreturn_t gpio_deb_irq_handler(int irq, void *dev_id) {
	struct timespec64 now;
	int db = 0;
	unsigned long long diff;
	int actualGPIOStatus;

	ktime_get_raw_ts64(&now);

	while (debounceBeans[db].debIrqDevName != NULL) {
		if (debounceBeans[db].debIrqNum == irq && debounceBeans[db].gpio != 0) {
			actualGPIOStatus = gpio_get_value(debounceBeans[db].gpio);

			diff = diff_usec(
					(struct timespec64*) &debounceBeans[db].lastDebIrqTs, &now);

			if (debounceBeans[db].debPastValue == actualGPIOStatus) {
				return IRQ_HANDLED;
			}

			debounceBeans[db].debPastValue = actualGPIOStatus;

			if (actualGPIOStatus == debounceBeans[db].debValue
					|| debounceBeans[db].debValue == DEBOUNCE_STATE_NOT_DEFINED) {
				if (actualGPIOStatus) {
					if (diff >= debounceBeans[db].debOffMinTime_usec) {
						debounceBeans[db].debValue = 0;
						debounceBeans[db].debOffStateCnt++;
					}
				} else {
					if (diff >= debounceBeans[db].debOnMinTime_usec) {
						debounceBeans[db].debValue = 1;
						debounceBeans[db].debOnStateCnt++;
					}
				}
			}

			debounceBeans[db].lastDebIrqTs = now;
			break;
		}
		db++;
	}

	return IRQ_HANDLED;
}

static void cleanup(void) {
	int di, ai;

	i2c_del_driver(&ionopimax_i2c_driver);

	gpio_free(GPIO_SW_RESET);
	gpio_free(GPIO_SW_EN);

	di = 0;
	while (devices[di].name != NULL) {
		if (devices[di].pDevice && !IS_ERR(devices[di].pDevice)) {
			ai = 0;
			while (devices[di].devAttrBeans[ai].devAttr.attr.name != NULL) {
				device_remove_file(devices[di].pDevice,
						&devices[di].devAttrBeans[ai].devAttr);
				if (devices[di].devAttrBeans[ai].gpioMode != 0) {
					gpio_free(devices[di].devAttrBeans[ai].gpio);
				}
				if (devices[di].devAttrBeans[ai].debBean != NULL) {
					free_irq(devices[di].devAttrBeans[ai].debBean->debIrqNum,
					NULL);
				}
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
}

static int __init ionopimax_init(void) {
	int result = 0;
	int di, ai;

	printk(KERN_INFO "ionopimax: - | init\n");

	i2c_add_driver(&ionopimax_i2c_driver);
	ateccAddDriver();

	pDeviceClass = class_create(THIS_MODULE, "ionopimax");
	if (IS_ERR(pDeviceClass)) {
		printk(KERN_ALERT "ionopimax: * | failed to create device class\n");
		goto fail;
	}

	di = 0;
	while (devices[di].name != NULL) {
		devices[di].pDevice = device_create(pDeviceClass, NULL, 0, NULL,
				devices[di].name);
		if (IS_ERR(devices[di].pDevice)) {
			printk(KERN_ALERT "ionopimax: * | failed to create device '%s'\n",
					devices[di].name);
			goto fail;
		}

		ai = 0;
		while (devices[di].devAttrBeans[ai].devAttr.attr.name != NULL) {
			result = device_create_file(devices[di].pDevice,
					&devices[di].devAttrBeans[ai].devAttr);
			if (result) {
				printk(
						KERN_ALERT "ionopimax: * | failed to create device file '%s/%s'\n",
						devices[di].name,
						devices[di].devAttrBeans[ai].devAttr.attr.name);
				goto fail;
			}
			if (devices[di].devAttrBeans[ai].gpioMode != 0) {
				result = gpioSetup(&devices[di], &devices[di].devAttrBeans[ai]);
				if (result) {
					printk(
					KERN_ALERT "ionopimax: * | error setting up GPIO %d\n",
							devices[di].devAttrBeans[ai].gpio);
					goto fail;
				}
			}
			if (devices[di].devAttrBeans[ai].debBean != NULL) {
				if (!devices[di].devAttrBeans[ai].debBean->debIrqNum) {
					devices[di].devAttrBeans[ai].debBean->debIrqNum =
							gpio_to_irq(
									devices[di].devAttrBeans[ai].debBean->gpio);
					if (request_irq(
							devices[di].devAttrBeans[ai].debBean->debIrqNum,
							(void *) gpio_deb_irq_handler,
							IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
							devices[di].devAttrBeans[ai].debBean->debIrqDevName,
							NULL)) {
						printk(
								KERN_ALERT "ionopimax: * | cannot register IRQ of %s in device %s\n",
								devices[di].devAttrBeans[ai].devAttr.attr.name,
								devices[di].name);
						goto fail;
					}
					ktime_get_raw_ts64(
							&devices[di].devAttrBeans[ai].debBean->lastDebIrqTs);
					devices[di].devAttrBeans[ai].debBean->debValue =
					DEBOUNCE_STATE_NOT_DEFINED;
					devices[di].devAttrBeans[ai].debBean->debPastValue =
							gpio_get_value(
									devices[di].devAttrBeans[ai].debBean->gpio);
				}
			}
			ai++;
		}
		di++;
	}

	gpio_set_value(GPIO_SW_EN, 0);
	gpio_set_value(GPIO_SW_RESET, 1);

	if (getFwVersion() < 0) {
		goto fail;
	}

	printk(KERN_INFO "ionopimax: - | ready FW%d.%d\n", fwVerMajor, fwVerMinor);
	return 0;

	fail:
	printk(KERN_ALERT "ionopimax: * | init failed\n");
	cleanup();
	return -1;
}

static void __exit ionopimax_exit(void) {
	cleanup();
	printk(KERN_INFO "ionopimax: - | exit\n");
}

module_init(ionopimax_init);
module_exit(ionopimax_exit);
