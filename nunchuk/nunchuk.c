#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/input-polldev.h>

#define DEVICE_NAME "nunchuk"



// boilerplate
static const struct i2c_device_id nunchuk_id[] = {
    { "nunchuk", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, nunchuk_id);

// Buffer that contains initialization data for the nunchuk
static char init_buf[] = { 0xf0, 0x55, 0xfb, 0x0 };

// Interface struct that gets passed as private data in input_polled_dev
// We're doing this to make the i2c client accessible on every poll
struct nunchuk_iface
{
    struct i2c_client *client;
    // Buffer which we'll read nunchuk data from'
    char buf_read[6];
};

static int nunchuk_read_registers(struct i2c_client *client, char out[6])
{
    char r = 0x0;
    mdelay(10);
    int res = i2c_master_send(client, &r, 1);
    if(!res) {
        pr_info("Something went wrong with i2c send");
        return -EINVAL;
    }
    mdelay(10);

    res = i2c_master_recv(client, out, 6);
    if(!res) {
        pr_info("Something went wrong with i2c recv");
        return -EINVAL;
    }
    return 0;
}

// Poll function that runs once per interval as defined in poll_interval
static void nunchuk_poll(struct input_polled_dev *dev)
{
    // Fetch i2c client from private data
    struct nunchuk_iface* iface = (struct nunchuk_iface*)(dev->private);
    struct i2c_client *client = iface->client;
    char* buf_read = iface->buf_read;

    // Read function has to be called twice because of device jank
    // The read function writes to buf_read, probably should be improved to
    // handle multiple devices
    nunchuk_read_registers(client, buf_read);
    nunchuk_read_registers(client, buf_read);

    // Fetch data from buf_read according to nunchuk datasheet
    int xpos = buf_read[0];
    int ypos = buf_read[1];

    int x_acc = buf_read[2];
    int y_acc = buf_read[3];
    int z_acc = buf_read[4];

    int zpressed = (buf_read[5] & 1) == 0;
    int cpressed = (buf_read[5] & 2) == 0;

    // Send events for every type of data read from the nunchuk

    // Analog stick
    input_event(dev->input, EV_REL, REL_X, xpos);
    input_event(dev->input, EV_REL, REL_Y, ypos);

    // Accelerometer data
    input_event(dev->input, EV_REL, REL_RX, x_acc);
    input_event(dev->input, EV_REL, REL_RY, y_acc);
    input_event(dev->input, EV_REL, REL_RZ, z_acc);

    // Button presses
    input_event(dev->input, EV_KEY, BTN_Z, zpressed);
    input_event(dev->input, EV_KEY, BTN_C, cpressed);

    input_sync(dev->input);
}

static int nunchuk_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    // Allocate polled device as well as the nunchuk interface which will be used later in poll
    struct nunchuk_iface* iface = devm_kzalloc(&client->dev, sizeof(struct nunchuk_iface), GFP_KERNEL);
    iface->client = client;
    struct input_polled_dev* polled_input = devm_input_allocate_polled_device(&(client->dev));

    if(!polled_input) {
        pr_info("Failed to allocate polled device");
        return -EINVAL;
    }
    // Set nunchuk interface (i2c client) as private data
    polled_input->private = iface;

    // Set up polled_input
    polled_input->poll = nunchuk_poll;
    polled_input->poll_interval = 50;

    // Set up input event types that will be used for the nunchuk
    struct input_dev* input = polled_input->input;
    input->name = "Nunchuk";
    input->id.bustype = BUS_I2C;
    set_bit(EV_KEY, input->evbit); // Key press events
    set_bit(EV_REL, input->evbit); // Relative data events
    set_bit(BTN_C, input->keybit); // C Button
    set_bit(BTN_Z, input->keybit); // Z Button
    set_bit(REL_X, input->relbit); // Analog stick (x-axis)
    set_bit(REL_Y, input->relbit); // Analog stick (y-axis)
    set_bit(REL_RX, input->relbit); // Accelerometer (x-axis)
    set_bit(REL_RY, input->relbit); // Accelerometer (y-axis)
    set_bit(REL_RZ, input->relbit); // Accelerometer (z-axis)
    input_register_polled_device(polled_input);

    // Init device via i2c
    pr_info("Custom I2C device probed at address 0x%02x\n", client->addr);
    int res = i2c_master_send(client, init_buf, 2);
    if(!res) {
        pr_info("Something went wrong with i2c send");
        return -EINVAL;
    }
    udelay(1000);
    res = i2c_master_send(client, init_buf+2, 2);
    if(!res) {
        pr_info("Something went wrong with i2c send #2");
        return -EINVAL;
    }
    return 0;
}

// No need to free data as things are allocated using devm
static int nunchuk_remove(struct i2c_client *client)
{
    pr_info("Custom I2C device removed\n");
    return 0;
}

// Boilerplate
static struct i2c_driver nunchuk_driver = {
    .driver = {
        .name = DEVICE_NAME,
    },
    .probe    = nunchuk_probe,
    .remove   = nunchuk_remove,
    .id_table = nunchuk_id,
};
module_i2c_driver(nunchuk_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bogdan Cvetanovski Pasalic");

