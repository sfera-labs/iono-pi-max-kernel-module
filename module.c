/*
 * ionopimax
 *
 *     Copyright (C) 2019 Sfera Labs S.r.l.
 *
 *     For information, see the Iono Pi Max web site:
 *     https://www.sferalabs.cc/iono
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

#include "soft_uart/raspberry_soft_uart.h"

#define I2C_ADDR_LOCAL 0x35

#define MODEL_NUM 10 // TODO set actual model num for iono pi max

#define SOFT_UART_BOUDRATE 4800
#define SOFT_UART_RX_BUFF_SIZE 	100

#define FW_MAX_SIZE 16000

#define FW_MAX_DATA_BYTES_PER_LINE 0x20
#define FW_MAX_LINE_LEN (FW_MAX_DATA_BYTES_PER_LINE * 2 + 12)

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
#define GPIO_SOFTSERIAL_TX 37
#define GPIO_SOFTSERIAL_RX 33
#define GPIO_USB1_DISABLE 31
#define GPIO_USB1_FAULT 1
#define GPIO_USB2_DISABLE 30
#define GPIO_USB2_FAULT 0

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sfera Labs - http://sferalabs.cc");
MODULE_DESCRIPTION("Iono Pi Max driver module");
MODULE_VERSION("0.3");

static struct class *pDeviceClass;

static struct device *pBuzzerDevice = NULL;
static struct device *pIoDevice = NULL;
static struct device *pWatchdogDevice = NULL;
static struct device *pPowerDevice = NULL;
static struct device *pUpsDevice = NULL;
static struct device *pRelayDevice = NULL;
static struct device *pLedDevice = NULL;
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

static struct device_attribute devAttrIoAv0;
static struct device_attribute devAttrIoAv1;
static struct device_attribute devAttrIoAv2;
static struct device_attribute devAttrIoAv3;

static struct device_attribute devAttrIoAi0;
static struct device_attribute devAttrIoAi1;
static struct device_attribute devAttrIoAi2;
static struct device_attribute devAttrIoAi3;

static struct device_attribute devAttrPowerDownEnabled;
static struct device_attribute devAttrPowerDownDelay;
static struct device_attribute devAttrPowerDownEnableMode;
static struct device_attribute devAttrPowerOffTime;
static struct device_attribute devAttrPowerUpDelay;
static struct device_attribute devAttrPowerUpMode;
static struct device_attribute devAttrPowerSdSwitch;

static struct device_attribute devAttrUpsBattery;
static struct device_attribute devAttrUpsPowerDelay;

static struct device_attribute devAttrRelayStatus;

static struct device_attribute devAttrLedStatus;
static struct device_attribute devAttrLedBlink;

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

static struct device_attribute devAttrMcuCmd;
static struct device_attribute devAttrMcuI2cRead;
static struct device_attribute devAttrMcuI2cWrite;
static struct device_attribute devAttrMcuConfig;
static struct device_attribute devAttrMcuFwVersion;
static struct device_attribute devAttrMcuFwInstall;
static struct device_attribute devAttrMcuFwInstallProgress;

static DEFINE_MUTEX( mcuMutex);

static bool softUartInitialized;
volatile static char softUartRxBuff[SOFT_UART_RX_BUFF_SIZE];
volatile static int softUartRxBuffIdx;

static uint8_t fwBytes[FW_MAX_SIZE];
static int fwMaxAddr = 0;
static char fwLine[FW_MAX_LINE_LEN];
static int fwLineIdx = 0;
volatile static int fwProgress = 0;

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

static bool startsWith(const char *str, const char *pre) {
	return strncmp(pre, str, strlen(pre)) == 0;
}

static int getGPIO(struct device* dev, struct device_attribute* attr) {
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
	} else if (dev == pRelayDevice) {
		return GPIO_RELAY;
	} else if (dev == pLedDevice) {
		return GPIO_LED;
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

static ssize_t GPIO_show(struct device* dev, struct device_attribute* attr,
		char *buf) {
	int gpio;
	gpio = getGPIO(dev, attr);
	if (gpio < 0) {
		return -EINVAL;
	}
	return sprintf(buf, "%d\n", gpio_get_value(gpio));
}

static ssize_t GPIO_store(struct device* dev, struct device_attribute* attr,
		const char *buf, size_t count) {
	bool val;
	int gpio = getGPIO(dev, attr);
	if (gpio < 0) {
		return -EINVAL;
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

static ssize_t GPIOBlink_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t count) {
	int i;
	long on = 0;
	long off = 0;
	long rep = 1;
	char *end = NULL;
	int gpio = getGPIO(dev, attr);
	if (gpio < 0) {
		return -EINVAL;
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

static int32_t ionopimax_i2c_read(uint8_t reg) {
	int32_t res;
	struct ionopimax_i2c_data *data = i2c_get_clientdata(ionopimax_i2c_client);

	if (!mutex_trylock(&data->update_lock)) {
		printk(KERN_ALERT "stratopi: * | I2C busy\n");
		return -EBUSY;
	}

	printk(KERN_INFO "ionopimax: - | I2C read reg=%u\n", reg);

	res = i2c_smbus_read_word_data(ionopimax_i2c_client, reg);

	mutex_unlock(&data->update_lock);

	if (res < 0) {
		return -EIO;
	}
	return res;
}

static int32_t ionopimax_i2c_write(uint8_t reg, uint16_t val) {
	int32_t res;
	struct ionopimax_i2c_data *data = i2c_get_clientdata(ionopimax_i2c_client);

	if (!mutex_trylock(&data->update_lock)) {
		printk(KERN_ALERT "stratopi: * | I2C busy\n");
		return -EBUSY;
	}

	printk(KERN_INFO "ionopimax: - | I2C write reg=%u val=0x%04x\n", reg, val);

	res = i2c_smbus_write_word_data(ionopimax_i2c_client, reg, val);

	mutex_unlock(&data->update_lock);

	return res;
}

static void getI2cRegisterAndSign(struct device* dev,
		struct device_attribute* attr, int16_t *reg, bool *sign) {
	if (dev == pIoDevice) {
		if (attr == &devAttrIoAv0) {
			*reg = 31;
			*sign = true;
		} else if (attr == &devAttrIoAv1) {
			*reg = 32;
			*sign = true;
		} else if (attr == &devAttrIoAv2) {
			*reg = 33;
			*sign = true;
		} else if (attr == &devAttrIoAv3) {
			*reg = 34;
			*sign = true;
		} else if (attr == &devAttrIoAi0) {
			*reg = 35;
			*sign = true;
		} else if (attr == &devAttrIoAi1) {
			*reg = 36;
			*sign = true;
		} else if (attr == &devAttrIoAi2) {
			*reg = 37;
			*sign = true;
		} else if (attr == &devAttrIoAi3) {
			*reg = 38;
			*sign = true;
		}

	} else if (dev == pWatchdogDevice) {
		if (attr == &devAttrWatchdogTimeout) {
			*reg = 17;
			*sign = false;
		} else if (attr == &devAttrWatchdogDownDelay) {
			*reg = 18;
			*sign = false;
		} else if (attr == &devAttrWatchdogSdSwitch) {
			*reg = 19;
			*sign = false;
		}

	} else if (dev == pPowerDevice) {
		if (attr == &devAttrPowerDownDelay) {
			*reg = 22;
			*sign = false;
		} else if (attr == &devAttrPowerOffTime) {
			*reg = 23;
			*sign = false;
		} else if (attr == &devAttrPowerUpDelay) {
			*reg = 24;
			*sign = false;
		}

	} else if (dev == pMcuDevice) {
		if (attr == &devAttrMcuFwVersion) {
			*reg = 0;
			*sign = false;
		}
	}
}

static ssize_t I2C_word_show(struct device* dev, struct device_attribute* attr,
		char *buf) {
	int32_t res;
	bool sign;
	int16_t reg = -1;

	getI2cRegisterAndSign(dev, attr, &reg, &sign);

	if (reg < 0) {
		return -EINVAL;
	}

	res = ionopimax_i2c_read((uint8_t) reg);

	if (res < 0) {
		return res;
	}

	if (sign) {
		return sprintf(buf, "%d\n", (int16_t) res);
	} else {
		return sprintf(buf, "%d\n", res);
	}
}

static ssize_t I2C_word_store(struct device* dev, struct device_attribute* attr,
		const char *buf, size_t count) {
	long val;
	int ret;
	bool sign;
	int16_t reg = -1;

	getI2cRegisterAndSign(dev, attr, &reg, &sign);

	if (reg < 0) {
		return -EINVAL;
	}

	ret = kstrtol(buf, 10, &val);
	if (ret < 0) {
		return ret;
	}

	if (ionopimax_i2c_write((uint8_t) reg, (uint16_t) val) < 0) {
		return -EIO;
	}

	return count;
}

static ssize_t I2C_bit_show(struct device* dev, struct device_attribute* attr,
		char *buf) {
	// TODO
	return 0;
}

static ssize_t I2C_bit_store(struct device* dev, struct device_attribute* attr,
		const char *buf, size_t count) {
	// TODO
	return count;
}

static int32_t i2c_read_val;

static ssize_t I2C_read_show(struct device* dev, struct device_attribute* attr,
		char *buf) {
	return sprintf(buf, "0x%04x\n", i2c_read_val);
}

static ssize_t I2C_read_store(struct device* dev, struct device_attribute* attr,
		const char *buf, size_t count) {
	int ret;
	long reg;
	ret = kstrtol(buf, 10, &reg);
	if (ret < 0) {
		return ret;
	}

	i2c_read_val = ionopimax_i2c_read((uint8_t) reg);

	if (i2c_read_val < 0) {
		return i2c_read_val;
	}

	return count;
}

static ssize_t I2C_write_store(struct device* dev,
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

static void softUartRxCallback(unsigned char character) {
	if (softUartRxBuffIdx < SOFT_UART_RX_BUFF_SIZE - 1) {
		// printk(KERN_INFO "ionopimax: - | soft uart ch %02X\n", (int) (character & 0xff));
		softUartRxBuff[softUartRxBuffIdx++] = character;
	}
}

static bool softUartSendAndWait(const char *cmd, int cmdLen, int respLen,
		int timeout, bool print) {
	int waitTime = 0;
	raspberry_soft_uart_open(NULL);
	softUartRxBuffIdx = 0;
	if (print) {
		printk(KERN_INFO "ionopimax: - | soft uart >>> %s\n", cmd);;
	}
	raspberry_soft_uart_send_string(cmd, cmdLen);
	while ((softUartRxBuffIdx < respLen || respLen < 0) && waitTime < timeout) {
		msleep(20);
		waitTime += 20;
	}
	raspberry_soft_uart_close();
	softUartRxBuff[softUartRxBuffIdx] = '\0';
	if (print) {
		printk(KERN_INFO "ionopimax: - | soft uart <<< %s\n", softUartRxBuff);;
	}
	return softUartRxBuffIdx == respLen || respLen < 0;
}

static ssize_t MCU_cmd_show(struct device* dev, struct device_attribute* attr,
		char *buf) {
	ssize_t ret;
	if (!mutex_trylock(&mcuMutex)) {
		printk(KERN_ALERT "ionopimax: * | MCU busy\n");
		return -EBUSY;
	}
	ret = sprintf(buf, "%s", softUartRxBuff);
	mutex_unlock(&mcuMutex);
	return ret;
}

static ssize_t MCU_cmd_store(struct device* dev, struct device_attribute* attr,
		const char *buf, size_t count) {
	ssize_t ret;

	if (!mutex_trylock(&mcuMutex)) {
		printk(KERN_ALERT "ionopimax: * | MCU busy\n");
		return -EBUSY;
	}

	if (!softUartSendAndWait(buf, count, -1, 500, true)) {
		ret = -EIO;
	} else {
		ret = count;
	}

	mutex_unlock(&mcuMutex);
	return ret;
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

static int nextByte(const char *buf, int offset) {
	int h, l;
	if (offset >= strlen(buf) - 1) {
		printk(KERN_ALERT "ionopimax: * | invalid hex file - reached end of line\n");
		return -1;
	}
	h = hex2int(buf[offset]);
	l = hex2int(buf[offset + 1]);
	if (h < 0 || l < 0) {
		printk(KERN_ALERT "ionopimax: * | invalid hex file - illegal character\n");
		return -1;
	}
	return (h << 4) | l;
}

static void fwCmdChecksum(char *cmd, int len) {
	int i, checksum = 0;
	for (i = 0; i < len - 2; i++) {
		checksum += (cmd[i] & 0xff);
	}
	checksum = ~checksum;
	cmd[len - 2] = (checksum >> 8) & 0xff;
	cmd[len - 1] = checksum & 0xff;
}

static bool fwSendCmd(int addr, char *cmd, int cmdLen, int respLen,
		const char *respPrefix) {
	cmd[3] = (addr >> 8) & 0xff;
	cmd[4] = addr & 0xff;
	fwCmdChecksum(cmd, 72);
	if (!softUartSendAndWait(cmd, cmdLen, respLen, 1000, false)) {
		printk(KERN_ALERT "ionopimax: * | FW cmd error 1\n");
		return false;
	}
	if (!startsWith((const char *) softUartRxBuff, respPrefix)) {
		printk(KERN_ALERT "ionopimax: * | FW cmd error 2\n");
		return false;
	}
	return true;
}

static ssize_t fwInstall_store(struct device* dev,
		struct device_attribute* attr, const char *buf, size_t bufLen) {
	uint8_t data[FW_MAX_DATA_BYTES_PER_LINE];
	int i, buff_i, count, addrH, addrL, addr, type, checksum, baseAddr = 0;
	bool eof = false;
	char *eol;
	char cmd[72 + 1];

	if (!mutex_trylock(&mcuMutex)) {
		printk(KERN_ALERT "ionopimax: * | MCU busy\n");
		return -EBUSY;
	}

	fwProgress = 0;

	if (startsWith(buf, ":020000040000FA")) {
		printk(KERN_INFO "ionopimax: - | loading firmware file...\n");
		fwLineIdx = 0;
		fwMaxAddr = 0;
		for (i = 0; i < FW_MAX_SIZE; i++) {
			fwBytes[i] = 0xff;
		}
	}

	buff_i = 0;
	while (buff_i < bufLen) {
		if (fwLineIdx == 0 && buf[buff_i++] != ':') {
			continue;
		}

		eol = strchr(buf + buff_i, '\n');
		if (eol == NULL) {
			eol = strchr(buf + buff_i, '\r');
			if (eol == NULL) {
				strcpy(fwLine, buf + buff_i);
				fwLineIdx = bufLen - buff_i;
				printk(KERN_INFO "ionopimax: - | waiting for data...\n");
				mutex_unlock(&mcuMutex);
				return bufLen;
			}
		}

		i = (int) (eol - (buf + buff_i));
		strncpy(fwLine + fwLineIdx, buf + buff_i, i);
		fwLine[i + fwLineIdx] = '\0';
		fwLineIdx = 0;

		// printk(KERN_INFO "ionopimax: - | line - %s\n", fwLine);

		count = nextByte(fwLine, 0);
		addrH = nextByte(fwLine, 2);
		addrL = nextByte(fwLine, 4);
		type = nextByte(fwLine, 6);
		if (count < 0 || addrH < 0 || addrL < 0 || type < 0) {
			mutex_unlock(&mcuMutex);
			return -EINVAL;
		}
		checksum = count + addrH + addrL + type;
		for (i = 0; i < count; i++) {
			data[i] = nextByte(fwLine, 8 + (i * 2));
			if (data[i] < 0) {
				mutex_unlock(&mcuMutex);
				return -EINVAL;
			}
			checksum += data[i];
		}
		checksum += nextByte(fwLine, 8 + (i * 2));
		if ((checksum & 0xff) != 0) {
			printk(KERN_ALERT "ionopimax: * | invalid hex file - checksum error\n");
			mutex_unlock(&mcuMutex);
			return -EINVAL;
		}

		if (type == 0) {
			addr = ((addrH << 8) | addrL) + baseAddr;
			// printk(KERN_INFO "ionopimax: - | addr %d\n", addr);
			if (addr + count < FW_MAX_SIZE) {
				for (i = 0; i < count; i++) {
					fwBytes[addr + i] = data[i];
				}
				i = addr + i - 1;
				if (i > fwMaxAddr) {
					fwMaxAddr = i;
				}
			}
		} else if (type == 1) {
			eof = true;
			break;
		} else if (type == 2) {
			baseAddr = ((data[0] << 8) | data[1]) * 16;
		} else if (type == 4) {
			baseAddr = ((data[0] << 8) | data[1]) << 16;
		} else {
			printk(KERN_INFO "ionopimax: - | ignored recored type %d\n", type);;
		}
	}

	if (!eof) {
		printk(KERN_INFO "ionopimax: - | waiting for data...\n");
		mutex_unlock(&mcuMutex);
		return bufLen;
	}

	if (fwMaxAddr < 0x05be) {
		printk(KERN_ALERT "ionopimax: * | invalid hex file - no model\n");
		mutex_unlock(&mcuMutex);
		return -EINVAL;
	}

	if (MODEL_NUM != fwBytes[0x05be]) {
		printk(KERN_ALERT "ionopimax: * | invalid hex file - missmatching model %d != %d\n", MODEL_NUM, fwBytes[0x05be]);
		mutex_unlock(&mcuMutex);
		return -EINVAL;
	}

	printk(KERN_INFO "ionopimax: - | enabling boot loader...\n");
	if (!softUartSendAndWait("XBOOT", 5, 7, 300, true)) {
		printk(KERN_ALERT "ionopimax: * | boot loader enable error 1\n");
		mutex_unlock(&mcuMutex);
		return -EIO;
	}
	if (strcmp("XBOOTOK", (const char *) softUartRxBuff) != 0
			&& strcmp("XBOOTIN", (const char *) softUartRxBuff) != 0) {
		printk(KERN_ALERT "ionopimax: * | boot loader enable error 2\n");
		mutex_unlock(&mcuMutex);
		return -EIO;
	}
	printk(KERN_INFO "ionopimax: - | boot loader enabled\n");

	gpio_set_value(GPIO_SHUTDOWN, 1);

	cmd[0] = 'X';
	cmd[1] = 'B';
	cmd[2] = 'W';
	cmd[5] = 64;
	cmd[72] = '\0';

	fwMaxAddr += 64;

	printk(KERN_INFO "ionopimax: - | invalidating FW...\n");
	for (i = 0; i < 64; i++) {
		cmd[6 + i] = 0xff;
	}
	if (!fwSendCmd(0x05C0, cmd, 72, 5, "XBWOK")) {
		mutex_unlock(&mcuMutex);
		return -EIO;
	}

	printk(KERN_INFO "ionopimax: - | writing FW...\n");
	for (i = 0; i <= fwMaxAddr - 0x0600; i++) {
		addr = 0x0600 + i;
		cmd[6 + (i % 64)] = fwBytes[addr];
		if (i % 64 == 63) {
			// printk(KERN_INFO "ionopimax: - | writing addr %d\n", addr - 63);
			if (!fwSendCmd(addr - 63, cmd, 72, 5, "XBWOK")) {
				mutex_unlock(&mcuMutex);
				return -EIO;
			}
			fwProgress = i * 50 / (fwMaxAddr - 0x0600);
			printk(KERN_INFO "ionopimax: - | progress %d%%\n", fwProgress);;
		}
	}

	printk(KERN_INFO "ionopimax: - | checking FW...\n");
	cmd[2] = 'R';
	for (i = 0; i <= fwMaxAddr - 0x0600; i++) {
		addr = 0x0600 + i;
		cmd[6 + (i % 64)] = fwBytes[addr];
		if (i % 64 == 63) {
			// printk(KERN_INFO "ionopimax: - | reading addr %d\n", addr - 63);
			if (!fwSendCmd(addr - 63, cmd, 6, 72, "XBR")) {
				mutex_unlock(&mcuMutex);
				return -EIO;
			}
			if (memcmp(cmd, (const char *) softUartRxBuff, 72) != 0) {
				printk(KERN_ALERT "ionopimax: * | FW check error\n");
				mutex_unlock(&mcuMutex);
				return -EIO;
			}
			fwProgress = 50 + i * 49 / (fwMaxAddr - 0x0600);
			printk(KERN_INFO "ionopimax: - | progress %d%%\n", fwProgress);;
		}
	}

	printk(KERN_INFO "ionopimax: - | validating FW...\n");
	cmd[2] = 'W';
	for (i = 0; i < 64; i++) {
		cmd[6 + i] = fwBytes[0x05C0 + i];
	}
	if (!fwSendCmd(0x05C0, cmd, 72, 5, "XBWOK")) {
		mutex_unlock(&mcuMutex);
		return -EIO;
	}

	fwProgress = 100;
	printk(KERN_INFO "ionopimax: - | progress %d%%\n", fwProgress);

	printk(KERN_INFO "ionopimax: - | firmware installed. Waiting for shutdown...\n");

	mutex_unlock(&mcuMutex);
	return bufLen;
}

static ssize_t fwInstallProgress_show(struct device* dev,
		struct device_attribute* attr, char *buf) {
	return sprintf(buf, "%d\n", fwProgress);
}

static struct device_attribute devAttrBuzzerStatus = { //
		.attr = { //
				.name = "status", //
						.mode = 0660, //
				},//
				.show = GPIO_show, //
				.store = GPIO_store, //
		};

static struct device_attribute devAttrBuzzerBeep = { //
		.attr = { //
				.name = "beep", //
						.mode = 0220, //
				},//
				.show = NULL, //
				.store = GPIOBlink_store, //
		};

static struct device_attribute devAttrIoAv0 = { //
		.attr = { //
				.name = "av0", //
						.mode = 0440, //
				},//
				.show = I2C_word_show, //
				.store = NULL, //
		};

static struct device_attribute devAttrIoAv1 = { //
		.attr = { //
				.name = "av1", //
						.mode = 0440, //
				},//
				.show = I2C_word_show, //
				.store = NULL, //
		};

static struct device_attribute devAttrIoAv2 = { //
		.attr = { //
				.name = "av2", //
						.mode = 0440, //
				},//
				.show = I2C_word_show, //
				.store = NULL, //
		};

static struct device_attribute devAttrIoAv3 = { //
		.attr = { //
				.name = "av3", //
						.mode = 0440, //
				},//
				.show = I2C_word_show, //
				.store = NULL, //
		};

static struct device_attribute devAttrIoAi0 = { //
		.attr = { //
				.name = "ai0", //
						.mode = 0440, //
				},//
				.show = I2C_word_show, //
				.store = NULL, //
		};

static struct device_attribute devAttrIoAi1 = { //
		.attr = { //
				.name = "ai1", //
						.mode = 0440, //
				},//
				.show = I2C_word_show, //
				.store = NULL, //
		};

static struct device_attribute devAttrIoAi2 = { //
		.attr = { //
				.name = "ai2", //
						.mode = 0440, //
				},//
				.show = I2C_word_show, //
				.store = NULL, //
		};

static struct device_attribute devAttrIoAi3 = { //
		.attr = { //
				.name = "ai3", //
						.mode = 0440, //
				},//
				.show = I2C_word_show, //
				.store = NULL, //
		};

static struct device_attribute devAttrWatchdogEnabled = { //
		.attr = { //
				.name = "enabled", //
						.mode = 0660, //
				},//
				.show = GPIO_show, //
				.store = GPIO_store, //
		};

static struct device_attribute devAttrWatchdogHeartbeat = { //
		.attr = { //
				.name = "heartbeat", //
						.mode = 0660, //
				},//
				.show = GPIO_show, //
				.store = GPIO_store, //
		};

static struct device_attribute devAttrWatchdogExpired = { //
		.attr = { //
				.name = "expired", //
						.mode = 0440, //
				},//
				.show = GPIO_show, //
				.store = NULL, //
		};

static struct device_attribute devAttrWatchdogEnableMode = { //
		.attr = { //
				.name = "enable_mode", //
						.mode = 0660, //
				},//
				.show = I2C_bit_show, //
				.store = I2C_bit_store, //
		};

static struct device_attribute devAttrWatchdogTimeout = { //
		.attr = { //
				.name = "timeout", //
						.mode = 0660, //
				},//
				.show = I2C_word_show, //
				.store = I2C_word_store, //
		};

static struct device_attribute devAttrWatchdogDownDelay = { //
		.attr = { //
				.name = "down_delay", //
						.mode = 0660, //
				},//
				.show = I2C_word_show, //
				.store = I2C_word_store, //
		};

static struct device_attribute devAttrWatchdogSdSwitch = { //
		.attr = { //
				.name = "sd_switch", //
						.mode = 0660, //
				},//
				.show = I2C_word_show, //
				.store = I2C_word_store, //
		};

static struct device_attribute devAttrPowerDownEnabled = { //
		.attr = { //
				.name = "down_enabled", //
						.mode = 0660, //
				},//
				.show = GPIO_show, //
				.store = GPIO_store, //
		};

static struct device_attribute devAttrPowerDownDelay = { //
		.attr = { //
				.name = "down_delay", //
						.mode = 0660, //
				},//
				.show = I2C_word_show, //
				.store = I2C_word_store, //
		};

static struct device_attribute devAttrPowerDownEnableMode = { //
		.attr = { //
				.name = "down_enable_mode", //
						.mode = 0660, //
				},//
				.show = I2C_bit_show, //
				.store = I2C_bit_store, //
		};

static struct device_attribute devAttrPowerOffTime = { //
		.attr = { //
				.name = "off_time", //
						.mode = 0660, //
				},//
				.show = I2C_word_show, //
				.store = I2C_word_store, //
		};

static struct device_attribute devAttrPowerUpDelay = { //
		.attr = { //
				.name = "up_delay", //
						.mode = 0660, //
				},//
				.show = I2C_word_show, //
				.store = I2C_word_store, //
		};

static struct device_attribute devAttrPowerUpMode = { //
		.attr = { //
				.name = "up_mode", //
						.mode = 0660, //
				},//
				.show = I2C_bit_show, //
				.store = I2C_bit_store, //
		};

static struct device_attribute devAttrPowerSdSwitch = { //
		.attr = { //
				.name = "sd_switch", //
						.mode = 0660, //
				},//
				.show = I2C_bit_show, //
				.store = I2C_bit_store, //
		};

static struct device_attribute devAttrUpsBattery = { //
		.attr = { //
				.name = "battery", //
						.mode = 0440, //
				},//
				.show = GPIO_show, //
				.store = NULL, //
		};

static struct device_attribute devAttrUpsPowerDelay = { //
		.attr = { //
				.name = "power_delay", //
						.mode = 0660, //
				},//
				.show = I2C_word_show, //
				.store = I2C_word_store, //
		};

static struct device_attribute devAttrRelayStatus = { //
		.attr = { //
				.name = "status", //
						.mode = 0660, //
				},//
				.show = GPIO_show, //
				.store = GPIO_store, //
		};

static struct device_attribute devAttrLedStatus = { //
		.attr = { //
				.name = "status", //
						.mode = 0660, //
				},//
				.show = GPIO_show, //
				.store = GPIO_store, //
		};

static struct device_attribute devAttrLedBlink = { //
		.attr = { //
				.name = "blink", //
						.mode = 0220, //
				},//
				.show = NULL, //
				.store = GPIOBlink_store, //
		};

static struct device_attribute devAttrButtonStatus = { //
		.attr = { //
				.name = "status", //
						.mode = 0440, //
				},//
				.show = GPIO_show, //
				.store = NULL, //
		};

static struct device_attribute devAttrExpBusEnabled = { //
		.attr = { //
				.name = "enabled", //
						.mode = 0660, //
				},//
				.show = GPIO_show, //
				.store = GPIO_store, //
		};

static struct device_attribute devAttrExpBusAux = { //
		.attr = { //
				.name = "aux", //
						.mode = 0440, //
				},//
				.show = GPIO_show, //
				.store = NULL, //
		};

static struct device_attribute devAttrSdSdxEnabled = { //
		.attr = { //
				.name = "sdx_enabled", //
						.mode = 0660, //
				},//
				.show = I2C_bit_show, //
				.store = I2C_bit_store, //
		};

static struct device_attribute devAttrSdSd1Enabled = { //
		.attr = { //
				.name = "sd1_enabled", //
						.mode = 0660, //
				},//
				.show = I2C_bit_show, //
				.store = I2C_bit_store, //
		};

static struct device_attribute devAttrSdSdxRouting = { //
		.attr = { //
				.name = "sdx_routing", //
						.mode = 0660, //
				},//
				.show = I2C_bit_show, //
				.store = I2C_bit_store, //
		};

static struct device_attribute devAttrSdSdxDefault = { //
		.attr = { //
				.name = "sdx_default", //
						.mode = 0660, //
				},//
				.show = I2C_bit_show, //
				.store = I2C_bit_store, //
		};

static struct device_attribute devAttrUsb1Disabled = { //
		.attr = { //
				.name = "disabled", //
						.mode = 0660, //
				},//
				.show = GPIO_show, //
				.store = GPIO_store, //
		};

static struct device_attribute devAttrUsb1Ok = { //
		.attr = { //
				.name = "ok", //
						.mode = 0440, //
				},//
				.show = GPIO_show, //
				.store = NULL, //
		};

static struct device_attribute devAttrUsb2Disabled = { //
		.attr = { //
				.name = "disabled", //
						.mode = 0660, //
				},//
				.show = GPIO_show, //
				.store = GPIO_store, //
		};

static struct device_attribute devAttrUsb2Ok = { //
		.attr = { //
				.name = "ok", //
						.mode = 0440, //
				},//
				.show = GPIO_show, //
				.store = NULL, //
		};

static struct device_attribute devAttrMcuCmd = { //
		.attr = { //
				.name = "cmd", //
						.mode = 0660, //
				},//
				.show = MCU_cmd_show, //
				.store = MCU_cmd_store, //
		};

static struct device_attribute devAttrMcuI2cRead = { //
		.attr = { //
				.name = "i2c_read", //
						.mode = 0660, //
				},//
				.show = I2C_read_show, //
				.store = I2C_read_store, //
		};

static struct device_attribute devAttrMcuI2cWrite = { //
		.attr = { //
				.name = "i2c_write", //
						.mode = 0220, //
				},//
				.show = NULL, //
				.store = I2C_write_store, //
		};

static struct device_attribute devAttrMcuConfig = { //
		.attr = { //
				.name = "config", //
						.mode = 0220, //
				},//
				.show = NULL, //
				.store = I2C_bit_store, //
		};

static struct device_attribute devAttrMcuFwVersion = { //
		.attr = { //
				.name = "fw_version", //
						.mode = 0440, //
				},//
				.show = I2C_word_show, //
				.store = NULL, //
		};

static struct device_attribute devAttrMcuFwInstall = { //
		.attr = { //
				.name = "fw_install", //
						.mode = 0220, //
				},//
				.show = NULL, //
				.store = fwInstall_store, //
		};

static struct device_attribute devAttrMcuFwInstallProgress = { //
		.attr = { //
				.name = "fw_install_progress", //
						.mode = 0440, //
				},//
				.show = fwInstallProgress_show, //
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

	printk(KERN_INFO "ionopimax: - | ionopimax_i2c_probe device addr 0x%02hx\n", client->addr); // TODO remove
	if (client->addr != I2C_ADDR_LOCAL) {
		// TODO test communication and add external device and sysFs files
		printk(KERN_INFO "ionopimax: - | ionopimax_i2c_probe external device 0x%02hx\n", client->addr);;
	}

	return 0;
}

static int ionopimax_i2c_remove(struct i2c_client *client) {
	struct ionopimax_i2c_data *data = i2c_get_clientdata(client);
	mutex_destroy(&data->update_lock);

	printk(KERN_INFO "ionopimax: - | ionopimax_i2c_remove device addr 0x%02hx\n", client->addr); // TODO remove
	if (client->addr != I2C_ADDR_LOCAL) {
		// TODO remove external device and sysFs files
		printk(KERN_INFO "ionopimax: - | ionopimax_i2c_remove external device 0x%02hx\n", client->addr);;
	}

	return 0;
}

static const struct i2c_device_id ionopimax_i2c_id[] = { //
		{ "ionopimax", 0 }, //
				{ } //
		};
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
	if (ionopimax_i2c_client) {
		i2c_unregister_device(ionopimax_i2c_client);
	}
	i2c_del_driver(&ionopimax_i2c_driver);

	if (pBuzzerDevice && !IS_ERR(pBuzzerDevice)) {
		device_remove_file(pBuzzerDevice, &devAttrBuzzerStatus);
		device_remove_file(pBuzzerDevice, &devAttrBuzzerBeep);

		device_destroy(pDeviceClass, 0);

		gpio_unexport(GPIO_BUZZER);
		gpio_free(GPIO_BUZZER);
	}

	if (pIoDevice && !IS_ERR(pIoDevice)) {
		device_remove_file(pIoDevice, &devAttrIoAv0);
		device_remove_file(pIoDevice, &devAttrIoAv1);
		device_remove_file(pIoDevice, &devAttrIoAv2);
		device_remove_file(pIoDevice, &devAttrIoAv3);

		device_remove_file(pIoDevice, &devAttrIoAi0);
		device_remove_file(pIoDevice, &devAttrIoAi1);
		device_remove_file(pIoDevice, &devAttrIoAi2);
		device_remove_file(pIoDevice, &devAttrIoAi3);

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
		device_remove_file(pMcuDevice, &devAttrMcuCmd);
		device_remove_file(pMcuDevice, &devAttrMcuI2cRead);
		device_remove_file(pMcuDevice, &devAttrMcuI2cWrite);
		device_remove_file(pMcuDevice, &devAttrMcuConfig);
		device_remove_file(pMcuDevice, &devAttrMcuFwVersion);
		device_remove_file(pMcuDevice, &devAttrMcuFwInstall);
		device_remove_file(pMcuDevice, &devAttrMcuFwInstallProgress);

		device_destroy(pDeviceClass, 0);
	}

	if (!IS_ERR(pDeviceClass)) {
		class_destroy(pDeviceClass);
	}

	gpio_unexport(GPIO_WATCHDOG_ENABLE);
	gpio_free(GPIO_WATCHDOG_ENABLE);
	gpio_unexport(GPIO_WATCHDOG_HEARTBEAT);
	gpio_free(GPIO_WATCHDOG_HEARTBEAT);
	gpio_unexport(GPIO_WATCHDOG_EXPIRED);
	gpio_free(GPIO_WATCHDOG_EXPIRED);
	gpio_unexport(GPIO_SHUTDOWN);
	gpio_free(GPIO_SHUTDOWN);

	if (softUartInitialized) {
		if (!raspberry_soft_uart_finalize()) {
			printk(KERN_ALERT "ionopimax: * | error finalizing soft UART\n");;
		}
	}

	mutex_destroy(&mcuMutex);
}

static bool softUartInit(void) {
	if (!raspberry_soft_uart_init(GPIO_SOFTSERIAL_TX, GPIO_SOFTSERIAL_RX)) {
		return false;
	}
	if (!raspberry_soft_uart_set_baudrate(SOFT_UART_BOUDRATE)) {
		raspberry_soft_uart_finalize();
		return false;
	}
	msleep(50);
	return true;
}

static int __init ionopimax_init(void) {
	struct i2c_adapter* i2cAdapter1;
	int result = 0;
	softUartInitialized = false;

	printk(KERN_INFO "ionopimax: - | init\n");

	mutex_init(&mcuMutex);

	i2c_add_driver(&ionopimax_i2c_driver);

	i2cAdapter1 = i2c_get_adapter(1);
	if (!i2cAdapter1) {
		printk(KERN_ALERT "ionopimax: * | I2C bus 1 not found, have you enabled it?\n");
		goto fail;
	}

	ionopimax_i2c_client = i2c_new_device(i2cAdapter1, ionopimax_i2c_board_info);
	if (!ionopimax_i2c_client) {
		printk(KERN_ALERT "ionopimax: * | error creating ionopimax I2C device\n");
		goto fail;
	}

	if (!raspberry_soft_uart_set_rx_callback(&softUartRxCallback)) {
		printk(KERN_ALERT "ionopimax: * | error setting soft UART callback\n");
		goto fail;
	}

	if (!softUartInit()) {
		printk(KERN_ALERT "ionopimax: * | error initializing soft UART\n");
		goto fail;
	}

	softUartInitialized = true;

	pDeviceClass = class_create(THIS_MODULE, "ionopimax");
	if (IS_ERR(pDeviceClass)) {
		printk(KERN_ALERT "ionopimax: * | failed to create device class\n");
		goto fail;
	}

	pBuzzerDevice = device_create(pDeviceClass, NULL, 0, NULL, "buzzer");
	pIoDevice = device_create(pDeviceClass, NULL, 0, NULL, "io");
	pWatchdogDevice = device_create(pDeviceClass, NULL, 0, NULL, "watchdog");
	pPowerDevice = device_create(pDeviceClass, NULL, 0, NULL, "power");
	pSdDevice = device_create(pDeviceClass, NULL, 0, NULL, "sd");
	pMcuDevice = device_create(pDeviceClass, NULL, 0, NULL, "mcu");

	if (IS_ERR(pBuzzerDevice) || IS_ERR(pIoDevice) || IS_ERR(pWatchdogDevice)
			|| IS_ERR(pPowerDevice) || IS_ERR(pSdDevice) || IS_ERR(pMcuDevice)) {
		printk(KERN_ALERT "ionopimax: * | failed to create devices\n");
		goto fail;
	}

	result |= device_create_file(pBuzzerDevice, &devAttrBuzzerStatus);
	result |= device_create_file(pBuzzerDevice, &devAttrBuzzerBeep);

	result |= device_create_file(pIoDevice, &devAttrIoAv0);
	result |= device_create_file(pIoDevice, &devAttrIoAv1);
	result |= device_create_file(pIoDevice, &devAttrIoAv2);
	result |= device_create_file(pIoDevice, &devAttrIoAv3);

	result |= device_create_file(pIoDevice, &devAttrIoAi0);
	result |= device_create_file(pIoDevice, &devAttrIoAi1);
	result |= device_create_file(pIoDevice, &devAttrIoAi2);
	result |= device_create_file(pIoDevice, &devAttrIoAi3);

	result |= device_create_file(pWatchdogDevice, &devAttrWatchdogEnabled);
	result |= device_create_file(pWatchdogDevice, &devAttrWatchdogHeartbeat);
	result |= device_create_file(pWatchdogDevice, &devAttrWatchdogExpired);
	result |= device_create_file(pWatchdogDevice, &devAttrWatchdogEnableMode);
	result |= device_create_file(pWatchdogDevice, &devAttrWatchdogTimeout);
	result |= device_create_file(pWatchdogDevice, &devAttrWatchdogDownDelay);

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

	result |= device_create_file(pMcuDevice, &devAttrMcuCmd);
	result |= device_create_file(pMcuDevice, &devAttrMcuI2cRead);
	result |= device_create_file(pMcuDevice, &devAttrMcuI2cWrite);
	result |= device_create_file(pMcuDevice, &devAttrMcuConfig);
	result |= device_create_file(pMcuDevice, &devAttrMcuFwVersion);
	result |= device_create_file(pMcuDevice, &devAttrMcuFwInstall);
	result |= device_create_file(pMcuDevice, &devAttrMcuFwInstallProgress);

	if (result) {
		printk(KERN_ALERT "ionopimax: * | failed to create device files\n");
		goto fail;
	}

	if (pBuzzerDevice) {
		gpio_request(GPIO_BUZZER, "ionopimax_buzzer");
		result |= gpio_direction_output(GPIO_BUZZER, false);
		gpio_export(GPIO_BUZZER, false);
	}

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

static void __exit ionopimax_exit(void) {
	cleanup();
	printk(KERN_INFO "ionopimax: - | exit\n");
}

module_init( ionopimax_init);
module_exit( ionopimax_exit);
