// SPDX-License-Identifier: GPL-2.0
/* LM75BDP temperature sensor on I2C as a misc device, replacing DHT11.
 * read() returns struct env_temp_sample. The chip converts continuously
 * (about every 100 ms) after leaving shutdown, so a read is one I2C
 * transfer of the latest result.
 */
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/kref.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include "sensor_data.h"
#include "lm75_decode.h"

struct env_lm75 {
    struct miscdevice misc;
    struct i2c_client *client;
    struct mutex lock;
    struct kref ref;
    bool present;
};

static void env_lm75_free(struct kref *ref)
{
    kfree(container_of(ref, struct env_lm75, ref));
}

static int env_lm75_open(struct inode *inode, struct file *file)
{
    struct env_lm75 *d = container_of(file->private_data, struct env_lm75, misc);
    int ret = 0;
    mutex_lock(&d->lock);
    if (!d->present)
        ret = -ENODEV;
    else {
        kref_get(&d->ref);
        file->private_data = d;
    }
    mutex_unlock(&d->lock);
    return ret;
}

static int env_lm75_release(struct inode *inode, struct file *file)
{
    struct env_lm75 *d = file->private_data;
    kref_put(&d->ref, env_lm75_free);
    return 0;
}

/* Temperature register: two bytes MSB first, so the swapped SMBus word. */
static int env_lm75_temp(struct i2c_client *client, int *mc)
{
    s32 word = i2c_smbus_read_word_swapped(client, ENV_LM75_REG_TEMP);
    if (word < 0)
        return word;
    *mc = env_lm75_decode((word >> 8) & 0xff, word & 0xff);
    return 0;
}

static ssize_t env_lm75_read(struct file *file, char __user *buf,
                             size_t count, loff_t *pos)
{
    struct env_lm75 *d = file->private_data;
    struct env_temp_sample sample = { 0 };
    int ret, mc;
    if (!count)
        return 0;
    if (count < sizeof(sample))
        return -EINVAL;
    if (mutex_lock_interruptible(&d->lock))
        return -ERESTARTSYS;
    if (!d->present) {
        ret = -ENODEV;
        goto unlock;
    }
    ret = env_lm75_temp(d->client, &mc);
    if (ret)
        goto unlock;
    sample.temperature_mc = mc;
    ret = copy_to_user(buf, &sample, sizeof(sample)) ? -EFAULT : sizeof(sample);
unlock:
    mutex_unlock(&d->lock);
    return ret;
}

static const struct file_operations env_lm75_fops = {
    .owner = THIS_MODULE,
    .open = env_lm75_open,
    .release = env_lm75_release,
    .read = env_lm75_read,
    .llseek = no_llseek,
};

static int env_lm75_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct env_lm75 *d;
    s32 conf, tos, thyst;
    int ret, mc;
    if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA |
                                 I2C_FUNC_SMBUS_WORD_DATA))
        return -EOPNOTSUPP;
    /* No ID register on LM75: an ACKed config read is the presence check. */
    conf = i2c_smbus_read_byte_data(client, ENV_LM75_REG_CONF);
    if (conf < 0) {
        dev_err(&client->dev, "no LM75 at 0x%02x: %d\n", client->addr, conf);
        return conf;
    }
    /* Normal (continuous) mode; keep comparator/OS settings as found. */
    if (conf & ENV_LM75_CONF_SHUTDOWN) {
        ret = i2c_smbus_write_byte_data(client, ENV_LM75_REG_CONF,
                                        conf & ~ENV_LM75_CONF_SHUTDOWN);
        if (ret)
            return ret;
        msleep(110);    /* first conversion after wake-up */
    }
    ret = env_lm75_temp(client, &mc);
    if (ret)
        return ret;
    tos = i2c_smbus_read_word_swapped(client, ENV_LM75_REG_TOS);
    thyst = i2c_smbus_read_word_swapped(client, ENV_LM75_REG_THYST);

    d = kzalloc(sizeof(*d), GFP_KERNEL);
    if (!d)
        return -ENOMEM;
    mutex_init(&d->lock);
    kref_init(&d->ref);
    d->client = client;
    d->present = true;
    d->misc.minor = MISC_DYNAMIC_MINOR;
    d->misc.name = "env_lm75";
    d->misc.fops = &env_lm75_fops;
    d->misc.parent = &client->dev;
    ret = misc_register(&d->misc);
    if (ret) {
        kref_put(&d->ref, env_lm75_free);
        return ret;
    }
    i2c_set_clientdata(client, d);
    /* Tos/Thyst only drive the chip's OS pin, which this project leaves
     * unconnected; the application has its own alarm limits.
     */
    dev_info(&client->dev, "ready: %s, addr 0x%02x, %d mC, Tos %d mC, Thyst %d mC\n",
             ENV_TEMP_DEVICE, client->addr, mc,
             tos < 0 ? 0 : ((s16)tos >> 7) * 500, thyst < 0 ? 0 : ((s16)thyst >> 7) * 500);
    return 0;
}

static int env_lm75_remove(struct i2c_client *client)
{
    struct env_lm75 *d = i2c_get_clientdata(client);
    misc_deregister(&d->misc);
    mutex_lock(&d->lock);
    d->present = false;
    mutex_unlock(&d->lock);
    kref_put(&d->ref, env_lm75_free);
    return 0;
}

static const struct of_device_id env_lm75_of_match[] = {
    { .compatible = "learning,env-lm75" },
    { }
};
MODULE_DEVICE_TABLE(of, env_lm75_of_match);
static const struct i2c_device_id env_lm75_ids[] = {
    { "env-lm75", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, env_lm75_ids);
static struct i2c_driver env_lm75_driver = {
    .probe = env_lm75_probe,
    .remove = env_lm75_remove,
    .id_table = env_lm75_ids,
    .driver = {
        .name = "env_lm75",
        .of_match_table = env_lm75_of_match,
    },
};
module_i2c_driver(env_lm75_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("IMX6ULL LM75BDP I2C temperature sensor");
