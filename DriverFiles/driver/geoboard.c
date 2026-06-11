/*
 * geoboard.c  ---  Driver del GeoBoard: matriz LED 8x8 (HT16K33/I2C) + botones (gpiod)
 * CE 4303 - Principios de Sistemas Operativos - Grupo 4
 *
 * Botones migrados al API moderno de descriptores (gpiod). Los pines se
 * declaran en geoboard-overlay.dts; el kernel crea un platform_device que
 * casa con este driver (compatible = "geoboard,buttons") y ejecuta probe().
 *
 * Requiere cargar el overlay:  dtoverlay=geoboard  en config.txt
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include "geoboard_ioctl.h"

#define I2C_BUS_NUM      1
#define HT16K33_ADDR     0x70
#define HT16K33_OSC_ON   0x21
#define HT16K33_DISP_ON  0x81
#define HT16K33_DISP_OFF 0x80
#define HT16K33_OSC_OFF  0x20
#define HT16K33_DIM      0xE0

static struct i2c_adapter *geo_adapter;
static struct i2c_client  *geo_client;
static DEFINE_MUTEX(geo_lock);

/* ====================== BOTONES (gpiod) ====================== */
/*
 * Nombres que coinciden con las propiedades "<nombre>-gpios" del overlay,
 * y el codigo que devuelve cada uno (definido en geoboard_ioctl.h).
 */
#define NBTN 6
static const char *btn_names[NBTN] = {
	"up", "down", "left", "right", "select", "check"
};
static const int btn_codes[NBTN] = {
	GEO_BTN_UP, GEO_BTN_DOWN, GEO_BTN_LEFT,
	GEO_BTN_RIGHT, GEO_BTN_SELECT, GEO_BTN_CHECK
};
static struct gpio_desc *btn_desc[NBTN];
/* ---- Servo (BUZZER GPIO 6) ---- */
static struct gpio_desc *buzzer;
/* ---- Servo (PWM por software con hrtimer en GPIO 13) ---- */
static struct gpio_desc *servo;
static struct hrtimer    servo_timer;
static int   servo_pulse_us;       /* ancho de pulso objetivo (us) */
static int   servo_cycles_left;    /* periodos de 20 ms restantes  */
static bool  servo_high;           /* fase actual del pulso        */

#define SERVO_PERIOD_US    20000   /* 50 Hz                        */
#define SERVO_UP_US         2000   /* bandera arriba (~+90)        */
#define SERVO_DOWN_US       1000   /* bandera abajo  (~-90)        */
#define SERVO_MOVE_CYCLES     30   /* 30 * 20 ms = 600 ms          */
#define US_TO_KT(us)  ns_to_ktime((u64)(us) * 1000)

/* Devuelve el codigo del primer boton presionado, o GEO_BTN_NONE.
 * Como el overlay marca los pines ACTIVE_LOW, gpiod_get_value() ya
 * devuelve 1 = presionado (sin necesidad de invertir). */
static int geo_read_button_state(void)
{
	int i;

	for (i = 0; i < NBTN; i++)
		if (gpiod_get_value(btn_desc[i]))
			return btn_codes[i];
	return GEO_BTN_NONE;
}

/* ====================== MATRIZ 8x8 (HT16K33) ====================== */
static u8 framebuffer[8];

static inline u8 geo_rotate(u8 row)
{
	return (u8)((row >> 1) | (row << 7));
}

static int geo_flush(void)
{
	u8 buf[16] = { 0 };
	int i;

	for (i = 0; i < 8; i++) {
		buf[i * 2]     = geo_rotate(framebuffer[i]);
		buf[i * 2 + 1] = 0x00;
	}
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
	ret = i2c_smbus_write_byte(geo_client, HT16K33_DIM | 0x0F);
	if (ret < 0)
		return ret;

	memset(framebuffer, 0, sizeof(framebuffer));
	return geo_flush();
}

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

/* Conmuta el GPIO para formar el pulso PWM; corre en contexto atomico. */
static enum hrtimer_restart servo_tick(struct hrtimer *t)
{
 if (servo_high) {
  gpiod_set_value(servo, 0);          /* fin del pulso alto */
  servo_high = false;
  hrtimer_forward_now(t, US_TO_KT(SERVO_PERIOD_US - servo_pulse_us));
  return HRTIMER_RESTART;
 }

 /* fin del periodo: empezar otro, salvo que se acaben los ciclos */
 if (--servo_cycles_left <= 0) {
  gpiod_set_value(servo, 0);          /* suelta el servo */
  return HRTIMER_NORESTART;
 }
 gpiod_set_value(servo, 1);
 servo_high = true;
 hrtimer_forward_now(t, US_TO_KT(servo_pulse_us));
 return HRTIMER_RESTART;
}

/* Mueve el servo al ancho de pulso indicado durante ~600 ms y lo suelta. */
static void servo_move(int pulse_us)
{
 hrtimer_cancel(&servo_timer);       /* detén cualquier movimiento previo */
 servo_pulse_us    = pulse_us;
 servo_cycles_left = SERVO_MOVE_CYCLES;
 servo_high        = true;
 gpiod_set_value(servo, 1);
 hrtimer_start(&servo_timer, US_TO_KT(pulse_us), HRTIMER_MODE_REL);
}

/* ====================== IOCTL ====================== */
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
			framebuffer[p.y] |=  (1 << p.x);
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
		if (b < 0)
			b = 0;
		if (b > 15)
			b = 15;
		ret = i2c_smbus_write_byte(geo_client, HT16K33_DIM | b);
		break;
	}

	case GEO_READ_BUTTON: {
		int b = geo_read_button_state();

		if (copy_to_user((void __user *)arg, &b, sizeof(b)))
			ret = -EFAULT;
		break;
	}
	
	case GEO_BUZZER: {
	  int on;

	  if (copy_from_user(&on, (void __user *)arg, sizeof(on))) {
	   ret = -EFAULT;
	   break;
	  }
	  gpiod_set_value(buzzer, on ? 1 : 0);
	  break;
	 }
	 
	case GEO_SERVO: {
	  int up;

	  if (copy_from_user(&up, (void __user *)arg, sizeof(up))) {
	   ret = -EFAULT;
	   break;
	  }
	  servo_move(up ? SERVO_UP_US : SERVO_DOWN_US);
	  break;
	 }

	default:
		ret = -ENOTTY;
	}
	mutex_unlock(&geo_lock);
	return ret;
}

static const struct file_operations geo_fops = {
	.owner          = THIS_MODULE,
	.write          = geo_write,
	.unlocked_ioctl = geo_ioctl,
};

static struct miscdevice geo_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "geoboard",
	.fops  = &geo_fops,
	.mode  = 0666,
};

/* ====================== PLATFORM DRIVER ====================== */
static int geoboard_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct i2c_board_info info =
		{ I2C_BOARD_INFO("geoboard-ht16k33", HT16K33_ADDR) };
	int ret, i;

	/* --- Matriz por I2C --- */
	geo_adapter = i2c_get_adapter(I2C_BUS_NUM);
	if (!geo_adapter)
		return dev_err_probe(dev, -EPROBE_DEFER,
				     "bus i2c-%d aun no disponible\n", I2C_BUS_NUM);

	geo_client = i2c_new_client_device(geo_adapter, &info);
	if (IS_ERR(geo_client)) {
		i2c_put_adapter(geo_adapter);
		return dev_err_probe(dev, PTR_ERR(geo_client),
				     "no se pudo crear el cliente I2C\n");
	}

	ret = ht16k33_init();
	if (ret < 0) {
		dev_err(dev, "fallo init HT16K33 (%d)\n", ret);
		goto err_client;
	}

	/* --- Botones por gpiod (resueltos desde el overlay) --- */
	for (i = 0; i < NBTN; i++) {
		btn_desc[i] = devm_gpiod_get(dev, btn_names[i], GPIOD_IN);
		if (IS_ERR(btn_desc[i])) {
			ret = PTR_ERR(btn_desc[i]);
			dev_err(dev, "fallo gpiod '%s-gpios' (%d)\n",
				btn_names[i], ret);
			goto err_client;   /* devm libera los ya pedidos */
		}
	}
	
	buzzer = devm_gpiod_get(dev, "buzzer", GPIOD_OUT_LOW);
	if (IS_ERR(buzzer)) {
	 ret = PTR_ERR(buzzer);
	 dev_err(dev, "fallo gpiod 'buzzer-gpios' (%d)\n", ret);
	 goto err_client;
	}
	
	servo = devm_gpiod_get(dev, "servo", GPIOD_OUT_LOW);
	 if (IS_ERR(servo)) {
	  ret = PTR_ERR(servo);
	  dev_err(dev, "fallo gpiod 'servo-gpios' (%d)\n", ret);
	  goto err_client;
	 }
	 hrtimer_init(&servo_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	 servo_timer.function = servo_tick;

	/* --- Character device --- */
	ret = misc_register(&geo_misc);
	if (ret) {
		dev_err(dev, "misc_register fallo (%d)\n", ret);
		goto err_client;
	}

	dev_info(dev, "geoboard listo: /dev/geoboard + %d botones gpiod\n", NBTN);
	return 0;

err_client:
	i2c_unregister_device(geo_client);
	i2c_put_adapter(geo_adapter);
	return ret;
}

/* NOTA: en kernel 6.11+ remove() devuelve void. En 6.6 (Bookworm) devuelve int. */
static void geoboard_remove(struct platform_device *pdev)
{
	misc_deregister(&geo_misc);
	i2c_smbus_write_byte(geo_client, HT16K33_DISP_OFF);
	i2c_smbus_write_byte(geo_client, HT16K33_OSC_OFF);
	i2c_unregister_device(geo_client);
	i2c_put_adapter(geo_adapter);
	/* los descriptores gpiod se liberan solos (devm) */
	dev_info(&pdev->dev, "geoboard descargado\n");
	
	hrtimer_cancel(&servo_timer);
}

static const struct of_device_id geoboard_of_match[] = {
	{ .compatible = "geoboard,buttons" },
	{ }
};
MODULE_DEVICE_TABLE(of, geoboard_of_match);

static struct platform_driver geoboard_driver = {
	.probe  = geoboard_probe,
	.remove = geoboard_remove,
	.driver = {
		.name           = "geoboard",
		.of_match_table = geoboard_of_match,
	},
};
module_platform_driver(geoboard_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Grupo 4 - CE 4303");
MODULE_DESCRIPTION("GeoBoard: matriz LED 8x8 (HT16K33/I2C) + botones (gpiod)");
