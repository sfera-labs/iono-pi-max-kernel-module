#include <linux/device.h>
#include <linux/i2c.h>

ssize_t devAttrAteccSerial_show(struct device* dev,
		struct device_attribute* attr, char *buf);

extern struct i2c_driver atecc_i2c_driver;
