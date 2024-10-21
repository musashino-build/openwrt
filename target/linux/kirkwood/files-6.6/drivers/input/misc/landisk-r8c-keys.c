// SPDX-License-Identifier: GPL-2.0-or-later

#define DRV_NAME	"landisk-r8c-keys"
#define pr_fmt(fmt)	DRV_NAME ": " fmt

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/input.h>

#include <linux/mfd/landisk-r8c.h>

struct r8c_key_device {
	const char *name;
	char evcode;
	unsigned int lkcode;
};

struct r8c_keys {
	struct input_dev *idev;
	struct notifier_block nb;
	char last_code;
	int keycnt;
	struct r8c_key_device keys[0];
};

static int r8c_key_event(struct notifier_block *nb,
			 unsigned long evcode, void *data)
{
	struct r8c_keys *k = container_of(nb, struct r8c_keys, nb);
	const struct r8c_key_device *kdev = k->keys;
	bool pressed = evcode <= 'Z' ? true : false;

	while (kdev < k->keys + k->keycnt) {
		if (evcode != kdev->evcode - (pressed ? 0x20 : 0x0)) {
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
		if (pressed && k->last_code == evcode)
			return NOTIFY_STOP;
		k->last_code = (char)evcode;

		input_report_key(k->idev, kdev->lkcode, pressed ? 1 : 0);
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
	struct r8c_keys *k;
	struct r8c_key_device *kdev;
	struct input_dev *idev;
	unsigned short *keymap;
	int keycnt;
	int i = 0, ret;

	keycnt = of_get_child_count(dev->of_node);
	k = devm_kzalloc(dev, struct_size(k, keys, keycnt), GFP_KERNEL);
	if (!k)
		return -ENOMEM;

	keymap = devm_kcalloc(dev, keycnt,
			      sizeof(keymap[0]), GFP_KERNEL);
	if (!keymap)
		return -ENOMEM;

	idev = devm_input_allocate_device(dev);
	if (!idev)
		return -ENOMEM;

	input_set_drvdata(idev, k);

	for_each_child_of_node(dev->of_node, np) {
		unsigned int type = EV_KEY;

		kdev = &k->keys[i];
		ret = of_property_read_string(np, "label", &kdev->name);
		if (!ret)
			ret = of_property_read_u8(np, "iodata,landisk-r8c-evcode",
						  &kdev->evcode);
		if (!ret)
			ret = of_property_read_u32(np, "linux,code",
						  &kdev->lkcode);
		if (ret)
			break;
		ret = of_property_read_u32(np, "linux,input-type", &type);
		if (ret == -EINVAL)
			ret = 0;
		else if (ret)
			break;
		if (kdev->evcode < 'a' || 'z' < kdev->evcode) {
			ret = -EINVAL;
			break;
		}
		keymap[i] = kdev->lkcode;
		input_set_capability(idev, type, keymap[i]);
		i++;
	}

	of_node_put(np);
	if (ret)
		return ret;

	idev->name = pdev->name;
	idev->phys = "landisk-r8c-keys/input0";
	idev->dev.parent = dev;
	idev->id.bustype = BUS_HOST;
	idev->id.vendor  = 0x0001;
	idev->id.product = 0x0001;
	idev->id.version = 0x0001;

	idev->keycode = keymap;
	idev->keycodesize = sizeof(keymap[0]);
	idev->keycodemax = keycnt;

	k->idev = idev;
	k->keycnt = keycnt;
	k->last_code = 0x0;
	k->nb.notifier_call = r8c_key_event;
	k->nb.priority = 128;

	ret = devm_landisk_r8c_register_event_notifier(dev, &k->nb);
	if (ret)
		return ret;

	return input_register_device(idev);
}

static const struct of_device_id landisk_r8c_keys_ids[] = {
	{ .compatible = "iodata,landisk-r8c-keys" },
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
