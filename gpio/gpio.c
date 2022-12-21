#include "gpio.h"
#include "../commons/commons.h"
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>

static void debounceTimerRestart(struct DebouncedGpioBean *deb) {
	unsigned long debTime_usec;

	if (gpioGetVal(&deb->gpio)) {
		debTime_usec = deb->onMinTime_usec;
	} else {
		debTime_usec = deb->offMinTime_usec;
	}

	hrtimer_cancel(&deb->timer);
	hrtimer_start(&deb->timer, ktime_set(0, debTime_usec * 1000),
			HRTIMER_MODE_REL);
}

static irqreturn_t debounceIrqHandler(int irq, void *dev) {
	struct DebouncedGpioBean *deb;
	deb = (struct DebouncedGpioBean*) dev;
	if (deb->irq != irq) {
		// should never happen
		return IRQ_HANDLED;
	}
	debounceTimerRestart(deb);
	return IRQ_HANDLED;
}

static enum hrtimer_restart debounceTimerHandler(struct hrtimer *tmr) {
	struct DebouncedGpioBean *deb;
	int val;

	deb = container_of(tmr, struct DebouncedGpioBean, timer);
	val = gpioGetVal(&deb->gpio);

	if (deb->value != val) {
		deb->value = val;
		if (val) {
			deb->onCnt++;
		} else {
			deb->offCnt++;
		}
		if (deb->notifKn != NULL) {
			sysfs_notify_dirent(deb->notifKn);
		}
	}

	return HRTIMER_NORESTART;
}

int gpioInit(struct GpioBean *g) {
	if (g->mode == GPIO_MODE_OUT) {
		gpio_request(g->gpio, g->name);
		return gpio_direction_output(g->gpio, 0);
	} else if (g->mode == GPIO_MODE_IN) {
		gpio_request(g->gpio, g->name);
		return gpio_direction_input(g->gpio);
	}
	return -1;
}

int gpioInitDebounce(struct DebouncedGpioBean *d) {
	int res;

	res = gpioInit(&d->gpio);
	if (res) {
		return res;
	}

	d->irqRequested = false;
	d->value = DEBOUNCE_STATE_NOT_DEFINED;
	d->onMinTime_usec = DEBOUNCE_DEFAULT_TIME_USEC;
	d->offMinTime_usec = DEBOUNCE_DEFAULT_TIME_USEC;
	d->onCnt = 0;
	d->offCnt = 0;

	hrtimer_init(&d->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	d->timer.function = &debounceTimerHandler;

	d->irq = gpio_to_irq(d->gpio.gpio);
	res = request_irq(d->irq, debounceIrqHandler,
			(IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING), d->gpio.name, d);
	if (res) {
		return res;
	}
	d->irqRequested = true;

	debounceTimerRestart(d);

	return res;
}

void gpioFree(struct GpioBean *g) {
	gpio_free(g->gpio);
}

void gpioFreeDebounce(struct DebouncedGpioBean *d) {
	gpioFree(&d->gpio);
	if (d->irqRequested) {
		free_irq(d->irq, d);
		hrtimer_cancel(&d->timer);
		d->irqRequested = false;
	}
}

int gpioGetVal(struct GpioBean *g) {
	int v;
	v = gpio_get_value(g->gpio);
	if (g->invert) {
		v = v == 0 ? 1 : 0;
	}
	return v;
}

void gpioSetVal(struct GpioBean *g, int val) {
	if (g->invert) {
		val = val == 0 ? 1 : 0;
	}
	gpio_set_value(g->gpio, val);
}

ssize_t devAttrGpioMode_show(struct device *dev, struct device_attribute *attr,
		char *buf) {
	struct GpioBean *g;
	g = gpioGetBean(dev, attr);
	if (g == NULL) {
		return -EFAULT;
	}
	if (g->mode == GPIO_MODE_IN) {
		return sprintf(buf, "in\n");
	}
	if (g->mode == GPIO_MODE_OUT) {
		return sprintf(buf, "out\n");
	}
	return sprintf(buf, "x\n");
}

ssize_t devAttrGpioMode_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count) {
	struct GpioBean *g;
	g = gpioGetBean(dev, attr);
	if (g == NULL) {
		return -EFAULT;
	}

	if (g->owner != NULL && g->owner != attr) {
		return -EBUSY;
	}

	if (toUpper(buf[0]) == 'I') {
		g->mode = GPIO_MODE_IN;
	} else if (toUpper(buf[0]) == 'O') {
		g->mode = GPIO_MODE_OUT;
	} else {
		g->mode = 0;
	}

	gpio_free(g->gpio);
	g->owner = NULL;
	if (g->mode != 0) {
		if (gpioInit(g)) {
			g->mode = 0;
			gpio_free(g->gpio);
			return -EFAULT;
		} else {
			g->owner = attr;
		}
	}

	return count;
}

ssize_t devAttrGpio_show(struct device *dev,
		struct device_attribute *attr, char *buf) {
	struct GpioBean *g;
	g = gpioGetBean(dev, attr);
	if (g == NULL) {
		return -EFAULT;
	}
	if (g->mode != GPIO_MODE_IN && g->mode != GPIO_MODE_OUT) {
		return -EPERM;
	}
	return sprintf(buf, "%d\n", gpioGetVal(g));
}

ssize_t devAttrGpio_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count) {
	bool val;
	struct GpioBean *g;
	g = gpioGetBean(dev, attr);
	if (g == NULL) {
		return -EFAULT;
	}
	if (g->mode != GPIO_MODE_OUT) {
		return -EPERM;
	}
	if (kstrtobool(buf, &val) < 0) {
		if (toUpper(buf[0]) == 'E') { // Enable
			val = true;
		} else if (toUpper(buf[0]) == 'D') { // Disable
			val = false;
		} else if (toUpper(buf[0]) == 'F' || toUpper(buf[0]) == 'T') { // Flip/Toggle
			val = gpioGetVal(g) == 1 ? false : true;
		} else {
			return -EINVAL;
		}
	}
	gpioSetVal(g, val ? 1 : 0);
	return count;
}

ssize_t devAttrGpioBlink_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count) {
	int i;
	long on = 0;
	long off = 0;
	long rep = 1;
	char *end = NULL;
	struct GpioBean *g;
	g = gpioGetBean(dev, attr);
	if (g == NULL) {
		return -EFAULT;
	}
	if (g->mode != GPIO_MODE_OUT) {
		return -EPERM;
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
			gpioSetVal(g, 1);
			msleep(on);
			gpioSetVal(g, 0);
			if (i < rep - 1) {
				msleep(off);
			}
		}
	}
	return count;
}

struct DebouncedGpioBean* gpioGetDebouncedBean(struct device *dev,
		struct device_attribute *attr) {
	struct GpioBean *g;
	struct DebouncedGpioBean *d;
	g = gpioGetBean(dev, attr);
	if (g == NULL) {
		return NULL;
	}
	d = container_of(g, struct DebouncedGpioBean, gpio);
	return d;
}

ssize_t devAttrGpioDeb_show(struct device *dev,
		struct device_attribute *attr, char *buf) {
	struct DebouncedGpioBean *d;
	d = gpioGetDebouncedBean(dev, attr);
	if (d == NULL) {
		return -EFAULT;
	}

	if (d->notifKn == NULL) {
		d->notifKn = sysfs_get_dirent(dev->kobj.sd, attr->attr.name);
	}

	return sprintf(buf, "%d\n", d->value);
}

ssize_t devAttrGpioDebMsOn_show(struct device *dev,
		struct device_attribute *attr, char *buf) {
	struct DebouncedGpioBean *d;
	d = gpioGetDebouncedBean(dev, attr);
	if (d == NULL) {
		return -EFAULT;
	}
	return sprintf(buf, "%lu\n", d->onMinTime_usec / 1000);
}

ssize_t devAttrGpioDebMsOff_show(struct device *dev,
		struct device_attribute *attr, char *buf) {
	struct DebouncedGpioBean *d;
	d = gpioGetDebouncedBean(dev, attr);
	if (d == NULL) {
		return -EFAULT;
	}
	return sprintf(buf, "%lu\n", d->offMinTime_usec / 1000);
}

ssize_t devAttrGpioDebMsOn_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count) {
	unsigned int val;
	int ret;
	struct DebouncedGpioBean *d;

	d = gpioGetDebouncedBean(dev, attr);
	if (d == NULL) {
		return -EFAULT;
	}
	ret = kstrtouint(buf, 10, &val);
	if (ret < 0) {
		return ret;
	}
	d->onMinTime_usec = val * 1000;
	d->onCnt = 0;
	d->offCnt = 0;
	d->value = DEBOUNCE_STATE_NOT_DEFINED;
	debounceTimerRestart(d);

	return count;
}

ssize_t devAttrGpioDebMsOff_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count) {
	unsigned int val;
	int ret;
	struct DebouncedGpioBean *d;

	d = gpioGetDebouncedBean(dev, attr);
	if (d == NULL) {
		return -EFAULT;
	}
	ret = kstrtouint(buf, 10, &val);
	if (ret < 0) {
		return ret;
	}
	d->offMinTime_usec = val * 1000;
	d->onCnt = 0;
	d->offCnt = 0;
	d->value = DEBOUNCE_STATE_NOT_DEFINED;
	debounceTimerRestart(d);

	return count;
}

ssize_t devAttrGpioDebOnCnt_show(struct device *dev,
		struct device_attribute *attr, char *buf) {
	struct DebouncedGpioBean *d;
	d = gpioGetDebouncedBean(dev, attr);
	if (d == NULL) {
		return -EFAULT;
	}
	return sprintf(buf, "%lu\n", d->onCnt);
}

ssize_t devAttrGpioDebOffCnt_show(struct device *dev,
		struct device_attribute *attr, char *buf) {
	struct DebouncedGpioBean *d;
	d = gpioGetDebouncedBean(dev, attr);
	if (d == NULL) {
		return -EFAULT;
	}
	return sprintf(buf, "%lu\n", d->offCnt);
}