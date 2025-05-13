#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/kdev_t.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/gpio/consumer.h>

#define CMD_A _IOW('L', 0, int)

struct device_test {
    dev_t dev_num;
    int major;
    int minor;
    struct cdev cdev_test;
    struct class *class;
    struct device *device;
    char kbuf[32];
    struct gpio_desc *led_gpio;
};



const struct of_device_id of_match_table_id[] = {
    {.compatible = "myTestLeds"},

};

static int cdev_test_open(struct inode *inode, struct file *file)
{
    struct device_test *dev = container_of(inode->i_cdev, struct device_test, cdev_test);
    file->private_data = dev;
    printk("This is cdev_test_open\n");
    return 0;
}

static long cdev_test_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct device_test *test_dev = file->private_data;
    int value_from_user;

    switch (cmd) {
    case CMD_A: {
        // 添加安全的用户空间数据拷贝
        if (copy_from_user(&value_from_user, (int __user *)arg, sizeof(int))) {
            return -EFAULT;
        }

        if (test_dev->led_gpio) {
            gpiod_set_value(test_dev->led_gpio, value_from_user);
            printk("%s led\n", value_from_user ? "open" : "poweroff");
        } else {
            printk(KERN_ERR "LED GPIO not initialized\n");
            return -EIO;
        }
        break;
    }
    default:
        return -ENOTTY;
    }
    return 0;
}

struct file_operations cdev_test_fops = {
    .owner = THIS_MODULE,
    .open = cdev_test_open,
    .unlocked_ioctl = cdev_test_ioctl,
};

static int my_platform_driver_probe(struct platform_device *pdev)
{
    struct device_test *dev;
    int ret;

    // 动态分配设备结构体
    dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    platform_set_drvdata(pdev, dev);

    // GPIO初始化（强制要求GPIO存在）
    dev->led_gpio = gpiod_get(&pdev->dev, "my", GPIOD_OUT_LOW);
    if (IS_ERR(dev->led_gpio)) {
        ret = PTR_ERR(dev->led_gpio);
        printk(KERN_ERR "Failed to get GPIO: %d\n", ret);
        goto err_exit;
    }

    // 字符设备注册
    ret = alloc_chrdev_region(&dev->dev_num, 0, 1, "led_dev");
    if (ret < 0) {
        printk(KERN_ERR "alloc_chrdev_region error\n");
        goto err_gpio_put;
    }

    dev->major = MAJOR(dev->dev_num);
    dev->minor = MINOR(dev->dev_num);

    cdev_init(&dev->cdev_test, &cdev_test_fops);
    dev->cdev_test.owner = THIS_MODULE;

    ret = cdev_add(&dev->cdev_test, dev->dev_num, 1);
    if (ret < 0) {
        printk(KERN_ERR "cdev_add error\n");
        goto err_unreg_chrdev;
    }

    // 创建设备节点
    dev->class = class_create(THIS_MODULE, "led_class");
    if (IS_ERR(dev->class)) {
        ret = PTR_ERR(dev->class);
        printk(KERN_ERR "create class error\n");
        goto err_cdev_del;
    }

    dev->device = device_create(dev->class, NULL, dev->dev_num, NULL, "test");
    if (IS_ERR(dev->device)) {
        ret = PTR_ERR(dev->device);
        printk(KERN_ERR "create device error\n");
        goto err_class_destroy;
    }

    printk(KERN_INFO "Probe success! Major:%d Minor:%d\n", dev->major, dev->minor);
    return 0;

err_class_destroy:
    class_destroy(dev->class);
err_cdev_del:
    cdev_del(&dev->cdev_test);
err_unreg_chrdev:
    unregister_chrdev_region(dev->dev_num, 1);
err_gpio_put:
    gpiod_put(dev->led_gpio);
err_exit:
    return ret;
}

static int my_platform_driver_remove(struct platform_device *pdev)
{
    struct device_test *dev = platform_get_drvdata(pdev);

    // 安全关闭GPIO
    if (dev->led_gpio) {
        gpiod_set_value(dev->led_gpio, 0);
        gpiod_put(dev->led_gpio);
    }

    device_destroy(dev->class, dev->dev_num);
    class_destroy(dev->class);
    cdev_del(&dev->cdev_test);
    unregister_chrdev_region(dev->dev_num, 1);

    printk(KERN_INFO "Driver removed\n");
    return 0;
}

static struct platform_driver my_platform_driver = {
    .probe = my_platform_driver_probe,
    .remove = my_platform_driver_remove,
    .driver = {
        .name = "led_platform_driver",
        .of_match_table = of_match_table_id,
        .owner = THIS_MODULE,
    },
};

module_platform_driver(my_platform_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("XJX");
MODULE_DESCRIPTION("IOCTRL+GPIO LED Control Driver");