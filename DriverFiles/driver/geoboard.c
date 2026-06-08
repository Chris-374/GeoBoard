#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/gpio.h>
#include "geoboard_ioctl.h"
#define I2C_BUS_NUM 1 /* Pi 3: el header de 40 pines es i2c-1 */
#define HT16K33_ADDR 0x70 /* direccion por defecto del backpack */
/* Comandos del HT16K33 (ver datasheet) */
#define HT16K33_OSC_ON 0x21 /* System Setup: oscilador encendido */
#define HT16K33_DISP_ON 0x81 /* Display Setup: encendido, sin parpadeo*/
#define HT16K33_DISP_OFF 0x80
#define HT16K33_OSC_OFF 0x20
#define HT16K33_DIM 0xE0 /* | (0..15) -> nivel de brillo */

// GPIO pins defined for the buttons
#define GPIO_BTN_UP      17
#define GPIO_BTN_DOWN    27
#define GPIO_BTN_LEFT    22
#define GPIO_BTN_RIGHT   23
#define GPIO_BTN_SELECT  24
#define GPIO_BTN_CHECK   25

static struct i2c_adapter *geo_adapter;
static struct i2c_client *geo_client;
static DEFINE_MUTEX(geo_lock);

/*
=============================================
METHODS FOR THE 8X8 DISPLAY
=============================================
*/

/* framebuffer logico: 1 byte por fila, el bit n = columna n */
static u8 framebuffer[8];
/*
 * El backpack 8x8 de Adafruit tiene las columnas rotadas: el bit de la
 * columna x aparece fisicamente en la posicion (x + 7) % 8. Eso equivale
 * a rotar el byte un bit hacia la derecha. Si tu figura aparece corrida
 * una columna, ajusta o elimina esta funcion (verificalo con un pixel).
 */
static inline u8 geo_rotate(u8 row)
{
 return (u8)((row >> 1) | (row << 7));
}
/* Vuelca el framebuffer logico a la RAM de display del HT16K33 */
static int geo_flush(void)
{
 u8 buf[16] = { 0 };
 int i;
 for (i = 0; i < 8; i++) {
 buf[i * 2] = geo_rotate(framebuffer[i]); /* byte bajo: columnas */
 buf[i * 2 + 1] = 0x00; /* byte alto: sin uso */
 }
 /* Escribe 16 bytes de RAM a partir del registro 0x00 */
 return i2c_smbus_write_i2c_block_data(geo_client, 0x00, 16, buf);
}
static int ht16k33_init(void)
{
 int ret;
 ret = i2c_smbus_write_byte(geo_client, HT16K33_OSC_ON);
 if (ret < 0)
 return ret;
 ret = i2c_smbus_write_byte(geo_client, HT16K33_DISP_ON);
 if (ret < 0)
 return ret;
 ret = i2c_smbus_write_byte(geo_client, HT16K33_DIM | 0x0F); /* brillo max */
 if (ret < 0)
return ret;
 memset(framebuffer, 0, sizeof(framebuffer));
 return geo_flush();
}
/* write(): recibe exactamente 8 bytes = la mascara completa de la matriz */
static ssize_t geo_write(struct file *f, const char __user *ubuf,
 size_t len, loff_t *off)
{
 u8 tmp[8];
 int ret;
 if (len != 8)
 return -EINVAL;
 if (copy_from_user(tmp, ubuf, 8))
 return -EFAULT;
 mutex_lock(&geo_lock);
 memcpy(framebuffer, tmp, 8);
 ret = geo_flush();
 mutex_unlock(&geo_lock);
 return (ret < 0) ? ret : (ssize_t)len;
}

/*
=============================================
METHODS FOR THE BUTTONS
=============================================
*/

static int geo_buttons_init(void)
{
    int ret;

    /*ret = gpio_request_one(GPIO_BTN_UP, GPIOF_IN, "geo_btn_up");
    if (ret) {
        pr_err("geoboard: fallo GPIO_BTN_UP gpio%d ret=%d\n", GPIO_BTN_UP, ret);
        return ret;
    }

    ret = gpio_request_one(GPIO_BTN_DOWN, GPIOF_IN, "geo_btn_down");
    if (ret) {
        pr_err("geoboard: fallo GPIO_BTN_DOWN gpio%d ret=%d\n", GPIO_BTN_DOWN, ret);
        goto err_down;
    }

    ret = gpio_request_one(GPIO_BTN_LEFT, GPIOF_IN, "geo_btn_left");
    if (ret) {
        pr_err("geoboard: fallo GPIO_BTN_LEFT gpio%d ret=%d\n", GPIO_BTN_LEFT, ret);
        goto err_left;
    }

    ret = gpio_request_one(GPIO_BTN_RIGHT, GPIOF_IN, "geo_btn_right");
    if (ret) {
        pr_err("geoboard: fallo GPIO_BTN_RIGHT gpio%d ret=%d\n", GPIO_BTN_RIGHT, ret);
        goto err_right;
    }*/

    ret = gpio_request_one(GPIO_BTN_SELECT, GPIOF_IN, "geo_btn_select");
    if (ret) {
        pr_err("geoboard: fallo GPIO_BTN_SELECT gpio%d ret=%d\n", GPIO_BTN_SELECT, ret);
        goto err_select;
    }

    /*ret = gpio_request_one(GPIO_BTN_CHECK, GPIOF_IN, "geo_btn_check");
    if (ret) {
        pr_err("geoboard: fallo GPIO_BTN_CHECK gpio%d ret=%d\n", GPIO_BTN_CHECK, ret);
        goto err_check;
    }*/

    pr_info("geoboard: botones inicializados correctamente\n");
    return 0;

err_check:
    gpio_free(GPIO_BTN_SELECT);
err_select:
    gpio_free(GPIO_BTN_RIGHT);
err_right:
    gpio_free(GPIO_BTN_LEFT);
err_left:
    gpio_free(GPIO_BTN_DOWN);
err_down:
    gpio_free(GPIO_BTN_UP);
    return ret;
}

static void geo_buttons_exit(void)
{
    /*gpio_free(GPIO_BTN_UP);
    gpio_free(GPIO_BTN_DOWN);
    gpio_free(GPIO_BTN_LEFT);
    gpio_free(GPIO_BTN_RIGHT);*/
    gpio_free(GPIO_BTN_SELECT);
    //gpio_free(GPIO_BTN_CHECK);

    pr_info("geoboard: botones liberados\n");
}

static int geo_read_button_state(void)
{
    if (!gpio_get_value(GPIO_BTN_UP))
        return GEO_BTN_UP;

    if (!gpio_get_value(GPIO_BTN_DOWN))
        return GEO_BTN_DOWN;

    if (!gpio_get_value(GPIO_BTN_LEFT))
        return GEO_BTN_LEFT;

    if (!gpio_get_value(GPIO_BTN_RIGHT))
        return GEO_BTN_RIGHT;

    if (!gpio_get_value(GPIO_BTN_SELECT))
        return GEO_BTN_SELECT;

    if (!gpio_get_value(GPIO_BTN_CHECK))
        return GEO_BTN_CHECK;

    return GEO_BTN_NONE;
}

/*
=============================================
MAIN METHODS
=============================================
*/
static long geo_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
 int ret = 0;
 if (_IOC_TYPE(cmd) != GEO_IOC_MAGIC)
 return -ENOTTY;
 if (_IOC_NR(cmd) > GEO_IOC_MAXNR)
 return -ENOTTY;
 mutex_lock(&geo_lock);
 switch (cmd) {
 case GEO_CLEAR:
 memset(framebuffer, 0, sizeof(framebuffer));
 ret = geo_flush();
 break;
 case GEO_SET_PIXEL: {
 struct geo_pixel p;
 if (copy_from_user(&p, (void __user *)arg, sizeof(p))) {
 ret = -EFAULT;
 break;
 }
 if (p.x < 0 || p.x > 7 || p.y < 0 || p.y > 7) {
 ret = -EINVAL;
 break;
 }
 if (p.state)
 framebuffer[p.y] |= (1 << p.x);
 else
 framebuffer[p.y] &= ~(1 << p.x);
 ret = geo_flush();
 break;
 }
 case GEO_BRIGHTNESS: {
 int b;
 if (copy_from_user(&b, (void __user *)arg, sizeof(b))) {
 ret = -EFAULT;
 break;
 }
 if (b < 0) b = 0;
 if (b > 15) b = 15;
 ret = i2c_smbus_write_byte(geo_client, HT16K33_DIM | b);
 break;
 }
 case GEO_READ_BUTTON: {
    int b = geo_read_button_state();

    if (copy_to_user((void __user *)arg, &b, sizeof(b))) {
        ret = -EFAULT;
        break;
    }

    break;
 }
 default: /* comandos reservados aun no implementados */
 ret = -ENOTTY;
 }
 mutex_unlock(&geo_lock);
 return ret;
}

static const struct file_operations geo_fops = {
 .owner = THIS_MODULE,
 .write = geo_write,
 .unlocked_ioctl = geo_ioctl,
};
static struct miscdevice geo_misc = {
 .minor = MISC_DYNAMIC_MINOR, /* deja que el kernel asigne el minor */
 .name = "geoboard", /* crea /dev/geoboard automaticamente */
 .fops = &geo_fops,
 .mode = 0666, /* accesible sin root (solo para pruebas)*/
};
static int __init geo_init(void)
{
    // Initializing the connection between i2c and the 8x8 adafruit led module 
    struct i2c_board_info info =
    { I2C_BOARD_INFO("geoboard-ht16k33", HT16K33_ADDR) };
    int ret;
    
    geo_adapter = i2c_get_adapter(I2C_BUS_NUM);
    if (!geo_adapter) {
        pr_err("geoboard: no existe el bus i2c-%d (habilitaste I2C?)\n", I2C_BUS_NUM);
        return -ENODEV;
    }
    
    geo_client = i2c_new_client_device(geo_adapter, &info);
    if (IS_ERR(geo_client)) {
        pr_err("geoboard: no se pudo crear el cliente I2C\n");
        i2c_put_adapter(geo_adapter);
        return PTR_ERR(geo_client);
    }
    
    ret = ht16k33_init();
    if (ret < 0) {
        pr_err("geoboard: fallo init HT16K33 (%d). Conexion/0x70?\n", ret);
        goto err_client;
    }
    
    // Initializing the connection with the buttons
    ret = geo_buttons_init();
    if (ret < 0) {
        pr_err("geoboard: fallo inicializando botones (%d)\n", ret);
        goto err_client;
    }
    
    // Initializing misc register
    ret = misc_register(&geo_misc);
    if (ret) {
        pr_err("geoboard: misc_register fallo (%d)\n", ret);
        goto err_buttons;
    }
    
    pr_info("geoboard: cargado. /dev/geoboard listo\n");
    return 0;
    
    err_buttons:
        geo_buttons_exit();
    
    err_client:
        i2c_unregister_device(geo_client);
        i2c_put_adapter(geo_adapter);
        return ret;
}
static void __exit geo_exit(void)
{
 misc_deregister(&geo_misc);
 geo_buttons_exit();
 i2c_smbus_write_byte(geo_client, HT16K33_DISP_OFF);
 i2c_smbus_write_byte(geo_client, HT16K33_OSC_OFF);
 i2c_unregister_device(geo_client);
 i2c_put_adapter(geo_adapter);
 pr_info("geoboard: descargado\n");
}
module_init(geo_init);
module_exit(geo_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Grupo 4 - CE 4303");
MODULE_DESCRIPTION("GeoBoard: matriz LED 8x8 via HT16K33 (I2C)");
