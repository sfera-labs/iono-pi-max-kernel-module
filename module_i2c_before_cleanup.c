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

#define I2C_ADDR_LOCAL 0x35

#define GPIO_BUZZER 40
#define GPIO_WATCHDOG_ENABLE 39
#define GPIO_WATCHDOG_HEARTBEAT 32
#define GPIO_WATCHDOG_EXPIRED 17
#define GPIO_SHUTDOWN 18

#define GPIO_UPS_BATTERY -1
#define GPIO_RELAY -1
#define GPIO_LED -1
#define GPIO_BUTTON -1
#define GPIO_I2CEXP_ENABLE -1
#define GPIO_I2CEXP_FEEDBACK -1

#define GPIO_USB1_DISABLE 31
#define GPIO_USB1_FAULT 1
#define GPIO_USB2_DISABLE 30
#define GPIO_USB2_FAULT 0

#define I2C_REG_POWER_DOWN_DELAY 25
#define I2C_REG_POWER_DOWN_OFF_TIME 26
#define I2C_REG_POWER_UP_DELAY 27

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sfera Labs - http://sferalabs.cc");
MODULE_DESCRIPTION("Iono Pi Max driver module");
MODULE_VERSION("0.4");

struct DeviceAttrSpecs {
	int16_t reg;
	bool maskedReg;
	int16_t mask;
	uint8_t shift;
	bool sign;
	char *vals;
};

struct DeviceAttrBean {
	struct device_attribute devAttr;
	struct DeviceAttrSpecs specsShow;
	struct DeviceAttrSpecs specsStore;
};

struct DeviceBean {
	char *name;
	struct device *pDevice;
	struct DeviceAttrBean *devAttrBeans;
};

static struct class *pDeviceClass;

static ssize_t devAttrGpio_show(struct device* dev, struct device_attribute* attr, char *buf);

static ssize_t devAttrGpio_store(struct device* dev, struct device_attribute* attr, const char *buf, size_t count);

static ssize_t devAttrI2c_store(struct device* dev, struct device_attribute* attr, const char *buf, size_t count);

static ssize_t devAttrI2c_show(struct device* dev, struct device_attribute* attr, char *buf);

static struct DeviceAttrBean devAttrBeansWatchdog[] = {
{
.devAttr = {
.attr = {
.name = "enabled",
.mode = 0660,
},//
.show = devAttrGpio_show,
.store = devAttrGpio_store,
}
},

{
.devAttr = {
.attr = {
.name = "heartbeat",
.mode = 0660, //
},//
.show = devAttrGpio_show,
.store = devAttrGpio_store,
}
},

{
.devAttr = {
.attr = {
.name = "expired",
.mode = 0440,
},
.show = devAttrGpio_show,
.store = NULL,
}
},

{
.devAttr = {
.attr = {
.name = "enable_mode",
.mode = 0660,
},
.show = devAttrI2c_show,
.store = devAttrI2c_store,
}
},

{
.devAttr = {
.attr = {
.name = "timeout",
.mode = 0660,
},
.show = devAttrI2c_show,
.store = devAttrI2c_store,
}
},

{
.devAttr = {
.attr = { //
.name = "down_delay",
.mode = 0660,
},//
.show = devAttrI2c_show,
.store = devAttrI2c_store,
}
},

{
.devAttr = {
.attr = {
.name = "sd_switch",
.mode = 0660,
},
.show = devAttrI2c_show,
.store = devAttrI2c_store,
}
},


	{
		.devAttr = {
			.attr = { .name= NULL }
		}
	}

};

static struct DeviceBean devices[] = {
	{
		.name = "watchdog",
		.devAttrBeans = devAttrBeansWatchdog,
	},

	{
		.name = "power",
		.devAttrBeans = devAttrBeansWatchdog,
	},

	{
		.name = NULL
	}

};

static struct device *pBuzzerDevice = NULL;
static struct device *pAnalogInDevice = NULL;
static struct device *pDigitalOutDevice = NULL;
static struct device *pAnalogTempDevice = NULL;
static struct device *pSysMonDevice = NULL;
static struct device *pWatchdogDevice = NULL;
static struct device *pPowerDevice = NULL;
static struct device *pUpsDevice = NULL;
static struct device *pButtonDevice = NULL;
static struct device *pExpBusDevice = NULL;
static struct device *pSdDevice = NULL;
static struct device *pUsb1Device = NULL;
static struct device *pUsb2Device = NULL;
static struct device *pMcuDevice = NULL;

static struct device_attribute devAttrBuzzerStatus;
static struct device_attribute devAttrBuzzerBeep;

static struct device_attribute devAttrWatchdogEnabled;
static struct device_attribute devAttrWatchdogHeartbeat;
static struct device_attribute devAttrWatchdogExpired;
static struct device_attribute devAttrWatchdogEnableMode;
static struct device_attribute devAttrWatchdogTimeout;
static struct device_attribute devAttrWatchdogDownDelay;
static struct device_attribute devAttrWatchdogSdSwitch;

static struct device_attribute devAttrAnalogInEnabled;
static struct device_attribute devAttrAnalogInHsf;
static struct device_attribute devAttrAnalogInAv1Mode;
static struct device_attribute devAttrAnalogInAv1;
static struct device_attribute devAttrAnalogInAv2;
static struct device_attribute devAttrAnalogInAv3;
static struct device_attribute devAttrAnalogInAv4;
static struct device_attribute devAttrAnalogInAi1;
static struct device_attribute devAttrAnalogInAi2;
static struct device_attribute devAttrAnalogInAi3;
static struct device_attribute devAttrAnalogInAi4;

static struct device_attribute devAttrDigitalOutO1;
static struct device_attribute devAttrDigitalOutO2;
static struct device_attribute devAttrDigitalOutO3;
static struct device_attribute devAttrDigitalOutO4;
static struct device_attribute devAttrDigitalOutOc1;
static struct device_attribute devAttrDigitalOutOc2;
static struct device_attribute devAttrDigitalOutOc3;
static struct device_attribute devAttrDigitalOutOc4;

static struct device_attribute devAttrAnalogTempAt1Mode;
static struct device_attribute devAttrAnalogTempAt2Mode;
static struct device_attribute devAttrAnalogTempAt1;
static struct device_attribute devAttrAnalogTempAt2;

static struct device_attribute devAttrPowerDownEnabled;
static struct device_attribute devAttrPowerDownDelay;
static struct device_attribute devAttrPowerDownEnableMode;
static struct device_attribute devAttrPowerOffTime;
static struct device_attribute devAttrPowerUpDelay;
static struct device_attribute devAttrPowerUpMode;
static struct device_attribute devAttrPowerSdSwitch;

static struct device_attribute devAttrSysMonPowerEnabled;
static struct device_attribute devAttrSysMonInV;
static struct device_attribute devAttrSysMonInI;
static struct device_attribute devAttrSysMonBattV;
static struct device_attribute devAttrSysMonBattI;
static struct device_attribute devAttrSysMonVsoV;
static struct device_attribute devAttrSysMonVsoI;
static struct device_attribute devAttrSysMonTempEnabled;
static struct device_attribute devAttrSysMonTempU9;
static struct device_attribute devAttrSysMonTempU38;

static struct device_attribute devAttrUpsBattery;
static struct device_attribute devAttrUpsPowerDelay;

static struct device_attribute devAttrButtonStatus;

static struct device_attribute devAttrExpBusEnabled;
static struct device_attribute devAttrExpBusAux;

static struct device_attribute devAttrSdSdxEnabled;
static struct device_attribute devAttrSdSd1Enabled;
static struct device_attribute devAttrSdSdxRouting;
static struct device_attribute devAttrSdSdxDefault;

static struct device_attribute devAttrUsb1Disabled;
static struct device_attribute devAttrUsb1Ok;

static struct device_attribute devAttrUsb2Disabled;
static struct device_attribute devAttrUsb2Ok;

static struct device_attribute devAttrMcuI2cRead;
static struct device_attribute devAttrMcuI2cWrite;
static struct device_attribute devAttrMcuConfig;
static struct device_attribute devAttrMcuFwVersion;

struct i2c_client *ionopimax_i2c_client = NULL;

struct ionopimax_i2c_data {
	struct mutex update_lock;
};

struct ionopimax_i2c_data *ionopimax_i2c_client_data = NULL;

static const char VALS_DIGITAL_OUTS_STATUS[] = { 4, '0', '1', 'F', 'A' };
static const char VALS_WATCHDOG_ENABLE_MODE[] = { 2, 'D', 'A' };
static const char VALS_POWER_DOWN_ENABLE_MODE[] = { 2, 'I', 'A' };
static const char VALS_POWER_UP_MODE[] = { 2, 'M', 'A' };
static const char VALS_SD_SDX_DEFAULT[] = { 2, 'A', 'B' };
static const char VALS_SD_SDX_ROUTING[] = { 2, 'A', 'B' };

static char toUpper(char c) {
	if (c >= 97 && c <= 122) {
		return c - 32;
	}
	return c;
}

static bool startsWith(const char *str, const char *pre) {
	return strncmp(pre, str, strlen(pre)) == 0;
}

static int hex2int(char ch) {
	if (ch >= '0' && ch <= '9')
		return ch - '0';
	if (ch >= 'A' && ch <= 'F')
		return ch - 'A' + 10;
	if (ch >= 'a' && ch <= 'f')
		return ch - 'a' + 10;
	return -1;
}

static int getGpio(struct device* dev, struct device_attribute* attr) {
	if (dev == pBuzzerDevice) {
		return GPIO_BUZZER;
	} else if (dev == pWatchdogDevice) {
		if (attr == &devAttrWatchdogEnabled) {
			return GPIO_WATCHDOG_ENABLE;
		} else if (attr == &devAttrWatchdogHeartbeat) {
			return GPIO_WATCHDOG_HEARTBEAT;
		} else if (attr == &devAttrWatchdogExpired) {
			return GPIO_WATCHDOG_EXPIRED;
		}
	} else if (dev == pPowerDevice) {
		return GPIO_SHUTDOWN;
	} else if (dev == pUpsDevice) {
		return GPIO_UPS_BATTERY;
	} else if (dev == pButtonDevice) {
		return GPIO_BUTTON;
	} else if (dev == pExpBusDevice) {
		if (attr == &devAttrExpBusEnabled) {
			return GPIO_I2CEXP_ENABLE;
		} else if (attr == &devAttrExpBusAux) {
			return GPIO_I2CEXP_FEEDBACK;
		}
	} else if (dev == pUsb1Device) {
		if (attr == &devAttrUsb1Disabled) {
			return GPIO_USB1_DISABLE;
		} else if (attr == &devAttrUsb1Ok) {
			return GPIO_USB1_FAULT;
		}
	} else if (dev == pUsb2Device) {
		if (attr == &devAttrUsb2Disabled) {
			return GPIO_USB2_DISABLE;
		} else if (attr == &devAttrUsb2Ok) {
			return GPIO_USB2_FAULT;
		}
	}
	return -1;
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
	off = simple_strtol(end + 1, &end, 10);
	rep = simple_strtol(end + 1, NULL, 10);
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

static void getI2cRegisterSpecs(struct device* dev,
		struct device_attribute* attr, bool read, int16_t *reg, bool *maskedReg,
		int16_t *mask, uint8_t *shift, bool *sign, char *vals[]) {
	if (dev == pAnalogInDevice) {
		if (attr == &devAttrAnalogInEnabled) {
			*reg = 32;
			*maskedReg = false;
			*mask = 0b1;
			*shift = 0;
			*sign = false;
			*vals = NULL;
		} else if (attr == &devAttrAnalogInHsf) {
			*reg = 32;
			*maskedReg = false;
			*mask = 0b1;
			*shift = 1;
			*sign = false;
			*vals = NULL;
		} else if (attr == &devAttrAnalogInAv1Mode) {
			// TODO
		} else if (attr == &devAttrAnalogInAv1) {
			*reg = 33;
			*maskedReg = false;
			*mask = 0xffff;
			*shift = 0;
			*sign = true;
			*vals = NULL;
		} else if (attr == &devAttrAnalogInAv2) {
			*reg = 34;
			*maskedReg = false;
			*mask = 0xffff;
			*shift = 0;
			*sign = true;
			*vals = NULL;
		} else if (attr == &devAttrAnalogInAv3) {
			*reg = 35;
			*maskedReg = false;
			*mask = 0xffff;
			*shift = 0;
			*sign = true;
			*vals = NULL;
		} else if (attr == &devAttrAnalogInAv4) {
			*reg = 36;
			*maskedReg = false;
			*mask = 0xffff;
			*shift = 0;
			*sign = true;
			*vals = NULL;
		} else if (attr == &devAttrAnalogInAi1) {
			*reg = 37;
			*maskedReg = false;
			*mask = 0xffff;
			*shift = 0;
			*sign = true;
			*vals = NULL;
		} else if (attr == &devAttrAnalogInAi2) {
			*reg = 38;
			*maskedReg = false;
			*mask = 0xffff;
			*shift = 0;
			*sign = true;
			*vals = NULL;
		} else if (attr == &devAttrAnalogInAi3) {
			*reg = 39;
			*maskedReg = false;
			*mask = 0xffff;
			*shift = 0;
			*sign = true;
			*vals = NULL;
		} else if (attr == &devAttrAnalogInAi4) {
			*reg = 40;
			*maskedReg = false;
			*mask = 0xffff;
			*shift = 0;
			*sign = true;
			*vals = NULL;
		}

	} else if (dev == pDigitalOutDevice) {
		if (attr == &devAttrDigitalOutO1) {
			if (read) {
				*reg = 87;
				*maskedReg = false;
				*mask = 0b11;
				*shift = 0;
				*sign = false;
				*vals = VALS_DIGITAL_OUTS_STATUS;
			} else {
				*reg = 86;
				*maskedReg = true;
				*mask = 0b1;
				*shift = 0;
				*sign = false;
				*vals = NULL;
			}
		} else if (attr == &devAttrDigitalOutO2) {
			if (read) {
				*reg = 87;
				*maskedReg = false;
				*mask = 0b11;
				*shift = 2;
				*sign = false;
				*vals = VALS_DIGITAL_OUTS_STATUS;
			} else {
				*reg = 86;
				*maskedReg = true;
				*mask = 0b1;
				*shift = 1;
				*sign = false;
				*vals = NULL;
			}
		} else if (attr == &devAttrDigitalOutO3) {
			if (read) {
				*reg = 87;
				*maskedReg = false;
				*mask = 0b11;
				*shift = 4;
				*sign = false;
				*vals = VALS_DIGITAL_OUTS_STATUS;
			} else {
				*reg = 86;
				*maskedReg = true;
				*mask = 0b1;
				*shift = 2;
				*sign = false;
				*vals = NULL;
			}
		} else if (attr == &devAttrDigitalOutO4) {
			if (read) {
				*reg = 87;
				*maskedReg = false;
				*mask = 0b11;
				*shift = 6;
				*sign = false;
				*vals = VALS_DIGITAL_OUTS_STATUS;
			} else {
				*reg = 86;
				*maskedReg = true;
				*mask = 0b1;
				*shift = 3;
				*sign = false;
				*vals = NULL;
			}
		} else if (attr == &devAttrDigitalOutOc1) {
			if (read) {
				*reg = 89;
				*maskedReg = false;
				*mask = 0b11;
				*shift = 0;
				*sign = false;
				*vals = VALS_DIGITAL_OUTS_STATUS;
			} else {
				*reg = 88;
				*maskedReg = true;
				*mask = 0b1;
				*shift = 0;
				*sign = false;
				*vals = NULL;
			}
		} else if (attr == &devAttrDigitalOutOc2) {
			if (read) {
				*reg = 89;
				*maskedReg = false;
				*mask = 0b11;
				*shift = 2;
				*sign = false;
				*vals = VALS_DIGITAL_OUTS_STATUS;
			} else {
				*reg = 88;
				*maskedReg = true;
				*mask = 0b1;
				*shift = 1;
				*sign = false;
				*vals = NULL;
			}
		} else if (attr == &devAttrDigitalOutOc3) {
			if (read) {
				*reg = 89;
				*maskedReg = false;
				*mask = 0b11;
				*shift = 4;
				*sign = false;
				*vals = VALS_DIGITAL_OUTS_STATUS;
			} else {
				*reg = 88;
				*maskedReg = true;
				*mask = 0b1;
				*shift = 2;
				*sign = false;
				*vals = NULL;
			}
		} else if (attr == &devAttrDigitalOutOc4) {
			if (read) {
				*reg = 89;
				*maskedReg = false;
				*mask = 0b11;
				*shift = 6;
				*sign = false;
				*vals = VALS_DIGITAL_OUTS_STATUS;
			} else {
				*reg = 88;
				*maskedReg = true;
				*mask = 0b1;
				*shift = 3;
				*sign = false;
				*vals = NULL;
			}
		}

	} else if (dev == pAnalogTempDevice) {
		if (attr == &devAttrAnalogTempAt1Mode) {
			*reg = 32;
			*maskedReg = false;
			*mask = 0b11;
			*shift = 12;
			*sign = false;
			*vals = NULL; // TODO use vals?
		} else if (attr == &devAttrAnalogTempAt2Mode) {
			*reg = 32;
			*maskedReg = false;
			*mask = 0b11;
			*shift = 14;
			*sign = false;
			*vals = NULL; // TODO use vals?
		} else if (attr == &devAttrAnalogTempAt1) {
			*reg = 41;
			*maskedReg = false;
			*mask = 0xffff;
			*shift = 0;
			*sign = true;
			*vals = NULL;
		} else if (attr == &devAttrAnalogTempAt2) {
			*reg = 42;
			*maskedReg = false;
			*mask = 0xffff;
			*shift = 0;
			*sign = true;
			*vals = NULL;
		}

	} else if (dev == pWatchdogDevice) {
		if (attr == &devAttrWatchdogEnableMode) {
			*reg = 16;
			*maskedReg = false;
			*mask = 0xffff;
			*shift = 0;
			*sign = false;
			*vals = VALS_WATCHDOG_ENABLE_MODE;
		} else if (attr == &devAttrWatchdogTimeout) {
			*reg = 17;
			*maskedReg = false;
			*mask = 0xffff;
			*shift = 0;
			*sign = false;
			*vals = NULL;
		} else if (attr == &devAttrWatchdogDownDelay) {
			*reg = 18;
			*maskedReg = false;
			*mask = 0xffff;
			*shift = 0;
			*sign = false;
			*vals = NULL;
		} else if (attr == &devAttrWatchdogSdSwitch) {
			*reg = 19;
			*maskedReg = false;
			*mask = 0xffff;
			*shift = 0;
			*sign = false;
			*vals = NULL;
		}

	} else if (dev == pPowerDevice) {
		if (attr == &devAttrPowerDownEnableMode) {
			*reg = 24;
			*maskedReg = false;
			*mask = 0b1;
			*shift = 0;
			*sign = false;
			*vals = VALS_POWER_DOWN_ENABLE_MODE;
		} else if (attr == &devAttrPowerUpMode) {
			*reg = 24;
			*maskedReg = false;
			*mask = 0b1;
			*shift = 1;
			*sign = false;
			*vals = VALS_POWER_UP_MODE;
		} else if (attr == &devAttrPowerSdSwitch) {
			*reg = 24;
			*maskedReg = false;
			*mask = 0b1;
			*shift = 2;
			*sign = false;
			*vals = NULL;
		} else if (attr == &devAttrPowerDownDelay) {
			*reg = 25;
			*maskedReg = false;
			*mask = 0xffff;
			*shift = 0;
			*sign = false;
			*vals = NULL;
		} else if (attr == &devAttrPowerOffTime) {
			*reg = 26;
			*maskedReg = false;
			*mask = 0xffff;
			*shift = 0;
			*sign = false;
			*vals = NULL;
		} else if (attr == &devAttrPowerUpDelay) {
			*reg = 27;
			*maskedReg = false;
			*mask = 0xffff;
			*shift = 0;
			*sign = false;
			*vals = NULL;
		}

	} else if (dev == pSdDevice) {
		if (attr == &devAttrSdSdxEnabled) {
			*reg = 30;
			*maskedReg = false;
			*mask = 0b1;
			*shift = 0;
			*sign = false;
			*vals = NULL;
		} else if (attr == &devAttrSdSd1Enabled) {
			*reg = 30;
			*maskedReg = false;
			*mask = 0b1;
			*shift = 1;
			*sign = false;
			*vals = NULL;
		} else if (attr == &devAttrSdSdxDefault) {
			*reg = 30;
			*maskedReg = false;
			*mask = 0b1;
			*shift = 2;
			*sign = false;
			*vals = VALS_SD_SDX_DEFAULT;
		} else if (attr == &devAttrSdSdxRouting) {
			*reg = 30;
			*maskedReg = false;
			*mask = 0b1;
			*shift = 3;
			*sign = false;
			*vals = VALS_SD_SDX_ROUTING;
		}

	} else if (dev == pSysMonDevice) {
		if (attr == &devAttrSysMonPowerEnabled) {
			*reg = 45;
			*maskedReg = false;
			*mask = 0xffff;
			*shift = 0;
			*sign = false;
			*vals = NULL;
		} else if (attr == &devAttrSysMonInV) {
			*reg = 46;
			*maskedReg = false;
			*mask = 0xffff;
			*shift = 0;
			*sign = false;
			*vals = NULL;
		} else if (attr == &devAttrSysMonInI) {
			*reg = 47;
			*maskedReg = false;
			*mask = 0xffff;
			*shift = 0;
			*sign = false;
			*vals = NULL;
		} else if (attr == &devAttrSysMonBattV) {
			*reg = 48;
			*maskedReg = false;
			*mask = 0xffff;
			*shift = 0;
			*sign = false;
			*vals = NULL;
		} else if (attr == &devAttrSysMonBattI) {
			*reg = 49;
			*maskedReg = false;
			*mask = 0xffff;
			*shift = 0;
			*sign = false;
			*vals = NULL;
		} else if (attr == &devAttrSysMonVsoV) {
			*reg = 50;
			*maskedReg = false;
			*mask = 0xffff;
			*shift = 0;
			*sign = false;
			*vals = NULL;
		} else if (attr == &devAttrSysMonVsoI) {
			*reg = 51;
			*maskedReg = false;
			*mask = 0xffff;
			*shift = 0;
			*sign = false;
			*vals = NULL;
		} else if (attr == &devAttrSysMonTempEnabled) {
			*reg = 53;
			*maskedReg = false;
			*mask = 0xffff;
			*shift = 0;
			*sign = false;
			*vals = NULL;
		} else if (attr == &devAttrSysMonTempU9) {
			*reg = 54;
			*maskedReg = false;
			*mask = 0xffff;
			*shift = 0;
			*sign = false;
			*vals = NULL;
		} else if (attr == &devAttrSysMonTempU38) {
			*reg = 55;
			*maskedReg = false;
			*mask = 0xffff;
			*shift = 0;
			*sign = false;
			*vals = NULL;
		}

	} else if (dev == pMcuDevice) {
		if (attr == &devAttrMcuFwVersion) {
			// TODO
			*reg = 0;
			*maskedReg = false;
			*mask = 0xffff;
			*shift = 0;
			*sign = false;
			*vals = NULL;
		}
	}
}

//static ssize_t devAttrI2cBits_show(struct device* dev,
//		struct device_attribute* attr, char *buf) {
//	int32_t res;
//	bool sign;
//	int16_t reg = -1;
//
//	getI2cWordRegisterSpecs(dev, attr, &reg, &sign);
//
//	if (reg < 0) {
//		return -EFAULT;
//	}
//
//	res = ionopimax_i2c_read((uint8_t) reg);
//
//	if (res < 0) {
//		return res;
//	}
//
//	if (sign) {
//		return sprintf(buf, "%d\n", (int16_t) res);
//	} else {
//		return sprintf(buf, "%d\n", res);
//	}
//}
//
//static ssize_t devAttrI2cBits_store(struct device* dev,
//		struct device_attribute* attr, const char *buf, size_t count) {
//	long val;
//	int ret;
//	int32_t res;
//	bool sign;
//	int16_t reg = -1;
//
//	getI2cWordRegisterSpecs(dev, attr, &reg, &sign);
//
//	if (reg < 0) {
//		return -EFAULT;
//	}
//
//	ret = kstrtol(buf, 10, &val);
//	if (ret < 0) {
//		return ret;
//	}
//
//	if (sign) {
//		if (val < -32768 || val > 32767) {
//			return -EINVAL;
//		}
//	} else {
//		if (val < 0 || val > 0xffff) {
//			return -EINVAL;
//		}
//	}
//
//	res = ionopimax_i2c_write((uint8_t) reg, (uint16_t) val);
//
//	if (res < 0) {
//		return res;
//	}
//
//	return count;
//}

static ssize_t devAttrI2c_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	int32_t res;
	bool maskedReg;
	int16_t mask;
	uint8_t shift;
	bool sign;
	int16_t reg = -1;
	char *vals = NULL;

	getI2cRegisterSpecs(dev, attr, true, &reg, &maskedReg, &mask, &shift, &sign,
			&vals);

	if (reg < 0) {
		return -EFAULT;
	}

	res = ionopimax_i2c_read_segment((uint8_t) reg, mask, shift);

	if (res < 0) {
		return res;
	}

	if (vals == NULL) {
		if (sign) {
			return sprintf(buf, "%d\n", (int16_t) res);
		} else {
			return sprintf(buf, "%d\n", res);
		}
	} else {
		if (res > vals[0] - 1) {
			return -EFAULT;
		}

		return sprintf(buf, "%c\n", vals[res + 1]);
	}
}

static ssize_t devAttrI2c_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
	long val;
	int ret;
	uint16_t i;
	int32_t res;
	bool maskedReg;
	int16_t mask;
	uint8_t shift;
	bool sign;
	char valC;
	int16_t reg = -1;
	char *vals = NULL;

	getI2cRegisterSpecs(dev, attr, false, &reg, &maskedReg, &mask, &shift,
			&sign, &vals);

	if (reg < 0) {
		return -EFAULT;
	}

	if (vals == NULL) {
		ret = kstrtol(buf, 10, &val);
		if (ret < 0) {
			return ret;
		}
	} else {
		val = -1;
		valC = toUpper(buf[0]);
		for (i = 0; i < vals[0]; i++) {
			if (vals[i + 1] == valC) {
				val = i;
				break;
			}
		}
		if (val == -1) {
			return -EINVAL;
		}
	}

	res = ionopimax_i2c_write_segment((uint8_t) reg, maskedReg, mask, shift,
			(uint16_t) val);

	if (res < 0) {
		return res;
	}

	return count;
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

static struct device_attribute devAttrBuzzerStatus = { //
		.attr = { //
				.name = "status", //
						.mode = 0660, //
				},//
				.show = devAttrGpio_show, //
				.store = devAttrGpio_store, //
		};

static struct device_attribute devAttrBuzzerBeep = { //
		.attr = { //
				.name = "beep", //
						.mode = 0220, //
				},//
				.show = NULL, //
				.store = devAttrGpioBlink_store, //
		};

static struct device_attribute devAttrWatchdogEnabled = { //
		.attr = { //
				.name = "enabled", //
						.mode = 0660, //
				},//
				.show = devAttrGpio_show, //
				.store = devAttrGpio_store, //
		};

static struct device_attribute devAttrWatchdogHeartbeat = { //
		.attr = { //
				.name = "heartbeat", //
						.mode = 0660, //
				},//
				.show = devAttrGpio_show, //
				.store = devAttrGpio_store, //
		};

static struct device_attribute devAttrWatchdogExpired = { //
		.attr = { //
				.name = "expired", //
						.mode = 0440, //
				},//
				.show = devAttrGpio_show, //
				.store = NULL, //
		};

static struct device_attribute devAttrWatchdogEnableMode = { //
		.attr = { //
				.name = "enable_mode", //
						.mode = 0660, //
				},//
				.show = devAttrI2c_show, //
				.store = devAttrI2c_store, //
		};

static struct device_attribute devAttrWatchdogTimeout = { //
		.attr = { //
				.name = "timeout", //
						.mode = 0660, //
				},//
				.show = devAttrI2c_show, //
				.store = devAttrI2c_store, //
		};

static struct device_attribute devAttrWatchdogDownDelay = { //
		.attr = { //
				.name = "down_delay", //
						.mode = 0660, //
				},//
				.show = devAttrI2c_show, //
				.store = devAttrI2c_store, //
		};

static struct device_attribute devAttrWatchdogSdSwitch = { //
		.attr = { //
				.name = "sd_switch", //
						.mode = 0660, //
				},//
				.show = devAttrI2c_show, //
				.store = devAttrI2c_store, //
		};

static struct device_attribute devAttrPowerDownEnabled = { //
		.attr = { //
				.name = "down_enabled", //
						.mode = 0660, //
				},//
				.show = devAttrGpio_show, //
				.store = devAttrGpio_store, //
		};

static struct device_attribute devAttrPowerDownDelay = { //
		.attr = { //
				.name = "down_delay", //
						.mode = 0660, //
				},//
				.show = devAttrI2c_show, //
				.store = devAttrI2c_store, //
		};

static struct device_attribute devAttrPowerDownEnableMode = { //
		.attr = { //
				.name = "down_enable_mode", //
						.mode = 0660, //
				},//
				.show = devAttrI2c_show, //
				.store = devAttrI2c_store, //
		};

static struct device_attribute devAttrPowerOffTime = { //
		.attr = { //
				.name = "off_time", //
						.mode = 0660, //
				},//
				.show = devAttrI2c_show, //
				.store = devAttrI2c_store, //
		};

static struct device_attribute devAttrPowerUpDelay = { //
		.attr = { //
				.name = "up_delay", //
						.mode = 0660, //
				},//
				.show = devAttrI2c_show, //
				.store = devAttrI2c_store, //
		};

static struct device_attribute devAttrPowerUpMode = { //
		.attr = { //
				.name = "up_mode", //
						.mode = 0660, //
				},//
				.show = devAttrI2c_show, //
				.store = devAttrI2c_store, //
		};

static struct device_attribute devAttrPowerSdSwitch = { //
		.attr = { //
				.name = "sd_switch", //
						.mode = 0660, //
				},//
				.show = devAttrI2c_show, //
				.store = devAttrI2c_store, //
		};

static struct device_attribute devAttrUpsBattery = { //
		.attr = { //
				.name = "battery", //
						.mode = 0440, //
				},//
				.show = devAttrGpio_show, //
				.store = NULL, //
		};

static struct device_attribute devAttrUpsPowerDelay = { //
		.attr = { //
				.name = "power_delay", //
						.mode = 0660, //
				},//
				.show = devAttrI2c_show, //
				.store = devAttrI2c_store, //
		};

static struct device_attribute devAttrLedStatus = { //
		.attr = { //
				.name = "status", //
						.mode = 0660, //
				},//
				.show = devAttrGpio_show, //
				.store = devAttrGpio_store, //
		};

static struct device_attribute devAttrLedBlink = { //
		.attr = { //
				.name = "blink", //
						.mode = 0220, //
				},//
				.show = NULL, //
				.store = devAttrGpioBlink_store, //
		};

static struct device_attribute devAttrButtonStatus = { //
		.attr = { //
				.name = "status", //
						.mode = 0440, //
				},//
				.show = devAttrGpio_show, //
				.store = NULL, //
		};

static struct device_attribute devAttrExpBusEnabled = { //
		.attr = { //
				.name = "enabled", //
						.mode = 0660, //
				},//
				.show = devAttrGpio_show, //
				.store = devAttrGpio_store, //
		};

static struct device_attribute devAttrExpBusAux = { //
		.attr = { //
				.name = "aux", //
						.mode = 0440, //
				},//
				.show = devAttrGpio_show, //
				.store = NULL, //
		};

static struct device_attribute devAttrSdSdxEnabled = { //
		.attr = { //
				.name = "sdx_enabled", //
						.mode = 0660, //
				},//
				.show = devAttrI2c_show, //
				.store = devAttrI2c_store, //
		};

static struct device_attribute devAttrSdSd1Enabled = { //
		.attr = { //
				.name = "sd1_enabled", //
						.mode = 0660, //
				},//
				.show = devAttrI2c_show, //
				.store = devAttrI2c_store, //
		};

static struct device_attribute devAttrSdSdxRouting = { //
		.attr = { //
				.name = "sdx_routing", //
						.mode = 0660, //
				},//
				.show = devAttrI2c_show, //
				.store = devAttrI2c_store, //
		};

static struct device_attribute devAttrSdSdxDefault = { //
		.attr = { //
				.name = "sdx_default", //
						.mode = 0660, //
				},//
				.show = devAttrI2c_show, //
				.store = devAttrI2c_store, //
		};

static struct device_attribute devAttrUsb1Disabled = { //
		.attr = { //
				.name = "disabled", //
						.mode = 0660, //
				},//
				.show = devAttrGpio_show, //
				.store = devAttrGpio_store, //
		};

static struct device_attribute devAttrUsb1Ok = { //
		.attr = { //
				.name = "ok", //
						.mode = 0440, //
				},//
				.show = devAttrGpio_show, //
				.store = NULL, //
		};

static struct device_attribute devAttrUsb2Disabled = { //
		.attr = { //
				.name = "disabled", //
						.mode = 0660, //
				},//
				.show = devAttrGpio_show, //
				.store = devAttrGpio_store, //
		};

static struct device_attribute devAttrUsb2Ok = { //
		.attr = { //
				.name = "ok", //
						.mode = 0440, //
				},//
				.show = devAttrGpio_show, //
				.store = NULL, //
		};

static struct device_attribute devAttrMcuI2cRead = { //
		.attr = { //
				.name = "i2c_read", //
						.mode = 0660, //
				},//
				.show = mcuI2cRead_show, //
				.store = mcuI2cRead_store, //
		};

static struct device_attribute devAttrMcuI2cWrite = { //
		.attr = { //
				.name = "i2c_write", //
						.mode = 0220, //
				},//
				.show = NULL, //
				.store = mcuI2cWrite_store, //
		};

static struct device_attribute devAttrMcuConfig = { //
		.attr = { //
				.name = "config", //
						.mode = 0220, //
				},//
				.show = NULL, //
				.store = devAttrI2c_store, //
		};

static struct device_attribute devAttrMcuFwVersion = { //
		.attr = { //
				.name = "fw_version", //
						.mode = 0440, //
				},//
				.show = devAttrI2c_show, //
				.store = NULL, //
		};

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
		printk(KERN_INFO "ionopimax: - | ionopimax_i2c_probe external device 0x%02hx\n", client->addr);;
	}

	return 0;
}

static int ionopimax_i2c_remove(struct i2c_client *client) {
	struct ionopimax_i2c_data *data = i2c_get_clientdata(client);
	mutex_destroy(&data->update_lock);

// printk(KERN_INFO "ionopimax: - | ionopimax_i2c_remove device addr 0x%02hx\n", client->addr);
	if (client->addr != I2C_ADDR_LOCAL) {
		// TODO remove external device and sysFs files
		printk(KERN_INFO "ionopimax: - | ionopimax_i2c_remove external device 0x%02hx\n", client->addr);;
	}

	return 0;
}

static const struct i2c_device_id ionopimax_i2c_id[] =
		{ { "ionopimax", 0 }, { } };

MODULE_DEVICE_TABLE( i2c, ionopimax_i2c_id);

static struct i2c_driver ionopimax_i2c_driver = { //
		.driver = { //
				.name = "ionopimax", //
				},//
				.probe = ionopimax_i2c_probe, //
				.remove = ionopimax_i2c_remove, //
				.id_table = ionopimax_i2c_id, //
		};

static struct i2c_board_info ionopimax_i2c_board_info[]
__initdata = { { I2C_BOARD_INFO("ionopimax", I2C_ADDR_LOCAL), } };

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
				device_remove_file(devices[di].pDevice, &devices[di].devAttrBeans[ai].devAttr);
				ai++;
			}
		}
		device_destroy(pDeviceClass, 0);
		di++;
	}


/*
	if (pBuzzerDevice && !IS_ERR(pBuzzerDevice)) {
		device_remove_file(pBuzzerDevice, &devAttrBuzzerStatus);
		device_remove_file(pBuzzerDevice, &devAttrBuzzerBeep);

		device_destroy(pDeviceClass, 0);
	}

	if (pWatchdogDevice && !IS_ERR(pWatchdogDevice)) {
		device_remove_file(pWatchdogDevice, &devAttrWatchdogEnabled);
		device_remove_file(pWatchdogDevice, &devAttrWatchdogHeartbeat);
		device_remove_file(pWatchdogDevice, &devAttrWatchdogExpired);
		device_remove_file(pWatchdogDevice, &devAttrWatchdogEnableMode);
		device_remove_file(pWatchdogDevice, &devAttrWatchdogTimeout);
		device_remove_file(pWatchdogDevice, &devAttrWatchdogDownDelay);
		device_remove_file(pWatchdogDevice, &devAttrWatchdogSdSwitch);

		device_destroy(pDeviceClass, 0);
	}

	if (pPowerDevice && !IS_ERR(pPowerDevice)) {
		device_remove_file(pPowerDevice, &devAttrPowerDownEnabled);
		device_remove_file(pPowerDevice, &devAttrPowerDownDelay);
		device_remove_file(pPowerDevice, &devAttrPowerDownEnableMode);
		device_remove_file(pPowerDevice, &devAttrPowerOffTime);
		device_remove_file(pPowerDevice, &devAttrPowerUpDelay);
		device_remove_file(pPowerDevice, &devAttrPowerUpMode);
		device_remove_file(pPowerDevice, &devAttrPowerSdSwitch);

		device_destroy(pDeviceClass, 0);
	}

	if (pSdDevice && !IS_ERR(pSdDevice)) {
		device_remove_file(pSdDevice, &devAttrSdSdxEnabled);
		device_remove_file(pSdDevice, &devAttrSdSd1Enabled);
		device_remove_file(pSdDevice, &devAttrSdSdxRouting);
		device_remove_file(pSdDevice, &devAttrSdSdxDefault);

		device_destroy(pDeviceClass, 0);
	}

	if (pMcuDevice && !IS_ERR(pMcuDevice)) {
		device_remove_file(pMcuDevice, &devAttrMcuI2cRead);
		device_remove_file(pMcuDevice, &devAttrMcuI2cWrite);
		device_remove_file(pMcuDevice, &devAttrMcuConfig);
		device_remove_file(pMcuDevice, &devAttrMcuFwVersion);

		device_destroy(pDeviceClass, 0);
	}
	*/

	if (!IS_ERR(pDeviceClass)) {
		class_destroy(pDeviceClass);
	}

	gpio_unexport(GPIO_BUZZER);
	gpio_free(GPIO_BUZZER);
	gpio_unexport(GPIO_WATCHDOG_ENABLE);
	gpio_free(GPIO_WATCHDOG_ENABLE);
	gpio_unexport(GPIO_WATCHDOG_HEARTBEAT);
	gpio_free(GPIO_WATCHDOG_HEARTBEAT);
	gpio_unexport(GPIO_WATCHDOG_EXPIRED);
	gpio_free(GPIO_WATCHDOG_EXPIRED);
	gpio_unexport(GPIO_SHUTDOWN);
	gpio_free(GPIO_SHUTDOWN);
}

static int __init
ionopimax_init(void) {
	struct i2c_adapter* i2cAdapter1;
	int result = 0;
	int di, ai;

	printk(KERN_INFO "ionopimax: - | init\n");

	i2c_add_driver(&ionopimax_i2c_driver);

	i2cAdapter1 = i2c_get_adapter(1);
	if (!i2cAdapter1) {
		printk(KERN_ALERT "ionopimax: * | I2C bus 1 not found, have you enabled it?\n");
		goto fail;
	}

	ionopimax_i2c_client = i2c_new_device(i2cAdapter1,
			ionopimax_i2c_board_info);
	if (!ionopimax_i2c_client) {
		printk(KERN_ALERT "ionopimax: * | error creating ionopimax I2C device\n");
		goto fail;
	}

	pDeviceClass = class_create(THIS_MODULE, "ionopimax");
	if (IS_ERR(pDeviceClass)) {
		printk(KERN_ALERT "ionopimax: * | failed to create device class\n");
		goto fail;
	}

	di = 0;
	while (devices[di].name != NULL) {
		devices[di].pDevice = device_create(pDeviceClass, NULL, 0, NULL, devices[di].name);
		if (IS_ERR(devices[di].pDevice)) {
			printk(KERN_ALERT "ionopimax: * | failed to create device '%s'\n", devices[di].name);
			goto fail;
		}
		printk(KERN_ALERT "ionopimax: * | created device '%s'\n", devices[di].name); // TODO remove

		ai = 0;
		while (devices[di].devAttrBeans[ai].devAttr.attr.name != NULL) {
			result |= device_create_file(devices[di].pDevice, &devices[di].devAttrBeans[ai].devAttr);
			if (result) {
				printk(KERN_ALERT "ionopimax: * | failed to create device file '%s/%s'\n",
						devices[di].name, devices[di].devAttrBeans[ai].devAttr.attr.name);
				goto fail;
			}
			printk(KERN_ALERT "ionopimax: * | created device file '%s/%s'\n",
									devices[di].name, devices[di].devAttrBeans[ai].devAttr.attr.name); // TODO remove
			ai++;
		}
		di++;
	}

	/*
	pBuzzerDevice = device_create(pDeviceClass, NULL, 0, NULL, "buzzer");
	pWatchdogDevice = device_create(pDeviceClass, NULL, 0, NULL, "watchdog");
	pPowerDevice = device_create(pDeviceClass, NULL, 0, NULL, "power");
	pSdDevice = device_create(pDeviceClass, NULL, 0, NULL, "sd");
	pMcuDevice = device_create(pDeviceClass, NULL, 0, NULL, "mcu");

	if (IS_ERR(pBuzzerDevice) || IS_ERR(pWatchdogDevice) || IS_ERR(pPowerDevice)
			|| IS_ERR(pSdDevice) || IS_ERR(pMcuDevice)) {
		printk(KERN_ALERT "ionopimax: * | failed to create devices\n");
		goto fail;
	}

	result |= device_create_file(pBuzzerDevice, &devAttrBuzzerStatus);
	result |= device_create_file(pBuzzerDevice, &devAttrBuzzerBeep);

	result |= device_create_file(pWatchdogDevice, &devAttrWatchdogEnabled);
	result |= device_create_file(pWatchdogDevice, &devAttrWatchdogHeartbeat);
	result |= device_create_file(pWatchdogDevice, &devAttrWatchdogExpired);
	result |= device_create_file(pWatchdogDevice, &devAttrWatchdogEnableMode);
	result |= device_create_file(pWatchdogDevice, &devAttrWatchdogTimeout);
	result |= device_create_file(pWatchdogDevice, &devAttrWatchdogDownDelay);
	result |= device_create_file(pWatchdogDevice, &devAttrWatchdogSdSwitch);

	result |= device_create_file(pPowerDevice, &devAttrPowerDownEnabled);
	result |= device_create_file(pPowerDevice, &devAttrPowerDownDelay);
	result |= device_create_file(pPowerDevice, &devAttrPowerDownEnableMode);
	result |= device_create_file(pPowerDevice, &devAttrPowerOffTime);
	result |= device_create_file(pPowerDevice, &devAttrPowerUpDelay);
	result |= device_create_file(pPowerDevice, &devAttrPowerUpMode);
	result |= device_create_file(pPowerDevice, &devAttrPowerSdSwitch);

	result |= device_create_file(pSdDevice, &devAttrSdSdxEnabled);
	result |= device_create_file(pSdDevice, &devAttrSdSd1Enabled);
	result |= device_create_file(pSdDevice, &devAttrSdSdxRouting);
	result |= device_create_file(pSdDevice, &devAttrSdSdxDefault);

	result |= device_create_file(pMcuDevice, &devAttrMcuI2cRead);
	result |= device_create_file(pMcuDevice, &devAttrMcuI2cWrite);
	result |= device_create_file(pMcuDevice, &devAttrMcuConfig);
	result |= device_create_file(pMcuDevice, &devAttrMcuFwVersion);

	if (result) {
		printk(KERN_ALERT "ionopimax: * | failed to create device files\n");
		goto fail;
	}

	*/

	gpio_request(GPIO_BUZZER, "ionopimax_buzzer");
	result |= gpio_direction_output(GPIO_BUZZER, false);
	gpio_export(GPIO_BUZZER, false);

	gpio_request(GPIO_WATCHDOG_ENABLE, "ionopimax_watchdog_enable");
	result |= gpio_direction_output(GPIO_WATCHDOG_ENABLE, false);
	gpio_export(GPIO_WATCHDOG_ENABLE, false);

	gpio_request(GPIO_WATCHDOG_HEARTBEAT, "ionopimax_watchdog_heartbeat");
	result |= gpio_direction_output(GPIO_WATCHDOG_HEARTBEAT, false);
	gpio_export(GPIO_WATCHDOG_HEARTBEAT, false);

	gpio_request(GPIO_WATCHDOG_EXPIRED, "ionopimax_watchdog_expired");
	result |= gpio_direction_input(GPIO_WATCHDOG_EXPIRED);
	gpio_export(GPIO_WATCHDOG_EXPIRED, false);

	gpio_request(GPIO_SHUTDOWN, "ionopimax_shutdown");
	result |= gpio_direction_output(GPIO_SHUTDOWN, false);
	gpio_export(GPIO_SHUTDOWN, false);

	if (result) {
		printk(KERN_ALERT "ionopimax: * | error setting up GPIOs\n");
		goto fail;
	}

	printk(KERN_INFO "ionopimax: - | ready\n");
	return 0;

	fail:
	printk(KERN_ALERT "ionopimax: * | init failed\n");
	cleanup();
	return -1;
}

static void __exit
ionopimax_exit(void) {
	cleanup();
printk(KERN_INFO "ionopimax: - | exit\n");
}

module_init( ionopimax_init);
module_exit( ionopimax_exit);
