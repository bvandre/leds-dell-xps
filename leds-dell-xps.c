
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <linux/workqueue.h>

#define LED_METHOD_GUID	    "A80593CE-A997-11DA-B012-B622A1EF5492"
#define MAX_ZONES	    4

struct app_wmi_args {
	u16 class;
	u16 selector;
	u32 arg1;
	u32 arg2;
	u32 arg3;
	u32 arg4;
	u32 res1;
	u32 res2;
	u32 res3;
	u32 res4;
	char dummy[92];
};

struct dell_xps_data {
	struct platform_device	*pdev;
	struct led_classdev	led;
	u8			brightness;
	u8			colors[MAX_ZONES];

	struct work_struct	work;
	u8			new;
};

#define led_to_dx_data(c)	container_of(c, struct dell_xps_data, led)
#define work_to_dx_data(c)	container_of(c, struct dell_xps_data, work)
#define COLOR_NAME_MAX 11

static const char *colors[17] = {
	"none",
	"ruby",
	"citrine",
	"amber",
	"peridot",
	"emerald",
	"jade",
	"topaz",
	"tanzanite",
	"aquamarine",
	"sapphire",
	"iolite",
	"amythest",
	"kunzite",
	"rhodolite",
	"coral",
	"diamond",
};

static struct platform_device *pdev;

static int dell_wmi_perform_query(struct app_wmi_args *args)
{
	struct app_wmi_args *bios_return;
	union acpi_object *obj;
	struct acpi_buffer input;
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	acpi_status status;
	u32 rc = -EINVAL;

	input.length = 128;
	input.pointer = args;

	status = wmi_evaluate_method(LED_METHOD_GUID, 0, 1,
				     &input, &output);
	if (!ACPI_SUCCESS(status))
		goto err_out0;

	obj = output.pointer;
	if (!obj)
		goto err_out0;

	if (obj->type != ACPI_TYPE_BUFFER)
		goto err_out1;

	bios_return = (struct app_wmi_args *)obj->buffer.pointer;
	rc = bios_return->res1;
	if (rc)
		goto err_out1;

	memcpy(args, bios_return, sizeof(struct app_wmi_args));
	rc = 0;

 err_out1:
	kfree(obj);
 err_out0:
	return rc;
}

static int set_led(struct app_wmi_args *args)
{
	args->class = 4;
	args->selector = 6;
	return dell_wmi_perform_query(args);
}

static enum led_brightness dell_xps_brightness_get(struct led_classdev *led)
{
	struct dell_xps_data *dx_data;

	dx_data = led_to_dx_data(led);
	dev_dbg(led->dev, "brightness: %d", dx_data->brightness);

	return dx_data->brightness;
}

static void dell_xps_work(struct work_struct *work)
{
	struct	dell_xps_data *dx_data;
	struct	app_wmi_args args;
	int	ret;

	dx_data = work_to_dx_data(work);

	memset(&args, 0, sizeof(args));
	if (dx_data->new) {
		args.arg1 = (dx_data->colors[0]) |
			    (dx_data->colors[1] << 8) |
			    (dx_data->colors[2] << 16) |
			    ((dx_data->new - 1) << 24);
	} else {
		args.arg1 = 0;
	}
	args.arg2 = 0;
	args.arg3 = dx_data->colors[3];
	ret = set_led(&args);
	if (ret)
		dev_dbg(dx_data->led.dev, "error setting brightness: %d\n",
			ret);
	dx_data->brightness = dx_data->new;
}

static void dell_xps_brightness_set(struct led_classdev *led,
				    enum led_brightness brightness)
{
	struct dell_xps_data *dx_data;

	dx_data = led_to_dx_data(led);
	dx_data->new = brightness;
	schedule_work(&dx_data->work);
}

static ssize_t zone_color_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct led_classdev *led = dev_get_drvdata(dev);
	struct dell_xps_data *dx_data;
	int i, zone;
	int len = 0;

	if (!sscanf(attr->attr.name, "zone_%d_color", &zone))
		return -EINVAL;
	zone--;

	dx_data = led_to_dx_data(led);
	for (i = 0; i < 17; i++) {
		if (i == dx_data->colors[zone])
			len += sprintf(buf + len, "[%s] ", colors[i]);
		else
			len += sprintf(buf + len, "%s ", colors[i]);
	}
	len += sprintf(len + buf, "\n");
	return len;
}

static ssize_t zone_color_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct led_classdev *led = dev_get_drvdata(dev);
	struct dell_xps_data *dx_data;
	char color_name[COLOR_NAME_MAX];
	int i, zone;
	size_t len;

	if (!sscanf(attr->attr.name, "zone_%d_color", &zone))
		return -EINVAL;
	zone--;

	dx_data = led_to_dx_data(led);
	color_name[sizeof(color_name) - 1] = '\0';
	strncpy(color_name, buf, sizeof(color_name) - 1);
	len = strlen(color_name);

	if (len && color_name[len - 1] == '\n')
		color_name[len - 1] = '\0';

	for (i = 0; i < 17; i++) {
		if (!strcmp(color_name, colors[i])) {
			dx_data->colors[zone] = i;
			led_set_brightness(&dx_data->led, dx_data->brightness);
			return count;
		}
	}

	return -EINVAL;
}

static DEVICE_ATTR(zone_1_color, (S_IWUSR | S_IRUGO), zone_color_show, zone_color_store);
static DEVICE_ATTR(zone_2_color, (S_IWUSR | S_IRUGO), zone_color_show, zone_color_store);
static DEVICE_ATTR(zone_3_color, (S_IWUSR | S_IRUGO), zone_color_show, zone_color_store);
static DEVICE_ATTR(zone_4_color, (S_IWUSR | S_IRUGO), zone_color_show, zone_color_store);

static struct attribute *dell_xps_led_attrs[] = {
	&dev_attr_zone_1_color.attr,
	&dev_attr_zone_2_color.attr,
	&dev_attr_zone_3_color.attr,
	&dev_attr_zone_4_color.attr,
	NULL,
};

ATTRIBUTE_GROUPS(dell_xps_led);

static int leds_dell_xps_probe(struct platform_device *pdev)
{
	struct dell_xps_data *dx_data;
	int ret;

	if (!wmi_has_guid(LED_METHOD_GUID)) {
		pr_warn("dell_xps_led method not here\n");
		ret = -ENODEV;
		goto exit;
	}

	pr_debug("dell_xps_led: init\n");

	dx_data = kzalloc(sizeof(*dx_data), GFP_KERNEL);
	if (IS_ERR(dx_data)) {
		pr_debug("dell_xps_led: memory allocation fail\n");
		ret = -ENOMEM;
		goto exit;
	}
	platform_set_drvdata(pdev, dx_data);
	dx_data->pdev = pdev;
	dx_data->brightness = 0;

	dx_data->led.name = "dellxps:rgb:case_light";
	dx_data->led.max_brightness = 8;
	dx_data->led.brightness_get = dell_xps_brightness_get;
	dx_data->led.brightness_set =  dell_xps_brightness_set;
	dx_data->led.groups =  dell_xps_led_groups;
	dx_data->led.flags = LED_CORE_SUSPENDRESUME;

	INIT_WORK(&dx_data->work, dell_xps_work);

	ret = led_classdev_register(&dx_data->pdev->dev, &dx_data->led);
	if (ret) {
		pr_debug("dell_xps_led: registering led failure\n");
		goto fail_led;
	}

	return ret;

fail_led:
	kfree(dx_data);
exit:
	return ret;
}

static int leds_dell_xps_remove(struct platform_device *pdev)
{
	struct dell_xps_data *dx_data = platform_get_drvdata(pdev);

	led_classdev_unregister(&dx_data->led);
	flush_work(&dx_data->work);
	kfree(dx_data);

	return 0;
}

static struct platform_driver leds_dell_xps_driver = {
	.driver = {
		   .name = "dell-xps-led",
		   .owner = THIS_MODULE,
		   },
	.probe = leds_dell_xps_probe,
	.remove = leds_dell_xps_remove,
};

static int __init dxl_init(void)
{
	int ret;

	ret = platform_driver_register(&leds_dell_xps_driver);
	if (ret) {
		ret = -ENODEV;
		goto exit;
	}

	pdev = platform_device_register_simple(
					      leds_dell_xps_driver.driver.name,
					      -1, NULL, 0);
	if (IS_ERR(pdev)) {
		ret = PTR_ERR(pdev);
		platform_driver_unregister(&leds_dell_xps_driver);
		goto exit;
	}

exit:
	return ret;
}

static void __exit dxl_exit(void)
{
	platform_device_unregister(pdev);
	platform_driver_unregister(&leds_dell_xps_driver);
}

module_init(dxl_init);
module_exit(dxl_exit);

MODULE_AUTHOR("Brian Vandre <bvandre@gmail.com>");
MODULE_DESCRIPTION("Dell Case Lighting LEDS");
MODULE_LICENSE("GPL");
MODULE_ALIAS("wmi:" LED_METHOD_GUID);

