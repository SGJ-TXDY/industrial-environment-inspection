// SPDX-License-Identifier: GPL-2.0
/* LED0 and buzzer on the Mini V2.2 board as one misc device.
 * write "0".."3": bit 0 LED on, bit 1 buzzer on. read: current value + '\n'.
 * Outputs keep their state after close (shell testing with echo) and are
 * switched off on module removal.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

#define ENV_ALARM_LED 1
#define ENV_ALARM_BUZZER 2

struct env_alarm_gpio {
    int gpio;
    bool active_low;
};

struct env_alarm {
    struct miscdevice misc;
    struct mutex lock;
    struct env_alarm_gpio led, buzzer;
    unsigned int value;
};

static void env_alarm_set(struct env_alarm *d, unsigned int value)
{
    gpio_set_value(d->led.gpio, !!(value & ENV_ALARM_LED) ^ d->led.active_low);
    gpio_set_value(d->buzzer.gpio,
                   !!(value & ENV_ALARM_BUZZER) ^ d->buzzer.active_low);
    d->value = value;
}

static int env_alarm_open(struct inode *inode, struct file *file)
{
    file->private_data = container_of(file->private_data, struct env_alarm, misc);
    return 0;
}

static ssize_t env_alarm_write(struct file *file, const char __user *buf,
                               size_t count, loff_t *pos)
{
    struct env_alarm *d = file->private_data;
    char c;
    if (!count)
        return 0;
    if (get_user(c, buf))
        return -EFAULT;
    /* Only the first byte counts, so "echo 3 > /dev/env_alarm" works. */
    if (c < '0' || c > '3')
        return -EINVAL;
    mutex_lock(&d->lock);
    env_alarm_set(d, c - '0');
    mutex_unlock(&d->lock);
    return count;
}

static ssize_t env_alarm_read(struct file *file, char __user *buf,
                              size_t count, loff_t *pos)
{
    struct env_alarm *d = file->private_data;
    char text[2];
    mutex_lock(&d->lock);
    text[0] = '0' + d->value;
    mutex_unlock(&d->lock);
    text[1] = '\n';
    return simple_read_from_buffer(buf, count, pos, text, sizeof(text));
}

static const struct file_operations env_alarm_fops = {
    .owner = THIS_MODULE,
    .open = env_alarm_open,
    .read = env_alarm_read,
    .write = env_alarm_write,
    .llseek = no_llseek,
};

static int env_alarm_gpio(struct platform_device *pdev, const char *name,
                          const char *label, struct env_alarm_gpio *out)
{
    enum of_gpio_flags flags;
    int gpio = of_get_named_gpio_flags(pdev->dev.of_node, name, 0, &flags);
    if (gpio < 0)
        return gpio;
    out->gpio = gpio;
    out->active_low = flags & OF_GPIO_ACTIVE_LOW;
    /* Start switched off: inactive level depends on polarity. */
    return devm_gpio_request_one(&pdev->dev, gpio,
                                 out->active_low ? GPIOF_OUT_INIT_HIGH :
                                                   GPIOF_OUT_INIT_LOW, label);
}

static int env_alarm_probe(struct platform_device *pdev)
{
    struct env_alarm *d;
    int ret;
    d = devm_kzalloc(&pdev->dev, sizeof(*d), GFP_KERNEL);
    if (!d)
        return -ENOMEM;
    ret = env_alarm_gpio(pdev, "led-gpios", "env-alarm-led", &d->led);
    if (ret)
        return ret;
    ret = env_alarm_gpio(pdev, "beep-gpios", "env-alarm-beep", &d->buzzer);
    if (ret)
        return ret;
    mutex_init(&d->lock);
    d->misc.minor = MISC_DYNAMIC_MINOR;
    d->misc.name = "env_alarm";
    d->misc.fops = &env_alarm_fops;
    d->misc.parent = &pdev->dev;
    ret = misc_register(&d->misc);
    if (ret)
        return ret;
    platform_set_drvdata(pdev, d);
    dev_info(&pdev->dev, "ready: /dev/env_alarm, LED GPIO %d, buzzer GPIO %d\n",
             d->led.gpio, d->buzzer.gpio);
    return 0;
}

static int env_alarm_remove(struct platform_device *pdev)
{
    struct env_alarm *d = platform_get_drvdata(pdev);
    /* rmmod cannot run while the device is open (fops owner holds the
     * module). Unbinding through sysfs while open is not supported.
     */
    misc_deregister(&d->misc);
    env_alarm_set(d, 0);
    return 0;
}

static const struct of_device_id env_alarm_of_match[] = {
    { .compatible = "learning,env-alarm" },
    { }
};
MODULE_DEVICE_TABLE(of, env_alarm_of_match);
static struct platform_driver env_alarm_driver = {
    .probe = env_alarm_probe,
    .remove = env_alarm_remove,
    .driver = {
        .name = "env_alarm",
        .of_match_table = env_alarm_of_match,
    },
};
module_platform_driver(env_alarm_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("IMX6ULL Mini LED0 and buzzer alarm output");
