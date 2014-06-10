/*
 *  HID driver for Lenovo:-
 *  * ThinkPad USB Keyboard with TrackPoint
 *  * ThinkPad Compact Bluetooth Keyboard with TrackPoint
 *  * ThinkPad Compact USB Keyboard with TrackPoint
 *
 *  Copyright (c) 2012 Bernhard Seibold
 *  Copyright (c) 2014 Jamie Lentin <jm@lentin.co.uk>
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/module.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/leds.h>

#include "hid-ids.h"

/* This is only used for the trackpoint part of the driver, hence _tp */
struct tpkbd_data_pointer {
	int led_state;
	struct led_classdev led_mute;
	struct led_classdev led_micmute;
	int press_to_select;
	int dragging;
	int release_to_select;
	int select_right;
	int sensitivity;
	int press_speed;
};

struct tpcompactkbd_sc {
	unsigned int fn_lock;
};

#define map_key_clear(c) hid_map_usage_clear(hi, usage, bit, max, EV_KEY, (c))

static int tpkbd_input_mapping_tp(struct hid_device *hdev,
		struct hid_input *hi, struct hid_field *field,
		struct hid_usage *usage, unsigned long **bit, int *max)
{
	if (usage->hid == (HID_UP_BUTTON | 0x0010)) {
		/* mark the device as pointer */
		hid_set_drvdata(hdev, (void *)1);
		map_key_clear(KEY_MICMUTE);
		return 1;
	}
	return 0;
}

static int tpcompactkbd_input_mapping(struct hid_device *hdev,
		struct hid_input *hi, struct hid_field *field,
		struct hid_usage *usage, unsigned long **bit, int *max)
{
	/* HID_UP_LNVENDOR = USB, HID_UP_MSVENDOR = BT */
	if ((usage->hid & HID_USAGE_PAGE) == HID_UP_MSVENDOR ||
	    (usage->hid & HID_USAGE_PAGE) == HID_UP_LNVENDOR) {
		set_bit(EV_REP, hi->input->evbit);
		switch (usage->hid & HID_USAGE) {
		case 0x00f1: /* Fn-F4: Mic mute */
			map_key_clear(KEY_MICMUTE);
			return 1;
		case 0x00f2: /* Fn-F5: Brightness down */
			map_key_clear(KEY_BRIGHTNESSDOWN);
			return 1;
		case 0x00f3: /* Fn-F6: Brightness up */
			map_key_clear(KEY_BRIGHTNESSUP);
			return 1;
		case 0x00f4: /* Fn-F7: External display (projector) */
			map_key_clear(KEY_SWITCHVIDEOMODE);
			return 1;
		case 0x00f5: /* Fn-F8: Wireless */
			map_key_clear(KEY_WLAN);
			return 1;
		case 0x00f6: /* Fn-F9: Control panel */
			map_key_clear(KEY_CONFIG);
			return 1;
		case 0x00f8: /* Fn-F11: View open applications (3 boxes) */
			map_key_clear(KEY_FN_F11);
			return 1;
		case 0x00fa: /* Fn-Esc: Fn-lock toggle */
			map_key_clear(KEY_FN_ESC);
			return 1;
		case 0x00fb: /* Fn-F12: Open My computer (6 boxes) USB-only */
			/* NB: This mapping is invented in raw_event below */
			map_key_clear(KEY_FILE);
			return 1;
		}
	}

	return 0;
}

static int tpkbd_input_mapping(struct hid_device *hdev,
		struct hid_input *hi, struct hid_field *field,
		struct hid_usage *usage, unsigned long **bit, int *max)
{
	if (hdev->product == USB_DEVICE_ID_LENOVO_TPKBD)
		return tpkbd_input_mapping_tp(hdev, hi, field, usage, bit, max);
	if (hdev->product == USB_DEVICE_ID_LENOVO_CUSBKBD)
		return tpcompactkbd_input_mapping(hdev, hi, field, usage, bit, max);
	if (hdev->product == USB_DEVICE_ID_LENOVO_CBTKBD)
		return tpcompactkbd_input_mapping(hdev, hi, field, usage, bit, max);
	return 0;
}

#undef map_key_clear

/* Send a config command to the keyboard */
static int tpcompactkbd_send_cmd(struct hid_device *hdev,
			unsigned char byte2, unsigned char byte3)
{
	int ret;
	unsigned char buf[] = {0x18, byte2, byte3};
	unsigned char report_type = HID_OUTPUT_REPORT;

	/* The USB keyboard accepts commands via SET_FEATURE */
	if (hdev->product == USB_DEVICE_ID_LENOVO_CUSBKBD) {
		buf[0] = 0x13;
		report_type = HID_FEATURE_REPORT;
	}

	ret = hdev->hid_output_raw_report(hdev, buf, sizeof(buf), report_type);
	return ret < 0 ? ret : 0; /* BT returns 0, USB returns sizeof(buf) */
}

static void tpcompactkbd_features_set(struct hid_device *hdev)
{
	struct tpcompactkbd_sc *tpcsc = hid_get_drvdata(hdev);

	if (tpcompactkbd_send_cmd(hdev, 0x05, tpcsc->fn_lock ? 0x01 : 0x00))
		hid_err(hdev, "Fn-lock setting failed\n");
}

static ssize_t tpcompactkbd_fn_lock_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct tpcompactkbd_sc *tpcsc = hid_get_drvdata(hdev);

	return snprintf(buf, PAGE_SIZE, "%u\n", tpcsc->fn_lock);
}

static ssize_t tpcompactkbd_fn_lock_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct tpcompactkbd_sc *tpcsc = hid_get_drvdata(hdev);
	int value;

	if (kstrtoint(buf, 10, &value))
		return -EINVAL;
	if (value < 0 || value > 1)
		return -EINVAL;

	tpcsc->fn_lock = value;
	tpcompactkbd_features_set(hdev);

	return count;
}

static struct device_attribute dev_attr_pointer_fn_lock =
	__ATTR(fn_lock, S_IWUSR | S_IRUGO,
			tpcompactkbd_fn_lock_show,
			tpcompactkbd_fn_lock_store);

static struct attribute *tpcompactkbd_attributes[] = {
	&dev_attr_pointer_fn_lock.attr,
	NULL
};

static const struct attribute_group tpcompactkbd_attr_group = {
	.attrs = tpcompactkbd_attributes,
};

static int tpkbd_raw_event(struct hid_device *hdev,
			struct hid_report *report, u8 *data, int size)
{
	/*
	 * USB keyboard's Fn-F12 report holds down many other keys, and it's
	 * own key is outside the usage page range. Remove extra keypresses and
	 * remap to inside usage page.
	 */
	if (unlikely(hdev->product == USB_DEVICE_ID_LENOVO_CUSBKBD
			&& size == 3
			&& data[0] == 0x15
			&& data[1] == 0x94
			&& data[2] == 0x01)) {
		data[1] = 0x0;
		data[2] = 0x4;
	}

	return 0;
}

static int tpkbd_features_set(struct hid_device *hdev)
{
	struct hid_report *report;
	struct tpkbd_data_pointer *data_pointer = hid_get_drvdata(hdev);

	report = hdev->report_enum[HID_FEATURE_REPORT].report_id_hash[4];

	report->field[0]->value[0]  = data_pointer->press_to_select   ? 0x01 : 0x02;
	report->field[0]->value[0] |= data_pointer->dragging          ? 0x04 : 0x08;
	report->field[0]->value[0] |= data_pointer->release_to_select ? 0x10 : 0x20;
	report->field[0]->value[0] |= data_pointer->select_right      ? 0x80 : 0x40;
	report->field[1]->value[0] = 0x03; // unknown setting, imitate windows driver
	report->field[2]->value[0] = data_pointer->sensitivity;
	report->field[3]->value[0] = data_pointer->press_speed;

	hid_hw_request(hdev, report, HID_REQ_SET_REPORT);
	return 0;
}

static ssize_t pointer_press_to_select_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct tpkbd_data_pointer *data_pointer = hid_get_drvdata(hdev);

	return snprintf(buf, PAGE_SIZE, "%u\n", data_pointer->press_to_select);
}

static ssize_t pointer_press_to_select_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct tpkbd_data_pointer *data_pointer = hid_get_drvdata(hdev);
	int value;

	if (kstrtoint(buf, 10, &value))
		return -EINVAL;
	if (value < 0 || value > 1)
		return -EINVAL;

	data_pointer->press_to_select = value;
	tpkbd_features_set(hdev);

	return count;
}

static ssize_t pointer_dragging_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct tpkbd_data_pointer *data_pointer = hid_get_drvdata(hdev);

	return snprintf(buf, PAGE_SIZE, "%u\n", data_pointer->dragging);
}

static ssize_t pointer_dragging_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct tpkbd_data_pointer *data_pointer = hid_get_drvdata(hdev);
	int value;

	if (kstrtoint(buf, 10, &value))
		return -EINVAL;
	if (value < 0 || value > 1)
		return -EINVAL;

	data_pointer->dragging = value;
	tpkbd_features_set(hdev);

	return count;
}

static ssize_t pointer_release_to_select_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct tpkbd_data_pointer *data_pointer = hid_get_drvdata(hdev);

	return snprintf(buf, PAGE_SIZE, "%u\n", data_pointer->release_to_select);
}

static ssize_t pointer_release_to_select_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct tpkbd_data_pointer *data_pointer = hid_get_drvdata(hdev);
	int value;

	if (kstrtoint(buf, 10, &value))
		return -EINVAL;
	if (value < 0 || value > 1)
		return -EINVAL;

	data_pointer->release_to_select = value;
	tpkbd_features_set(hdev);

	return count;
}

static ssize_t pointer_select_right_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct tpkbd_data_pointer *data_pointer = hid_get_drvdata(hdev);

	return snprintf(buf, PAGE_SIZE, "%u\n", data_pointer->select_right);
}

static ssize_t pointer_select_right_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct tpkbd_data_pointer *data_pointer = hid_get_drvdata(hdev);
	int value;

	if (kstrtoint(buf, 10, &value))
		return -EINVAL;
	if (value < 0 || value > 1)
		return -EINVAL;

	data_pointer->select_right = value;
	tpkbd_features_set(hdev);

	return count;
}

static ssize_t pointer_sensitivity_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct tpkbd_data_pointer *data_pointer = hid_get_drvdata(hdev);

	return snprintf(buf, PAGE_SIZE, "%u\n",
		data_pointer->sensitivity);
}

static ssize_t pointer_sensitivity_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct tpkbd_data_pointer *data_pointer = hid_get_drvdata(hdev);
	int value;

	if (kstrtoint(buf, 10, &value) || value < 1 || value > 255)
		return -EINVAL;

	data_pointer->sensitivity = value;
	tpkbd_features_set(hdev);

	return count;
}

static ssize_t pointer_press_speed_show(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct tpkbd_data_pointer *data_pointer = hid_get_drvdata(hdev);

	return snprintf(buf, PAGE_SIZE, "%u\n",
		data_pointer->press_speed);
}

static ssize_t pointer_press_speed_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t count)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct tpkbd_data_pointer *data_pointer = hid_get_drvdata(hdev);
	int value;

	if (kstrtoint(buf, 10, &value) || value < 1 || value > 255)
		return -EINVAL;

	data_pointer->press_speed = value;
	tpkbd_features_set(hdev);

	return count;
}

static struct device_attribute dev_attr_pointer_press_to_select =
	__ATTR(press_to_select, S_IWUSR | S_IRUGO,
			pointer_press_to_select_show,
			pointer_press_to_select_store);

static struct device_attribute dev_attr_pointer_dragging =
	__ATTR(dragging, S_IWUSR | S_IRUGO,
			pointer_dragging_show,
			pointer_dragging_store);

static struct device_attribute dev_attr_pointer_release_to_select =
	__ATTR(release_to_select, S_IWUSR | S_IRUGO,
			pointer_release_to_select_show,
			pointer_release_to_select_store);

static struct device_attribute dev_attr_pointer_select_right =
	__ATTR(select_right, S_IWUSR | S_IRUGO,
			pointer_select_right_show,
			pointer_select_right_store);

static struct device_attribute dev_attr_pointer_sensitivity =
	__ATTR(sensitivity, S_IWUSR | S_IRUGO,
			pointer_sensitivity_show,
			pointer_sensitivity_store);

static struct device_attribute dev_attr_pointer_press_speed =
	__ATTR(press_speed, S_IWUSR | S_IRUGO,
			pointer_press_speed_show,
			pointer_press_speed_store);

static struct attribute *tpkbd_attributes_pointer[] = {
	&dev_attr_pointer_press_to_select.attr,
	&dev_attr_pointer_dragging.attr,
	&dev_attr_pointer_release_to_select.attr,
	&dev_attr_pointer_select_right.attr,
	&dev_attr_pointer_sensitivity.attr,
	&dev_attr_pointer_press_speed.attr,
	NULL
};

static const struct attribute_group tpkbd_attr_group_pointer = {
	.attrs = tpkbd_attributes_pointer,
};

static enum led_brightness tpkbd_led_brightness_get(
			struct led_classdev *led_cdev)
{
	struct device *dev = led_cdev->dev->parent;
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct tpkbd_data_pointer *data_pointer = hid_get_drvdata(hdev);
	int led_nr = 0;

	if (led_cdev == &data_pointer->led_micmute)
		led_nr = 1;

	return data_pointer->led_state & (1 << led_nr)
				? LED_FULL
				: LED_OFF;
}

static void tpkbd_led_brightness_set(struct led_classdev *led_cdev,
			enum led_brightness value)
{
	struct device *dev = led_cdev->dev->parent;
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct tpkbd_data_pointer *data_pointer = hid_get_drvdata(hdev);
	struct hid_report *report;
	int led_nr = 0;

	if (led_cdev == &data_pointer->led_micmute)
		led_nr = 1;

	if (value == LED_OFF)
		data_pointer->led_state &= ~(1 << led_nr);
	else
		data_pointer->led_state |= 1 << led_nr;

	report = hdev->report_enum[HID_OUTPUT_REPORT].report_id_hash[3];
	report->field[0]->value[0] = (data_pointer->led_state >> 0) & 1;
	report->field[0]->value[1] = (data_pointer->led_state >> 1) & 1;
	hid_hw_request(hdev, report, HID_REQ_SET_REPORT);
}

static int tpkbd_probe_tp(struct hid_device *hdev)
{
	struct device *dev = &hdev->dev;
	struct tpkbd_data_pointer *data_pointer;
	size_t name_sz = strlen(dev_name(dev)) + 16;
	char *name_mute, *name_micmute;
	int i;

	/* Ignore unless tpkbd_input_mapping_tp marked it as a pointer */
	if (!hid_get_drvdata(hdev))
		return 0;
	hid_set_drvdata(hdev, NULL);

	/* Validate required reports. */
	for (i = 0; i < 4; i++) {
		if (!hid_validate_values(hdev, HID_FEATURE_REPORT, 4, i, 1))
			return -ENODEV;
	}
	if (!hid_validate_values(hdev, HID_OUTPUT_REPORT, 3, 0, 2))
		return -ENODEV;

	if (sysfs_create_group(&hdev->dev.kobj,
				&tpkbd_attr_group_pointer)) {
		hid_warn(hdev, "Could not create sysfs group\n");
	}

	data_pointer = devm_kzalloc(&hdev->dev,
				    sizeof(struct tpkbd_data_pointer),
				    GFP_KERNEL);
	if (data_pointer == NULL) {
		hid_err(hdev, "Could not allocate memory for driver data\n");
		return -ENOMEM;
	}

	// set same default values as windows driver
	data_pointer->sensitivity = 0xa0;
	data_pointer->press_speed = 0x38;

	name_mute = devm_kzalloc(&hdev->dev, name_sz, GFP_KERNEL);
	name_micmute = devm_kzalloc(&hdev->dev, name_sz, GFP_KERNEL);
	if (name_mute == NULL || name_micmute == NULL) {
		hid_err(hdev, "Could not allocate memory for led data\n");
		return -ENOMEM;
	}
	snprintf(name_mute, name_sz, "%s:amber:mute", dev_name(dev));
	snprintf(name_micmute, name_sz, "%s:amber:micmute", dev_name(dev));

	hid_set_drvdata(hdev, data_pointer);

	data_pointer->led_mute.name = name_mute;
	data_pointer->led_mute.brightness_get = tpkbd_led_brightness_get;
	data_pointer->led_mute.brightness_set = tpkbd_led_brightness_set;
	data_pointer->led_mute.dev = dev;
	led_classdev_register(dev, &data_pointer->led_mute);

	data_pointer->led_micmute.name = name_micmute;
	data_pointer->led_micmute.brightness_get = tpkbd_led_brightness_get;
	data_pointer->led_micmute.brightness_set = tpkbd_led_brightness_set;
	data_pointer->led_micmute.dev = dev;
	led_classdev_register(dev, &data_pointer->led_micmute);

	tpkbd_features_set(hdev);

	return 0;
}

static int tpcompactkbd_probe(struct hid_device *hdev,
			const struct hid_device_id *id)
{
	int ret;
	struct tpcompactkbd_sc *tpcsc;

	tpcsc = devm_kzalloc(&hdev->dev, sizeof(*tpcsc), GFP_KERNEL);
	if (tpcsc == NULL) {
		hid_err(hdev, "can't alloc keyboard descriptor\n");
		return -ENOMEM;
	}
	hid_set_drvdata(hdev, tpcsc);

	/* All the custom action happens on the mouse device for USB */
	if (hdev->product == USB_DEVICE_ID_LENOVO_CUSBKBD
			&& hdev->type != HID_TYPE_USBMOUSE) {
		pr_debug("Ignoring keyboard half of device\n");
		return 0;
	}

	/*
	 * Tell the keyboard a driver understands it, and turn F7, F9, F11 into
	 * regular keys
	 */
	ret = tpcompactkbd_send_cmd(hdev, 0x01, 0x03);
	if (ret)
		hid_warn(hdev, "Failed to switch F7/9/11 into regular keys\n");

	/* Init Fn-Lock in off state */
	tpcsc->fn_lock = 1;
	tpcompactkbd_features_set(hdev);

	if (sysfs_create_group(&hdev->dev.kobj,
				&tpcompactkbd_attr_group)) {
		hid_warn(hdev, "Could not create sysfs group\n");
	}

	return 0;
}

static int tpkbd_probe(struct hid_device *hdev,
		const struct hid_device_id *id)
{
	int ret;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "hid_parse failed\n");
		goto err;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		hid_err(hdev, "hid_hw_start failed\n");
		goto err;
	}

	if (hdev->product == USB_DEVICE_ID_LENOVO_TPKBD
			&& tpkbd_probe_tp(hdev))
		goto err_hid;
	if (hdev->product == USB_DEVICE_ID_LENOVO_CUSBKBD
			&& tpcompactkbd_probe(hdev, id))
		goto err_hid;
	if (hdev->product == USB_DEVICE_ID_LENOVO_CBTKBD
			&& tpcompactkbd_probe(hdev, id))
		goto err_hid;

	return 0;
err_hid:
	hid_hw_stop(hdev);
err:
	return ret;
}

static void tpkbd_remove_tp(struct hid_device *hdev)
{
	struct tpkbd_data_pointer *data_pointer = hid_get_drvdata(hdev);

	sysfs_remove_group(&hdev->dev.kobj,
			&tpkbd_attr_group_pointer);

	led_classdev_unregister(&data_pointer->led_micmute);
	led_classdev_unregister(&data_pointer->led_mute);

	hid_set_drvdata(hdev, NULL);
}

static void tpcompactkbd_remove(struct hid_device *hdev)
{
	sysfs_remove_group(&hdev->dev.kobj,
			&tpcompactkbd_attr_group);
}

static void tpkbd_remove(struct hid_device *hdev)
{
	if (!hid_get_drvdata(hdev))
		return;

	if (hdev->product == USB_DEVICE_ID_LENOVO_TPKBD)
		tpkbd_remove_tp(hdev);
	if (hdev->product == USB_DEVICE_ID_LENOVO_CUSBKBD)
		tpcompactkbd_remove(hdev);
	if (hdev->product == USB_DEVICE_ID_LENOVO_CBTKBD)
		tpcompactkbd_remove(hdev);

	hid_hw_stop(hdev);
}

static const struct hid_device_id tpkbd_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_LENOVO, USB_DEVICE_ID_LENOVO_TPKBD) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_LENOVO, USB_DEVICE_ID_LENOVO_CUSBKBD) },
	{ HID_BLUETOOTH_DEVICE(USB_VENDOR_ID_LENOVO, USB_DEVICE_ID_LENOVO_CBTKBD) },
	{ }
};

MODULE_DEVICE_TABLE(hid, tpkbd_devices);

static struct hid_driver tpkbd_driver = {
	.name = "lenovo_tpkbd",
	.id_table = tpkbd_devices,
	.input_mapping = tpkbd_input_mapping,
	.probe = tpkbd_probe,
	.remove = tpkbd_remove,
	.raw_event = tpkbd_raw_event,
};
module_hid_driver(tpkbd_driver);

MODULE_LICENSE("GPL");
