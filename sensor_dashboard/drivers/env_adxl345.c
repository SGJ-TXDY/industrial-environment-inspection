// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/kref.h>
#include <linux/uaccess.h>
#include "sensor_data.h"

struct env_accel {
    struct miscdevice misc;
    struct spi_device *spi;
    struct mutex lock;
    struct kref ref;
};

static void env_accel_free(struct kref *ref)
{
    kfree(container_of(ref, struct env_accel, ref));
}

static int env_accel_open(struct inode *inode, struct file *file)
{
    struct env_accel *d = container_of(file->private_data, struct env_accel, misc);
    int ret = 0;
    mutex_lock(&d->lock);
    if (!d->spi)
        ret = -ENODEV;
    else {
        kref_get(&d->ref);
        file->private_data = d;
    }
    mutex_unlock(&d->lock);
    return ret;
}

static int env_accel_release(struct inode *inode, struct file *file)
{
    struct env_accel *d = file->private_data;
    kref_put(&d->ref, env_accel_free);
    return 0;
}

static int env_accel_write_reg(struct spi_device *spi, u8 address, u8 value)
{
    u8 tx[2] = { address, value };
    return spi_write(spi, tx, sizeof(tx));
}

static ssize_t env_accel_read(struct file *file, char __user *buf,
                              size_t count, loff_t *pos)
{
    struct env_accel *d = file->private_data;
    struct env_accel_sample sample;
    u8 command = 0x32 | 0x80 | 0x40;
    u8 raw[6];
    int ret;
    if (!count)
        return 0;
    if (count < sizeof(sample))
        return -EINVAL;
    if (mutex_lock_interruptible(&d->lock))
        return -ERESTARTSYS;
    if (!d->spi) {
        ret = -ENODEV;
        goto unlock;
    }
    /* One message keeps CS asserted across command and all six data bytes. */
    ret = spi_write_then_read(d->spi, &command, 1, raw, sizeof(raw));
    if (ret)
        goto unlock;
    sample.x = (s16)((u16)raw[0] | ((u16)raw[1] << 8));
    sample.y = (s16)((u16)raw[2] | ((u16)raw[3] << 8));
    sample.z = (s16)((u16)raw[4] | ((u16)raw[5] << 8));
    sample.reserved = 0;
    ret = copy_to_user(buf, &sample, sizeof(sample)) ? -EFAULT : sizeof(sample);
unlock:
    mutex_unlock(&d->lock);
    return ret;
}

static const struct file_operations env_accel_fops = {
    .owner = THIS_MODULE,
    .open = env_accel_open,
    .release = env_accel_release,
    .read = env_accel_read,
    .llseek = no_llseek,
};

static int env_accel_probe(struct spi_device *spi)
{
    struct env_accel *d;
    u8 command = 0x80;
    u8 id = 0;
    int ret;
    spi->mode = SPI_MODE_3;
    spi->bits_per_word = 8;
    if (!spi->max_speed_hz || spi->max_speed_hz > 1000000)
        spi->max_speed_hz = 1000000;
    ret = spi_setup(spi);
    if (ret)
        return ret;
    ret = spi_write_then_read(spi, &command, 1, &id, 1);
    if (ret)
        return ret;
    if (id != 0xe5) {
        dev_err(&spi->dev, "DEVID=0x%02x, expected 0xe5; check SPI wiring\n", id);
        return -ENODEV;
    }
    ret = env_accel_write_reg(spi, 0x2d, 0x00); /* standby */
    if (ret)
        return ret;
    ret = env_accel_write_reg(spi, 0x31, 0x0b); /* full resolution, +/-16g */
    if (ret)
        return ret;
    ret = env_accel_write_reg(spi, 0x2c, 0x0a); /* 100 Hz */
    if (ret)
        return ret;
    ret = env_accel_write_reg(spi, 0x38, 0x00); /* bypass FIFO */
    if (ret)
        return ret;
    ret = env_accel_write_reg(spi, 0x2e, 0x00); /* polling, no interrupts */
    if (ret)
        return ret;
    ret = env_accel_write_reg(spi, 0x2d, 0x08); /* measurement */
    if (ret)
        return ret;
    d = kzalloc(sizeof(*d), GFP_KERNEL);
    if (!d) {
        env_accel_write_reg(spi, 0x2d, 0x00);
        return -ENOMEM;
    }
    mutex_init(&d->lock);
    kref_init(&d->ref);
    d->spi = spi;
    d->misc.minor = MISC_DYNAMIC_MINOR;
    d->misc.name = "env_adxl345";
    d->misc.fops = &env_accel_fops;
    d->misc.parent = &spi->dev;
    ret = misc_register(&d->misc);
    if (ret) {
        env_accel_write_reg(spi, 0x2d, 0x00);
        kref_put(&d->ref, env_accel_free);
        return ret;
    }
    spi_set_drvdata(spi, d);
    dev_info(&spi->dev, "DEVID=0xe5, ready: %s\n", ENV_ACCEL_DEVICE);
    return 0;
}

static int env_accel_remove(struct spi_device *spi)
{
    struct env_accel *d = spi_get_drvdata(spi);
    misc_deregister(&d->misc);
    mutex_lock(&d->lock);
    env_accel_write_reg(spi, 0x2d, 0x00);
    d->spi = NULL;
    mutex_unlock(&d->lock);
    kref_put(&d->ref, env_accel_free);
    return 0;
}

static const struct of_device_id env_accel_of_match[] = {
    { .compatible = "learning,env-adxl345" },
    { }
};
MODULE_DEVICE_TABLE(of, env_accel_of_match);
static const struct spi_device_id env_accel_ids[] = {
    { "env-adxl345", 0 },
    { }
};
MODULE_DEVICE_TABLE(spi, env_accel_ids);
static struct spi_driver env_accel_driver = {
    .probe = env_accel_probe,
    .remove = env_accel_remove,
    .id_table = env_accel_ids,
    .driver = {
        .name = "env_adxl345",
        .of_match_table = env_accel_of_match,
    },
};
module_spi_driver(env_accel_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ADXL345 full-resolution SPI sensor driver for IMX6ULL");
