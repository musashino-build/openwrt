// SPDX-License-Identifier: GPL-2.0-or-later

#define DRV_NAME	"landisk-r8c"
#define pr_fmt(fmt)	DRV_NAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/sched.h>
#include <linux/serdev.h>
#include <linux/workqueue.h>

#include <linux/mfd/landisk-r8c.h>

#define R8C_BAUD		57600

#define R8C_MSG_MIN_LEN		2	/* "@<key code>" */
#define R8C_MSG_MAX_LEN		64	/* ":<cmd> <params>\n" */
#define R8C_RCV_MAX_LEN		32

#define R8C_MSG_TYPE_CMD	':'
#define R8C_MSG_TYPE_REPL	';'
#define R8C_MSG_TYPE_EV		'@'

#define R8C_CMD_MODEL		"model"
#define R8C_CMD_VER		"ver"

struct r8c_mcu {
	struct serdev_device *serdev;
	struct mutex lock;

	char ev_code;
	char buf[R8C_RCV_MAX_LEN];
	int cur_ofs;

	char *dbgbuf;

	struct delayed_work ev_work;
	struct blocking_notifier_head ev_list;
	struct completion rpl_recv;
};

static void landisk_r8c_unregister_event_notifier(struct device *dev,
						  void *res)
{
	struct r8c_mcu *r8c = dev_get_drvdata(dev->parent);
	struct notifier_block *nb = *(struct notifier_block **)res;
	struct blocking_notifier_head *bnh = &r8c->ev_list;

	WARN_ON(blocking_notifier_chain_unregister(bnh, nb));
}

int devm_landisk_r8c_register_event_notifier(struct device *dev,
					     struct notifier_block *nb)
{
	struct r8c_mcu *r8c = dev_get_drvdata(dev->parent);
	struct notifier_block **rcnb;
	int ret;

	rcnb = devres_alloc(landisk_r8c_unregister_event_notifier,
			    sizeof(*rcnb), GFP_KERNEL);
	if (!rcnb)
		return -ENOMEM;

	ret = blocking_notifier_chain_register(&r8c->ev_list, nb);
	if (!ret) {
		*rcnb = nb;
		devres_add(dev, rcnb);
	} else {
		devres_free(rcnb);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(devm_landisk_r8c_register_event_notifier);

static void landisk_r8c_notifier_call_work(struct work_struct *work)
{
	struct r8c_mcu *r8c = container_of(work, struct r8c_mcu,
					   ev_work.work);

	blocking_notifier_call_chain(&r8c->ev_list, r8c->ev_code, NULL);
}

static int landisk_r8c_exec(struct r8c_mcu *r8c, const char *buf, size_t len,
			    char *rpl_buf, size_t rpl_len)
{
	unsigned long jiffies_left;
	int ret = 0;

	if (len > R8C_MSG_MAX_LEN)
		return -EINVAL;

	pr_debug("buf-> \"%s\" (%d bytes)\n", buf, len);

	mutex_lock(&r8c->lock);
	r8c->rpl_recv = COMPLETION_INITIALIZER_ONSTACK(r8c->rpl_recv);
	serdev_device_write(r8c->serdev, buf, len, HZ);

	if (rpl_len <= 0)
		goto exit;

	jiffies_left = wait_for_completion_timeout(&r8c->rpl_recv, HZ);
	if (!jiffies_left) {
		dev_err(&r8c->serdev->dev, "command timeout\n");
		ret = -ETIMEDOUT;
	} else {
		ret = strscpy(rpl_buf, r8c->buf, rpl_len);
	}

	memset(r8c->buf, 0, sizeof(r8c->buf));

exit:
	mutex_unlock(&r8c->lock);
	return ret;
}

int landisk_r8c_exec_cmd(struct r8c_mcu *r8c, const char *cmd, const char *arg,
			 char *rpl_buf, size_t rpl_len)
{
	char buf[R8C_MSG_MAX_LEN];
	int ret;

	if (!r8c)
		return -EINVAL;

	/* don't place a space at the end when no arguments specified */
	ret = scnprintf(buf, R8C_MSG_MAX_LEN, "%c%s%s%s\n",
			R8C_MSG_TYPE_CMD, cmd, arg ? " " : "", arg ? arg : "");
	return landisk_r8c_exec(r8c, buf, ret, rpl_buf, rpl_len);
}
EXPORT_SYMBOL_GPL(landisk_r8c_exec_cmd);

/* for debugging */
static ssize_t command_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct r8c_mcu *r8c = dev_get_drvdata(dev);

	return scnprintf(buf, R8C_RCV_MAX_LEN, "%s\n", r8c->dbgbuf);
}

static ssize_t command_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct r8c_mcu *r8c = dev_get_drvdata(dev);
	ssize_t ret;
	char cmdbuf[R8C_MSG_MAX_LEN];
	const char *cur = buf;

	if (len == 0 || len > R8C_MSG_MAX_LEN - 2)
		return -EINVAL;

	if (cur[0] == R8C_MSG_TYPE_CMD) {
		if (len <= 1)
			return -EINVAL;
		cur++;
	}

	if (!r8c->dbgbuf) {
		r8c->dbgbuf = devm_kzalloc(dev, R8C_RCV_MAX_LEN, GFP_KERNEL);
		if (!r8c->dbgbuf)
			return -ENOMEM;
	}

	ret = scnprintf(cmdbuf, R8C_MSG_MAX_LEN, ":%s%s",
			cur, strchr(buf, '\n') ? "" : "\n");
	ret = landisk_r8c_exec(r8c, cmdbuf, ret, r8c->dbgbuf, R8C_RCV_MAX_LEN);

	return ret < 0 ? ret : len;
}

static DEVICE_ATTR_RW(command);

/* Linux 6.9~: int->size_t */
static int landisk_r8c_receive_buf(struct serdev_device *serdev,
				   const unsigned char *buf, size_t count)
{
	struct r8c_mcu *r8c = dev_get_drvdata(&serdev->dev);
	char *lf;
	int ret;

	switch (buf[0]) {
	case R8C_MSG_TYPE_EV:
		pr_debug("ev code: %c\n", buf[1]);
		r8c->ev_code = buf[1];
		mod_delayed_work(system_wq, &r8c->ev_work, 0);
		return count;
	case R8C_MSG_TYPE_REPL:
		ret = strscpy(r8c->buf, buf + 1, R8C_RCV_MAX_LEN);
		if (ret >= 0)
			r8c->cur_ofs = ret;
		break;
	default:
		/* return when not waiting */
		if (r8c->cur_ofs == 0)
			return count;
		/* handle remaining data */
		if (r8c->cur_ofs + 1 >= R8C_RCV_MAX_LEN)
			ret = -E2BIG;
		else
			ret = strscpy(r8c->buf + r8c->cur_ofs, buf,
				      R8C_RCV_MAX_LEN - r8c->cur_ofs);
		if (ret >= 0)
			r8c->cur_ofs += ret;
		break;
	}

	if (ret < 0)
		pr_debug("response data is too long!\n");

	/* wait remaining data if no '\n' */
	lf = strchr(r8c->buf, '\n');
	if (!lf && ret >= 0)
		return count;
	else if (ret >= 0)
		*lf = '\0';
	pr_debug("buf: \"%s\"\n", r8c->buf);
	r8c->cur_ofs = 0;
	complete(&r8c->rpl_recv);

	return count;
}

static const struct serdev_device_ops landisk_r8c_ops = {
	.receive_buf = landisk_r8c_receive_buf,
	.write_wakeup = serdev_device_write_wakeup,
};

static int landisk_r8c_detect(struct r8c_mcu *r8c, struct device_node *np)
{
	char model[12], ver[8];
	const char *dt_model;
	int ret;

	ret = of_property_read_string(np, "iodata,landisk-r8c-model", &dt_model);
	if (ret)
		return ret;

	ret = landisk_r8c_exec_cmd(r8c, R8C_CMD_MODEL, NULL, model, sizeof(model));
	if (ret > 0)
		ret = landisk_r8c_exec_cmd(r8c, R8C_CMD_VER, NULL,
					   ver, sizeof(ver));
	if (ret <= 0)
		return ret == 0 ? -EINVAL : ret;

	if (strncmp(dt_model, model, strlen(dt_model))) {
		dev_err(&r8c->serdev->dev,
			"invalid model detected! (\"%s\")", model);
		return -EINVAL;
	}
	dev_info(&r8c->serdev->dev, "MCU: %s v%s\n", model, ver);

	return 0;
}

static int landisk_r8c_probe(struct serdev_device *serdev)
{
	struct r8c_mcu *r8c;
	struct device *dev = &serdev->dev;
	int ret;

	r8c = devm_kzalloc(dev, sizeof(*r8c), GFP_KERNEL);
	if (!r8c) {
		dev_err(dev, "failed to allocate memory\n");
		return -ENOMEM;
	}

	r8c->serdev = serdev;
	serdev_device_set_drvdata(serdev, r8c);

	ret = devm_mutex_init(dev, &r8c->lock);
	if (ret)
		return ret;
	BLOCKING_INIT_NOTIFIER_HEAD(&r8c->ev_list);
	INIT_DELAYED_WORK(&r8c->ev_work, landisk_r8c_notifier_call_work);

	/* setup serial port */
	serdev_device_set_client_ops(serdev, &landisk_r8c_ops);
	ret = devm_serdev_device_open(dev, serdev);
	if (ret)
		goto err;
	serdev_device_set_baudrate(serdev, R8C_BAUD);
	serdev_device_set_flow_control(serdev, false);
	ret = serdev_device_set_parity(serdev, SERDEV_PARITY_NONE);
	if (ret)
		goto err;

	ret = landisk_r8c_detect(r8c, dev->of_node);
	if (ret)
		goto err;

	ret = device_create_file(dev, &dev_attr_command);
	if (ret) {
		dev_err(dev, "failed to create command sysfs\n");
		goto err;
	}

	ret = devm_of_platform_populate(dev);
	if (!ret)
		return 0;

err:
	device_remove_file(dev, &dev_attr_command);

	return ret;
}

static const struct of_device_id landisk_r8c_ids[] = {
	{ .compatible = "iodata,landisk-r8c" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, landisk_r8c_ids);

static struct serdev_device_driver landisk_r8c_driver = {
	.probe  = landisk_r8c_probe,
	.driver = {
		.name = "landisk-r8c",
		.of_match_table = landisk_r8c_ids,
	},
};
module_serdev_device_driver(landisk_r8c_driver);

MODULE_AUTHOR("INAGAKI Hiroshi <musashino.open@gmail.com>");
MODULE_DESCRIPTION("R8C MCU driver for I-O DATA LAN DISK series NAS");
MODULE_LICENSE("GPL v2");
