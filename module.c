/*
 * ionopimax
 *
 *     Copyright (C) 2020 Sfera Labs S.r.l.
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

#define I2C_ADDR_LOCAL 0x35

#define WIEGAND_MAX_BITS 64

#define GPIO_MODE_IN 1
#define GPIO_MODE_OUT 2

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sfera Labs - http://sferalabs.cc");
MODULE_DESCRIPTION("Iono Pi Max driver module");
MODULE_VERSION("0.5");

struct DeviceAttrRegSpecs {
	int16_t reg;
	bool maskedReg;
	int16_t mask;
	uint8_t shift;
	bool sign;
	const char *vals;
};

struct DeviceAttrBean {
	struct device_attribute devAttr;
	struct DeviceAttrRegSpecs regSpecsShow;
	struct DeviceAttrRegSpecs regSpecsStore;
	int gpioMode;
	int gpio;
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
	struct timespec lastBitTs;
};

static struct class *pDeviceClass;

static ssize_t devAttrGpio_show(struct device* dev,
		struct device_attribute* attr, char *buf);

static ssize_t devAttrGpio_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);

static ssize_t devAttrGpioBlink_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);

static ssize_t devAttrI2c_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count);

static ssize_t devAttrI2c_show(struct device* dev,
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

static const char VALS_DIGITAL_OUTS_STATUS[] = { 4, '0', '1', 'F', 'A' };
static const char VALS_WATCHDOG_ENABLE_MODE[] = { 2, 'D', 'A' };
static const char VALS_POWER_DOWN_ENABLE_MODE[] = { 2, 'I', 'A' };
static const char VALS_POWER_UP_MODE[] = { 2, 'M', 'A' };
static const char VALS_SD_SDX_ROUTING[] = { 2, 'A', 'B' };
static const char VALS_ANALOG_OUTS_MODE[] = { 2, 'I', 'V' };

static struct WiegandBean w1 = {
	.d0 = {
		.gpio = 29,
		.irqRequested = false,
	},
	.d1 = {
		.gpio = 34,
		.irqRequested = false,
	},
	.enabled = false,
	.pulseWidthMin_usec = 10,
	.pulseWidthMax_usec = 150,
	.pulseIntervalMin_usec = 1200,
	.pulseIntervalMax_usec = 2700,
};

static struct WiegandBean w2 = {
	.d0 = {
		.gpio = 35,
		.irqRequested = false,
	},
	.d1 = {
		.gpio = 36,
		.irqRequested = false,
	},
	.enabled = false,
	.pulseWidthMin_usec = 10,
	.pulseWidthMax_usec = 150,
	.pulseIntervalMin_usec = 1200,
	.pulseIntervalMax_usec = 2700,
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

	{
		.devAttr = {
			.attr = { .name = NULL }
		}
	}
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
	},

	{
		.devAttr = {
			.attr = { .name = NULL }
		}
	}
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
		.regSpecsShow = {
			.reg = 64,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 65,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 66,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 67,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 68,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 69,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 70,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 71,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 72,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 73,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 74,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 75,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 76,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 77,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 78,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 79,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 80,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 81,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 82,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 83,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
		},
	},

	{
		.devAttr = {
			.attr = { .name = NULL }
		}
	}
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
		.regSpecsShow = {
			.reg = 32,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 32,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 1,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "av1_mode",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecsShow = {
		// TODO
				},
		.regSpecsStore = {
			.reg = -1,
		},
	},

	// TODO av1_mode ... ai4_mode

	{
		.devAttr = {
			.attr = {
				.name = "av1",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecsShow = {
			.reg = 33,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = true,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 34,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = true,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 35,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = true,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 36,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = true,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 37,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = true,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 38,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = true,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 39,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = true,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 40,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = true,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
		},
	},

	{
		.devAttr = {
			.attr = { .name = NULL }
		}
	}
};

static struct DeviceAttrBean devAttrBeansAnalogTemp[] = {
	{
		.devAttr = {
			.attr = {
				.name = "at1_mode",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecsShow = {
			.reg = 32,
			.maskedReg = false,
			.mask = 0b11,
			.shift = 12,
			.sign = false,
			.vals = NULL, // TODO use vals?
				},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 32,
			.maskedReg = false,
			.mask = 0b11,
			.shift = 14,
			.sign = false,
			.vals = NULL, // TODO use vals?
				},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 41,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = true,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 42,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = true,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
		},
	},

	{
		.devAttr = {
			.attr = { .name = NULL }
		}
	}
};

static struct DeviceAttrBean devAttrBeansDigitalIO[] = {
	/*
	 {
	 .devAttr = {
	 .attr = {
	 .name = "dt1_direction",
	 .mode = 0660,
	 },
	 .show = devAttrGpio_show, // TODO dtDirection_show
	 .store = devAttrGpio_store, // TODO dtDirection_store
	 },
	 .gpio = 29,
	 },

	 {
	 .devAttr = {
	 .attr = {
	 .name = "dt2_direction",
	 .mode = 0660,
	 },
	 .show = devAttrGpio_show, // TODO dtDirection_show
	 .store = devAttrGpio_store, // TODO dtDirection_store
	 },
	 .gpio = 34,
	 },

	 {
	 .devAttr = {
	 .attr = {
	 .name = "dt3_direction",
	 .mode = 0660,
	 },
	 .show = devAttrGpio_show, // TODO dtDirection_show
	 .store = devAttrGpio_store, // TODO dtDirection_store
	 },
	 .gpio = 35,
	 },

	 {
	 .devAttr = {
	 .attr = {
	 .name = "dt4_direction",
	 .mode = 0660,
	 },
	 .show = devAttrGpio_show, // TODO dtDirection_show
	 .store = devAttrGpio_store, // TODO dtDirection_store
	 },
	 .gpio = 36,
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
	 .gpioMode = GPIO_MODE_OUT, // TODO or NULL or GPIO_MODE_IN?
	 .gpio = 29,
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
	 .gpioMode = GPIO_MODE_OUT, // TODO or NULL or GPIO_MODE_IN?
	 .gpio = 34,
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
	 .gpioMode = GPIO_MODE_OUT, // TODO or NULL or GPIO_MODE_IN?
	 .gpio = 35,
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
	 .gpioMode = GPIO_MODE_OUT, // TODO or NULL or GPIO_MODE_IN?
	 .gpio = 36,
	 },
	 */

	{
		.devAttr = {
			.attr = { .name = NULL }
		}
	}
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
		.gpio = 16,
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
		.gpio = 19,
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
		.gpio = 20,
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
		.gpio = 21,
	},

	{
		.devAttr = {
			.attr = { .name = NULL }
		}
	}
};

static struct DeviceAttrBean devAttrBeansDigitalOut[] = {
	{
		.devAttr = {
			.attr = {
				.name = "o1",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecsShow = {
			.reg = 87,
			.maskedReg = false,
			.mask = 0b11,
			.shift = 0,
			.sign = false,
			.vals = VALS_DIGITAL_OUTS_STATUS,
		},
		.regSpecsStore = {
			.reg = 86,
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
		.regSpecsShow = {
			.reg = 87,
			.maskedReg = false,
			.mask = 0b11,
			.shift = 2,
			.sign = false,
			.vals = VALS_DIGITAL_OUTS_STATUS,
		},
		.regSpecsStore = {
			.reg = 86,
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
		.regSpecsShow = {
			.reg = 87,
			.maskedReg = false,
			.mask = 0b11,
			.shift = 4,
			.sign = false,
			.vals = VALS_DIGITAL_OUTS_STATUS,
		},
		.regSpecsStore = {
			.reg = 86,
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
		.regSpecsShow = {
			.reg = 87,
			.maskedReg = false,
			.mask = 0b11,
			.shift = 6,
			.sign = false,
			.vals = VALS_DIGITAL_OUTS_STATUS,
		},
		.regSpecsStore = {
			.reg = 86,
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
		.regSpecsShow = {
			.reg = 89,
			.maskedReg = false,
			.mask = 0b11,
			.shift = 0,
			.sign = false,
			.vals = VALS_DIGITAL_OUTS_STATUS,
		},
		.regSpecsStore = {
			.reg = 88,
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
		.regSpecsShow = {
			.reg = 89,
			.maskedReg = false,
			.mask = 0b11,
			.shift = 2,
			.sign = false,
			.vals = VALS_DIGITAL_OUTS_STATUS,
		},
		.regSpecsStore = {
			.reg = 88,
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
		.regSpecsShow = {
			.reg = 89,
			.maskedReg = false,
			.mask = 0b11,
			.shift = 4,
			.sign = false,
			.vals = VALS_DIGITAL_OUTS_STATUS,
		},
		.regSpecsStore = {
			.reg = 88,
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
		.regSpecsShow = {
			.reg = 89,
			.maskedReg = false,
			.mask = 0b11,
			.shift = 6,
			.sign = false,
			.vals = VALS_DIGITAL_OUTS_STATUS,
		},
		.regSpecsStore = {
			.reg = 88,
			.maskedReg = true,
			.mask = 0b1,
			.shift = 3,
			.sign = false,
			.vals = NULL,
		},
	},

	{
		.devAttr = {
			.attr = { .name = NULL }
		}
	}
};

static struct DeviceAttrBean devAttrBeansSysMon[] = {
	{
		.devAttr = {
			.attr = {
				.name = "power_enabled",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecsShow = {
			.reg = 45,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "mains_v",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecsShow = {
			.reg = 46,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "mains_i",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecsShow = {
			.reg = 47,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "battery_v",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecsShow = {
			.reg = 48,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "battery_i",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecsShow = {
			.reg = 49,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "vso_v",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecsShow = {
			.reg = 50,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "vso_i",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecsShow = {
			.reg = 51,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "temp_enabled",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecsShow = {
			.reg = 53,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "temp_u9",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecsShow = {
			.reg = 54,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "temp_u38",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecsShow = {
			.reg = 55,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
		},
	},

	{
		.devAttr = {
			.attr = { .name = NULL }
		}
	}
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
		.regSpecsShow = {
			.reg = 16,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = VALS_WATCHDOG_ENABLE_MODE,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 17,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 18,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 19,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
		},
	},

	{
		.devAttr = {
			.attr = { .name = NULL }
		}
	}
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
		.regSpecsShow = {
			.reg = 24,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 0,
			.sign = false,
			.vals = VALS_POWER_DOWN_ENABLE_MODE,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 24,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 1,
			.sign = false,
			.vals = VALS_POWER_UP_MODE,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 24,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 2,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 25,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 26,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 27,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
		},
	},

	{
		.devAttr = {
			.attr = { .name = NULL }
		}
	},
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
		.regSpecsShow = {
			.reg = 96,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 15,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 97,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 7,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "battery_cells",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecsShow = {
			.reg = 96,
			.maskedReg = false,
			.mask = 0b1111,
			.shift = 6,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "battery_type",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecsShow = {
			.reg = 96,
			.maskedReg = false,
			.mask = 0b1111,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 97,
			.maskedReg = false,
			.mask = 0xff,
			.shift = 8,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "battery_status",
				.mode = 0440,
			},
			.show = devAttrI2c_show,
			.store = NULL,
		},
		.regSpecsShow = {
			.reg = 97,
			.maskedReg = false,
			.mask = 0b1111,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 98,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "battery_current_max",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecsShow = {
			.reg = 99,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 100,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
		},
	},

	// TODO all the rest

	{
		.devAttr = {
			.attr = { .name = NULL }
		}
	}
};

static struct DeviceAttrBean devAttrBeansExpBus[] = {
	// TODO
	{
		.devAttr = {
			.attr = { .name = NULL }
		}
	}
};

static struct DeviceAttrBean devAttrBeansSd[] = {
	{
		.devAttr = {
			.attr = {
				.name = "sdx_enabled",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecsShow = {
			.reg = 30,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "sd1_enabled",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecsShow = {
			.reg = 30,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 1,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 30,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 2,
			.sign = false,
			.vals = VALS_SD_SDX_ROUTING,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 30,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 3,
			.sign = false,
			.vals = VALS_SD_SDX_ROUTING,
		},
		.regSpecsStore = {
			.reg = -1,
		},
	},

	{
		.devAttr = {
			.attr = { .name = NULL }
		}
	}
};

static struct DeviceAttrBean devAttrBeansUsb[] = {
	{
		.devAttr = {
			.attr = {
				.name = "usb1_disabled",
				.mode = 0660,
			},
			.show = devAttrGpio_show,
			.store = devAttrGpio_store,
		},
		.gpioMode = GPIO_MODE_OUT,
		.gpio = 30,
	},

	{
		.devAttr = {
			.attr = {
				.name = "usb1_ok",
				.mode = 0440,
			},
			.show = devAttrGpio_show,
			.store = NULL,
		},
		.gpioMode = GPIO_MODE_IN,
		.gpio = 0,
	},

	{
		.devAttr = {
			.attr = {
				.name = "usb2_disabled",
				.mode = 0660,
			},
			.show = devAttrGpio_show,
			.store = devAttrGpio_store,
		},
		.gpioMode = GPIO_MODE_OUT,
		.gpio = 31,
	},

	{
		.devAttr = {
			.attr = {
				.name = "usb2_ok",
				.mode = 0440,
			},
			.show = devAttrGpio_show,
			.store = NULL,
		},
		.gpioMode = GPIO_MODE_IN,
		.gpio = 1,
	},

	{
		.devAttr = {
			.attr = { .name = NULL }
		}
	}
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
		.regSpecsShow = {
			.reg = 58,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 15,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 58,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 14,
			.sign = false,
			.vals = VALS_ANALOG_OUTS_MODE,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 59,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 61,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 15,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 61,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 14,
			.sign = false,
			.vals = VALS_ANALOG_OUTS_MODE,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 62,
			.maskedReg = false,
			.mask = 0xffff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
		},
	},

	{
		.devAttr = {
			.attr = { .name = NULL }
		}
	}
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
		.regSpecsShow = {
			.reg = 93,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 15,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
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
		.regSpecsShow = {
			.reg = 93,
			.maskedReg = false,
			.mask = 0x7fff,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
		},
	},

	{
		.devAttr = {
			.attr = { .name = NULL }
		}
	}
};

static struct DeviceAttrBean devAttrBeansSysPowerSwitch[] = {
	{
		.devAttr = {
			.attr = {
				.name = "usb",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecsShow = {
			.reg = 90,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 0,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "i2c_ext",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecsShow = {
			.reg = 90,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 1,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "v5_ext",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecsShow = {
			.reg = 90,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 2,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "srtc",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecsShow = {
			.reg = 90,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 3,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "v5o",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecsShow = {
			.reg = 90,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 4,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
		},
	},

	{
		.devAttr = {
			.attr = {
				.name = "v5_iso",
				.mode = 0660,
			},
			.show = devAttrI2c_show,
			.store = devAttrI2c_store,
		},
		.regSpecsShow = {
			.reg = 90,
			.maskedReg = false,
			.mask = 0b1,
			.shift = 5,
			.sign = false,
			.vals = NULL,
		},
		.regSpecsStore = {
			.reg = -1,
		},
	},

	{
		.devAttr = {
			.attr = { .name = NULL }
		}
	}
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

	{
		.devAttr = {
			.attr = { .name = NULL }
		}
	}
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
		.gpio = 41,
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
		.gpio = 45,
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

	{
		.devAttr = {
			.attr = { .name = NULL }
		}
	}
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
		.name = "analog_temp",
		.devAttrBeans = devAttrBeansAnalogTemp,
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
		.name = "sys_mon",
		.devAttrBeans = devAttrBeansSysMon,
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
		.name = "exp_bus",
		.devAttrBeans = devAttrBeansExpBus,
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
		.name = "power_out",
		.devAttrBeans = devAttrBeansPowerOut,
	},

	{
		.name = "sys_power_switch",
		.devAttrBeans = devAttrBeansSysPowerSwitch,
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
		.name = NULL
	}

};

struct i2c_client *ionopimax_i2c_client = NULL;

struct ionopimax_i2c_data {
	struct mutex update_lock;
};

struct ionopimax_i2c_data *ionopimax_i2c_client_data = NULL;

static char toUpper(char c) {
	if (c >= 97 && c <= 122) {
		return c - 32;
	}
	return c;
}

static struct DeviceAttrBean* devAttrGetBean(struct device* dev,
		struct device_attribute* attr) {
	int di, ai;
	di = 0;
	while (devices[di].name != NULL) {
		if (dev == devices[di].pDevice) {
			ai = 0;
			while (devices[di].devAttrBeans[ai].devAttr.attr.name != NULL) {
				if (attr == &devices[di].devAttrBeans[ai].devAttr) {
					return &devices[di].devAttrBeans[ai];
					break;
				}
				ai++;
			}
			break;
		}
		di++;
	}
	return NULL;
}

static int getGpio(struct device* dev, struct device_attribute* attr) {
	struct DeviceAttrBean* dab;
	dab = devAttrGetBean(dev, attr);
	if (dab == NULL || dab->gpioMode == 0) {
		return -1;
	}
	return dab->gpio;
}

static ssize_t devAttrGpio_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	int gpio;
	gpio = getGpio(dev, attr);
	if (gpio < 0) {
		return -EFAULT;
	}
	return sprintf(buf, "%d\n", gpio_get_value(gpio));
}

static ssize_t devAttrGpio_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
	bool val;
	int gpio = getGpio(dev, attr);
	if (gpio < 0) {
		return -EFAULT;
	}
	if (kstrtobool(buf, &val) < 0) {
		if (toUpper(buf[0]) == 'E') { // Enable
			val = true;
		} else if (toUpper(buf[0]) == 'D') { // Disable
			val = false;
		} else if (toUpper(buf[0]) == 'F' || toUpper(buf[0]) == 'T') { // Flip/Toggle
			val = gpio_get_value(gpio) == 1 ? false : true;
		} else {
			return -EINVAL;
		}
	}
	gpio_set_value(gpio, val ? 1 : 0);
	return count;
}

static ssize_t devAttrGpioBlink_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
	int i;
	long on = 0;
	long off = 0;
	long rep = 1;
	char *end = NULL;
	int gpio = getGpio(dev, attr);
	if (gpio < 0) {
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
	printk(KERN_INFO "ionopimax: - | gpio blink %ld %ld %ld\n", on, off, rep);
	if (on > 0) {
		for (i = 0; i < rep; i++) {
			gpio_set_value(gpio, 1);
			msleep(on);
			gpio_set_value(gpio, 0);
			if (i < rep - 1) {
				msleep(off);
			}
		}
	}
	return count;
}

static bool ionopimax_i2c_lock(void) {
	ionopimax_i2c_client_data = i2c_get_clientdata(ionopimax_i2c_client);
	if (!mutex_trylock(&ionopimax_i2c_client_data->update_lock)) {
		printk(KERN_ALERT "stratopi: * | I2C busy\n");
		return false;
	}
	return true;
}

static void ionopimax_i2c_unlock(void) {
	mutex_unlock(&ionopimax_i2c_client_data->update_lock);
}

static int32_t ionopimax_i2c_read_no_lock(uint8_t reg) {
	printk(KERN_INFO "ionopimax: - | I2C read reg=%u\n", reg);
	return i2c_smbus_read_word_data(ionopimax_i2c_client, reg);
}

static int32_t ionopimax_i2c_write_no_lock(uint8_t reg, uint16_t val) {
	printk(KERN_INFO "ionopimax: - | I2C write reg=%u val=0x%04x\n", reg, val);
	return i2c_smbus_write_word_data(ionopimax_i2c_client, reg, val);
}

static int32_t ionopimax_i2c_read(uint8_t reg) {
	int32_t res;

	if (!ionopimax_i2c_lock()) {
		return -EBUSY;
	}

	res = ionopimax_i2c_read_no_lock(reg);

	ionopimax_i2c_unlock();

	if (res < 0) {
		return -EIO;
	}
	return res;
}

static int32_t ionopimax_i2c_write(uint8_t reg, uint16_t val) {
	int32_t res;

	if (!ionopimax_i2c_lock()) {
		return -EBUSY;
	}

	res = ionopimax_i2c_write_no_lock(reg, val);

	ionopimax_i2c_unlock();

	if (res < 0) {
		return -EIO;
	}
	return res;
}

static int32_t ionopimax_i2c_read_segment(uint8_t reg, int16_t mask,
		uint8_t shift) {
	int32_t res;
	res = ionopimax_i2c_read(reg);
	if (res < 0) {
		return res;
	}
	return (res >> shift) & mask;
}

static int32_t ionopimax_i2c_write_segment(uint8_t reg, bool maskedReg,
		int16_t mask, uint8_t shift, uint16_t val) {
	int32_t res = 0;

	if (!ionopimax_i2c_lock()) {
		return -EBUSY;
	}

	val = (val & mask) << shift;
	mask <<= shift;

	if (maskedReg) {
		val = (mask << 8) | val;
	} else if (mask != 0xffff) {
		res = ionopimax_i2c_read_no_lock(reg);
		val = (res & ~mask) | val;
	}

	if (res >= 0) {
		res = ionopimax_i2c_write_no_lock(reg, val);
	}

	ionopimax_i2c_unlock();

	if (res < 0) {
		return -EIO;
	}
	return res;
}

static ssize_t devAttrI2c_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	int32_t res;
	struct DeviceAttrRegSpecs *specs;
	struct DeviceAttrBean* dab = devAttrGetBean(dev, attr);
	if (dab == NULL) {
		return -EFAULT;
	}
	specs = &dab->regSpecsShow;
	if (specs->reg < 0) {
		return -EFAULT;
	}

	res = ionopimax_i2c_read_segment((uint8_t) specs->reg, specs->mask,
			specs->shift);

	if (res < 0) {
		return res;
	}

	if (specs->vals == NULL) {
		if (specs->sign) {
			return sprintf(buf, "%d\n", (int16_t) res);
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
	struct DeviceAttrBean* dab = devAttrGetBean(dev, attr);
	if (dab == NULL) {
		return -EFAULT;
	}
	specs = &dab->regSpecsStore;
	if (specs->reg < 0) {
		specs = &dab->regSpecsShow;
		if (specs->reg < 0) {
			return -EFAULT;
		}
	}

	if (specs->vals == NULL) {
		ret = kstrtol(buf, 10, &val);
		if (ret < 0) {
			return ret;
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
	dab = devAttrGetBean(dev, attr);
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

static unsigned long to_usec(struct timespec *t) {
	return (t->tv_sec * 1000000) + (t->tv_nsec / 1000);
}

static unsigned long diff_usec(struct timespec *t1, struct timespec *t2) {
	struct timespec diff;
	diff = timespec_sub(*t2, *t1);
	return to_usec(&diff);
}

static irq_handler_t wiegandDataIrqHandler(unsigned int irq, void *dev_id,
		struct pt_regs *regs) {
	bool isLow;
	struct timespec now;
	unsigned long diff;
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

	getrawmonotonic(&now);

	if (l->wasLow == isLow) {
		// got the interrupt but didn't change state. Maybe a fast pulse
		printk(KERN_ALERT "ionopimax: * | repeated interrupt on GPIO %d\n",
				l->gpio);
		return (irq_handler_t) IRQ_HANDLED;
	}

	l->wasLow = isLow;

	if (isLow) {
		if (w->bitCount != 0) {
			diff = diff_usec((struct timespec *) &(w->lastBitTs), &now);

			if (diff < w->pulseIntervalMin_usec) {
				// pulse too early
				// TODO remove
				printk(KERN_ALERT "ionopimax: * | pulse too early\n");
				goto noise;
			}

			if (diff > w->pulseIntervalMax_usec) {
				w->data = 0;
				w->bitCount = 0;
			}
		}

		if (w->activeLine != NULL) {
			// there's movement on both lines
			// TODO remove
			printk(
			KERN_ALERT "ionopimax: * | there's movement on both lines\n");
			goto noise;
		}

		w->activeLine = l;

		w->lastBitTs.tv_sec = now.tv_sec;
		w->lastBitTs.tv_nsec = now.tv_nsec;

	} else {
		if (w->activeLine != l) {
			// there's movement on both lines or previous noise
			// TODO remove
			printk(
					KERN_ALERT "ionopimax: * | there's movement on both lines or previous noise\n");
			goto noise;
		}

		w->activeLine = NULL;

		if (w->bitCount >= WIEGAND_MAX_BITS) {
			return (irq_handler_t) IRQ_HANDLED;
		}

		diff = diff_usec((struct timespec *) &(w->lastBitTs), &now);
		if (diff < w->pulseWidthMin_usec || diff > w->pulseWidthMax_usec) {
			// pulse too short or too long
			// TODO remove
			printk(KERN_ALERT "ionopimax: * | pulse too short or too long\n");
			goto noise;
		}

		w->data <<= 1;
		if (l == &w->d1) {
			w->data |= 1;
		}
		w->bitCount++;
	}

	// TODO remove
//	printk(KERN_ALERT "ionopimax: * | wiegandDataIrqHandler %d %lld %lu\n",
//			w->bitCount, w->data, diff);

	return (irq_handler_t) IRQ_HANDLED;

	noise:
	wiegandReset(w);
	return (irq_handler_t) IRQ_HANDLED;
}

static void wiegandDisable(struct WiegandBean* w) {
	w->enabled = false;

	gpio_unexport(w->d0.gpio);
	gpio_unexport(w->d1.gpio);

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
			reqName[11] = '1';
		} else {
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
			gpio_export(w->d0.gpio, false);
			gpio_export(w->d1.gpio, false);

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
	struct timespec now;
	unsigned long diff;
	struct WiegandBean* w;
	w = getWiegandBean(dev, attr);

	if (!w->enabled) {
		return -ENODEV;
	}

	getrawmonotonic(&now);
	diff = diff_usec((struct timespec *) &(w->lastBitTs), &now);
	if (diff <= w->pulseIntervalMax_usec) {
		return -EBUSY;
	}

	return sprintf(buf, "%lu %d %llu\n", to_usec(&w->lastBitTs), w->bitCount,
			w->data);
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

static ssize_t devAttrMcuFwVersion_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	int32_t val;
	val = ionopimax_i2c_read(0);

	if (val < 0) {
		return val;
	}

	return sprintf(buf, "%d.%d\n", (val >> 8) & 0xf, val & 0xf);
}

static ssize_t devAttrMcuConfig_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
	int32_t res;
	uint16_t val;
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

	res = ionopimax_i2c_write(2, val);
	if (res < 0) {
		return res;
	}

	for (i = 0; i < 10; i++) {
		msleep(50);
		res = ionopimax_i2c_read(3);
		if (res >= 0) {
			if (((res >> 8) & 1) == 0) {
				if (((res >> 6) & 1) == 1) {
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

	mcuI2cReadVal = ionopimax_i2c_read((uint8_t) reg);

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

	if (ionopimax_i2c_write((uint8_t) reg, (uint16_t) val) < 0) {
		return -EIO;
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

	// printk(KERN_INFO "ionopimax: - | ionopimax_i2c_probe device addr 0x%02hx\n", client->addr);
	if (client->addr != I2C_ADDR_LOCAL) {
		// TODO test communication and add external device and sysFs files
		printk(
				KERN_INFO "ionopimax: - | ionopimax_i2c_probe external device 0x%02hx\n",
				client->addr);
	}

	return 0;
}

static int ionopimax_i2c_remove(struct i2c_client *client) {
	struct ionopimax_i2c_data *data = i2c_get_clientdata(client);
	mutex_destroy(&data->update_lock);

	// printk(KERN_INFO "ionopimax: - | ionopimax_i2c_remove device addr 0x%02hx\n", client->addr);
	if (client->addr != I2C_ADDR_LOCAL) {
		// TODO remove external device and sysFs files
		printk(
				KERN_INFO "ionopimax: - | ionopimax_i2c_remove external device 0x%02hx\n",
				client->addr);
	}

	return 0;
}

static const struct i2c_device_id ionopimax_i2c_id[] =
		{ { "ionopimax", 0 }, { } };

MODULE_DEVICE_TABLE(i2c, ionopimax_i2c_id);

static struct i2c_driver ionopimax_i2c_driver = {
	.driver = {
		.name = "ionopimax",
	},
	.probe = ionopimax_i2c_probe,
	.remove = ionopimax_i2c_remove,
	.id_table = ionopimax_i2c_id,
};

static struct i2c_board_info ionopimax_i2c_board_info[] __initdata = {
	{ I2C_BOARD_INFO("ionopimax", I2C_ADDR_LOCAL) }
};

static void cleanup(void) {
	int di, ai;

	if (ionopimax_i2c_client) {
		i2c_unregister_device(ionopimax_i2c_client);
	}
	i2c_del_driver(&ionopimax_i2c_driver);

	di = 0;
	while (devices[di].name != NULL) {
		if (devices[di].pDevice && !IS_ERR(devices[di].pDevice)) {
			ai = 0;
			while (devices[di].devAttrBeans[ai].devAttr.attr.name != NULL) {
				device_remove_file(devices[di].pDevice,
						&devices[di].devAttrBeans[ai].devAttr);
				if (devices[di].devAttrBeans[ai].gpioMode != 0) {
					gpio_unexport(devices[di].devAttrBeans[ai].gpio);
					gpio_free(devices[di].devAttrBeans[ai].gpio);
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
	struct i2c_adapter* i2cAdapter1;
	char gpioReqName[256];
	char *gpioReqNamePart;
	int result = 0;
	int di, ai;

	printk(KERN_INFO "ionopimax: - | init\n");

	strcpy(gpioReqName, "ionopimax_");
	gpioReqNamePart = gpioReqName + strlen("ionopimax_");

	i2c_add_driver(&ionopimax_i2c_driver);

	i2cAdapter1 = i2c_get_adapter(1);
	if (!i2cAdapter1) {
		printk(
				KERN_ALERT "ionopimax: * | I2C bus 1 not found, have you enabled it?\n");
		goto fail;
	}

	ionopimax_i2c_client = i2c_new_device(i2cAdapter1,
			ionopimax_i2c_board_info);
	if (!ionopimax_i2c_client) {
		printk(
		KERN_ALERT "ionopimax: * | error creating ionopimax I2C device\n");
		goto fail;
	}

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
				strcpy(gpioReqNamePart, devices[di].name);
				gpioReqNamePart[strlen(devices[di].name)] = '_';

				strcpy(gpioReqNamePart + strlen(devices[di].name) + 1,
						devices[di].devAttrBeans[ai].devAttr.attr.name);

				// TODO remove
				// printk(KERN_ALERT "ionopimax: * | requesting GPIO %s\n", gpioReqName);

				gpio_request(devices[di].devAttrBeans[ai].gpio, gpioReqName);
				if (devices[di].devAttrBeans[ai].gpioMode == GPIO_MODE_OUT) {
					result = gpio_direction_output(
							devices[di].devAttrBeans[ai].gpio, false);
				} else {
					result = gpio_direction_input(
							devices[di].devAttrBeans[ai].gpio);
				}
				if (result) {
					printk(
					KERN_ALERT "ionopimax: * | error setting up GPIO %d\n",
							devices[di].devAttrBeans[ai].gpio);
					goto fail;
				}
				gpio_export(devices[di].devAttrBeans[ai].gpio, false);
			}
			ai++;
		}
		di++;
	}

	printk(KERN_INFO "ionopimax: - | ready\n");
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
