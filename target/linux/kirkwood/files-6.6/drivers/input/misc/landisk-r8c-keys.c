// SPDX-License-Identifier: GPL-2.0-or-later

#define DRV_NAME	"landisk-r8c-keys"
#define pr_fmt(fmt)	DRV_NAME ": " fmt

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/input.h>

#include <linux/mfd/landisk-r8c.h>

#define CMD_BTN			"btn"
#define R8C_BTN_CHARS		7

#define R8C_POLL_INTERVAL	1000

struct r8c_key_device {
	const char *name;
	char r8kcode;
	unsigned int lkcode;
	unsigned int evtype;
	bool last_state;
};

struct r8c_keys {
	struct r8c_mcu *r8c;
	struct input_dev *idev;
	struct notifier_block nb;
	int keycnt;
	struct r8c_key_device keys[];
};

static void r8c_key_poll(struct input_dev *idev)
{
	struct r8c_keys *k = input_get_drvdata(idev);
	char buf[16];
	int ret, i = 0;

	ret = landisk_r8c_exec_cmd(k->r8c, CMD_BTN, NULL, buf, sizeof(buf));
	if (ret != R8C_BTN_CHARS)
		return;
	/* example: "crpWxyZ" */
	while (buf[i]) {
		struct r8c_key_device *kdev = k->keys;
		bool pressed = (buf[i] <= 'Z');

		while (kdev < k->keys + k->keycnt) {
			if (buf[i] != kdev->r8kcode - (pressed ? 0x20 : 0x0)) {
				kdev++;
				continue;
			}

			if (kdev->last_state == pressed)
				break;
			kdev->last_state = pressed;

			input_event(idev, kdev->evtype, kdev->lkcode, pressed);
			input_sync(idev);
			break;
		}
		i++;
	}
}

static int r8c_key_event(struct notifier_block *nb,
			 unsigned long evcode, void *data)
{
	struct r8c_keys *k = container_of(nb, struct r8c_keys, nb);
	struct r8c_key_device *kdev = k->keys;
	bool pressed = evcode <= 'Z';

	while (kdev < k->keys + k->keycnt) {
		if (evcode != kdev->r8kcode - (pressed ? 0x20 : 0x0)) {
			kdev++;
			continue;
		}

		/*
		 * check duplicates
		 *   skip if the same with the last code
		 *
		 * R8C returns the same code if the key was held
		 * while the specific time
		 * ex.:
		 *   'R' (pressed), 'r' (released), 'R' (pressed),
		 *   -> 'R' (held), 'r' (released)
		 */
		if (pressed && kdev->last_state == pressed)
			return NOTIFY_STOP;
		kdev->last_state = pressed;

		input_event(k->idev, kdev->evtype, kdev->lkcode, pressed);
		input_sync(k->idev);

		dev_dbg(&k->idev->dev, "%s %s\n",
			kdev->name, pressed ? "pressed" : "released");

		return NOTIFY_STOP;
	}

	return NOTIFY_DONE;
}

static int landisk_r8c_keys_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np;
	struct r8c_mcu *r8c;
	struct r8c_keys *k;
	unsigned short *keymap;
	int keycnt;
	int ret, i = 0;

	r8c = dev_get_drvdata(dev->parent);
	if (!r8c)
		return -ENODEV;

	keycnt = of_get_child_count(dev->of_node);
	if (!keycnt)
		return -ENODEV;

	keymap = devm_kcalloc(dev, keycnt, sizeof(keymap[0]), GFP_KERNEL);
	if (!keymap)
		return -ENOMEM;

	k = devm_kzalloc(dev, struct_size(k, keys, keycnt), GFP_KERNEL);
	if (!k)
		return -ENOMEM;

	k->idev = devm_input_allocate_device(dev);
	if (!k->idev)
		return -ENOMEM;
	k->r8c = r8c;

	input_set_drvdata(k->idev, k);

	for_each_child_of_node(dev->of_node, np) {
		ret = of_property_read_string(np, "label", &k->keys[i].name);
		if (!ret)
			ret = of_property_read_u8(np, "iodata,landisk-r8c-keycode",
						  &k->keys[i].r8kcode);
		if (!ret)
			ret = of_property_read_u32(np, "linux,code",
						   &k->keys[i].lkcode);
		if (ret)
			break;
		if (of_property_read_u32(np, "linux,input-type",
					 &k->keys[i].evtype))
			k->keys[i].evtype = EV_KEY;

		switch(k->keys[i].r8kcode) {
		case 'c':
		case 'p':
		case 'r':
			break;
		case 'w':
		case 'x':
		case 'y':
		case 'z':
			if(of_device_is_compatible(dev->of_node,
						   "iodata,landisk-r8c-keys-event"))
			{
				dev_err(dev,
					"event for keycode '%c' is unavailable\n",
					k->keys[i].r8kcode);
				ret = -EINVAL;
			}
			break;
		default:
			dev_err(dev, "keycode '%c' is not supported\n",
				k->keys[i].r8kcode);
			ret = -ERANGE;
			break;
		}
		if (ret)
			break;
		keymap[i] = k->keys[i].lkcode;
		input_set_capability(k->idev, k->keys[i].evtype, keymap[i]);
		i++;
	}

	of_node_put(np);
	if (ret)
		return ret;

	k->keycnt = i;

	k->idev->name = pdev->name;
	k->idev->phys = "landisk-r8c-keys/input0";
	k->idev->dev.parent = dev;
	k->idev->id.bustype = BUS_HOST;
	k->idev->id.vendor  = 0x0001;
	k->idev->id.product = 0x0001;
	k->idev->id.version = 0x0001;
	k->idev->keycode     = keymap;
	k->idev->keycodesize = sizeof(keymap[0]);
	k->idev->keycodemax  = keycnt;

	if (of_device_is_compatible(dev->of_node,
				    "iodata,landisk-r8c-keys-polled")) {
		unsigned int interval;

		if (of_property_read_u32(dev->of_node, "poll-interval", &interval))
			interval = R8C_POLL_INTERVAL;
		ret = input_setup_polling(k->idev, r8c_key_poll);
		if (ret)
			return ret;
		input_set_poll_interval(k->idev, interval);
	} else {
		k->nb.notifier_call = r8c_key_event;
		k->nb.priority = 128;
		ret = devm_landisk_r8c_register_event_notifier(dev, &k->nb);
		if (ret)
			return ret;
	}

	ret = input_register_device(k->idev);
	if (ret)
		return ret;

	/* report the initial state of keys */
	if (of_device_is_compatible(dev->of_node,
				    "iodata,landisk-r8c-keys-polled"))
		r8c_key_poll(k->idev);

	return 0;
}

static const struct of_device_id landisk_r8c_keys_ids[] = {
	{ .compatible = "iodata,landisk-r8c-keys-event" },
	{ .compatible = "iodata,landisk-r8c-keys-polled" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, landisk_r8c_keys_ids);

static struct platform_driver landisk_r8c_keys_driver = {
	.probe = landisk_r8c_keys_probe,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = landisk_r8c_keys_ids,
	},
};
module_platform_driver(landisk_r8c_keys_driver);

MODULE_AUTHOR("INAGAKI Hiroshi <musashino.open@gmail.com>");
MODULE_DESCRIPTION("R8C Key driver for I-O DATA LAN DISK series");
MODULE_LICENSE("GPL v2");
