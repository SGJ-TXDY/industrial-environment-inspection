// SPDX-License-Identifier: GPL-2.0
/* GT9147 register/reset protocol follows the supplied course. Single pointer
 * output deliberately avoids depending on tslib or a fixed event number. */
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/slab.h>
struct env_touch {
    struct i2c_client *client;
    struct input_dev *input;
    int irq_gpio, reset_gpio, irq;
    unsigned int max_x, max_y;
};
static int gt_read(struct env_touch *d, u16 reg, u8 *data, u16 len)
{
    u8 addr[2] = { reg >> 8, reg & 255 };
    struct i2c_msg msg[2] = {
        { .addr = d->client->addr, .len = 2, .buf = addr },
        { .addr = d->client->addr, .flags = I2C_M_RD, .len = len, .buf = data }
    };
    int ret = i2c_transfer(d->client->adapter, msg, 2);
    return ret == 2 ? 0 : (ret < 0 ? ret : -EIO);
}
static int gt_write(struct env_touch *d, u16 reg, u8 value)
{
    u8 data[3] = { reg >> 8, reg & 255, value };
    int ret = i2c_master_send(d->client, data, 3);
    return ret == 3 ? 0 : (ret < 0 ? ret : -EIO);
}
static irqreturn_t gt_irq(int irq, void *arg)
{
    struct env_touch *d = arg;
    u8 status, p[8];
    int ret, x, y;
    ret = gt_read(d, 0x814e, &status, 1);
    if (ret || !(status & 0x80))
        return IRQ_HANDLED;
    if ((status & 15) > 5)
        goto ack;
    if (status & 15) {
        ret = gt_read(d, 0x814f, p, sizeof(p));
        if (ret)
            goto ack;
        x = p[1] | (p[2] << 8);
        y = p[3] | (p[4] << 8);
        if (x >= d->max_x || y >= d->max_y)
            goto ack;
        input_report_abs(d->input, ABS_X, x);
        input_report_abs(d->input, ABS_Y, y);
        input_report_key(d->input, BTN_TOUCH, 1);
    } else {
        input_report_key(d->input, BTN_TOUCH, 0);
    }
    input_sync(d->input);
ack:
    if (gt_write(d, 0x814e, 0))
        dev_err_ratelimited(&d->client->dev, "touch acknowledgement failed\n");
    return IRQ_HANDLED;
}
static int gt_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    static const unsigned long modes[] = {
        IRQF_TRIGGER_RISING, IRQF_TRIGGER_FALLING,
        IRQF_TRIGGER_LOW, IRQF_TRIGGER_HIGH
    };
    struct env_touch *d;
    u8 cfg[7], pid[5] = {0};
    int ret;
    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
        return -EOPNOTSUPP;
    d = devm_kzalloc(&client->dev, sizeof(*d), GFP_KERNEL);
    if (!d) return -ENOMEM;
    d->client = client;
    d->irq_gpio = of_get_named_gpio(client->dev.of_node, "interrupt-gpios", 0);
    d->reset_gpio = of_get_named_gpio(client->dev.of_node, "reset-gpios", 0);
    if (d->irq_gpio < 0) return d->irq_gpio;
    if (d->reset_gpio < 0) return d->reset_gpio;
    ret = devm_gpio_request_one(&client->dev, d->reset_gpio, GPIOF_OUT_INIT_LOW, "gt-reset");
    if (ret) return ret;
    ret = devm_gpio_request_one(&client->dev, d->irq_gpio, GPIOF_OUT_INIT_HIGH, "gt-int");
    if (ret) return ret;
    /* INT high while reset is released selects I2C address 0x14. */
    msleep(20);
    gpio_set_value(d->reset_gpio, 1);
    msleep(10);
    gpio_set_value(d->irq_gpio, 0);
    msleep(50);
    ret = gpio_direction_input(d->irq_gpio);
    if (ret) return ret;
    ret = gt_read(d, 0x8140, pid, 4);
    if (ret) return ret;
    ret = gt_read(d, 0x8047, cfg, sizeof(cfg));
    if (ret) return ret;
    d->max_x = cfg[1] | (cfg[2] << 8);
    d->max_y = cfg[3] | (cfg[4] << 8);
    if (!d->max_x || !d->max_y || d->max_x > 4096 || d->max_y > 4096)
        return -EINVAL;
    d->input = devm_input_allocate_device(&client->dev);
    if (!d->input) return -ENOMEM;
    d->input->name = "env-gt9147";
    d->input->id.bustype = BUS_I2C;
    input_set_capability(d->input, EV_KEY, BTN_TOUCH);
    __set_bit(INPUT_PROP_DIRECT, d->input->propbit);
    input_set_abs_params(d->input, ABS_X, 0, d->max_x - 1, 0, 0);
    input_set_abs_params(d->input, ABS_Y, 0, d->max_y - 1, 0, 0);
    ret = input_register_device(d->input);
    if (ret) return ret;
    d->irq = gpio_to_irq(d->irq_gpio);
    if (d->irq < 0) return d->irq;
    ret = gt_write(d, 0x814e, 0);
    if (ret) return ret;
    ret = devm_request_threaded_irq(&client->dev, d->irq, NULL, gt_irq,
                                  modes[cfg[6] & 3] | IRQF_ONESHOT, "env-gt9147", d);
    if (ret) return ret;
    i2c_set_clientdata(client, d);
    dev_info(&client->dev, "PID=%s range=%ux%u; single pointer ready\n", pid, d->max_x, d->max_y);
    return 0;
}
static const struct of_device_id gt_of[] = {
    { .compatible = "learning,env-gt9147" }, { }
};
MODULE_DEVICE_TABLE(of, gt_of);
static const struct i2c_device_id gt_ids[] = { { "env-gt9147", 0 }, { } };
MODULE_DEVICE_TABLE(i2c, gt_ids);
static struct i2c_driver gt_driver = {
    .driver = { .name = "env_gt9147", .of_match_table = gt_of },
    .probe = gt_probe, .id_table = gt_ids,
};
module_i2c_driver(gt_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GT9147 touch input for the course 800x480 panel");
