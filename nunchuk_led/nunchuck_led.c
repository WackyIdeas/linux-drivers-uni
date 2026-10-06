/* Download it from http://j.mp/GOFKfU */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/input-polldev.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/io.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include "utils.h"
#include "common.h"

//led
static int led_probe(struct platform_device *pdev);
static int led_remove(struct platform_device *pdev);

struct led_dev {
    void __iomem      *regs;                /* Virtual address where the physical GPIO address is mapped */
    u8                gpio;                 /* GPIO pin that the LED is connected to */
};

static struct led_dev *led_gpio;

struct nunchuk_dev {
    struct input_polled_dev *polled_input;
    struct i2c_client *i2c_client;
};

static int nunchuk_read_registers(struct i2c_client *client,
                                  u8 *recv)
{
    u8 buf[1];
    int ret;

    /* Ask the device to get ready for a read */

    mdelay(10);

    buf[0] = 0x00;
    ret = i2c_master_send(client, buf, 1);
    if (ret != 1) {
        dev_err(&client->dev, "i2c send failed (%d)\n", ret);
        return ret < 0 ? ret : -EIO;
    }

    mdelay(10);

    /* Now read registers */

    ret = i2c_master_recv(client, recv, 6);
    if (ret != 6) {
        dev_err(&client->dev, "i2c recv failed (%d)\n", ret);
        return ret < 0 ? ret : -EIO;
    }

    return 0;
}

static void nunchuk_poll(struct input_polled_dev *polled_input)
{
    u8 recv[6];
    int zpressed, cpressed;

    /* Retrieve the physical i2c device */

    struct nunchuk_dev *nunchuk = polled_input->private;
    struct i2c_client *client = nunchuk->i2c_client;

    /* Get the state of the device registers */

    if (nunchuk_read_registers(client, recv) < 0)
        return;

    zpressed = (recv[5] & BIT(0)) ? 0 : 1;

    if (zpressed)
        dev_info(&client->dev, "Z button pressed\n");

    cpressed = (recv[5] & BIT(1)) ? 0 : 1;

    if (cpressed) {
        SetGpioPin(led_gpio->regs, led_gpio->gpio);
        dev_info(&client->dev, "C button pressed\n");
    } else {
        ClearGpioPin(led_gpio->regs, led_gpio->gpio);
    }

    /* Send events to the INPUT subsystem */
    input_event(polled_input->input,
                EV_KEY, BTN_Z, zpressed);

    input_event(polled_input->input,
                EV_KEY, BTN_C, cpressed);

    input_sync(polled_input->input);
}

static int nunchuk_probe(struct i2c_client *client,
                         const struct i2c_device_id *id)
{
    int ret;
    u8 buf[2];

    struct input_polled_dev *polled_input;
    struct input_dev *input;
    struct nunchuk_dev *nunchuk;

    pr_info("Called nunchuk_probe\n");

    nunchuk = devm_kzalloc(&client->dev, sizeof(struct nunchuk_dev), GFP_KERNEL);
    if (!nunchuk) {
        dev_err(&client->dev, "Failed to allocate memory\n");
        return -ENOMEM;
    }

    /* Initialize device */

    buf[0] = 0xf0;
    buf[1] = 0x55;

    ret = i2c_master_send(client, buf, 2);
    if (ret != 2) {
        dev_err(&client->dev, "i2c send failed (%d)\n", ret);
        return ret < 0 ? ret : -EIO;
    }

    udelay(1000);

    buf[0] = 0xfb;
    buf[1] = 0x00;

    ret = i2c_master_send(client, buf, 2);
    if (ret != 2) {
        dev_err(&client->dev, "i2c send failed (%d)\n", ret);
        return ret < 0 ? ret : -EIO;
    }

    /* Register input device */

    polled_input = input_allocate_polled_device();
    if (!polled_input) {
        dev_err(&client->dev, "Failed to allocate memory\n");
        return -ENOMEM;
    }

    /* Implement pointers between logical and physical */

    nunchuk->i2c_client = client;
    nunchuk->polled_input = polled_input;
    polled_input->private = nunchuk;
    i2c_set_clientdata(client, nunchuk);
    input = polled_input->input;
    input->dev.parent = &client->dev;

    /* Configure input device */

    input->name = "Wii Nunchuk";
    input->id.bustype = BUS_I2C;

    set_bit(EV_KEY, input->evbit);
    set_bit(BTN_C, input->keybit);
    set_bit(BTN_Z, input->keybit);

    polled_input->poll = nunchuk_poll;
    polled_input->poll_interval = 50;

    /* Register the input device when everything is ready */

    ret = input_register_polled_device(polled_input);
    if (ret < 0) {
        dev_err(&client->dev, "cannot register input device (%d)\n",
                ret);
        goto register_input_fail;
    }

    return 0;

    register_input_fail:
    input_free_polled_device(polled_input);
    return ret;
}

static int nunchuk_remove(struct i2c_client *client)
{
    struct nunchuk_dev *nunchuk = i2c_get_clientdata(client);
    pr_info("Called nunchuk_remove\n");

    input_unregister_polled_device(nunchuk->polled_input);
    input_free_polled_device(nunchuk->polled_input);

    return 0;
}

static const struct i2c_device_id nunchuk_id[] = {
    { "nunchuk", 0 },
    { },
};
static const struct platform_device_id led_id[] = {
    { "led", 0 },
    { },
};
MODULE_DEVICE_TABLE(i2c, nunchuk_id);
MODULE_DEVICE_TABLE(platform, led_id);

#ifdef CONFIG_OF
static struct of_device_id nunchuk_dt_match[] = {
    { .compatible = "nintendo,nunchuk" },
    { },
};
static struct of_device_id led_dt_match[] = {
    { .compatible = "nintendo,led" },
    { },
};
#endif

static struct platform_driver led_driver = {
    .driver = {
        .name = "led",
        .owner = THIS_MODULE,
        .of_match_table = of_match_ptr(led_dt_match),
    },
    .probe = led_probe,
    .remove = led_remove,

};
static struct i2c_driver nunchuk_driver = {
    .driver = {
        .name = "nunchuk",
        .owner = THIS_MODULE,
        .of_match_table = of_match_ptr(nunchuk_dt_match),
    },
    .probe = nunchuk_probe,
    .remove = nunchuk_remove,
    .id_table = nunchuk_id,
};

static int led_probe(struct platform_device *pdev)
{
    struct resource *res;
    struct led_dev *dev;

    pr_info("Called led_probe\n");

    dev = devm_kzalloc(&pdev->dev, sizeof(struct led_dev), GFP_KERNEL);
    if (!dev) {
        dev_err(&pdev->dev, "cannot allocate memory\n");
        return -ENOMEM;
    }

    /* fill led_dev structure with init date like before (note that some date are read from dts) */

    /* fill regs */
    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    if (!res) {
        dev_err(&pdev->dev, "Cannot get resource\n");
        return -EBUSY;
    }

    dev->regs = devm_ioremap(&pdev->dev, res->start, resource_size(res));
    if (IS_ERR(dev->regs))
        return PTR_ERR(dev->regs);

    /* fill gpio pin number */
    if (of_property_read_u8(pdev->dev.of_node, "gpio_pin_no", &dev->gpio) != 0) {
        dev_err(&pdev->dev, "Cannot get gpio pin number\n");
        return -EBUSY;
    }

    /* Initialize GPIO pins. */
    /* LEDS */
    SetGpioPinDirection(dev->regs, dev->gpio, GPIO_DIRECTION_OUT);
    led_gpio = dev;


    return 0;
}

static int led_remove(struct platform_device *pdev)
{
    struct led_dev *dev = platform_get_drvdata(pdev);

    pr_info("Called led_remove\n");

    return 0;
}

static int __init nunchuk_init(void)
{
    int ret;
    ret = i2c_add_driver(&nunchuk_driver);
    ret = platform_driver_register(&led_driver);
    return ret;
}
module_init(nunchuk_init);

static void __exit nunchuk_exit(void)
{
    pr_info("Exiting module...");

    /* Clear GPIO pins (turn off led). */
    ClearGpioPin(led_gpio->regs, led_gpio->gpio);

    /* Set GPIO pins as inputs and disable pull-ups. */
    SetGpioPinDirection(led_gpio->regs, led_gpio->gpio, GPIO_DIRECTION_IN);

    platform_driver_unregister(&led_driver);
    i2c_del_driver(&nunchuk_driver);
}
module_exit(nunchuk_exit);


MODULE_LICENSE("GPL");

