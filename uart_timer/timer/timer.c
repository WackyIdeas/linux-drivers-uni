#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/pm_runtime.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/sched.h>

#define REG_CONTROL     0x00
#define REG_COUNTER_LO  0x04
#define REG_COMPARE(n)  (0x0c + (n) * 4)

// Offsets for relevant PL011 registers
#define UART_DR 0x00
#define UART_FR 0x18
// Bit offset for the TXFF flag
#define FR_TX_FULL (1 << 5)

#define MAX_TIMER       3
#define DEFAULT_TIMER   1

// Character to output via UART
static char output_char = 'A';
// Use a primitive spinlock as the usecase is simplistic, potentially redundant
// as there's no discernable difference in throughput or behavior, probably limited by
// the FIFO buffer capacity anyway.
// https://www.kernel.org/doc/html/latest/locking/spinlocks.html
static DEFINE_SPINLOCK(lock_uart);

struct timer_dev
{
	void __iomem *regs;
	void __iomem *regs_uart;
	struct miscdevice miscdev;
	int irq[MAX_TIMER+1];
	unsigned char timer;
	unsigned timer_freq;
	unsigned timer_ticks;
	atomic_t counter;
	char buffer[32];
	wait_queue_head_t timer_wait;
	unsigned timer_sec;
};

static unsigned int reg_read(struct timer_dev *dev, int offset, bool uart)
{
	return readl((uart ? dev->regs_uart : dev->regs) + offset);
}

static void reg_write(struct timer_dev *dev, int value, int offset, bool uart)
{
	writel(value, (uart ? dev->regs_uart : dev->regs) + offset);
}

static irqreturn_t timer_irq(int irq, void *dev_id)
{
	struct timer_dev *dev = dev_id;
	unsigned int old_value;

	// Resetting the counter
	reg_write(dev, (1 << dev->timer), REG_CONTROL, 0);
	old_value = reg_read(dev, REG_COMPARE(dev->timer), 0);
	reg_write(dev, old_value + dev->timer_ticks, REG_COMPARE(dev->timer), 0);

	// Increment counter by 1, atomically
	atomic_inc(&dev->counter);

	// Wake up threads in this wait queue that can be interrupted
	wake_up_interruptible(&dev->timer_wait);
	return IRQ_HANDLED;
}

static ssize_t timer_read(struct file *file, char __user *buf, size_t sz, loff_t *ppos)
{
	struct timer_dev *dev;
	unsigned long flags;
	// Create a wait queue entry
	DEFINE_WAIT(wait);
	// get timer_dev via container_of
	dev = container_of(file->private_data, struct timer_dev, miscdev);

	// Timer boilerplate
	// Using a dynamically loaded wait queue head instead of using DECLARE_WAIT_QUEUE_HEAD
	// https://lwn.net/Articles/22913/
	// https://www.kernel.org/doc./htmldocs/kernel-hacking/queues.html
	// Add wait to the wait queue head, and make this thread still responsive to outside signals
	prepare_to_wait(&dev->timer_wait, &wait, TASK_INTERRUPTIBLE);
	// Right before the thread goes to sleep, call schedule() to defer execution to another thread
	// The task therefore willingly gives up execution
	// See https://trepo.tuni.fi/bitstream/handle/10024/96864/GRADU-1428493916.pdf pages 31-32
	schedule();
	finish_wait(&dev->timer_wait, &wait);

	// Read the PL011 flag register and examine the value at TXFF
	// https://developer.arm.com/documentation/ddi0183/f/programmer-s-model/register-descriptions/flag-register--uartfr?lang=en
	while(reg_read(dev, UART_FR, 1) & FR_TX_FULL)
	{
		cpu_relax(); // To prevent busy waiting
	}
	// Write the output character to the PL011 data register
	spin_lock_irqsave(&lock_uart, flags);
	reg_write(dev, output_char, UART_DR, 1);
	spin_unlock_irqrestore(&lock_uart, flags);
	return 1;
}

static ssize_t timer_write(struct file *file, const char __user *buf, size_t sz, loff_t *ppos)
{
	struct timer_dev *dev;
	dev = container_of(file->private_data, struct timer_dev, miscdev);
	// Reset counter to 0, there really isn't anything to do here
	atomic_set(&dev->counter, 0);
	return sz;
}

static long timer_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	switch(cmd)
	{
		case 0:
			// Get the character provided from userspace
			if(copy_from_user(&output_char, (char*)(arg), sizeof(output_char)))
			{
				pr_err("Failed to write to output_char!\n");
			}
			pr_info("output_char = %c\n", output_char);
			break;
		default:
			pr_err("Unknown command!\n");
			break;
	}
	return 0;
}

static const struct file_operations timer_fops =
{
	.owner = THIS_MODULE,
	.write = timer_write,
	.read = timer_read,
	.unlocked_ioctl = timer_ioctl,
};

static int timer_probe(struct platform_device *pdev)
{
	struct timer_dev *dev;
	struct resource *res;
	struct resource *res_uart;
	unsigned int freq;
	unsigned int sec = 1;
	int i;
	int ret;

	// Allocate memory for the structure
	dev = devm_kzalloc(&pdev->dev, sizeof(struct timer_dev), GFP_KERNEL);

	if (!dev)
		return -ENOMEM;

	/* Filter just MEM ones */
	// For debugging purposes, we want there to be two IORESOURCE_MEM entries
	pr_info("custom: -- IORESOURCE_MEM entries --\n");
	for (i = 0; i < pdev->num_resources; i++)
	{
		struct resource *r = &pdev->resource[i];
		if (resource_type(r) == IORESOURCE_MEM)
			pr_info("custom: MEM[%d]: 0x%llx .. 0x%llx (size 0x%llx)\n",
					i,
		   (unsigned long long)r->start,
					(unsigned long long)r->end,
					(unsigned long long)resource_size(r));
	}

	// Get physical address for timer, from the first IORESOURCE_MEM resource
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!res)
		return -ENODEV;

	// Remap from physical address to virtual address
	// We can reserve this address space to ourselves as no
	// other kernel module will use it
	dev->regs = devm_ioremap_resource(&pdev->dev, res);

	if (!dev->regs)
		return -ENOMEM;
	else
		printk("Register Timer: %p\n", (dev->regs));

	// Get base address of PL011 UART, from the second IORESOURCE_MEM resource
	res_uart = platform_get_resource(pdev, IORESOURCE_MEM, 1);

	if (!res_uart)
		return -ENODEV;

	// Remap from physical address to virtual address
	// Calling devm_ioremap_resource here will fail, as the address space
	// is already reserved by the UART driver in use. Instead simply call
	// ioremap which will provide us with a valid virtual address space
	dev->regs_uart = ioremap(res_uart->start, res_uart->end - res_uart->start);

	if (!dev->regs_uart)
		return -ENOMEM;
	else
		printk("Register UART: %p\n", (dev->regs_uart));

	// Power management boilerplate
	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	// Get frequency and uart_period property from pdev's device of_node
	// The PDF doesn't mention what the uart_period is used for, so we'll
	// define it to be the period of the timer, defined as 2 seconds by
	// default (2000ms).
	of_property_read_u32(pdev->dev.of_node, "clock-frequency", &freq);
	of_property_read_u32(pdev->dev.of_node, "uart_period", &sec);

	// DEFAULT_TIMER is TIMER1
	dev->timer_freq = freq;
	dev->timer = DEFAULT_TIMER;
	dev->timer_sec = sec;
	dev->timer_ticks = dev->timer_freq * (dev->timer_sec / 1000);

	// Init wait queue
	init_waitqueue_head(&dev->timer_wait);

	// Get IRQ number from each timer in the DTS
	for (i = 0; i <= MAX_TIMER; i++)
		dev->irq[i] = platform_get_irq(pdev, i);
	// Setting atomic stuff requires the use of the atomic API
	atomic_set(&dev->counter, 0);

	/* clear timer match detect status bit */
	reg_write(dev, (1 << dev->timer), REG_CONTROL, 0);
	// devm_request_irq takes a device ptr, IRQ signal number, IRQ callback, irqflags, the device name and
	// a generic reference to any structure
	ret = devm_request_irq(&pdev->dev, dev->irq[dev->timer], timer_irq, 0, "timer", dev);

	if(ret)
	{
		return ret;
	}

	// Writing the new value the counter should follow, in our case 2 seconds
	// REG_COMPARE will return the offset to the comparator register for a given timer (indexed)
	reg_write(dev, reg_read(dev, REG_COUNTER_LO, 0) + dev->timer_ticks, REG_COMPARE(dev->timer), 0);

	// Create misc driver for ioctl and read
	dev->miscdev.minor = MISC_DYNAMIC_MINOR;
	dev->miscdev.name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "timer-%x", res->start);
	dev->miscdev.fops = &timer_fops;

	misc_register(&dev->miscdev);
	// Make the timer_dev device accessible from the platform device structure
	platform_set_drvdata(pdev, dev);

	return 0;
}

static int timer_remove(struct platform_device *pdev)
{
	struct timer_dev *dev = platform_get_drvdata(pdev);
	misc_deregister(&dev->miscdev);
	pm_runtime_disable(&pdev->dev);
	return 0;
}

static struct of_device_id timer_dt_match[] =
{
	{ .compatible = "rtrk,timer" },
	{ },
};

MODULE_DEVICE_TABLE(of, timer_dt_match);

static struct platform_driver timer_driver =
{
	.driver =
	{
		.name = "rtrk",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(timer_dt_match),
	},
	.probe = timer_probe,
	.remove = timer_remove,
};

module_platform_driver(timer_driver);

MODULE_LICENSE("GPL");
