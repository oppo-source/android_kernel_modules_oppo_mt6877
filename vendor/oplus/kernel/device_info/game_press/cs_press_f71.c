/*
 *-----------------------------------------------------------------------------
 * The confidential and proprietary information contained in this file may
 * only be used by a person authorised under and to the extent permitted
 * by a subsisting licensing agreement from  CHIPSEA.
 *
 *              (C) COPYRIGHT 2020 SHENZHEN CHIPSEA TECHNOLOG_ERRIES CO.,LTD.
 *                  ALL RIGHTS RESERVED
 *
 * This entire notice must be reproduced on all copies of this file
 * and copies of this file may only be made by a person if such person is
 * permitted to do so under the terms of a subsisting license agreement
 * from CHIPSEA.
 *
 *        Release Information : CSA37F71 chip forcetouch fw linux driver source file
 *        version : v1.x
 *-----------------------------------------------------------------------------
 */

#include "cs_press_f71.h"
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <uapi/linux/sched/types.h>

//#define ALIENTEK
#ifndef ALIENTEK
//#include <soc/oplus/system/boot_mode.h>
#include <linux/thermal.h>
#endif
const char *cs_driver_ver = "1.13";

#define PROC_FOPS_NUM  14
#define PROC_NAME_LEN  32

#ifndef ALIENTEK
#define FOPS_ARRAY(_open, _write) \
{\
    .proc_open = _open, \
    .proc_read = seq_read, \
    .proc_release = single_release, \
    .proc_write = _write, \
    .proc_lseek = seq_lseek, \
}
#else
#define FOPS_ARRAY(_open, _write) \
{\
    .owner = THIS_MODULE, \
    .open = _open, \
    .read = seq_read, \
    .llseek = seq_lseek, \
    .release = single_release, \
    .write = _write, \
}
#endif
LIST_HEAD(gScenes);
LIST_HEAD(gSceneClients);

static struct mutex    i2c_rw_lock;
static DEFINE_MUTEX(i2c_rw_lock);

static DEFINE_MUTEX(press_lock);

static struct cs_press_t g_cs_press;

#define NORMAL_GEAT_2LEVLE_EN 1
#define FW_ACTION_HOTPLUG 1

#ifdef INT_SET_EN
static DECLARE_WAIT_QUEUE_HEAD(cs_press_waiter);
static int cs_press_irq_flag;
int cs_press_irq_gpio;
int cs_press_irq_num;

/*1 enable,0 disable,  need to confirm after register eint*/
static int cs_irq_flag = 1;
struct input_dev *cs_input_dev;
#endif
/******* fuction definition start **********/
#ifdef INT_SET_EN
static void cs_irq_enable(void);
static void cs_irq_disable(void);
#endif
//static void ftm_boot_mode_check(void);

void cs_press_struct_init(void)
{
    int i;
    g_cs_press.update_type = HIGH_VER_FILE_UPDATE;/*1:force update, 0:to higher ver update*/
    g_cs_press.updating_flag = 0;
    g_cs_press.update_done = 0;
    /* Initialize press statistics variables */
    g_cs_press.long_press_over_10min_cnt = 0;
    g_cs_press.long_press_max_duration_ms = 0;
    g_cs_press.press_start_time_jiffies = 0;
    g_cs_press.is_press_active = false;
    g_cs_press.is_press_over_10min = false;
    /* Initialize force value statistics variables */
    g_cs_press.cur_down_max_force = 0;
    g_cs_press.total_down_max_force = 0;
    memset(g_cs_press.down_max_force, 0, sizeof(g_cs_press.down_max_force));
    /* Initialize event statistics */
    for (i = 0; i < EVENT_STATISTICS_ARRAY_SIZE; i++) {
        g_cs_press.event_total_cnt[i] = 0;
        g_cs_press.suspend_abnormal_event_cnt[i] = 0;
    }
    /* Initialize mode type mismatch statistics */
    memset(g_cs_press.mode_type_mismatch_cnt, 0, sizeof(g_cs_press.mode_type_mismatch_cnt));
    g_cs_press.none_mode_interrupt_cnt = 0;
}

/**@brief     read n reg datas from reg_addr.
 * @param[in]  dev:      csa37f71 device struct.
 * @param[in]  reg_addr: register addr
 * @param[in]  len:      read data lenth
 * @param[out] buf:     read data buffer addr.
 * @return     i2c_transfer status
 *             OK:      return num of send data
 *             err:     return err code
 */
static int cs_i2c_read_bytes(struct i2c_client *client, unsigned char reg_addr, void *buf, unsigned int len)
{
    int ret = 0;
    struct i2c_msg msg[2];
    struct i2c_adapter *adapter = client->adapter;
    int i = 5;

    if (buf == NULL) {
        LOG_ERR("input buf is NULL\n");
        return -1;
    }
    if (len == 0) {
        LOG_ERR("input len is invalid,len = 0\n");
        return -1;
    }
    /* msg[0] send first addr for read */
    msg[0].addr = client->addr;            /* csa37f71 addr */
    msg[0].flags = 0;                      /* send flag */
    msg[0].buf = &reg_addr;                /* reg addr */
    msg[0].len = 1;                        /* reg lenth */

    /* msg[1]read data */
    msg[1].addr = client->addr;
    msg[1].flags = I2C_M_RD;               /* read flag */
    msg[1].buf = buf;
    msg[1].len = len;
    mutex_lock(&i2c_rw_lock);
    do {
        ret = i2c_transfer(adapter, msg, sizeof(msg) / sizeof(struct i2c_msg));
        if (ret <= 0) {
            LOG_ERR("i2c read failed! err_code:%d\n", ret);
            msleep(20);
        } else {
            break;
        }
        i--;
    } while (i > 0);
    mutex_unlock(&i2c_rw_lock);
    return ret;
}

 /**@brief    send multi datas to reg_addr.
 * @param[in]  dev:      csa37f71 device struct.
 * @param[in]  reg_addr: register addr
 * @param[in]  len:      send data lenth
 * @param[out] buf:     send data buffer addr.
 * @return     i2c_transfer status
 *             OK:      return num of send data
 *             err:     return err code
 */
static int cs_i2c_write_bytes(struct i2c_client *client, unsigned char reg_addr, unsigned char *buf, unsigned char len)
{
    int ret = 0;
    unsigned char *t_buf = NULL;
    struct i2c_msg msg;
    struct i2c_adapter *adapter = client->adapter;
    int i = 5;

    if (buf == NULL) {
        LOG_ERR("input buf is NULL\n");
        return -1;
    }
    if (len == 0) {
        LOG_ERR("input len is invalid,len = 0\n");
        return -1;
    }
    t_buf = (unsigned char *)kmalloc(len + sizeof(reg_addr), GFP_KERNEL);
    if (!t_buf) {
        LOG_ERR("kmalloc  failed\n");
        return -1;
    }
    t_buf[0] = reg_addr;            /* register first addr */
    memcpy(&t_buf[1],buf,len);      /* copy send data to b[256]*/

    msg.addr = client->addr;
    msg.flags = 0;                  /* write flag*/
    msg.buf = t_buf;
    msg.len = len + 1;
    mutex_lock(&i2c_rw_lock);
    do {
        ret = i2c_transfer(adapter, &msg, 1);
        if (ret <= 0) {
            LOG_ERR("i2c write failed! err_code:%d\n", ret);
            msleep(20);
        } else if (ret > 0) {
            break;
        }
        i--;
    } while (i > 0);
    mutex_unlock(&i2c_rw_lock);
    kfree(t_buf);
    return ret;
}

/**@brief      read n reg datas from reg_addr.
 * @param[in]  dev:         csa37f71 device struct.
 * @param[in]  reg_addr:    register addr,addr type is word type
 * @param[in]  len:         read data lenth
 * @param[out] buf:         read data buffer addr.
 * @return     i2c_transfer status
 *             OK:      return num of send data
 *             err:     return err code
 */
static int cs_i2c_read_bytes_by_u16_addr(struct i2c_client *client, unsigned short reg_addr, void *buf, int len)
{
    int ret = 0;
    unsigned char reg16[2];
    struct i2c_msg msg[2];
    struct i2c_adapter *adapter = client->adapter;

    if (buf == NULL) {
        LOG_ERR("input buf is NULL\n");
        return -1;
    }
    if (len == 0) {
        LOG_ERR("input len is invalid,len = 0\n");
        return -1;
    }

    reg16[0] = (reg_addr >> 8)&0xff;
    reg16[1] = reg_addr & 0xff;
    /* msg[0] send first addr for read */
    msg[0].addr = client->addr;            /* csa37f71 addr */
    msg[0].flags = 0;                      /* send flag */
    msg[0].buf = reg16;                    /* reg addr */
    msg[0].len = sizeof(reg16);            /* reg lenth:2*byte */

    /* msg[1]read data */
    msg[1].addr = client->addr;
    msg[1].flags = I2C_M_RD;               /* read flag*/
    msg[1].buf = buf;
    msg[1].len = len;

    ret = i2c_transfer(adapter, msg, 2);
    if (ret == 2) {
        ret = 0;
    } else {
        LOG_ERR("i2c read failed! err_code:%d\n", ret);
        msleep(20);
    }
    return ret;
}

 /**@brief    send multi datas to reg_addr.
 * @param[in] dev:      csa37f71 device struct.
 * @param[in] reg_addr: register addr
 * @param[in] len:      send data lenth
 * @param[out] buf:     send data buffer addr.
 * @return     i2c_transfer status
 *             OK:   return num of send data
 *             err:  return err code
 */
static int cs_i2c_write_bytes_by_u16_addr(struct i2c_client *client, unsigned short reg_addr, unsigned char *buf, unsigned char len)
{
    int ret = 0;
    unsigned char *t_buf = NULL;
    struct i2c_msg msg;
    struct i2c_adapter *adapter = client->adapter;

    if (buf == NULL) {
        LOG_ERR("input buf is NULL\n");
        return -1;
    }
    if (len == 0) {
        LOG_ERR("input len is invalid,len = 0\n");
        return -1;
    }
    t_buf = (unsigned char *)kmalloc(len + sizeof(reg_addr), GFP_KERNEL);
    if (!t_buf) {
        LOG_ERR("kmalloc  failed\n");
        return -1;
    }

    t_buf[0] = (reg_addr >> 8)&0xff; /* register first addr */
    t_buf[1] = reg_addr & 0xff;
    memcpy(&t_buf[2],buf,len);       /* copy send data to b[256]*/

    msg.addr = client->addr;
    msg.flags = 0;                   /* write flag*/

    msg.buf = t_buf;
    msg.len = len + 2;
    ret = i2c_transfer(adapter, &msg, 1);
    if (ret < 0)
    {
        LOG_ERR("i2c write failed! err_code:%d\n", ret);
        msleep(20);
    }
    kfree(t_buf);
    return ret;
}

/**
  * @brief      iic write funciton
  * @param[in]  regAddress: reg data
  * @param[in]  dat:        point to data to write
  * @param[in]  length:     write data length
  * @retval     0:success, < 0: fail
  */
static int cs_press_iic_write(unsigned char regAddress, unsigned char *dat, unsigned int length)
{
    int ret = 0;
    int i = 0;
#if defined(CONFIG_OPLUS_FEATURE_FEEDBACK) || defined(CONFIG_OPLUS_FEATURE_FEEDBACK_MODULE)
    char payload[1024] = {0x00};
#endif

    if (dat == NULL) {
        LOG_ERR("input buf is NULL\n");
        return -1;
    }
    if (length == 0) {
        LOG_ERR("input len is invalid,len = 0\n");
        return -1;
    }
    ret = cs_i2c_write_bytes(g_cs_press.client, regAddress, dat, length);
    if (ret < 0) {
#if defined(CONFIG_OPLUS_FEATURE_FEEDBACK) || defined(CONFIG_OPLUS_FEATURE_FEEDBACK_MODULE)
        scnprintf(payload, sizeof(payload),
                   "NULL$$EventField@@I2cWrite$$FieldData@@Err%d$$detailData@@%u[%*ph]%d",
                   ret, regAddress, length, dat, length);
        oplus_kevent_fb(PSW_BSP_KEYPAD, CS_PRESS_FB_BUS_TRANS_TYPE, payload);
#endif
        i = g_cs_press.bus_error_cnt % BUS_ERROR_MSG_CNT;
        memset(g_cs_press.bus_error_msg[i], 0, BUS_ERROR_MSG_SIZE);
        scnprintf(g_cs_press.bus_error_msg[i], sizeof(g_cs_press.bus_error_msg[i]) - 1,
                   "%d WrErr%d %u[%*ph]%u", g_cs_press.bus_error_cnt, ret, regAddress, length, dat, length);
        g_cs_press.bus_error_cnt++;
        return -1;
    }
    return 0;
}

/**
  * @brief       iic read funciton
  * @param[in]   regAddress: reg data,
  * @param[out]  dat:        read data buffer,
  * @param[in]   length:     read data length
  * @retval      0:success, -1: fail
  */
static int cs_press_iic_read(unsigned char regAddress, unsigned char *dat, unsigned int length)
{
    int ret = 0;
    int i = 0;
#if defined(CONFIG_OPLUS_FEATURE_FEEDBACK) || defined(CONFIG_OPLUS_FEATURE_FEEDBACK_MODULE)
    char payload[1024] = {0x00};
#endif

    if (dat == NULL) {
        LOG_ERR("input buf is NULL\n");
        return -1;
    }
    if (length == 0) {
        LOG_ERR("input len is invalid,len = 0\n");
        return -1;
    }
    /* user program */
    ret = cs_i2c_read_bytes(g_cs_press.client, regAddress, dat, length);
    if (ret < 0) {
        LOG_ERR("cs_i2c_read_bytes err\n");
#if defined(CONFIG_OPLUS_FEATURE_FEEDBACK) || defined(CONFIG_OPLUS_FEATURE_FEEDBACK_MODULE)
        scnprintf(payload, sizeof(payload),
                   "NULL$$EventField@@I2cRead$$FieldData@@Err%d$$detailData@@%u[%*ph]%d",
                   ret, regAddress, length, dat, length);
        oplus_kevent_fb(PSW_BSP_KEYPAD, CS_PRESS_FB_BUS_TRANS_TYPE, payload);
#endif
        i = g_cs_press.bus_error_cnt % BUS_ERROR_MSG_CNT;
        memset(g_cs_press.bus_error_msg[i], 0, BUS_ERROR_MSG_SIZE);
        scnprintf(g_cs_press.bus_error_msg[i], sizeof(g_cs_press.bus_error_msg[i]) - 1,
                   "%d RdErr%d %u[%*ph]%u", g_cs_press.bus_error_cnt, ret, regAddress, length, dat, length);
        g_cs_press.bus_error_cnt++;
        return -1;
    }
    return 0;
}

/**
  * @brief  iic write funciton
  * @param[in]  regAddress: reg data,
  * @param[in]  *dat:       point to data to write,
  * @param[in]  length:     write data length
  * @retval 0:success, -1: fail
  */
static int cs_press_iic_write_double_reg(unsigned short regAddress, unsigned char *dat, unsigned int length)
{
    int ret = 0;
    int i = 0;
#if defined(CONFIG_OPLUS_FEATURE_FEEDBACK) || defined(CONFIG_OPLUS_FEATURE_FEEDBACK_MODULE)
    char payload[1024] = {0x00};
#endif

    if (dat == NULL) {
        LOG_ERR("input buf is NULL\n");
        return -1;
    }
    if (length == 0) {
        LOG_ERR("input len is invalid,len = 0\n");
        return -1;
    }
    /* user program*/
    ret = cs_i2c_write_bytes_by_u16_addr(g_cs_press.client, regAddress, dat,length);
    if (ret < 0) {
        LOG_ERR("cs_i2c_read_bytes err\n");
#if defined(CONFIG_OPLUS_FEATURE_FEEDBACK) || defined(CONFIG_OPLUS_FEATURE_FEEDBACK_MODULE)
        scnprintf(payload, sizeof(payload),
                   "NULL$$EventField@@I2cWriteDouble$$FieldData@@Err%d$$detailData@@%u[%*ph]%d",
                   ret, regAddress, length, dat, length);
        oplus_kevent_fb(PSW_BSP_KEYPAD, CS_PRESS_FB_BUS_TRANS_TYPE, payload);
#endif
        i = g_cs_press.bus_error_cnt % BUS_ERROR_MSG_CNT;
        memset(g_cs_press.bus_error_msg[i], 0, BUS_ERROR_MSG_SIZE);
        scnprintf(g_cs_press.bus_error_msg[i], sizeof(g_cs_press.bus_error_msg[i]) - 1,
                   "%d WrDbErr%d %u[%*ph]%u", g_cs_press.bus_error_cnt, ret, regAddress, length, dat, length);
        g_cs_press.bus_error_cnt++;
    }
    return ret;
}

/**
  * @brief      iic read funciton
  * @param[in]  regAddress: reg data,
  * @param[out] *dat:       read data buffer,
  * @param[in]  length:     read data length
  * @retval 0:success, -1: fail
  */
static int cs_press_iic_read_double_reg(unsigned short regAddress, unsigned char *dat, unsigned int length)
{
    int ret = 0;
    int i = 0;
#if defined(CONFIG_OPLUS_FEATURE_FEEDBACK) || defined(CONFIG_OPLUS_FEATURE_FEEDBACK_MODULE)
    char payload[1024] = {0x00};
#endif

    if (dat == NULL) {
        LOG_ERR("input buf is NULL\n");
        return -1;
    }
    if (length == 0) {
        LOG_ERR("input len is invalid,len = 0\n");
        return -1;
    }
    /* user program*/
    ret = cs_i2c_read_bytes_by_u16_addr(g_cs_press.client, regAddress, dat, length);
    if (ret < 0) {
        LOG_ERR("cs_i2c_read_double_reg err\n");
#if defined(CONFIG_OPLUS_FEATURE_FEEDBACK) || defined(CONFIG_OPLUS_FEATURE_FEEDBACK_MODULE)
        scnprintf(payload, sizeof(payload),
                   "NULL$$EventField@@I2cReadDouble$$FieldData@@Err%d$$detailData@@%u[%*ph]%d",
                   ret, regAddress, length, dat, length);
        oplus_kevent_fb(PSW_BSP_KEYPAD, CS_PRESS_FB_BUS_TRANS_TYPE, payload);
#endif
        i = g_cs_press.bus_error_cnt % BUS_ERROR_MSG_CNT;
        memset(g_cs_press.bus_error_msg[i], 0, BUS_ERROR_MSG_SIZE);
        scnprintf(g_cs_press.bus_error_msg[i], sizeof(g_cs_press.bus_error_msg[i]) - 1,
                   "%d RdDbErr%d %u[%*ph]%u", g_cs_press.bus_error_cnt, ret, regAddress, length, dat, length);
        g_cs_press.bus_error_cnt++;
    }
    return ret;
}

/**
  * @brief      delay function
  * @param[in]  time_ms: delay time, unit:ms
  * @retval     None
  */
static void cs_press_delay_ms(unsigned int time_ms)
{
    msleep(time_ms);
}

#if !RSTPIN_RESET_ENABLE
/**
  * @brief  cs_press_power_up
  * @param  None
  * @retval None
  */
static void cs_press_power_up(void)
{
    // user program
    #if 0
    if (gpio_is_valid(g_cs_press.power_gpio)) {
        gpio_set_value(g_cs_press.power_gpio, RST_GPIO_HIGH);
    } else {
        LOG_ERR("gpio rst is invalid\n");
    }
    #endif
}

/**
  * @brief  ic power down function
  * @param  None
  * @retval None
  */
static void cs_press_power_down(void)
{
    // user program
    #if 0
    if (gpio_is_valid(g_cs_press.power_gpio)) {
        gpio_set_value(g_cs_press.power_gpio, RST_GPIO_LOW);
    } else {
        LOG_ERR("gpio rst is invalid\n");
    }
    #endif
}

#else

/**
  * @brief  ic rst pin set high
  * @param  None
  * @retval None
  */
static void cs_press_rstpin_high(void)
{
    /* user program*/
    #if 1
    if (gpio_is_valid(g_cs_press.rst_gpio)) {
    LOG_ERR("gpio_set_value %d HIGH\n", g_cs_press.rst_gpio);
        gpio_set_value(g_cs_press.rst_gpio, RST_GPIO_HIGH);
    } else {
        LOG_ERR("gpio rst is invalid\n");
    }
    #endif
}

/**
  * @brief  ic rst pin set low
  * @param  None
  * @retval None
  */
static void cs_press_rstpin_low(void)
{
    /* user program*/
    #if 1
    if (gpio_is_valid(g_cs_press.rst_gpio)) {
    LOG_ERR("gpio_set_value %d LOW\n", g_cs_press.rst_gpio);
        gpio_set_value(g_cs_press.rst_gpio, RST_GPIO_LOW);
    } else {
        LOG_ERR("gpio rst is invalid\n");
    }
    #endif
}
#endif
/**
  * @brief      iic function test
  * @param[in]  test_data: test data
  * @retval     0:success, -1: fail
  */
int cs_press_iic_rw_test(unsigned char test_data)
{
    int ret = 0;
    unsigned char retry = RETRY_NUM;
    unsigned char read_data = 0;
    unsigned char write_data = test_data;

    do
    {
        cs_press_iic_write(AP_RW_TEST_REG, &write_data, 1);
        cs_press_iic_read(AP_RW_TEST_REG, &read_data, 1);
        ret = 0;
        retry--;
        if (read_data != write_data)
        {
            ret = -1;
            LOG_ERR("iic test failed,w:%d,rd:%d  %d\n",write_data,read_data, ret);
            cs_press_delay_ms(1);
        } else {
            LOG_INFO("iic test ok,w:%d,rd:%d\n",write_data,read_data);
            retry = 0;
        }
    } while (retry > 0);
    return ret;
}

/**
  * @brief  wakeup iic
  * @param  None
  * @retval 0:success, -1: fail
  */
static char cs_press_wakeup_iic(void)
{
    int ret = 0;

    ret = cs_press_iic_rw_test(0x67);
    if (g_cs_press.suspend_lock) {
        __pm_wakeup_event(g_cs_press.suspend_lock, 2000);
    }
    return (char)ret;
}

int cs_press_set_trigger_strength(trigger_strength_config strength_cfg)
{
    int ret = 0;
    unsigned char cfg[STRENGTH_CFG_NUM] = { 0 };
    unsigned char reg_active[2] = {AP_RW_SCANT_PERIOD_REG, 0xa2};
    int retry = 20;

    cfg[0] = strength_cfg.tap_1_down_strength;
    cfg[1] = strength_cfg.tap_1_up_strength;
    cfg[2] = strength_cfg.tap_2_down_strength;
    cfg[3] = strength_cfg.tap_2_up_strength;
    cfg[4] = strength_cfg.tap_3_down_strength;
    cfg[5] = strength_cfg.tap_3_up_strength;
    cfg[6] = strength_cfg.tap_4_down_strength;
    cfg[7] = strength_cfg.tap_4_up_strength;
    cfg[8] = strength_cfg.tap_5_down_strength;
    cfg[9] = strength_cfg.tap_5_up_strength;
    cfg[10] = strength_cfg.area_1;
    cfg[11] = strength_cfg.area_2;
    cfg[12] = strength_cfg.long_tap_judge_time;
    cfg[13] = strength_cfg.muti_tap_judge_time;

    LOG_INFO("%s: cfg = [%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u].\n", __func__,
            cfg[0], cfg[1], cfg[2], cfg[3], cfg[4], cfg[5], cfg[6],
            cfg[7], cfg[8], cfg[9], cfg[10], cfg[11], cfg[12], cfg[13]);

    ret = cs_press_iic_write(AP_RW_SCANT_PERIOD_REG, cfg, STRENGTH_CFG_NUM);
    if (ret < 0) {
        LOG_ERR("IIC write AP_RW_SCANT_PERIOD_REG failed!!\n");
        return ret;
    }

    ret = cs_press_iic_write(0x01, reg_active, 2);
    if (ret < 0) {
        LOG_ERR("IIC write 0x01 failed!!\n");
        return ret;
    }

    while ((reg_active[0] || reg_active[1]) && retry) {
        retry--;
        cs_press_delay_ms(2);
        ret = cs_press_iic_read(0x01, reg_active, 2);
        if (ret < 0) {
            LOG_ERR("%s: IIC read 0x01 failed!!\n", __func__);
            return ret;
        }
        LOG_INFO("read 0x01 [0x%02x 0x%02x]\n", reg_active[0], reg_active[1]);
    }
    if (!retry) {
        LOG_ERR("%s: IIC read 0x01 retry over times!!\n", __func__);
    }
    LOG_INFO("exit %s\n", __func__);
    return ret;
}
/*
int cs_press_enable_high_speed_frame(int en)
{
    int ret = 0;
    unsigned char ic_mode = 0;
    unsigned char reg_active[2] = {AP_HIGH_SPEED_FRAME_REG, 0x46};
    int retry = 20;

    ic_mode = !!en;

    ret = cs_press_iic_write(AP_HIGH_SPEED_FRAME_REG, &ic_mode, 1);
    if (ret < 0) {
        LOG_ERR("IIC write AP_HIGH_SPEED_FRAME_REG 0x%02x failed!!\n", ic_mode);
        return ret;
    }

    ret = cs_press_iic_write(0x01, reg_active, 2);
    if (ret < 0) {
        LOG_ERR("IIC write 0x01 failed!!\n");
        return ret;
    }

    while ((reg_active[0] || reg_active[1]) && retry) {
        retry--;
        cs_press_delay_ms(2);
        ret = cs_press_iic_read(0x01, reg_active, 2);
        if (ret < 0) {
            LOG_ERR("%s: IIC read 0x01 failed!!\n", __func__);
            return ret;
        }
        LOG_INFO("read 0x01 [0x%02x 0x%02x]\n", reg_active[0], reg_active[1]);
    }
    if (!retry) {
        LOG_ERR("%s: IIC read 0x01 retry over times!!\n", __func__);
    }

    LOG_INFO("exit %s\n", __func__);
    return ret;
}
*/
int cs_press_keep_irq_report(int en)
{
    int ret = 0;
    unsigned char ic_mode = 0;
    unsigned char reg_active[2] = {AP_KEEP_IRQ_REPORT_REG, 0x45};
    int retry = 20;

    ic_mode = !!en;

    ret = cs_press_iic_write(AP_KEEP_IRQ_REPORT_REG, &ic_mode, 1);
    if (ret < 0) {
        LOG_ERR("IIC write AP_KEEP_IRQ_REPORT_REG 0x%02x failed!!\n", ic_mode);
        return ret;
    }

    ret = cs_press_iic_write(0x01, reg_active, 2);
    if (ret < 0) {
        LOG_ERR("IIC write 0x01 failed!!\n");
        return ret;
    }

    while ((reg_active[0] || reg_active[1]) && retry) {
        retry--;
        cs_press_delay_ms(2);
        ret = cs_press_iic_read(0x01, reg_active, 2);
        if (ret < 0) {
            LOG_ERR("%s: IIC read 0x01 failed!!\n", __func__);
            return ret;
        }
        LOG_INFO("read 0x01 [0x%02x 0x%02x]\n", reg_active[0], reg_active[1]);
    }
    if (!retry) {
        LOG_ERR("%s: IIC read 0x01 retry over times!!\n", __func__);
    }

    LOG_INFO("exit %s\n", __func__);
    return ret;
}

/**
 * @brief Get ic_mode from mode value
 * @param mode: camera key mode or game key mode
 * @return ic_mode value corresponding to the mode
 */
static unsigned char cs_press_get_ic_mode_from_mode(int mode)
{
    switch (mode) {
    case CAMERA_KEY_DEFAULT_MODE:
    case GAME_KEY_DEFAULT_MODE:
        return CAMERA_KEY_IC_DEFAULT_EVENT_MODE;
    case CAMERA_KEY_CAMERA_MODE:
        return CAMERA_KEY_IC_REALTIME_EVENT_MODE;
    case CAMERA_KEY_POPUP_MODE:
    case GAME_KEY_POPUP_MODE:
        return CAMERA_KEY_IC_DELAY_EVENT_MODE;
    case CAMERA_KEY_SLEEP_MODE:
    case GAME_KEY_SLEEP_MODE:
        return CAMERA_KEY_IC_SLEEP_EVENT_MODE;
    case GAME_KEY_GAME_MODE:
        return CAMERA_KEY_IC_GAME_EVENT_MODE;
    default:
        return 0;
    }
}

/**
 * @brief Convert mode to ic_mode
 * @param mode: camera key mode or game key mode
 * @return ic_mode value corresponding to the mode
 */
static unsigned char cs_press_mode_to_ic_mode(int mode)
{
    if (g_cs_press.quick_on_closed
            && (mode != CAMERA_KEY_CAMERA_MODE)
            && (mode != GAME_KEY_GAME_MODE)) {
        return CAMERA_KEY_IC_NONE_EVENT_MODE;
    }
    return cs_press_get_ic_mode_from_mode(mode);
}

/**
 * @brief Wait for register active status
 * @param reg_active: register active status buffer
 * @return 0 on success, negative on error
 */
static int cs_press_wait_register_active(unsigned char *reg_active)
{
    int ret = 0;
    int retry = 20;

    while ((reg_active[0] || reg_active[1]) && retry) {
        retry--;
        cs_press_delay_ms(2);
        ret = cs_press_iic_read(0x01, reg_active, 2);
        if (ret < 0) {
            LOG_ERR("%s: IIC read 0x01 failed!!\n", __func__);
            return ret;
        }
        LOG_INFO("read 0x01 [0x%02x 0x%02x]\n", reg_active[0], reg_active[1]);
    }
    if (!retry) {
        LOG_ERR("%s: IIC read 0x01 retry over times!!\n", __func__);
    }
    return ret;
}

/**
 * @brief Set trigger strength based on mode
 * @param mode: camera key mode or game key mode
 */
static void cs_press_set_mode_trigger_strength(int mode)
{
    if (mode != GAME_KEY_GAME_MODE) {
        cs_press_set_trigger_strength(g_cs_press.strength_cfg);
    } else {
        cs_press_set_trigger_strength(g_cs_press.game_mode_strength_cfg);
    }
}

int cs_press_set_mode(int mode)
{
    int ret = 0;
    unsigned char ic_mode = 0;
    unsigned char reg_active[2] = {AP_RD_APPLICATION_SATUS_REG, 0xab};

    ic_mode = cs_press_mode_to_ic_mode(mode);
    LOG_INFO("%s: mode = %d (ic mode = 0x%02x%s)\n", __func__,
                mode, ic_mode, g_cs_press.quick_on_closed ? ", quick on closed..." : "");

    ret = cs_press_iic_write(AP_RD_APPLICATION_SATUS_REG, &ic_mode, 1);
    if (ret < 0) {
        LOG_ERR("IIC write AP_RD_APPLICATION_SATUS_REG 0x%02x failed!!\n", ic_mode);
        return ret;
    }

    ret = cs_press_iic_write(0x01, reg_active, 2);
    if (ret < 0) {
        LOG_ERR("IIC write 0x01 failed!!\n");
        return ret;
    }

    ret = cs_press_wait_register_active(reg_active);
    if (ret < 0) {
        return ret;
    }

    cs_press_set_mode_trigger_strength(mode);

    LOG_INFO("exit %s\n", __func__);
    return ret;
}

#ifdef INT_SET_EN
/**
  * @brief    input system register
  * @param
  * @retval none
  */
void fml_input_dev_init(void)
{
    int ret = 0;

    cs_input_dev = input_allocate_device();
    if (cs_input_dev != NULL) {
        cs_input_dev->name = CS_PRESS_NAME;
        __set_bit(EV_KEY, cs_input_dev->evbit);
        __set_bit(EV_SYN, cs_input_dev->evbit);
        __set_bit(EV_ABS, cs_input_dev->evbit);
        __set_bit(EV_KEY, cs_input_dev->evbit);
        /* Camera Key */
        __set_bit(KEY_LIGHT_TAP, cs_input_dev->keybit);
        __set_bit(KEY_HEAVY_TAP, cs_input_dev->keybit);
        __set_bit(KEY_SHORT_TAP, cs_input_dev->keybit);
        __set_bit(KEY_LONG_TAP, cs_input_dev->keybit);
        __set_bit(KEY_DOUBLE_TAP, cs_input_dev->keybit);
        __set_bit(KEY_SWIPE_UP, cs_input_dev->keybit);
        __set_bit(KEY_SWIPE_DOWN, cs_input_dev->keybit);
        __set_bit(KEY_PHYSICAL_TAP, cs_input_dev->keybit);
        /* Game Key */
        __set_bit(GKEY_SHORT_TAP, cs_input_dev->keybit);
        __set_bit(GKEY_DOUBLE_TAP, cs_input_dev->keybit);
        __set_bit(GKEY_HEAVY_TAP, cs_input_dev->keybit);
        __set_bit(GKEY_TAP_1_AREA_1, cs_input_dev->keybit);
        __set_bit(GKEY_TAP_2_AREA_1, cs_input_dev->keybit);
        __set_bit(GKEY_TAP_3_AREA_1, cs_input_dev->keybit);
        __set_bit(GKEY_TAP_4_AREA_1, cs_input_dev->keybit);
        __set_bit(GKEY_TAP_5_AREA_1, cs_input_dev->keybit);
        //__set_bit(GKEY_TAP_SHORTCUTS_AREA_1, cs_input_dev->keybit);
        __set_bit(GKEY_TAP_1_AREA_2, cs_input_dev->keybit);
        __set_bit(GKEY_TAP_2_AREA_2, cs_input_dev->keybit);
        __set_bit(GKEY_TAP_3_AREA_2, cs_input_dev->keybit);
        __set_bit(GKEY_TAP_4_AREA_2, cs_input_dev->keybit);
        __set_bit(GKEY_TAP_5_AREA_2, cs_input_dev->keybit);
        //__set_bit(GKEY_TAP_SHORTCUTS_AREA_2, cs_input_dev->keybit);
        input_set_abs_params(cs_input_dev, ABS_DISTANCE, -20, 20, 0, 0);
        input_set_abs_params(cs_input_dev, ABS_X, 0, 100, 0, 0);
        ret = input_register_device(cs_input_dev);
        if (ret != 0)
            LOG_ERR("input register device error = %d\n", ret);
    }
}

/**
  * @brief    input system unregister
  * @param
  * @retval none
  */
void fml_input_dev_exit(void)
{
    /* release input_dev */
    input_unregister_device(cs_input_dev);
    input_free_device(cs_input_dev);
}

/**
  * @brief    cs_irq_enable
  * @param
  * @retval
  */
static void cs_irq_enable(void)
{
    if (cs_irq_flag == 0) {
        cs_irq_flag++;
        enable_irq(cs_press_irq_num);
    } else {
        LOG_ERR("cs_press Eint already enabled!\n");
    }
    /*LOG_ERR("Enable irq_flag=%d\n", cs_irq_flag);*/
}

/**
  * @brief    cs_irq_disable
  * @param
  * @retval
*/
static void cs_irq_disable(void)
{
    if (cs_irq_flag == 1) {
        cs_irq_flag--;
        disable_irq_nosync(cs_press_irq_num);
    } else {
        LOG_ERR("cs_press Eint already disabled!\n");
    }
    /*LOG_ERR("Disable irq_flag=%d\n", cs_irq_flag);*/
}

/**
  * @brief    cs_press_interrupt_handler
  * @param
  * @retval
*/
static irqreturn_t cs_press_interrupt_handler(int irq, void *dev_id)
{
    cs_press_irq_flag = 1;

    cs_irq_disable();
    if (g_cs_press.suspend_lock) {
        __pm_wakeup_event(g_cs_press.suspend_lock, 1000);
    }
    wake_up_interruptible(&cs_press_waiter);

    return IRQ_HANDLED;
}


/**
  * @brief  clear reset register
  * @param  None
  * @retval 0:success, -1: fail
  */
char cs_press_clear_reset_register(void)
{
    char ret = 0;
    unsigned char retry = RETRY_NUM;
    // M series required write value 0xcc
    unsigned char temp_data[2] = {0xB7, 0x49};
    unsigned char addr = DEBUG_RESET_SOURCE_REG;
    unsigned char rbuf[4] = {0};

    LOG_INFO("%s\n", __func__);
    // repeatly reset including first time i2c wake up
    do
    {
        if (ret != 0)
        {
            cs_press_delay_ms(1);
        }
        ret = cs_press_iic_write(AP_RESET_MCU_REG, temp_data, 2);
    } while ((ret != 0) && (retry--));

    cs_press_delay_ms(5);
    ret = cs_press_iic_read(addr, rbuf, 4);
    if (ret >= 0) {
        LOG_DEBUG("after clear, reset register read [%02x %02x].\n", rbuf[0], rbuf[1]);
    }

    return ret;
}

/**
 * @brief Check if currently in press state
 * @return true: in press state, false: not in press state
 */
static bool cs_press_is_pressed(void)
{
    return (g_cs_press.is_light_tap_down ||
            g_cs_press.is_heavy_tap_down ||
            g_cs_press.is_long_tap_down ||
            g_cs_press.is_physical_tap_down ||
            g_cs_press.is_g_heavy_tap_down ||
            g_cs_press.is_game_tap_1_down ||
            g_cs_press.is_game_tap_2_down ||
            g_cs_press.is_game_tap_3_down ||
            g_cs_press.is_game_tap_4_down ||
            g_cs_press.is_game_tap_5_down);
}

/**
 * @brief Update long press statistics
 * Count total times of continuous press over 10 minutes and record maximum duration
 */
static void cs_press_update_long_press_statistics(void)
{
    bool is_pressed = cs_press_is_pressed();
    unsigned long current_time = jiffies;
    unsigned long press_duration_ms;
    unsigned long ten_minutes_jiffies = msecs_to_jiffies(10 * 60 * 1000); /* Convert 10 minutes to jiffies */

    if (g_cs_press.is_press_active) {
        /* Continuous pressing, check if exceeded 10 minutes */
        if (current_time - g_cs_press.press_start_time_jiffies >= ten_minutes_jiffies) {
            if (!g_cs_press.is_press_over_10min) {
                /* First time exceeding 10 minutes, increment count */
                g_cs_press.long_press_over_10min_cnt++;
                g_cs_press.is_press_over_10min = true;
                LOG_DEBUG("keep long press over 10min.\n");
            }
        }
    }

    if (is_pressed) {
        /* Currently in press state */
        if (!g_cs_press.is_press_active) {
            /* Transition from non-press to press state, record start time */
            g_cs_press.press_start_time_jiffies = current_time;
            g_cs_press.is_press_active = true;
            g_cs_press.is_press_over_10min = false;
            return;
        }
    } else {
        /* Currently not in press state */
        if (g_cs_press.is_press_active) {
            /* Transition from press to non-press state, calculate duration and update max duration */
            press_duration_ms = jiffies_to_msecs(current_time - g_cs_press.press_start_time_jiffies);

            if (press_duration_ms > g_cs_press.long_press_max_duration_ms) {
                g_cs_press.long_press_max_duration_ms = press_duration_ms;
            }
            LOG_DEBUG("keep press %lums.\n", press_duration_ms);

            g_cs_press.is_press_active = false;
            g_cs_press.is_press_over_10min = false;
        }
    }
}

/**
 * @brief Record event statistics
 * Record total count of all events and abnormal events in suspend state.
 * In suspend state, only heavy tap and double tap events are allowed.
 * Other events should be recorded as abnormal.
 * @param event_code: key event code
 */
static void cs_press_record_event_statistics(int event_code)
{
    int index;

    /* Check if event code is in valid range */
    if (event_code < EVENT_STATISTICS_CODE_BASE ||
        event_code >= EVENT_STATISTICS_CODE_BASE + EVENT_STATISTICS_ARRAY_SIZE) {
        return;
    }

    index = event_code - EVENT_STATISTICS_CODE_BASE;

    /* Record total event count */
    g_cs_press.event_total_cnt[index]++;

    /* Check if in suspend state and record abnormal event */
    if (g_cs_press.is_suspended) {
        /* Allowed events in suspend state: heavy tap and double tap */
        if (event_code != KEY_HEAVY_TAP &&
            event_code != GKEY_HEAVY_TAP &&
            event_code != KEY_DOUBLE_TAP &&
            event_code != GKEY_DOUBLE_TAP) {
            /* Record abnormal event in suspend state */
            g_cs_press.suspend_abnormal_event_cnt[index]++;
        }
    }
}

/**
 * @brief Record down max force statistics
 * Record cur_down_max_force to down_max_force distribution and update total_down_max_force
 */
static void cs_press_record_down_max_force(void)
{
    int i;

    if (g_cs_press.cur_down_max_force) {
        LOG_INFO("record max force last down %d\n", g_cs_press.cur_down_max_force);
        if (g_cs_press.cur_down_max_force < 0) {
            g_cs_press.cur_down_max_force = -g_cs_press.cur_down_max_force;
        }
        if (g_cs_press.cur_down_max_force > g_cs_press.total_down_max_force) {
            g_cs_press.total_down_max_force = g_cs_press.cur_down_max_force;
        }
        i = g_cs_press.cur_down_max_force / DOWN_MAX_FORCE_RANGE_SIZE;
        if (i >= DOWN_MAX_FORCE_RANGE_COUNT) {
            i = DOWN_MAX_FORCE_RANGE_COUNT - 1;
        }
        g_cs_press.down_max_force[i]++;
        g_cs_press.cur_down_max_force = 0;
    }
}

/**
 * @brief Update cur_down_max_force during press
 * @param curr_force: current force value
 */
static void cs_press_update_cur_down_max_force(int16_t curr_force)
{
    if (g_cs_press.is_physical_tap_down || g_cs_press.is_light_tap_down || g_cs_press.is_heavy_tap_down) {
        if ((curr_force > 0) && (curr_force > g_cs_press.cur_down_max_force)) {
            g_cs_press.cur_down_max_force = curr_force;
        }
        if ((curr_force < 0) && (g_cs_press.cur_down_max_force < 0) && (curr_force < g_cs_press.cur_down_max_force)) {
            g_cs_press.cur_down_max_force = curr_force;
        }
    }
}

/**
 * @brief Handle heavy tap up event with lag calculation
 * @param ch_force: channel force values
 * @param heavy_tap_lag: output array for heavy tap lag values
 */
static void cs_press_handle_heavy_tap_up(int16_t *ch_force, int32_t *heavy_tap_lag)
{
    int i, j, j_max;

    for (i = 0; i < CH_COUNT; i++) {
        if (g_cs_press.heavy_tap_force_max[i]) {
            j_max = 0;
            heavy_tap_lag[i] = ch_force[i] * 1000 / g_cs_press.heavy_tap_force_max[i];
            for (j = 0; j < LAG_MIN_COUNT; j++) {
                if (!g_cs_press.heavy_tap_lag[i][j]) {
                    j_max = j;
                    break;
                } else if (g_cs_press.heavy_tap_lag[i][j] > g_cs_press.heavy_tap_lag[i][j_max]) {
                    j_max = j;
                }
            }
            if (!g_cs_press.heavy_tap_lag[i][j_max]
                    || (g_cs_press.heavy_tap_lag[i][j_max] > heavy_tap_lag[i])) {
                g_cs_press.heavy_tap_lag[i][j_max] = heavy_tap_lag[i];
            }
        }
        g_cs_press.heavy_tap_force_max[i] = 0;
    }
    LOG_INFO("heavy_tap_lag=[%d, %d]\n", heavy_tap_lag[0], heavy_tap_lag[1]);
}

/**
 * @brief Handle game tap up event for a specific tap level
 * @param tap_down: pointer to tap down flag (int16_t*)
 * @param key_area1: key code for area 1
 * @param key_area2: key code for area 2
 */
static void cs_press_handle_game_tap_up(int16_t *tap_down, int key_area1, int key_area2)
{
    int tap_level = 0;

    /* Determine tap level from key code */
    if (key_area1 >= GKEY_TAP_1_AREA_1 && key_area1 <= GKEY_TAP_5_AREA_1) {
        tap_level = key_area1 - GKEY_TAP_1_AREA_1 + 1;
    } else if (key_area2 >= GKEY_TAP_1_AREA_2 && key_area2 <= GKEY_TAP_5_AREA_2) {
        tap_level = key_area2 - GKEY_TAP_1_AREA_2 + 1;
    }

    if (*tap_down & BIT_AREA_1) {
        LOG_INFO("[REPORT_KEY]GKEY_TAP_%d_AREA_1 up\n", tap_level);
        input_report_key(cs_input_dev, key_area1, 0);
        input_sync(cs_input_dev);
    }
    if (*tap_down & BIT_AREA_2) {
        LOG_INFO("[REPORT_KEY]GKEY_TAP_%d_AREA_2 up\n", tap_level);
        input_report_key(cs_input_dev, key_area2, 0);
        input_sync(cs_input_dev);
    }
    *tap_down = 0;
}

/**
 * @brief Handle key up events
 * @param realtime_action: realtime action bits
 */
void report_key_up(int16_t realtime_action)
{
    bool has_up = false;

    if ((realtime_action & BIT_ACTION_TAP_5_UP)
            || (realtime_action & BIT_ACTION_TAP_4_UP)
            || (realtime_action & BIT_ACTION_TAP_3_UP)
            || (realtime_action & BIT_ACTION_TAP_2_UP)
            || (realtime_action & BIT_ACTION_TAP_1_UP)) {
        if (g_cs_press.is_heavy_tap_down) {
            LOG_INFO("[REPORT_KEY]KEY_HEAVY_TAP up\n");
            input_report_key(cs_input_dev, KEY_HEAVY_TAP, 0);
            input_sync(cs_input_dev);
            g_cs_press.is_heavy_tap_down = false;
        }
        if (g_cs_press.is_g_heavy_tap_down) {
            LOG_INFO("[REPORT_KEY]GKEY_HEAVY_TAP up\n");
            input_report_key(cs_input_dev, GKEY_HEAVY_TAP, 0);
            input_sync(cs_input_dev);
            g_cs_press.is_g_heavy_tap_down = false;
        }
        cs_press_handle_game_tap_up(&g_cs_press.is_game_tap_5_down, GKEY_TAP_5_AREA_1, GKEY_TAP_5_AREA_2);
        has_up = true;
    }
    if ((realtime_action & BIT_ACTION_TAP_4_UP)
            || (realtime_action & BIT_ACTION_TAP_3_UP)
            || (realtime_action & BIT_ACTION_TAP_2_UP)
            || (realtime_action & BIT_ACTION_TAP_1_UP)) {
        cs_press_handle_game_tap_up(&g_cs_press.is_game_tap_4_down, GKEY_TAP_4_AREA_1, GKEY_TAP_4_AREA_2);
        has_up = true;
    }
    if ((realtime_action & BIT_ACTION_TAP_3_UP)
            || (realtime_action & BIT_ACTION_TAP_2_UP)
            || (realtime_action & BIT_ACTION_TAP_1_UP)) {
        cs_press_handle_game_tap_up(&g_cs_press.is_game_tap_3_down, GKEY_TAP_3_AREA_1, GKEY_TAP_3_AREA_2);
        has_up = true;
    }
    if ((realtime_action & BIT_ACTION_TAP_2_UP)
            || (realtime_action & BIT_ACTION_TAP_1_UP)) {
        if (g_cs_press.is_light_tap_down) {
            LOG_INFO("[REPORT_KEY]KEY_LIGHT_TAP up\n");
            input_report_key(cs_input_dev, KEY_LIGHT_TAP, 0);
            input_sync(cs_input_dev);
            g_cs_press.is_light_tap_down = false;
        }
        cs_press_handle_game_tap_up(&g_cs_press.is_game_tap_2_down, GKEY_TAP_2_AREA_1, GKEY_TAP_2_AREA_2);
        has_up = true;
    }
    if (realtime_action & BIT_ACTION_TAP_1_UP) {
        if (g_cs_press.is_physical_tap_down) {
            LOG_INFO("[REPORT_KEY]KEY_PHYSICAL_TAP up\n");
            input_report_key(cs_input_dev, KEY_PHYSICAL_TAP, 0);
            input_sync(cs_input_dev);
            g_cs_press.is_physical_tap_down = false;
        }
        cs_press_handle_game_tap_up(&g_cs_press.is_game_tap_1_down, GKEY_TAP_1_AREA_1, GKEY_TAP_1_AREA_2);
        has_up = true;
    }

    /* Record down max force statistics when key is released */
    if (has_up) {
        cs_press_record_down_max_force();
    }
}

/**
 * @brief Handle camera key tap down events in camera mode
 * @param realtime_action: realtime action bits
 * @param curr_force: current force value
 */
static void cs_press_handle_camera_tap_down(int16_t realtime_action, int16_t curr_force)
{
    if ((realtime_action & BIT_ACTION_TAP_1_DOWN) && !g_cs_press.is_physical_tap_down) {
        LOG_INFO("[REPORT_KEY]KEY_PHYSICAL_TAP down\n");
        cs_press_record_event_statistics(KEY_PHYSICAL_TAP);
        input_report_key(cs_input_dev, KEY_PHYSICAL_TAP, 1);
        input_sync(cs_input_dev);
        g_cs_press.is_physical_tap_down = true;
        g_cs_press.cur_down_max_force = curr_force;
    }
    if ((realtime_action & BIT_ACTION_TAP_2_DOWN) && !g_cs_press.is_light_tap_down) {
        LOG_INFO("[REPORT_KEY]KEY_LIGHT_TAP down\n");
        cs_press_record_event_statistics(KEY_LIGHT_TAP);
        input_report_key(cs_input_dev, KEY_LIGHT_TAP, 1);
        input_sync(cs_input_dev);
        g_cs_press.is_light_tap_down = true;
        g_cs_press.cur_down_max_force = curr_force;
    }
    if ((realtime_action & BIT_ACTION_TAP_5_DOWN) && !g_cs_press.is_heavy_tap_down) {
        LOG_INFO("[REPORT_KEY]KEY_HEAVY_TAP down\n");
        cs_press_record_event_statistics(KEY_HEAVY_TAP);
        input_report_key(cs_input_dev, KEY_HEAVY_TAP, 1);
        input_sync(cs_input_dev);
        g_cs_press.is_heavy_tap_down = true;
        g_cs_press.cur_down_max_force = curr_force;
    }
}

/**
 * @brief Handle delay action events (double tap, short tap)
 * @param delay_action: delay action bits
 * @param is_game_mode: true if in game mode, false for camera mode
 */
static void cs_press_handle_delay_action(int16_t delay_action, bool is_game_mode)
{
    if (delay_action & BIT_ACTION_DOUBLE_TAP_5) {
        if (is_game_mode) {
            LOG_INFO("[REPORT_KEY]GKEY_DOUBLE_TAP\n");
            cs_press_record_event_statistics(GKEY_DOUBLE_TAP);
            input_report_key(cs_input_dev, GKEY_DOUBLE_TAP, 1);
        } else {
            LOG_INFO("[REPORT_KEY]KEY_DOUBLE_TAP\n");
            cs_press_record_event_statistics(KEY_DOUBLE_TAP);
            input_report_key(cs_input_dev, KEY_DOUBLE_TAP, 1);
        }
        input_sync(cs_input_dev);
        input_report_key(cs_input_dev, is_game_mode ? GKEY_DOUBLE_TAP : KEY_DOUBLE_TAP, 0);
        input_sync(cs_input_dev);
        g_cs_press.double_tap_cnt++;
    } else if (delay_action & BIT_ACTION_SHORT_TAP_5) {
        if (is_game_mode) {
            LOG_INFO("[REPORT_KEY]GKEY_SHORT_TAP\n");
            cs_press_record_event_statistics(GKEY_SHORT_TAP);
            input_report_key(cs_input_dev, GKEY_SHORT_TAP, 1);
        } else {
            LOG_INFO("[REPORT_KEY]KEY_SHORT_TAP\n");
            cs_press_record_event_statistics(KEY_SHORT_TAP);
            input_report_key(cs_input_dev, KEY_SHORT_TAP, 1);
        }
        input_sync(cs_input_dev);
        input_report_key(cs_input_dev, is_game_mode ? GKEY_SHORT_TAP : KEY_SHORT_TAP, 0);
        input_sync(cs_input_dev);
    }
}

/**
 * @brief Get tap mask for a given tap level
 * @param tap_level: tap level (1-5)
 * @return tap mask value, 0 if invalid
 */
static int16_t cs_press_get_tap_mask(int tap_level)
{
    switch (tap_level) {
    case 1:
        return BIT_ACTION_TAP_1_DOWN | BIT_ACTION_TAP_2_DOWN | BIT_ACTION_TAP_3_DOWN |
               BIT_ACTION_TAP_4_DOWN | BIT_ACTION_TAP_5_DOWN;
    case 2:
        return BIT_ACTION_TAP_2_DOWN | BIT_ACTION_TAP_3_DOWN | BIT_ACTION_TAP_4_DOWN |
               BIT_ACTION_TAP_5_DOWN;
    case 3:
        return BIT_ACTION_TAP_3_DOWN | BIT_ACTION_TAP_4_DOWN | BIT_ACTION_TAP_5_DOWN;
    case 4:
        return BIT_ACTION_TAP_4_DOWN | BIT_ACTION_TAP_5_DOWN;
    case 5:
        return BIT_ACTION_TAP_5_DOWN;
    default:
        return 0;
    }
}

/**
 * @brief Handle game tap down event
 * @param realtime_action: realtime action bits
 * @param area: area bits
 * @param curr_force: current force value
 * @param tap_level: tap level (1-5)
 * @param tap_down: pointer to tap down flag
 * @param key_area1: key code for area 1
 * @param key_area2: key code for area 2
 */
static void cs_press_handle_game_tap_down(int16_t realtime_action, int16_t area, int16_t curr_force,
        int tap_level, int16_t *tap_down, int key_area1, int key_area2)
{
    int16_t tap_mask = cs_press_get_tap_mask(tap_level);

    if (!tap_mask || !((realtime_action & tap_mask) && !(*tap_down))) {
        return;
    }

    if (area & BIT_AREA_1) {
        LOG_INFO("[REPORT_KEY]GKEY_TAP_%d_AREA_1 down\n", tap_level);
        cs_press_record_event_statistics(key_area1);
        input_report_key(cs_input_dev, key_area1, 1);
    } else {
        LOG_INFO("[REPORT_KEY]GKEY_TAP_%d_AREA_2 down\n", tap_level);
        cs_press_record_event_statistics(key_area2);
        input_report_key(cs_input_dev, key_area2, 1);
    }
    input_sync(cs_input_dev);
    *tap_down = area;
    g_cs_press.cur_down_max_force = curr_force;
}

/**
 * @brief Handle swipe event (up or down)
 * @param swipe_type: swipe type (KEY_SWIPE_UP or KEY_SWIPE_DOWN)
 * @param distance: swipe distance
 * @param curr_pos: current position
 * @return true if swipe event occurred, false otherwise
 */
static bool cs_press_handle_swipe_event(int swipe_type, int16_t distance, int16_t curr_pos)
{
    int key_code = (swipe_type == KEY_SWIPE_UP) ? KEY_SWIPE_UP : KEY_SWIPE_DOWN;

    LOG_INFO("[REPORT_KEY]%s %d\n", (swipe_type == KEY_SWIPE_UP) ? "KEY_SWIPE_UP" : "KEY_SWIPE_DOWN", distance);
    cs_press_record_event_statistics(key_code);
    input_report_key(cs_input_dev, key_code, 1);
    input_report_abs(cs_input_dev, ABS_DISTANCE, distance);
    input_report_abs(cs_input_dev, ABS_X, curr_pos);
    input_sync(cs_input_dev);
    input_report_key(cs_input_dev, key_code, 0);
    input_report_abs(cs_input_dev, ABS_DISTANCE, 0);
    input_report_abs(cs_input_dev, ABS_X, 0);
    input_sync(cs_input_dev);
    return true;
}

/**
 * @brief Handle heavy tap up event
 * @param ch_force: channel force values
 * @param heavy_tap_lag: output array for heavy tap lag values
 */
static void cs_press_handle_heavy_tap_up_event(int16_t *ch_force, int32_t *heavy_tap_lag)
{
    LOG_INFO("[REPORT_KEY]KEY_HEAVY_TAP up\n");
    input_report_key(cs_input_dev, KEY_HEAVY_TAP, 0);
    input_sync(cs_input_dev);
    g_cs_press.is_heavy_tap_down = false;
    cs_press_handle_heavy_tap_up(ch_force, heavy_tap_lag);
    cs_press_record_down_max_force();
}

/**
 * @brief Handle light tap up event
 */
static void cs_press_handle_light_tap_up_event(void)
{
    LOG_INFO("[REPORT_KEY]KEY_LIGHT_TAP up\n");
    input_report_key(cs_input_dev, KEY_LIGHT_TAP, 0);
    input_sync(cs_input_dev);
    g_cs_press.is_light_tap_down = false;
    cs_press_record_down_max_force();
}

/**
 * @brief Update tap force statistics
 * @param curr_force: current force value
 */
static void cs_press_update_tap_force_stats(int16_t curr_force)
{
    if (!g_cs_press.is_light_tap_down && !g_cs_press.is_heavy_tap_down) {
        return;
    }
    if (!g_cs_press.tap_force_min || (curr_force < g_cs_press.tap_force_min)) {
        g_cs_press.tap_force_min = curr_force;
    }
    if (!g_cs_press.tap_force_max || (curr_force > g_cs_press.tap_force_max)) {
        g_cs_press.tap_force_max = curr_force;
    }
}

/**
 * @brief Update heavy tap channel force statistics
 * @param ch_force: channel force values
 */
static void cs_press_update_heavy_tap_ch_force(int16_t *ch_force)
{
    int i;

    if (!g_cs_press.is_heavy_tap_down) {
        return;
    }
    for (i = 0; i < CH_COUNT; i++) {
        if (ch_force[i] > g_cs_press.heavy_tap_force_max[i]) {
            g_cs_press.heavy_tap_force_max[i] = ch_force[i];
        }
    }
}

/**
 * @brief Update swipe force statistics
 * @param curr_force: current force value
 */
static void cs_press_update_swipe_force_stats(int16_t curr_force)
{
    if (!g_cs_press.swipe_force_min || (curr_force < g_cs_press.swipe_force_min)) {
        g_cs_press.swipe_force_min = curr_force;
    }
    if (!g_cs_press.swipe_force_max || (curr_force > g_cs_press.swipe_force_max)) {
        g_cs_press.swipe_force_max = curr_force;
    }
}

/**
 * @brief Handle swipe events
 * @param realtime_action: realtime action bits
 * @param distance: swipe distance
 * @param curr_pos: current position
 * @return true if swipe occurred, false otherwise
 */
static bool cs_press_handle_swipe_events(int16_t realtime_action, int16_t distance, int16_t curr_pos)
{
    if (realtime_action & BIT_ACTION_FOLLOW_SWIPE_UP) {
        return cs_press_handle_swipe_event(KEY_SWIPE_UP, distance, curr_pos);
    }
    if (realtime_action & BIT_ACTION_FOLLOW_SWIPE_DOWN) {
        return cs_press_handle_swipe_event(KEY_SWIPE_DOWN, distance, curr_pos);
    }
    return false;
}

/**
 * @brief Handle camera mode realtime events
 * @param realtime_action: realtime action bits
 * @param delay_action: delay action bits
 * @param curr_force: current force value
 * @param ch_force: channel force values
 * @param distance: swipe distance
 * @param curr_pos: current position
 * @param heavy_tap_lag: output array for heavy tap lag values
 */
static void cs_press_handle_camera_mode_realtime(int16_t realtime_action, int16_t delay_action,
        int16_t curr_force, int16_t *ch_force, int16_t distance, int16_t curr_pos, int32_t *heavy_tap_lag)
{
    bool is_swipe = false;

    /* Tap Event */
    cs_press_handle_camera_tap_down(realtime_action, curr_force);
    cs_press_update_cur_down_max_force(curr_force);

    if ((realtime_action & BIT_ACTION_TAP_5_UP) && g_cs_press.is_heavy_tap_down) {
        cs_press_handle_heavy_tap_up_event(ch_force, heavy_tap_lag);
    }
    if ((realtime_action & BIT_ACTION_TAP_2_UP) && g_cs_press.is_light_tap_down) {
        cs_press_handle_light_tap_up_event();
    }
    if ((realtime_action & BIT_ACTION_TAP_1_UP) && g_cs_press.is_physical_tap_down) {
        report_key_up(realtime_action);
    }

    /* Swipe Event */
    is_swipe = cs_press_handle_swipe_events(realtime_action, distance, curr_pos);

    /* Update force statistics */
    cs_press_update_tap_force_stats(curr_force);
    cs_press_update_heavy_tap_ch_force(ch_force);
    if (is_swipe) {
        cs_press_update_swipe_force_stats(curr_force);
    }
}

/**
 * @brief Handle delay mode events
 * @param realtime_action: realtime action bits
 * @param delay_action: delay action bits
 * @param curr_force: current force value
 */
static void cs_press_handle_delay_mode(int16_t realtime_action, int16_t delay_action, int16_t curr_force)
{
    if (g_cs_press.camera_key_mode < GAME_KEY_DEFAULT_MODE) {
        report_key_up(realtime_action);
        cs_press_handle_delay_action(delay_action, false);
    } else {
        if ((realtime_action & BIT_ACTION_TAP_5_DOWN) && !g_cs_press.is_g_heavy_tap_down) {
            LOG_INFO("[REPORT_KEY]GKEY_HEAVY_TAP down\n");
            cs_press_record_event_statistics(GKEY_HEAVY_TAP);
            input_report_key(cs_input_dev, GKEY_HEAVY_TAP, 1);
            input_sync(cs_input_dev);
            g_cs_press.is_g_heavy_tap_down = true;
            g_cs_press.cur_down_max_force = curr_force;
        }
        /* Update cur_down_max_force during press */
        if (g_cs_press.is_g_heavy_tap_down) {
            if ((g_cs_press.cur_down_max_force > 0) && (curr_force > g_cs_press.cur_down_max_force)) {
                g_cs_press.cur_down_max_force = curr_force;
            }
            if ((g_cs_press.cur_down_max_force < 0) && (curr_force < g_cs_press.cur_down_max_force)) {
                g_cs_press.cur_down_max_force = curr_force;
            }
        }
        report_key_up(realtime_action);
        cs_press_handle_delay_action(delay_action, true);
    }
}

/**
 * @brief Update cur_down_max_force during game press
 * @param curr_force: current force value
 */
static void cs_press_update_game_cur_down_max_force(int16_t curr_force)
{
    if (g_cs_press.cur_down_max_force) {
        if ((g_cs_press.cur_down_max_force > 0) && (curr_force > g_cs_press.cur_down_max_force)) {
            g_cs_press.cur_down_max_force = curr_force;
        }
        if ((g_cs_press.cur_down_max_force < 0) && (curr_force < g_cs_press.cur_down_max_force)) {
            g_cs_press.cur_down_max_force = curr_force;
        }
    }
}

/**
 * @brief Get offset information
 * @param offset: output array for offset values
 * @param offset_diff: output array for offset difference values
 * @return 0 on success, negative on error
 */
int cs_press_get_offset(int32_t *offset, int32_t *offset_diff)
{
    unsigned char rbuf[CH_COUNT * 2 * 2] = {0};
    int16_t dac_conver = 0;
    int i, ret = 0;

    ret = cs_press_iic_read(AP_DAC_UV_CONVER_REG, rbuf, 2);
    if (ret < 0) {
        LOG_ERR("read reg=0x%02x error, ret = %d\n", AP_OFFSET_REG, ret);
        return -1;
    }
    dac_conver = (int16_t)(rbuf[0] + (rbuf[1] << 8));

    memset(rbuf, 0, CH_COUNT * 2 * 2);
    ret = cs_press_iic_read(AP_OFFSET_REG, rbuf, CH_COUNT * 2 * 2);
    if (ret < 0) {
        LOG_ERR("read reg=0x%02x error, ret = %d\n", AP_OFFSET_REG, ret);
        return -1;
    }
    for (i = 0; i < CH_COUNT; i++) {
        offset[i] = (int16_t)(rbuf[2 * i] + (rbuf[2 * i + 1] << 8)) * dac_conver;
        LOG_INFO("offset[%i] = %d (%d[%02x %02x] * %d)\n", i, offset[i],
                (int16_t)(rbuf[2 * i] + (rbuf[2 * i + 1] << 8)), rbuf[2 * i], rbuf[2 * i + 1], dac_conver);
    }

    if (offset_diff) {
        for (i = 0; i < CH_COUNT; i++) {
            offset_diff[i] = offset[i] - (int16_t)(rbuf[2 * CH_COUNT + 2 * i] + (rbuf[2 * CH_COUNT + 2 * i + 1] << 8)) * dac_conver;
            LOG_INFO("offset_diff[%i] = %d (%d - %d[%02x %02x] * %d)\n", i, offset_diff[i], offset[i],
                    (int16_t)(rbuf[2 * CH_COUNT + 2 * i] + (rbuf[2 * CH_COUNT + 2 * i + 1] << 8)),
                    rbuf[2 * CH_COUNT + 2 * i], rbuf[2 * CH_COUNT + 2 * i + 1], dac_conver);
        }
    }

    return ret;
}

/**
 * @brief Get noise variance information
 * @param noise_std: output array for noise standard deviation values
 * @return 0 on success, negative on error
 */
int cs_press_get_noise_var(int32_t *noise_std)
{
    unsigned char rbuf[AP_FORCEDATA_LEN] = {0};
    int16_t dac_conver = 0;
    int i, ret = 0;
    int retry_times = 5;
    int16_t pre_adc[CH_COUNT] = {0};
    int16_t adc_delta[CH_COUNT][NOISE_TEST_COUNT] = {0};
    int32_t sum[CH_COUNT] = {0};
    int32_t avg[CH_COUNT] = {0};
    unsigned char enter_test_cmd[2] = {0x46, 0xBA};
    unsigned char exit_test_cmd[2] = {0x47, 0xB9};
    unsigned char debug_data = 0x00;

    /* 1. switch to test mode */
    ret = cs_press_iic_write(AP_DEVICE_ID_REG, enter_test_cmd, 2);
    if (ret < 0) {
        LOG_ERR("enter test mode error %d\n", ret);
        return ret;
    }

    while ((enter_test_cmd[0] || enter_test_cmd[1]) && retry_times) {
        retry_times--;
        cs_press_delay_ms(50);
        ret = cs_press_iic_read(AP_DEVICE_ID_REG, enter_test_cmd, 2);
        if (ret < 0) {
            LOG_ERR("%s: IIC read AP_DEVICE_ID_REG failed!!\n", __func__);
            return ret;
        }
        LOG_INFO("read AP_DEVICE_ID_REG [0x%02x 0x%02x]\n", enter_test_cmd[0], enter_test_cmd[1]);
    }
    if (!retry_times) {
        LOG_ERR("%s: IIC read AP_DEVICE_ID_REG retry over times!!\n", __func__);
    }

    /* 2. read ADC2Uv convert */
    ret = cs_press_iic_read(AP_DAC_UV_CONVER_REG, rbuf, 2);
    if (ret < 0) {
        LOG_ERR("read reg=0x%02x error, ret = %d\n", AP_OFFSET_REG, ret);
        return -1;
    }
    dac_conver = (int16_t)(rbuf[0] + (rbuf[1] << 8));
    LOG_INFO("read dac_conver %d\n", dac_conver);

    /* 3. read data */
    /* 3.1 */
    ret = cs_press_iic_write(DEBUG_MODE_V2_REG, &debug_data, 1);
    if (ret < 0) {
        LOG_ERR("write DEBUG_MODE_V2_REG error %d\n", ret);
        return ret;
    }
    /* 3.2 */
    ret = cs_press_iic_write(DEBUG_READY_V2_REG, &debug_data, 1);
    if (ret < 0) {
        LOG_ERR("write DEBUG_READY_V2_REG error %d\n", ret);
        return ret;
    }
    /* 3.3 */
    debug_data = 0x14;
    ret = cs_press_iic_write(DEBUG_MODE_V2_REG, &debug_data, 1);
    if (ret < 0) {
        LOG_ERR("write DEBUG_MODE_V2_REG error %d\n", ret);
        return ret;
    }

    for (i = 0; i < NOISE_TEST_COUNT + 1; i++) {
        debug_data = 0;
        retry_times = 5;
        /* 3.4 */
        cs_press_delay_ms(10);
        ret = cs_press_iic_read(DEBUG_READY_V2_REG, &debug_data, 1);
        if (ret < 0) {
            LOG_ERR("%s: IIC read DEBUG_READY_V2_REG failed!!\n", __func__);
            return ret;
        }
        while (!debug_data && retry_times) {
            retry_times--;
            cs_press_delay_ms(5);
            ret = cs_press_iic_read(DEBUG_READY_V2_REG, &debug_data, 1);
            if (ret < 0) {
                LOG_ERR("%s: IIC read DEBUG_READY_V2_REG failed!!\n", __func__);
                return ret;
            }
            LOG_INFO("read DEBUG_READY_V2_REG [0x%02x]\n", debug_data);
        }
        if (!retry_times) {
            LOG_ERR("%s: IIC read DEBUG_READY_V2_REG retry over times!!\n", __func__);
            return -1;
        }
        if (debug_data < 4) {
            LOG_ERR("%s: debug data len is %u, not correct\n", __func__, debug_data);
            return -1;
        }
        /* 3.5 */
        memset(rbuf, 0, AP_FORCEDATA_LEN);
        ret = cs_press_iic_read(DEBUG_DATA_V2_REG, rbuf, 4);
        if (ret < 0) {
            LOG_ERR("read reg=0x%02x error, ret = %d\n", DEBUG_DATA_V2_REG, ret);
            return ret;
        }
        if (!i) {
            pre_adc[0] = (int16_t)(rbuf[0] + (rbuf[1] << 8));
            pre_adc[1] = (int16_t)(rbuf[2] + (rbuf[3] << 8));
            LOG_INFO("%d:pre_adc[0]=%d[%02x %02x], pre_adc[1]=%d[%02x %02x]",
                        i, pre_adc[0], rbuf[0], rbuf[1], pre_adc[1], rbuf[2], rbuf[3]);
        } else {
            adc_delta[0][i - 1] = (int16_t)(rbuf[0] + (rbuf[1] << 8)) - pre_adc[0];
            adc_delta[1][i - 1] = (int16_t)(rbuf[2] + (rbuf[3] << 8)) - pre_adc[1];
            pre_adc[0] = (int16_t)(rbuf[0] + (rbuf[1] << 8));
            pre_adc[1] = (int16_t)(rbuf[2] + (rbuf[3] << 8));
            sum[0] += adc_delta[0][i - 1];
            sum[1] += adc_delta[1][i - 1];
            LOG_INFO("%d:adc_delta[0]=%d[%02x %02x](sum=%d), adc_delta[1]=%d[%02x %02x](sum=%d)",
                        i, adc_delta[0][i - 1], rbuf[0], rbuf[1], sum[0], adc_delta[1][i - 1], rbuf[2], rbuf[3], sum[1]);
        }
        /* 3.6 */
        debug_data = 0x00;
        ret = cs_press_iic_write(DEBUG_READY_V2_REG, &debug_data, 1);
        if (ret < 0) {
            LOG_ERR("write DEBUG_READY_V2_REG error %d\n", ret);
            return ret;
        }
    }
    /* 3.8 */
    debug_data = 0x00;
    ret = cs_press_iic_write(DEBUG_MODE_V2_REG, &debug_data, 1);
    if (ret < 0) {
        LOG_ERR("write DEBUG_MODE_V2_REG error %d\n", ret);
        return ret;
    }
    /* 3.9 */
    debug_data = 0x00;
    ret = cs_press_iic_write(DEBUG_READY_V2_REG, &debug_data, 1);
    if (ret < 0) {
        LOG_ERR("write DEBUG_READY_V2_REG error %d\n", ret);
        return ret;
    }

    /* 4. calculate noise std */
    avg[0] = sum[0] / NOISE_TEST_COUNT;
    avg[1] = sum[1] / NOISE_TEST_COUNT;

    sum[0] = 0;
    sum[1] = 0;
    for (i = 0; i < NOISE_TEST_COUNT; i++) {
        sum[0] += (adc_delta[0][i] - avg[0]) * (adc_delta[0][i] - avg[0]);
        sum[1] += (adc_delta[1][i] - avg[1]) * (adc_delta[1][i] - avg[1]);
        LOG_INFO("%d:var0=%d, var1=%d", i, sum[0], sum[1]);
    }
    noise_std[0] = sum[0] * dac_conver * dac_conver / NOISE_TEST_COUNT;
    noise_std[1] = sum[1] * dac_conver * dac_conver / NOISE_TEST_COUNT;
    LOG_INFO("noise_std0=%d, noise_std1=%d", noise_std[0], noise_std[1]);

    /* 5. switch to normal mode */
    ret = cs_press_iic_write(AP_DEVICE_ID_REG, exit_test_cmd, 2);
    if (ret < 0) {
        LOG_ERR("enter test mode error %d\n", ret);
        return ret;
    }
    retry_times = 5;
    while ((exit_test_cmd[0] || exit_test_cmd[1]) && retry_times) {
        retry_times--;
        cs_press_delay_ms(50);
        ret = cs_press_iic_read(AP_DEVICE_ID_REG, exit_test_cmd, 2);
        if (ret < 0) {
            LOG_ERR("%s: IIC read AP_DEVICE_ID_REG failed!!\n", __func__);
            return ret;
        }
        LOG_INFO("read AP_DEVICE_ID_REG [0x%02x 0x%02x]\n", exit_test_cmd[0], exit_test_cmd[1]);
    }
    if (!retry_times) {
        LOG_ERR("%s: IIC read AP_DEVICE_ID_REG retry over times!!\n", __func__);
    }

    return ret;
}

/**
 * @brief Output firmware version information
 * @param m: seq_file pointer for output
 */
static void cs_press_output_fw_version(struct seq_file *m)
{
    int ret;
    unsigned char read_temp[FW_ONE_BLOCK_LENGTH_R] = {0};

    ret = cs_press_iic_read(AP_VERSION_REG, read_temp, CS_FW_VERSION_LENGTH);
    if (ret == 0) {
        seq_printf(m, "fw_ver:%d %d %d %d\n",
             read_temp[0], read_temp[1], read_temp[2], read_temp[3]);
    } else {
        seq_printf(m, "fw_ver:read error\n");
    }
}

/**
 * @brief Output offset information
 * @param m: seq_file pointer for output
 */
static void cs_press_output_offset_info(struct seq_file *m)
{
    int ret = cs_press_get_offset(g_cs_press.dac_offset, g_cs_press.dac_offset_diff);

    if (ret == 0) {
        seq_printf(m, "ch1_offset:%d\n", g_cs_press.dac_offset[0]);
        seq_printf(m, "ch2_offset:%d\n", g_cs_press.dac_offset[1]);
        seq_printf(m, "ch1_offset_diff:%d\n", g_cs_press.dac_offset_diff[0]);
        seq_printf(m, "ch2_offset_diff:%d\n", g_cs_press.dac_offset_diff[1]);
    } else {
        seq_printf(m, "ch1_offset:read error\n");
        seq_printf(m, "ch2_offset:read error\n");
        seq_printf(m, "ch1_offset_diff:read error\n");
        seq_printf(m, "ch2_offset_diff:read error\n");
    }
}

/**
 * @brief Output noise variance information
 * @param m: seq_file pointer for output
 */
static void cs_press_output_noise_var_info(struct seq_file *m)
{
    int ret = cs_press_get_noise_var(g_cs_press.dac_noise_var);

    if (ret == 0) {
        seq_printf(m, "ch1_noise_var:%d\n", g_cs_press.dac_noise_var[0]);
        seq_printf(m, "ch2_noise_var:%d\n", g_cs_press.dac_noise_var[1]);
    } else {
        seq_printf(m, "ch1_noise_var:read error\n");
        seq_printf(m, "ch2_noise_var:read error\n");
    }
}

/**
 * @brief Output basic statistics
 * @param m: seq_file pointer for output
 */
static void cs_press_output_basic_stats(struct seq_file *m)
{
    int i;

    if (g_cs_press.tap_force_min) {
        seq_printf(m, "tap_force_min:%d\n", g_cs_press.tap_force_min);
    }
    if (g_cs_press.tap_force_max) {
        seq_printf(m, "tap_force_max:%d\n", g_cs_press.tap_force_max);
    }
    if (g_cs_press.swipe_force_min) {
        seq_printf(m, "swipe_force_min:%d\n", g_cs_press.swipe_force_min);
    }
    if (g_cs_press.swipe_force_max) {
        seq_printf(m, "swipe_force_max:%d\n", g_cs_press.swipe_force_max);
    }
    seq_printf(m, "double_tap_cnt:%d\n", g_cs_press.double_tap_cnt);
    for (i = 0; i < CH_COUNT; i++) {
        seq_printf(m, "ch%d_lag:%d,%d,%d,%d,%d\n", i + 1,
                    g_cs_press.heavy_tap_lag[i][0], g_cs_press.heavy_tap_lag[i][1],
                    g_cs_press.heavy_tap_lag[i][2], g_cs_press.heavy_tap_lag[i][3],
                    g_cs_press.heavy_tap_lag[i][4]);
    }
}

/**
 * @brief Output reset and error counts
 * @param m: seq_file pointer for output
 */
static void cs_press_output_reset_error_counts(struct seq_file *m)
{
    int i, cnt;

    if (g_cs_press.hard_reset_cnt) {
        seq_printf(m, "hard_reset_cnt:%d\n", g_cs_press.hard_reset_cnt);
    }
    if (g_cs_press.soft_reset_cnt) {
        seq_printf(m, "soft_reset_cnt:%d\n", g_cs_press.soft_reset_cnt);
    }
    if (g_cs_press.wdt_reset_cnt) {
        seq_printf(m, "wdt_reset_cnt:%d\n", g_cs_press.wdt_reset_cnt);
    }
    if (g_cs_press.fw_update_error) {
        seq_printf(m, "fw_update_error:0x%x\n", g_cs_press.fw_update_error);
    }
    if (g_cs_press.bus_error_cnt) {
        seq_printf(m, "bus_error_cnt:%d\n", g_cs_press.bus_error_cnt);
        cnt = (g_cs_press.bus_error_cnt > BUS_ERROR_MSG_CNT) ? BUS_ERROR_MSG_CNT : g_cs_press.bus_error_cnt;
        for (i = 0; i < cnt; i++) {
            seq_printf(m, "bus_error_msg[%d]:%s\n", i, g_cs_press.bus_error_msg[i]);
        }
    }
}

/**
 * @brief Output mode type mismatch statistics
 * @param m: seq_file pointer for output
 */
static void cs_press_output_mode_mismatch_stats(struct seq_file *m)
{
    int expected, read;

    /* Output all non-zero (expected_ic_mode, mode_type_byte) combinations for normal modes */
    for (expected = 0; expected < CAMERA_KEY_IC_MODE_COUNT; expected++) {
        for (read = 0; read < CAMERA_KEY_IC_MODE_COUNT; read++) {
            if ((expected != read) && (g_cs_press.mode_type_mismatch_cnt[expected][read] > 0)) {
                seq_printf(m, "mode_type_mismatch expected=0x%02x read=0x%02x count:%lu\n",
                        expected, read, g_cs_press.mode_type_mismatch_cnt[expected][read]);
            }
        }
    }

    /* Output CAMERA_KEY_IC_NONE_EVENT_MODE interrupt count */
    if (g_cs_press.none_mode_interrupt_cnt > 0) {
        seq_printf(m, "none_mode_interrupt_cnt:%lu\n", g_cs_press.none_mode_interrupt_cnt);
    }
}

/**
 * @brief Output device status information
 * @param m: seq_file pointer for output
 */
static void cs_press_output_device_status(struct seq_file *m)
{
    if (g_cs_press.is_boot_ver_err) {
        seq_printf(m, "device_status:reset_error\n");
    }
    if (!g_cs_press.irq_ok) {
        seq_printf(m, "device_status:irq_error\n");
    }
}

/**
 * @brief Output force statistics
 * @param m: seq_file pointer for output
 */
static void cs_press_output_force_statistics(struct seq_file *m)
{
    int i;

    seq_printf(m, "total_down_max_force:%d\n", g_cs_press.total_down_max_force);
    /* Output down max force distribution */
    for (i = 0; i < DOWN_MAX_FORCE_RANGE_COUNT; i++) {
        if (g_cs_press.down_max_force[i]) {
            if (i == DOWN_MAX_FORCE_RANGE_COUNT - 1) {
                seq_printf(m, "down_force >%dg:%lu\n",
                        (DOWN_MAX_FORCE_RANGE_COUNT - 1) * DOWN_MAX_FORCE_RANGE_SIZE,
                        g_cs_press.down_max_force[i]);
            } else {
                seq_printf(m, "down_force %d-%dg:%lu\n",
                        i * DOWN_MAX_FORCE_RANGE_SIZE,
                        (i + 1) * DOWN_MAX_FORCE_RANGE_SIZE - 1,
                        g_cs_press.down_max_force[i]);
            }
        }
    }
}

/**
 * @brief Output event statistics
 * @param m: seq_file pointer for output
 */
static void cs_press_output_event_statistics(struct seq_file *m)
{
    int i;

    for (i = 0; i < EVENT_STATISTICS_ARRAY_SIZE; i++) {
        if (g_cs_press.event_total_cnt[i] > 0) {
            seq_printf(m, "event_total_cnt[%d]:%lu\n",
                    i + EVENT_STATISTICS_CODE_BASE, g_cs_press.event_total_cnt[i]);
        }
        if (g_cs_press.suspend_abnormal_event_cnt[i] > 0) {
            seq_printf(m, "suspend_abnormal_event[%d]:%lu\n",
                    i + EVENT_STATISTICS_CODE_BASE, g_cs_press.suspend_abnormal_event_cnt[i]);
        }
    }
}

/**
 * @brief Handle IC reset detection and recovery
 * @param rbuf: buffer containing reset source register data
 * @return true if reset detected, false otherwise
 */
static bool cs_press_handle_reset(unsigned char *rbuf)
{
    int16_t rsts = 0;

    if (rbuf[0] == 0xEE && rbuf[1] == 0xEE) {
        unsigned char addr = DEBUG_RESET_SOURCE_REG;
        int ret;

        memset(rbuf, 0, AP_FORCEDATA_LEN);
        ret = cs_press_iic_read(addr, rbuf, 4);
        if (ret < 0) {
            LOG_ERR("read reg=0x%02x error, ret = %d\n", addr, ret);
        } else {
            LOG_ERR("IC reset[%02x %02x] happened, mode recovery...\n", rbuf[0], rbuf[1]);
            rsts = (int16_t)(rbuf[0] + (rbuf[1] << 8));
            if (rsts & RSTS_PIN) {
                g_cs_press.hard_reset_cnt++;
                LOG_DEBUG("[HARD_RESET]\n");
            }
            if (rsts & RSTS_SYS) {
                g_cs_press.soft_reset_cnt++;
                LOG_DEBUG("[SOFT_RESET]\n");
            }
            if ((rsts & RSTS_IWDG) || (rsts & RSTS_WDT)) {
                g_cs_press.wdt_reset_cnt++;
                LOG_DEBUG("[WDT_RESET]\n");
            }
            cs_press_clear_reset_register();
        }
        cs_press_set_mode(g_cs_press.camera_key_mode);
        return true;
    }
    return false;
}

/**
 * @brief Calculate expected ic_mode based on current state
 * Consider is_suspended, quick_on_closed, and camera_key_mode
 * @return expected ic_mode value
 */
static unsigned char cs_press_get_expected_ic_mode(void)
{
    int mode;

    /* If suspended, use sleep mode */
    if (g_cs_press.is_suspended) {
        mode = (g_cs_press.camera_key_mode < GAME_KEY_DEFAULT_MODE) ?
               CAMERA_KEY_SLEEP_MODE : GAME_KEY_SLEEP_MODE;
    } else {
        mode = g_cs_press.camera_key_mode;
    }

    return cs_press_mode_to_ic_mode(mode);
}

void report_camera_key(void)
{
    unsigned char rbuf[AP_FORCEDATA_LEN] = {0};
    unsigned char addr = AP_FORCEDATA_REG;
    unsigned char expected_ic_mode;
    unsigned char mode_type_byte;
    int16_t realtime_action, delay_action, area, distance, curr_pos, curr_force, start_pos, mode_type, trig;
    int16_t ch_rawdata[CH_COUNT] = { 0 };
    int16_t ch_baseline[CH_COUNT] = { 0 };
    int16_t ch_force[CH_COUNT] = { 0 };
    int32_t heavy_tap_lag[CH_COUNT] = { 0 };
    int ret = 0;

    g_cs_press.irq_ok = true;

    ret = cs_press_iic_read(addr, rbuf, AP_FORCEDATA_LEN);
    if (ret < 0) {
        LOG_ERR("read reg=0x%02x error, ret = %d\n", addr, ret);
        return;
    }

    if (cs_press_handle_reset(rbuf)) {
        return;
    }
    realtime_action = (int16_t)(rbuf[WORD_EVENT_BASIC_ACTION * 2] + (rbuf[WORD_EVENT_BASIC_ACTION * 2 + 1] << 8));
    delay_action = (int16_t)(rbuf[WORD_EVENT_SENIOR_ACTION * 2] + (rbuf[WORD_EVENT_SENIOR_ACTION * 2 + 1] << 8));
    area = (int16_t)(rbuf[WORD_EVENT_AREA * 2] + (rbuf[WORD_EVENT_AREA * 2 + 1] << 8));
    curr_pos = (int16_t)(rbuf[WORD_CURPOS * 2] + (rbuf[WORD_CURPOS * 2 + 1] << 8));
    curr_force = (int16_t)(rbuf[WORD_TOTAL_FORCE * 2] + (rbuf[WORD_TOTAL_FORCE * 2 + 1] << 8));
    distance = (int16_t)(rbuf[WORD_RELATIVE_MILEAGE * 2] + (rbuf[WORD_RELATIVE_MILEAGE * 2 + 1] << 8));
    start_pos = (int16_t)(rbuf[WORD_ORI_STA_POS * 2] + (rbuf[WORD_ORI_STA_POS * 2 + 1] << 8));
    mode_type = (int16_t)(rbuf[WORD_MODE_TYPE * 2] + (rbuf[WORD_MODE_TYPE * 2 + 1] << 8));
    ch_rawdata[0] = (int16_t)(rbuf[WORD_RAWDATA1 * 2] + (rbuf[WORD_RAWDATA1 * 2 + 1] << 8));
    ch_rawdata[1] = (int16_t)(rbuf[WORD_RAWDATA2 * 2] + (rbuf[WORD_RAWDATA2 * 2 + 1] << 8));
    ch_baseline[0] = (int16_t)(rbuf[WORD_BASELINE1 * 2] + (rbuf[WORD_BASELINE1 * 2 + 1] << 8));
    ch_baseline[1] = (int16_t)(rbuf[WORD_BASELINE2 * 2] + (rbuf[WORD_BASELINE2 * 2 + 1] << 8));
    ch_force[0] = (int16_t)(rbuf[WORD_FORCE_1 * 2] + (rbuf[WORD_FORCE_1 * 2 + 1] << 8));
    ch_force[1] = (int16_t)(rbuf[WORD_FORCE_2 * 2] + (rbuf[WORD_FORCE_2 * 2 + 1] << 8));
    trig = (int16_t)(rbuf[WORD_MOTO_TRIG_INFO * 2] + (rbuf[WORD_MOTO_TRIG_INFO * 2 + 1] << 8));
    LOG_DEBUG("mode=[%d], ation=[%04x %04x], area=[%04x], "
            " curPos=%d, curForce=%d, swipeDis=%d, startPos=%d, trig=%d, modeType=[%d], "
            " chRaw=(%d %d), chBase=(%d %d), chForce=(%d %d), data: %*ph",
            g_cs_press.camera_key_mode, realtime_action, delay_action, area,
            curr_pos, curr_force, distance, start_pos, trig, mode_type,
            ch_rawdata[0], ch_rawdata[1], ch_baseline[0], ch_baseline[1], ch_force[0], ch_force[1],
            AP_FORCEDATA_LEN, rbuf);

    /* Check mode_type consistency with expected ic_mode */
    expected_ic_mode = cs_press_get_expected_ic_mode();
    mode_type_byte = (unsigned char)(mode_type & 0xFF);

    /* Special handling for CAMERA_KEY_IC_NONE_EVENT_MODE: any interrupt is abnormal */
    if (expected_ic_mode == CAMERA_KEY_IC_NONE_EVENT_MODE) {
        g_cs_press.none_mode_interrupt_cnt++;
        LOG_ERR("none_mode interrupt: expected=0x%02x (NONE_MODE), read=0x%02x, camera_key_mode=%d, is_suspended=%d, quick_on_closed=%d\n",
                expected_ic_mode, mode_type_byte, g_cs_press.camera_key_mode,
                g_cs_press.is_suspended, g_cs_press.quick_on_closed);
    } else if (expected_ic_mode < CAMERA_KEY_IC_MODE_COUNT) {
        /* Record each (expected_ic_mode, mode_type_byte) combination for normal modes */
        if (mode_type_byte < CAMERA_KEY_IC_MODE_COUNT) {
            g_cs_press.mode_type_mismatch_cnt[expected_ic_mode][mode_type_byte]++;
            if (mode_type_byte != expected_ic_mode) {
                LOG_ERR("mode_type mismatch: read=0x%02x, expected=0x%02x, camera_key_mode=%d, is_suspended=%d, quick_on_closed=%d\n",
                        mode_type_byte, expected_ic_mode, g_cs_press.camera_key_mode,
                        g_cs_press.is_suspended, g_cs_press.quick_on_closed);
            }
        }
    }

    if (distance < 0) {
        distance = -distance;
    }


    /*if (g_cs_press.camera_key_mode == CAMERA_KEY_GAME_SHORTCUTS_MODE) {
        if ((realtime_action & BIT_ACTION_TAP_5_DOWN)
                && !g_cs_press.is_game_shortcuts_down) {
            if (area & BIT_AREA_1) {
                LOG_INFO("[REPORT_KEY]GKEY_TAP_SHORTCUTS_AREA_1 down\n");
                input_report_key(cs_input_dev, GKEY_TAP_SHORTCUTS_AREA_1, 1);
            } else if (area & BIT_AREA_2) {
                LOG_INFO("[REPORT_KEY]GKEY_TAP_SHORTCUTS_AREA_2 down\n");
                input_report_key(cs_input_dev, GKEY_TAP_SHORTCUTS_AREA_2, 1);
            }
            input_sync(cs_input_dev);
            g_cs_press.is_game_shortcuts_down = area;
        }
        report_key_up(realtime_action);
    } else */
    if (g_cs_press.camera_key_mode == CAMERA_KEY_CAMERA_MODE) {
        //LOG_DEBUG("in REALTIME_EVENT_MODE\n");
        cs_press_handle_camera_mode_realtime(realtime_action, delay_action, curr_force,
                ch_force, distance, curr_pos, heavy_tap_lag);
        /* Update press statistics */
        cs_press_update_long_press_statistics();
    } else {
        //LOG_DEBUG("in DELAY_EVENT_MODE\n");
        cs_press_handle_delay_mode(realtime_action, delay_action, curr_force);
        /* Update press statistics */
        cs_press_update_long_press_statistics();
    }
}

void report_game_key(void)
{
    unsigned char rbuf[AP_GAME_FORCEDATA_LEN] = {0};
    unsigned char addr = AP_GAME_FORCEDATA_REG;
    int16_t realtime_action, delay_action, area, curr_force;
    int ret = 0;

    g_cs_press.irq_ok = true;

    ret = cs_press_iic_read(addr, rbuf, AP_GAME_FORCEDATA_LEN);
    if (ret < 0) {
        LOG_ERR("read reg=0x%02x error, ret = %d\n", addr, ret);
        return;
    }

    if (cs_press_handle_reset(rbuf)) {
        return;
    }
    realtime_action = (int16_t)(rbuf[WORD_EVENT_BASIC_ACTION * 2] + (rbuf[WORD_EVENT_BASIC_ACTION * 2 + 1] << 8));
    delay_action = (int16_t)(rbuf[WORD_EVENT_SENIOR_ACTION * 2] + (rbuf[WORD_EVENT_SENIOR_ACTION * 2 + 1] << 8));
    area = (int16_t)(rbuf[WORD_EVENT_AREA * 2] + (rbuf[WORD_EVENT_AREA * 2 + 1] << 8));
    curr_force = (int16_t)(rbuf[WORD_CURPOS * 2] + (rbuf[WORD_CURPOS * 2 + 1] << 8));
    LOG_DEBUG("mode=[%d], ation=[%04x %04x], area=[%04x], curForce=%d, data: %*ph",
            g_cs_press.camera_key_mode, realtime_action, delay_action, area, curr_force, AP_GAME_FORCEDATA_LEN, rbuf);

    //LOG_DEBUG("in REALTIME_EVENT_MODE\n");
    /* Tap Down Event */
    cs_press_handle_game_tap_down(realtime_action, area, curr_force, 1,
            &g_cs_press.is_game_tap_1_down, GKEY_TAP_1_AREA_1, GKEY_TAP_1_AREA_2);
    cs_press_handle_game_tap_down(realtime_action, area, curr_force, 2,
            &g_cs_press.is_game_tap_2_down, GKEY_TAP_2_AREA_1, GKEY_TAP_2_AREA_2);
    cs_press_handle_game_tap_down(realtime_action, area, curr_force, 3,
            &g_cs_press.is_game_tap_3_down, GKEY_TAP_3_AREA_1, GKEY_TAP_3_AREA_2);
    cs_press_handle_game_tap_down(realtime_action, area, curr_force, 4,
            &g_cs_press.is_game_tap_4_down, GKEY_TAP_4_AREA_1, GKEY_TAP_4_AREA_2);
    cs_press_handle_game_tap_down(realtime_action, area, curr_force, 5,
            &g_cs_press.is_game_tap_5_down, GKEY_TAP_5_AREA_1, GKEY_TAP_5_AREA_2);
    /* Update cur_down_max_force during press */
    cs_press_update_game_cur_down_max_force(curr_force);

    /* Tap Up Event */
    report_key_up(realtime_action);
    /* Update press statistics */
    cs_press_update_long_press_statistics();
}

/**
  * @brief    cs_press_event_handler
  * @param
  * @retval
*/
static int cs_press_event_handler(void *unused)
{
    struct sched_param param = {.sched_priority = SCHEDULE_CS_PRESS_PRIORITY};

    sched_setscheduler(current, SCHED_FIFO, &param);

    do {
        wait_event_interruptible(cs_press_waiter,
            cs_press_irq_flag != 0);
        mutex_lock(&press_lock);
        cs_press_irq_flag = 0;
        if (!g_cs_press.is_suspended && g_cs_press.camera_key_mode == GAME_KEY_GAME_MODE) {
            report_game_key();
        } else {
            report_camera_key();
        }
        cs_irq_enable();
        mutex_unlock(&press_lock);
    } while (!kthread_should_stop());

    return 0;
}

/**
  * @brief
  * @param
  * @retval
*/
void eint_init(void)
{
    init_waitqueue_head(&cs_press_waiter);

    kthread_run(cs_press_event_handler, 0, CS_PRESS_NAME);
    cs_irq_enable();
    LOG_ERR("init_irq ok");
    fml_input_dev_init();
}
void eint_exit(void)
{
    fml_input_dev_exit();
}
#endif

/**
  * @brief  watchdog reset the device
  * @param  None
  * @retval 0:success, -1: fail
  */
char cs_press_wdt_reset_device(void)
{
    char ret = 0;
    unsigned char retry = RETRY_NUM;
    // M series required write value 0xcc
    unsigned char temp_data[2] = {0xB8, 0x48};

    LOG_INFO("%s\n", __func__);
    // repeatly reset including first time i2c wake up
    do
    {
        if (ret != 0)
        {
            cs_press_delay_ms(1);
        }
        ret = cs_press_iic_write(AP_RESET_MCU_REG, temp_data, 2);
    } while ((ret != 0) && (retry--));

    return ret;
}

/**
  * @brief  soft_reset the device
  * @param  None
  * @retval 0:success, -1: fail
  */
char cs_press_soft_reset_device(void)
{
    char ret = 0;
    unsigned char retry = RETRY_NUM;
    // M series required write value 0xcc
    unsigned char temp_data[2] = {0xB5, 0x4B};

    LOG_INFO("%s\n", __func__);
    // repeatly reset including first time i2c wake up
    do
    {
        if (ret != 0)
        {
            cs_press_delay_ms(1);
        }
        ret = cs_press_iic_write(AP_RESET_MCU_REG, temp_data, 2);
    } while ((ret != 0) && (retry--));

    return ret;
}

/**
  * @brief  reset ic
  * @param  None
  * @retval 0:success, -1: fail
  */
char cs_press_reset_ic(void)
{
    char ret = 0;

    LOG_ERR("enter cs_press_reset_ic\n");
    rt_mutex_lock(&(g_cs_press.client->adapter->bus_lock));
    #if RSTPIN_RESET_ENABLE
        cs_press_rstpin_high();
        cs_press_delay_ms(100);
        cs_press_rstpin_low();
        cs_press_delay_ms(80);

    #else/* hw reset ic*/
        cs_press_power_down();
        cs_press_delay_ms(100);
        cs_press_power_up();
        cs_press_delay_ms(80);

    #endif
    rt_mutex_unlock(&(g_cs_press.client->adapter->bus_lock));
    LOG_ERR("exit cs_press_reset_ic\n");
    return ret;
}

/**
  * @brief  init config para
  * @param  None
  * @retval 0:success, -1: fail
  */
int camera_key_para_init(void)
{
    int rc;
    int ret = 0;
    struct device_node *np;
    uint32_t cfg_param[STRENGTH_CFG_NUM] = { 0 };

    np = g_cs_press.client->dev.of_node;

    rc = of_property_read_u32_array(np, "game_mode_strength_cfg", cfg_param, STRENGTH_CFG_NUM);
    if (rc < 0) {
        memcpy(&g_cs_press.game_mode_strength_cfg, &g_cs_press.strength_cfg, sizeof(trigger_strength_config));
    } else {
        g_cs_press.game_mode_strength_cfg.tap_1_down_strength = cfg_param[0] / STRENGTH_PER_LEVEL;
        g_cs_press.game_mode_strength_cfg.tap_1_up_strength = cfg_param[1] / STRENGTH_PER_LEVEL;
        g_cs_press.game_mode_strength_cfg.tap_2_down_strength = cfg_param[2] / STRENGTH_PER_LEVEL;
        g_cs_press.game_mode_strength_cfg.tap_2_up_strength = cfg_param[3] / STRENGTH_PER_LEVEL;
        g_cs_press.game_mode_strength_cfg.tap_3_down_strength = cfg_param[4] / STRENGTH_PER_LEVEL;
        g_cs_press.game_mode_strength_cfg.tap_3_up_strength = cfg_param[5] / STRENGTH_PER_LEVEL;
        g_cs_press.game_mode_strength_cfg.tap_4_down_strength = cfg_param[6] / STRENGTH_PER_LEVEL;
        g_cs_press.game_mode_strength_cfg.tap_4_up_strength = cfg_param[7] / STRENGTH_PER_LEVEL;
        g_cs_press.game_mode_strength_cfg.tap_5_down_strength = cfg_param[8] / STRENGTH_PER_LEVEL;
        g_cs_press.game_mode_strength_cfg.tap_5_up_strength = cfg_param[9] / STRENGTH_PER_LEVEL;
        g_cs_press.game_mode_strength_cfg.area_1 = cfg_param[10];
        g_cs_press.game_mode_strength_cfg.area_2 = cfg_param[11];
        g_cs_press.game_mode_strength_cfg.long_tap_judge_time = cfg_param[12] / TIME_MS_PER_LEVEL;
        g_cs_press.game_mode_strength_cfg.muti_tap_judge_time = cfg_param[13] / TIME_MS_PER_LEVEL;
    }
    LOG_INFO("game mode cfg %s: tap_strength[%u,%u][%u,%u][%u,%u][%u,%u][%u,%u], area[%u,%u], judge_time[%u,%u]\n",
        (rc < 0) ? "not set, default config" : "set config",
        g_cs_press.game_mode_strength_cfg.tap_1_down_strength, g_cs_press.game_mode_strength_cfg.tap_1_down_strength,
        g_cs_press.game_mode_strength_cfg.tap_2_down_strength, g_cs_press.game_mode_strength_cfg.tap_2_down_strength,
        g_cs_press.game_mode_strength_cfg.tap_3_down_strength, g_cs_press.game_mode_strength_cfg.tap_3_down_strength,
        g_cs_press.game_mode_strength_cfg.tap_4_down_strength, g_cs_press.game_mode_strength_cfg.tap_4_down_strength,
        g_cs_press.game_mode_strength_cfg.tap_5_down_strength, g_cs_press.game_mode_strength_cfg.tap_5_down_strength,
        g_cs_press.game_mode_strength_cfg.area_1, g_cs_press.game_mode_strength_cfg.area_2,
        g_cs_press.game_mode_strength_cfg.long_tap_judge_time, g_cs_press.game_mode_strength_cfg.muti_tap_judge_time);

    return ret;
}

int camera_key_config_read(trigger_strength_config *strength_cfg)
{
    unsigned char cfg_read[STRENGTH_CFG_NUM] = {0};
    int ret = 0;

    ret = cs_press_iic_read(AP_RW_SCANT_PERIOD_REG, cfg_read, STRENGTH_CFG_NUM);
    if (ret >= 0) {
        strength_cfg->tap_1_down_strength = cfg_read[0];
        strength_cfg->tap_1_up_strength = cfg_read[1];
        strength_cfg->tap_2_down_strength = cfg_read[2];
        strength_cfg->tap_2_up_strength = cfg_read[3];
        strength_cfg->tap_3_down_strength = cfg_read[4];
        strength_cfg->tap_3_up_strength = cfg_read[5];
        strength_cfg->tap_4_down_strength = cfg_read[6];
        strength_cfg->tap_4_up_strength = cfg_read[7];
        strength_cfg->tap_5_down_strength = cfg_read[8];
        strength_cfg->tap_5_up_strength = cfg_read[9];
        strength_cfg->area_1 = cfg_read[10];
        strength_cfg->area_2 = cfg_read[11];
        strength_cfg->long_tap_judge_time = cfg_read[12];
        strength_cfg->muti_tap_judge_time = cfg_read[13];
        LOG_INFO("trigger game config: tap_strength[%u,%u][%u,%u][%u,%u][%u,%u][%u,%u], area[%u,%u], judge_time[%u,%u]\n",
            strength_cfg->tap_1_down_strength,
            strength_cfg->tap_1_up_strength,
            strength_cfg->tap_2_down_strength,
            strength_cfg->tap_2_up_strength,
            strength_cfg->tap_3_down_strength,
            strength_cfg->tap_3_up_strength,
            strength_cfg->tap_4_down_strength,
            strength_cfg->tap_4_up_strength,
            strength_cfg->tap_5_down_strength,
            strength_cfg->tap_5_up_strength,
            strength_cfg->area_1, strength_cfg->area_2,
            strength_cfg->long_tap_judge_time,
            strength_cfg->muti_tap_judge_time);
    }

    return ret;
}


int cs_press_fw_write(const unsigned char *fw_code_start, unsigned int fw_code_length)
{
    unsigned int fw_count = 0;
    int ret = 0, i = 0;
    unsigned int fw_block_num_w = 0;
    unsigned char boot_fw_write_cmd[BOOT_CMD_LENGTH] = BOOT_FW_WRITE_CMD;

    fw_block_num_w = fw_code_length / FW_ONE_BLOCK_LENGTH_W;
    /* send fw write cmd */
    cs_press_iic_write_double_reg(BOOT_CMD_REG ,boot_fw_write_cmd, BOOT_CMD_LENGTH);
    cs_press_delay_ms(1500);    /* waiting flash erase*/
    /* send fw code */
    fw_count = 0;
    for (i = 0; i < fw_block_num_w; i++)
    {
        ret = cs_press_iic_write_double_reg(i * FW_ONE_BLOCK_LENGTH_W, (unsigned char*)fw_code_start + fw_count, FW_ONE_BLOCK_LENGTH_W);
        fw_count += FW_ONE_BLOCK_LENGTH_W;
        if (ret < 0)
        {
            LOG_ERR("ERR:iic write failed\n");
            return ret;
        }
        cs_press_delay_ms(10);
    }
    return 0;
}

int cs_press_fw_read_check(const unsigned char *fw_code_start, unsigned int fw_code_length)
{
    unsigned int fw_count = 0;
    int ret = 0, i = 0, j = 0;
    unsigned int fw_block_num_r = 0;
    unsigned char fw_read_code[FW_ONE_BLOCK_LENGTH_R] = {0};
    unsigned char page_end = 0;

    fw_block_num_r = fw_code_length / FW_ONE_BLOCK_LENGTH_R;
    page_end = fw_code_length % 256;
    /* read & check fw code */
    for (i = 0; i < fw_block_num_r; i++)
    {
        /* read code data */
        ret = cs_press_iic_read_double_reg(i * FW_ONE_BLOCK_LENGTH_R, fw_read_code, FW_ONE_BLOCK_LENGTH_R);
        if (ret < 0)
        {
            LOG_ERR("ERR:iic write failed\n");
            return ret;
        }
        /* check code data */
        for (j = 0; j < FW_ONE_BLOCK_LENGTH_R; j++)
        {
            if (fw_read_code[j] != fw_code_start[fw_count + j])
            {
                LOG_ERR("ERR:check code data failed\n");
                return -1;
            }
        }
        fw_count += FW_ONE_BLOCK_LENGTH_R;
        cs_press_delay_ms(5);
    }
    if (page_end > 0) {
        /* read code data */
        ret = cs_press_iic_read_double_reg(fw_block_num_r * FW_ONE_BLOCK_LENGTH_R, fw_read_code, 128);
        if (ret < 0)
        {
            LOG_ERR("ERR:iic write failed\n");
            return ret;
        }
        /* check code data */
        for (j = 0; j < 128; j++)
        {
            if (fw_read_code[j] != fw_code_start[fw_count + j])
            {
                LOG_ERR("ERR:check code data failed\n");
                return -1;
            }
        }
        cs_press_delay_ms(5);
    }

    return 0;
}

void set_device_updating_flag(unsigned char val)
{
    g_cs_press.updating_flag = val;
}

unsigned char  get_device_updating_flag(void)
{
    return g_cs_press.updating_flag;
}
/**
  * @brief      forced firmware update
  * @param[in]  *fw_array: point to fw hex array
  * @retval     0:success, -1: fail
  */
char cs_press_fw_force_update(const unsigned char *fw_array)
{
    int ret = 0;
    char result = 0;
    bool retry = false;
    unsigned int fw_code_length = 0;
    const unsigned char *fw_code_start = NULL;
    unsigned char fw_read_code[FW_ONE_BLOCK_LENGTH_R] = {0};
    unsigned short fw_default_version = 0;
    unsigned short fw_read_version = 0;
    unsigned char boot_fw_wflag_cmd[BOOT_CMD_LENGTH] = BOOT_FW_WFLAG_CMD;
#ifdef INT_SET_EN
    cs_irq_disable(); /*close enit irq.*/
#endif
UPDATE_RETRY:
    LOG_INFO("fw update start\n");

    /* fw init */
    fw_code_length = ((((unsigned short)fw_array[FW_ADDR_CODE_LENGTH + 0] << 8) & 0xff00) | fw_array[FW_ADDR_CODE_LENGTH + 1]);
    fw_code_start = &fw_array[FW_ADDR_CODE_START];
    fw_default_version = ((((unsigned short)fw_array[FW_ADDR_VERSION + 0] << 8) & 0xff00) | fw_array[FW_ADDR_VERSION + 1]);
    if (fw_code_length % 128) {
        LOG_INFO("fw is not 128*\n");
        goto FLAG_FW_FAIL;
    }
    cs_press_reset_ic();

    /* send fw write cmd */
    ret = cs_press_fw_write(fw_code_start, fw_code_length);
    if (ret < 0)
    {
        LOG_ERR("ERR:iic write failed\n");
        result = (char)(-1);
        goto FLAG_FW_FAIL;
    }

    /* read & check fw code */
    ret = cs_press_fw_read_check(fw_code_start, fw_code_length);
    if (ret < 0)
    {
        LOG_ERR("ERR:iic read check failed\n");
        result = (char)(-1);
        goto FLAG_FW_FAIL;
    }

    /* send fw flag cmd */
    cs_press_iic_write_double_reg(BOOT_CMD_REG ,boot_fw_wflag_cmd, BOOT_CMD_LENGTH);
    cs_press_delay_ms(50);

    /* reset */
    cs_press_reset_ic();

    /* check fw version */
    cs_press_delay_ms(900); /* skip boot */
    fw_read_version = 0;

    ret = cs_press_iic_read(AP_VERSION_REG, fw_read_code, CS_FW_VERSION_LENGTH);
    if (ret >= 0) {
        fw_read_version = ((((unsigned short)fw_read_code[2] << 8) & 0xff00) | fw_read_code[3]);
    }

    LOG_INFO("bin ver:%d.%d,soc ver:%d.%d\n", (fw_default_version >> 8), (fw_default_version & 0xff), (fw_read_version >> 8), (fw_read_version & 0xff));
    if (fw_read_version != fw_default_version) {
        LOG_ERR("ERR:fw_read_version != fw_default_version\n");
        result = (char)(-1);
        goto FLAG_FW_FAIL;
    }
FLAG_FW_FAIL:
    if (!retry && (result < 0 || result >= 0x80)) {
        LOG_ERR("fw update fail, try again...\n");
        result = 0;
        retry = true;
        goto UPDATE_RETRY;
    }
#ifdef INT_SET_EN
    cs_irq_enable(); /*open enit irq.*/
#endif
    return result;
}


/**
  * @brief      firmware high version update
  * @param[in]  *fw_array: point to fw hex array
  * @retval     0:success, -1: fail, 1: no need update
  */
char cs_press_fw_high_version_update(const unsigned char *fw_array)
{
    int ret = 0;
    char result = 0;
    unsigned char read_temp[FW_ONE_BLOCK_LENGTH_R] = {0};
    unsigned short read_version = 0;
    unsigned short default_version = 0;
    char flag_update = 0;   /* 0: no need update fw, 1: need update fw */
    unsigned char retry = RETRY_NUM;

    cs_press_delay_ms(300); /* skip boot jump time */
    /* read ap version */
    ret = cs_press_iic_read(AP_VERSION_REG, read_temp, CS_FW_VERSION_LENGTH);

    if (ret >= 0)
    {
        /* get driver ap version */
        default_version = ((((unsigned short)fw_array[FW_ADDR_VERSION + 0] << 8) & 0xff00) | fw_array[FW_ADDR_VERSION + 1]);
        /* get ic ap version */
        read_version = ((((unsigned short)read_temp[2] << 8) & 0xff00) | read_temp[3]);
        /* compare */
        if (read_version != default_version)
        {
            flag_update = 1;
        }
    }
    else
    {
        flag_update = 1;

        LOG_ERR("read AP_VERSION_REG failed\n");
    }
    if (flag_update == 0)
    {
        LOG_INFO("no need update\n");
        return 1;   /* no need update */
    }
    /* update fw */
    retry = RETRY_NUM;
    do
    {
        result = cs_press_fw_force_update(fw_array);
        if (result < 0 || result >= 0x80) {
            LOG_ERR("update failed, ret:%d\n", result);
        }
    } while ((result != 0)&&(retry--));
    LOG_INFO("update finished, default version:%d,read version:%d\n", default_version,read_version);

    return result;
}

/**
  * @brief    fml_firmware_send_data
  * @param
  * @retval   none
  */
int fml_firmware_send_data(unsigned char *data, int len)
{
    int fw_body_len = 0;
    int fw_len = 0;
    int ret = 0;
    char result = 0;
#if defined(CONFIG_OPLUS_FEATURE_FEEDBACK) || defined(CONFIG_OPLUS_FEATURE_FEEDBACK_MODULE)
    char payload[1024] = {0x00};
#endif

    if (data == NULL) {
        ret = -1;
        LOG_ERR("data buffer null:\n");
        goto exit_fw_buf;
    }
    fw_body_len = ((int)data[FW_ADDR_CODE_LENGTH] << 8) | ((int)data[FW_ADDR_CODE_LENGTH + 1]);
    fw_len = fw_body_len + 256;
    LOG_INFO("[fw file len:%x,fw body len: %x]\n", len, fw_body_len);
    if (fw_body_len <= 0 || fw_body_len > FW_UPDATA_MAX_LEN)
    {
        LOG_ERR("[err!fw body len err!len=%x]\n", len);
        ret = -1;
        goto exit_fw_buf;
    }
    if (fw_len != len)
    {
        LOG_ERR("[err!fw file len err! len=%x]\n", fw_len);
        ret = -1;
        goto exit_fw_buf;
    }
    set_device_updating_flag(1);
    if (g_cs_press.update_type == FORCE_FILE_UPDATE)
    {
        result = cs_press_fw_force_update(data);
        g_cs_press.update_type = HIGH_VER_FILE_UPDATE;
    } else {
        result = cs_press_fw_high_version_update(data);
    }
    set_device_updating_flag(0);
    if (result < 0 || result >= 0x80)
    {
        ret  = -1;
        LOG_ERR("Burning firmware fails\n");
#if defined(CONFIG_OPLUS_FEATURE_FEEDBACK) || defined(CONFIG_OPLUS_FEATURE_FEEDBACK_MODULE)
        memset(payload, 0, 1024);
        scnprintf(payload, sizeof(payload),
                   "NULL$$EventField@@FwUpdate$$FieldData@@FwUpdate$$detailData@@result=0x%x", result);
        LOG_INFO("%s\n", payload);
        oplus_kevent_fb(PSW_BSP_KEYPAD, CS_PRESS_FB_FW_UPDATE_TYPE, payload);
#endif
        g_cs_press.fw_update_error = result;
        g_cs_press.is_update_log = 1;
    } else {
        g_cs_press.is_update_log = 0;
    }
exit_fw_buf:
    msleep(100);
/*
    cs_press_get_noise_var(g_cs_press.dac_noise_var_boot);
    cs_press_get_offset(g_cs_press.dac_offset_boot, NULL);
#if defined(CONFIG_OPLUS_FEATURE_FEEDBACK) || defined(CONFIG_OPLUS_FEATURE_FEEDBACK_MODULE)
    memset(payload, 0, 1024);
    scnprintf(payload, sizeof(payload),
               "NULL$$EventField@@Offset$$FieldData@@Offset$$detailData@@offset[0]=%d,offset[1]=%d",
               g_cs_press.dac_offset_boot[0], g_cs_press.dac_offset_boot[1]);
    LOG_INFO("%s\n", payload);
    oplus_kevent_fb(PSW_BSP_KEYPAD, CS_PRESS_FB_OFFSET_TYPE, payload);
    memset(payload, 0, 1024);
    scnprintf(payload, sizeof(payload),
               "NULL$$EventField@@Noise$$FieldData@@NoiseVar$$detailData@@noiseVar[0]=%d,noiseVar[1]=%d",
               g_cs_press.dac_noise_var_boot[0], g_cs_press.dac_noise_var_boot[1]);
    LOG_INFO("%s\n", payload);
    oplus_kevent_fb(PSW_BSP_KEYPAD, CS_PRESS_FB_NOISE_VAR_TYPE, payload);
#endif
*/
    LOG_INFO("end\n");
    return ret;
}

/**
  * @brief    fml_firmware_config_cb
  * @param
  * @retval   none
  */
static void fml_firmware_config_cb(const struct firmware *cfg, void *ctx)
{
    int fw_error = 0;

    mutex_lock(&press_lock);
    if (g_cs_press.update_done) {
        LOG_INFO("firmware update is done\n");
        goto exit;
    }
    if (cfg)
    {
        fw_error = fml_firmware_send_data((unsigned char*)cfg->data, cfg->size);
        if (fw_error)
        {
            LOG_ERR("firmware send data err:%d\n", fw_error);
            goto err_release_cfg;
        }
    }
err_release_cfg:
    release_firmware(cfg);
    cs_press_delay_ms(10);
    //ftm_boot_mode_check();
    /*camera_key_config_read(&g_cs_press.strength_cfg);*/
    camera_key_para_init();
    cs_press_set_mode(g_cs_press.camera_key_mode);
    g_cs_press.update_done = 1;
exit:
    mutex_unlock(&press_lock);
    LOG_INFO("end\n");
}


/**
  * @brief    firmware update by file
  * @param
  * @retval 0:success, -1: fail,1:no need update
  */
int fml_fw_update_by_file(void)
{
    int ret_error = 0;
    struct cs_press_t *fw_st = NULL;

    fw_st = devm_kzalloc(&(g_cs_press.client->dev), sizeof(*fw_st), GFP_KERNEL);
    if (!fw_st)
    {
        LOG_ERR("devm_kzalloc failed\n");
        return -1;
    }
    fw_st->client = g_cs_press.client;
    ret_error = request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG, FW_FILE_NAME, &(g_cs_press.client->dev), GFP_KERNEL,
                                        fw_st, fml_firmware_config_cb);
    devm_kfree(&(g_cs_press.client->dev), fw_st);
    if (ret_error)
    {
        LOG_ERR("request_firmware_nowait failed:%d\n", ret_error);
        return -1;
    }
    LOG_INFO("end\n");
    return 0;
}

/**
  * @brief       read fw info
  * @param[out]  *fw_info: point to info struct
  * @retval      0:success, -1: fail
  */
char cs_press_read_fw_info(CS_FW_INFO_Def *fw_info)
{
    char ret = 0;
    unsigned char read_temp[FW_ONE_BLOCK_LENGTH_R] = {0};

    if (fw_info == NULL) {
        LOG_ERR("ERR:point fw_info NULL\n");
    }
    cs_press_wakeup_iic();
    /* read fw info*/
    ret |= cs_press_iic_read(AP_VERSION_REG, read_temp, CS_FW_VERSION_LENGTH);  /* FW Version*/
    if (ret == 0)
    {
        fw_info->fw_version = ((unsigned short)read_temp[2]<<8) | read_temp[3];
    }
    return ret;
}

int cs_read_boot_version(unsigned char *version_data)
{
    int ret = 0;

    if (version_data == NULL) {
        LOG_ERR("point version_data is NULL\n");
        return -1;
    }
    cs_press_reset_ic();
    ret = cs_press_iic_read_double_reg(0xF100, version_data, 4);
    return ret;
}

unsigned char check_sum(unsigned char *pbuf, unsigned char len)
{
    unsigned char cs = 0;
    unsigned char i = 0;
    for (i = 0; i < len; i++) {
        cs += pbuf[i];
    }
    return cs;
}

/**
  * @brief  init
  * @param  None
  * @retval 0:success, -1: fail
  */
int cs_press_init(void)
{
    int ret = 0;
    char i;
    unsigned char boot_ver_buf[4];

    LOG_DEBUG("cs driver ver %s\n", cs_driver_ver);
    for (i = 0; i < 3; i++) {
        if (cs_read_boot_version(boot_ver_buf) >= 0) {
            LOG_INFO("boot version %02x %02x %02x %02x\n", boot_ver_buf[0], boot_ver_buf[1],
                                                           boot_ver_buf[2], boot_ver_buf[3]);
            if (boot_ver_buf[0] == 0x71)
            {
                break;
            }
        } else {
            LOG_ERR("read boot version err %d\n",i);
        }
    }
    if (i >= 3)
    {
        LOG_ERR("chipid err return\n");
        g_cs_press.is_update_log = 1;
        g_cs_press.update_done = 1;
        g_cs_press.is_boot_ver_err = true;
        return -1;
    }
    /* reset ic */
    /* check whether need to update ap fw */

    /*****fw updating return****/
    if (get_device_updating_flag()) {
        LOG_ERR("updating, no need update again\n");
        return 0;
    }

    if (g_cs_press.update_done) {
        LOG_ERR("update done, no need update again\n");
        return 0;
    }

    ret = fml_fw_update_by_file();
    /*ret = fml_fw_update_by_array();*/

    if (ret >= 0)
    {
        return 0;
    }
    return ret;
}

/**
  * @brief    check iic function
  * @param
  * @retval
*/
int cs_check_i2c(void)
{
    int retry = 3;
    unsigned char rbuf[2];
    unsigned char addr;
    int len = 0;

    addr = 0x03;
    len = 1;
    rbuf[0] = 0;
    rbuf[1] = 0;

    do {
        if (cs_press_iic_rw_test(0x67) >= 0)
            return 1;
        msleep(50);
        retry--;
        LOG_INFO("read fw ver fail!retry:%d\n", retry);
    } while (retry > 0);

    retry = 3;
    do {
        cs_press_reset_ic();
        msleep(300);
        if (cs_press_iic_rw_test(0x67) >= 0)
            return 1;
        retry--;
        LOG_INFO("reset fw fail!retry:%d\n", retry);
    } while (retry > 0);

    return -1;
}

/**
  * @brief    update function
  * @param
  * @retval
*/

static void update_work_func(struct work_struct *worker)
{
    int ret;
    ret = cs_press_init();
    if (ret < 0) {
        LOG_ERR("press init err\n");
    } else {
        LOG_DEBUG("press init ok\n");
    }
    cancel_delayed_work(&g_cs_press.update_worker);
}

/* procfs api*/
static int cs_proc_fw_info_show(struct seq_file *m,void *v)
{
    char ret = 0;
    unsigned char read_temp[FW_ONE_BLOCK_LENGTH_R] = {0};
    if (get_device_updating_flag()) {
        LOG_ERR("fw updating exit\n");
        seq_printf(m, "fw updating\n");
        return 0;
    }

    cs_press_wakeup_iic();
    ret |= cs_press_iic_read(AP_VERSION_REG, read_temp, CS_FW_VERSION_LENGTH);  /*FW Version*/
    if (ret == 0) {
        seq_printf(m, "ic: CSA37F71\nfw_ver: %d %d %d %d\n",
             read_temp[0], read_temp[1], read_temp[2], read_temp[3]);
    } else {
        seq_printf(m, "read fw info err\n");
    }
    return 0;
}

static int cs_proc_fw_info_open(struct inode *inode, struct file *filp)
{
    if (filp == NULL) {
        return -1;
    }
    return single_open(filp,cs_proc_fw_info_show,NULL);
}


static int cs_proc_local_fw_info_show(struct seq_file *m,void *v)
{
    char ret = 0;
    const struct firmware *fw = NULL;
    //unsigned char *fw_array;

    ret = request_firmware(&fw, FW_FILE_NAME, &(g_cs_press.client->dev));
    if (!ret && fw) {
        //fw_array = fw->data;
        if (g_cs_press.fw_update_error < 0
                || g_cs_press.fw_update_error >= 0x80){
            seq_printf(m,"ic: CSA37F71\nfw_ver: %u %u %u %u\nfw update err: 0x%02x",
                 fw->data[FW_ADDR_VERSION-2], fw->data[FW_ADDR_VERSION-1], fw->data[FW_ADDR_VERSION], fw->data[FW_ADDR_VERSION+1],
                 g_cs_press.fw_update_error);
        } else {
            seq_printf(m,"ic: CSA37F71\nfw_ver: %u %u %u %u\n",
                 fw->data[FW_ADDR_VERSION-2], fw->data[FW_ADDR_VERSION-1], fw->data[FW_ADDR_VERSION], fw->data[FW_ADDR_VERSION+1]);
        }
    } else {
        LOG_ERR("read fw info err, ret=%d\n", ret);
        seq_printf(m, "read fw info err\n");
    }

    if (fw != NULL) {
        release_firmware(fw);
    }
    return 0;
}

static int cs_proc_local_fw_info_open(struct inode *inode, struct file *filp)
{
    if (filp == NULL) {
        return -1;
    }
    return single_open(filp,cs_proc_local_fw_info_show,NULL);
}

static ssize_t cs_proc_fw_file_update_write(struct file *file, const char __user *buf,
                                            size_t count, loff_t *offset)
{
    char *kbuf = NULL;
    int err = 0;

    kbuf = kzalloc(count, GFP_KERNEL);
    if (!kbuf) {
        goto exit;
    }
    if (copy_from_user(kbuf, buf, count)) {
        goto exit_kfree;
    }
    if (kbuf[0] == '2')
    {
        g_cs_press.update_type = FORCE_FILE_UPDATE;
    } else
    {
        g_cs_press.update_type = HIGH_VER_FILE_UPDATE;
    }
    g_cs_press.update_done = 0;
    err = fml_fw_update_by_file();
    if (err == 0)
        LOG_DEBUG("pass!\n");
    else
        LOG_ERR("%d,failed!\n", err);

exit_kfree:
    kfree(kbuf);
exit:
    return count;
}

static int cs_read_boot_version_show(struct seq_file *m, void *v)
{
    char result = 0;

    unsigned char boot_ver_buf[4];

    result = cs_read_boot_version(boot_ver_buf);
    if (result == 0) {
        seq_printf(m, "%02X %02X %02X %02X\n", boot_ver_buf[0],boot_ver_buf[1],boot_ver_buf[2],boot_ver_buf[3]);
    } else {
        seq_printf(m, "ERR\n");
    }
    return 0;
}

#define CS_BOOT_VERSION_HEADER  0x71
static int cs_auto_test_show(struct seq_file *m, void *v)
{
    unsigned char reg_rw[FW_ONE_BLOCK_LENGTH_R] = { 0 };
    int i = 0;
    int retry = 100;
    int result = -1;
    uint8_t len = 0;
    uint16_t status = 0;
    char ret = 0;
    bool need_fw_update = false;
    int update_wait = 200;
    unsigned char read_temp[FW_ONE_BLOCK_LENGTH_R] = {0};
/*
    unsigned char boot_ver_buf[4];

    result = cs_read_boot_version(boot_ver_buf);

    LOG_INFO("boot_ver: %02X %02X %02X %02X\n", boot_ver_buf[0], boot_ver_buf[1], boot_ver_buf[2], boot_ver_buf[3]);
*/
    //cs_press_wakeup_iic();
    while (get_device_updating_flag() && update_wait) {
        LOG_INFO("updating now, wait for finish...\n");
        msleep(500);
        update_wait--;
    }

    ret = cs_press_iic_read(AP_VERSION_REG, read_temp, CS_FW_VERSION_LENGTH);  /*FW Version*/
    if (ret == 0) {
        LOG_INFO("fw_ver: %d %d %d %d\n",
             read_temp[0], read_temp[1], read_temp[2], read_temp[3]);
        if (!read_temp[0] && !read_temp[1]
                && !read_temp[2] && !read_temp[3]) {
            LOG_ERR("read fw info all zero!!\n");
            need_fw_update = true;
        }
    } else {
        LOG_ERR("read fw info err\n");
        need_fw_update = true;
    }

    if (need_fw_update) {
        g_cs_press.update_done = 0;
        g_cs_press.update_type = FORCE_FILE_UPDATE;
        ret = fml_fw_update_by_file();
        if (ret == 0) {
            LOG_INFO("fw update success!\n");
        } else {
            LOG_ERR("fw update %d, failed!\n", ret);
        }
    }

    while (!g_cs_press.update_done && update_wait) {
        msleep(100);
        update_wait--;
    }
    if (!g_cs_press.update_done) {
        LOG_ERR("wait fw update timeout, continue...\n");
    }

    memset(reg_rw, 0x00, FW_ONE_BLOCK_LENGTH_R);
    ret = cs_press_iic_write(0xFB, reg_rw, 1);
    if (ret < 0) {
        LOG_ERR("IIC write 0xFB 0x%02x failed!!\n", reg_rw[0]);
        goto exit;
    }

    memset(reg_rw, 0x00, FW_ONE_BLOCK_LENGTH_R);
    ret = cs_press_iic_write(0xFC, reg_rw, 1);
    if (ret < 0) {
        LOG_ERR("IIC write 0xFC 0x%02x failed!!\n", reg_rw[0]);
        goto exit;
    }

    memset(reg_rw, 0x00, FW_ONE_BLOCK_LENGTH_R);
    reg_rw[0] = 0xA4;

    ret = cs_press_iic_write(0xFB, reg_rw, 1);
    if (ret < 0) {
        LOG_ERR("IIC write 0xFB 0x%02x failed!!\n", reg_rw[0]);
        goto exit;
    }

    memset(reg_rw, 0x00, FW_ONE_BLOCK_LENGTH_R);
    while (!reg_rw[0] && retry) {
        retry--;
        cs_press_delay_ms(2);
        ret = cs_press_iic_read(0xFC, reg_rw, 1);
        if (ret < 0) {
            LOG_ERR("%s: IIC read 0xFC failed!!\n", __func__);
            goto exit;
        }
        LOG_INFO("read 0xFC 0x%02x\n", reg_rw[0]);
    }
    len = (uint8_t)reg_rw[0];
    if (!len || (len > FW_ONE_BLOCK_LENGTH_R)) {
        LOG_ERR("%s: len=%u invalid!!\n", __func__, len);
        goto exit;
    }
    memset(reg_rw, 0x00, FW_ONE_BLOCK_LENGTH_R);
    ret = cs_press_iic_read(0xFD, reg_rw, len);
    if (ret < 0) {
        LOG_ERR("%s: IIC read 0xFD failed!!\n", __func__);
        goto exit;
    }
    status = (uint16_t)(reg_rw[0] + (reg_rw[1] << 8));
    LOG_INFO("channel broken status: 0x%02x\n", status);
    for (i = 1; i < len / 2 - 1; i++) {
        LOG_DEBUG("word[%d]: 0x%02x\n", i, (uint16_t)(reg_rw[i * 2] + (reg_rw[i * 2 + 1] << 8)));
    }
    for (i = 0; i < CH_COUNT; i++) {
        if (status & (0x01 << i)) {
            LOG_ERR("auto test FAILED, channel %d broken!!\n", i + 1);
            goto exit;
        }
    }
    LOG_INFO("auto test PASS\n");
    result = 0;

    memset(reg_rw, 0x00, FW_ONE_BLOCK_LENGTH_R);
    ret = cs_press_iic_write(0xFB, reg_rw, 1);
    if (ret < 0) {
        LOG_ERR("IIC write 0xFB 0x%02x failed!!\n", reg_rw[0]);
    }

    memset(reg_rw, 0x00, FW_ONE_BLOCK_LENGTH_R);
    ret = cs_press_iic_write(0xFC, reg_rw, 1);
    if (ret < 0) {
        LOG_ERR("IIC write 0xFC 0x%02x failed!!\n", reg_rw[0]);
    }
exit:
    if (result == 0) {
        seq_printf(m, "OK\n");
    } else {
        seq_printf(m, "NG\n");
    }
    return 0;
}

static ssize_t cs_proc_game_key_mode_write(struct file *file, const char __user *buf,
                                            size_t count, loff_t *offset)
{
    int tempdata = 0;
    char *kbuf = NULL;

    if (!buf) {
        LOG_ERR("buff is null!\n");
        goto exit_flag;
    }
    if ((count <= 0) || (count > 4)) {
        LOG_ERR("argument err\n");
        goto exit_flag;
    }
    kbuf = kzalloc(count + 1, GFP_KERNEL);
    if (!kbuf) {
        goto exit_kfree;
    }

    if (copy_from_user(kbuf, buf, count)) {
        goto exit_kfree;
    }

    kbuf[count] = '\0';
    LOG_INFO("%s: kbuf=%s, count=%zu\n", __func__, kbuf, count);
    if (!sscanf(kbuf, "%d", &tempdata)) {
        LOG_ERR("%s: sscanf error.\n", __func__);
        goto exit_kfree;
    }

    mutex_lock(&press_lock);

    LOG_INFO("%s: %d --> %d\n", __func__, g_cs_press.camera_key_mode, tempdata);
    g_cs_press.camera_key_mode = tempdata;
    if (g_cs_press.is_suspended) {
        cs_press_set_mode(g_cs_press.camera_key_mode < GAME_KEY_DEFAULT_MODE ? CAMERA_KEY_SLEEP_MODE : GAME_KEY_SLEEP_MODE);
    } else {
        cs_press_set_mode(g_cs_press.camera_key_mode);
    }

    mutex_unlock(&press_lock);
    /*}*/
exit_kfree:
    kfree(kbuf);
exit_flag:
    return count;
}


static ssize_t cs_proc_camera_key_config_write(struct file *file, const char __user *buf,
                                            size_t count, loff_t *offset)
{
    char *kbuf = NULL;
    unsigned int value[STRENGTH_CFG_NUM] = { 0 };
    int ret = 0;

    if (count <= 0) {
        LOG_ERR("argument err\n");
        goto exit_flag;
    }
    kbuf = kzalloc(count + 1, GFP_KERNEL);
    if (!kbuf) {
        goto exit_flag;
    }
    if (copy_from_user(kbuf, buf, count)) {
        goto exit_kfree;
    }
    kbuf[count] = '\0';

    mutex_lock(&press_lock);
    ret = sscanf(kbuf, "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
            &value[0], &value[1], &value[2], &value[3], &value[4], &value[5], &value[6],
            &value[7], &value[8], &value[9], &value[10], &value[11], &value[12], &value[13]);

    if (ret == STRENGTH_CFG_NUM) {
        LOG_INFO("%s: value is [%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u].\n", __func__,
                value[0], value[1], value[2], value[3], value[4], value[5], value[6],
                value[7], value[8], value[9], value[10], value[11], value[12], value[13]);

        g_cs_press.strength_cfg.tap_1_down_strength = value[0] / STRENGTH_PER_LEVEL;
        g_cs_press.strength_cfg.tap_1_up_strength = value[1] / STRENGTH_PER_LEVEL;
        g_cs_press.strength_cfg.tap_2_down_strength = value[2] / STRENGTH_PER_LEVEL;
        g_cs_press.strength_cfg.tap_2_up_strength = value[3] / STRENGTH_PER_LEVEL;
        g_cs_press.strength_cfg.tap_3_down_strength = value[4] / STRENGTH_PER_LEVEL;
        g_cs_press.strength_cfg.tap_3_up_strength = value[5] / STRENGTH_PER_LEVEL;
        g_cs_press.strength_cfg.tap_4_down_strength = value[6] / STRENGTH_PER_LEVEL;
        g_cs_press.strength_cfg.tap_4_up_strength = value[7] / STRENGTH_PER_LEVEL;
        g_cs_press.strength_cfg.tap_5_down_strength = value[8] / STRENGTH_PER_LEVEL;
        g_cs_press.strength_cfg.tap_5_up_strength = value[9] / STRENGTH_PER_LEVEL;
        g_cs_press.strength_cfg.area_1 = value[10];
        g_cs_press.strength_cfg.area_2 = value[11];
        g_cs_press.strength_cfg.long_tap_judge_time = value[12] / TIME_MS_PER_LEVEL;
        g_cs_press.strength_cfg.muti_tap_judge_time = value[13] / TIME_MS_PER_LEVEL;

        if (g_cs_press.camera_key_mode != GAME_KEY_GAME_MODE) {
            cs_press_set_trigger_strength(g_cs_press.strength_cfg);
        }
    } else {
        LOG_ERR("%s: sscanf error.\n", __func__);
    }
    mutex_unlock(&press_lock);
exit_kfree:
    kfree(kbuf);
exit_flag:
    return count;
}

static ssize_t cs_proc_game_key_config_write(struct file *file, const char __user *buf,
                                            size_t count, loff_t *offset)
{
    char *kbuf = NULL;
    unsigned int value[STRENGTH_CFG_NUM] = { 0 };
    int ret = 0;

    if (count <= 0) {
        LOG_ERR("argument err\n");
        goto exit_flag;
    }
    kbuf = kzalloc(count + 1, GFP_KERNEL);
    if (!kbuf) {
        goto exit_flag;
    }
    if (copy_from_user(kbuf, buf, count)) {
        goto exit_kfree;
    }
    kbuf[count] = '\0';

    mutex_lock(&press_lock);
    ret = sscanf(kbuf, "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
            &value[0], &value[1], &value[2], &value[3], &value[4], &value[5], &value[6],
            &value[7], &value[8], &value[9], &value[10], &value[11], &value[12], &value[13]);

    if (ret == STRENGTH_CFG_NUM) {
        LOG_INFO("%s: value is [%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u].\n", __func__,
                value[0], value[1], value[2], value[3], value[4], value[5], value[6],
                value[7], value[8], value[9], value[10], value[11], value[12], value[13]);

        g_cs_press.game_mode_strength_cfg.tap_1_down_strength = value[0] / STRENGTH_PER_LEVEL;
        g_cs_press.game_mode_strength_cfg.tap_1_up_strength = value[1] / STRENGTH_PER_LEVEL;
        g_cs_press.game_mode_strength_cfg.tap_2_down_strength = value[2] / STRENGTH_PER_LEVEL;
        g_cs_press.game_mode_strength_cfg.tap_2_up_strength = value[3] / STRENGTH_PER_LEVEL;
        g_cs_press.game_mode_strength_cfg.tap_3_down_strength = value[4] / STRENGTH_PER_LEVEL;
        g_cs_press.game_mode_strength_cfg.tap_3_up_strength = value[5] / STRENGTH_PER_LEVEL;
        g_cs_press.game_mode_strength_cfg.tap_4_down_strength = value[6] / STRENGTH_PER_LEVEL;
        g_cs_press.game_mode_strength_cfg.tap_4_up_strength = value[7] / STRENGTH_PER_LEVEL;
        g_cs_press.game_mode_strength_cfg.tap_5_down_strength = value[8] / STRENGTH_PER_LEVEL;
        g_cs_press.game_mode_strength_cfg.tap_5_up_strength = value[9] / STRENGTH_PER_LEVEL;
        g_cs_press.game_mode_strength_cfg.area_1 = value[10];
        g_cs_press.game_mode_strength_cfg.area_2 = value[11];
        g_cs_press.game_mode_strength_cfg.long_tap_judge_time = value[12] / TIME_MS_PER_LEVEL;
        g_cs_press.game_mode_strength_cfg.muti_tap_judge_time = value[13] / TIME_MS_PER_LEVEL;

        if (g_cs_press.camera_key_mode == GAME_KEY_GAME_MODE) {
            cs_press_set_trigger_strength(g_cs_press.game_mode_strength_cfg);
        }
    } else {
        LOG_ERR("%s: sscanf error.\n", __func__);
    }
    mutex_unlock(&press_lock);
exit_kfree:
    kfree(kbuf);
exit_flag:
    return count;
}

static ssize_t cs_proc_camera_key_report_write(struct file *file, const char __user *buf,
                                            size_t count, loff_t *offset)
{
    char *kbuf = NULL;
    unsigned int key_code = 0;
    unsigned int key_action = 0;
    unsigned int distance = 0;

    if (count <= 0) {
        LOG_ERR("argument err\n");
        goto exit_flag;
    }
    kbuf = kzalloc(count, GFP_KERNEL);
    if (!kbuf) {
        goto exit_flag;
    }
    if (copy_from_user(kbuf, buf, count)) {
        goto exit_kfree;
    }

    if (!sscanf(kbuf, "%u,%u,%u",
                    &key_code, &key_action, &distance)) {
        LOG_ERR("%s: sscanf error.\n", __func__);
        goto exit_kfree;
    }

    if ((key_code < KEY_LIGHT_TAP) || (key_code > KEY_SWIPE_DOWN)) {
        LOG_ERR("%s: invalid keycode %u.\n", __func__, key_code);
        goto exit_kfree;
    }

    mutex_lock(&press_lock);
    LOG_INFO("%s: report [%u, %u, %u].\n",
                    __func__, key_code, key_action, distance);

    if (key_action) {
        input_report_key(cs_input_dev, key_code, 1);
        if ((key_code == KEY_SWIPE_UP) || (key_code == KEY_SWIPE_DOWN)) {
            input_report_abs(cs_input_dev, ABS_DISTANCE, distance);
        }
        input_sync(cs_input_dev);
    }
    if (!(key_action & 0x01)) {
        input_report_key(cs_input_dev, key_code, 0);
        if ((key_code == KEY_SWIPE_UP) || (key_code == KEY_SWIPE_DOWN)) {
            input_report_abs(cs_input_dev, ABS_DISTANCE, 0);
        }
        input_sync(cs_input_dev);
    }
    mutex_unlock(&press_lock);

exit_kfree:
    kfree(kbuf);
exit_flag:
    return count;
}

static ssize_t cs_proc_quick_on_close_write(struct file *file, const char __user *buf,
                                            size_t count, loff_t *offset)
{
    int tempdata = 0;
    char *kbuf = NULL;

    if (!buf) {
        LOG_ERR("buff is null!\n");
        goto exit_flag;
    }
    if ((count <= 0) || (count > 4)) {
        LOG_ERR("argument err\n");
        goto exit_flag;
    }
    kbuf = kzalloc(count + 1, GFP_KERNEL);
    if (!kbuf) {
        goto exit_kfree;
    }

    if (copy_from_user(kbuf, buf, count)) {
        goto exit_kfree;
    }

    kbuf[count] = '\0';
    LOG_INFO("%s: kbuf=%s, count=%zu\n", __func__, kbuf, count);
    if (!sscanf(kbuf, "%d", &tempdata)) {
        LOG_ERR("%s: sscanf error.\n", __func__);
        goto exit_kfree;
    }

    mutex_lock(&press_lock);

    LOG_INFO("%s: %d --> %d\n", __func__, g_cs_press.quick_on_closed, tempdata);
    g_cs_press.quick_on_closed = !!tempdata;
    if (g_cs_press.is_suspended) {
        cs_press_set_mode(g_cs_press.camera_key_mode < GAME_KEY_DEFAULT_MODE ? CAMERA_KEY_SLEEP_MODE : GAME_KEY_SLEEP_MODE);
    } else {
        cs_press_set_mode(g_cs_press.camera_key_mode);
    }

    mutex_unlock(&press_lock);
exit_kfree:
    kfree(kbuf);
exit_flag:
    return count;
}

static ssize_t cs_proc_gpio_control_write(struct file *file, const char __user *buf,
                                            size_t count, loff_t *offset)
{
    char *kbuf = NULL;
    unsigned int en = 0;
    int ret = 0;

    if (count <= 0) {
        LOG_ERR("argument err\n");
        goto exit_flag;
    }
    kbuf = kzalloc(count + 1, GFP_KERNEL);
    if (!kbuf) {
        goto exit_flag;
    }
    if (copy_from_user(kbuf, buf, count)) {
        goto exit_kfree;
    }

    if (sscanf(kbuf, "hard_reset %u", &en)) {
        if (en) {
            cs_press_rstpin_high();
            LOG_INFO("%s: hard_reset gpio set high\n", __func__);
        } else {
            cs_press_rstpin_low();
            LOG_INFO("%s: hard_reset gpio set low\n", __func__);
        }
    } else if (strstr(kbuf, "soft_reset")) {
        cs_press_soft_reset_device();
    } else if (strstr(kbuf, "wdt_reset")) {
        cs_press_wdt_reset_device();
    } else if (sscanf(kbuf, "irq %u", &en)) {
        if (en) {
            cs_irq_enable();
            LOG_INFO("%s: irq enable\n", __func__);
        } else {
            cs_irq_disable();
            LOG_INFO("%s: irq disable\n", __func__);
        }
    } else if (sscanf(kbuf, "power %u", &en)) {
        if (en) {
            if (g_cs_press.vdd_2v8 != NULL) {
                ret = regulator_enable(g_cs_press.vdd_2v8);
                if (!ret) {
                    LOG_INFO("%s: vdd_2v8 enable success\n", __func__);
                    goto exit_kfree;
                }
            }
            LOG_ERR("%s: vdd_2v8 enable fail, ret=%d\n", __func__, ret);
        } else {
            if (g_cs_press.vdd_2v8 != NULL) {
                ret = regulator_disable(g_cs_press.vdd_2v8);
                if (!ret) {
                    LOG_INFO("%s: vdd_2v8 disable success\n", __func__);
                    goto exit_kfree;
                }
            }
            LOG_ERR("%s: vdd_2v8 diable fail, ret=%d\n", __func__, ret);
        }
    }
exit_kfree:
    kfree(kbuf);
exit_flag:
    return count;
}

/**
 * @brief Reset all statistics counters
 */
static void cs_press_reset_all_statistics(void)
{
    int i, j;

    g_cs_press.hard_reset_cnt = 0;
    g_cs_press.soft_reset_cnt = 0;
    g_cs_press.wdt_reset_cnt = 0;
    g_cs_press.tap_force_min = 0;
    g_cs_press.tap_force_max = 0;
    g_cs_press.swipe_force_min = 0;
    g_cs_press.swipe_force_max = 0;
    g_cs_press.double_tap_cnt = 0;
    g_cs_press.bus_error_cnt = 0;
    g_cs_press.fw_update_error = 0;
    for (i = 0; i < CH_COUNT; i++) {
        for (j = 0; j < LAG_MIN_COUNT; j++) {
            g_cs_press.heavy_tap_lag[i][j] = 0;
        }
    }
    g_cs_press.long_press_over_10min_cnt = 0;
    g_cs_press.long_press_max_duration_ms = 0;
    g_cs_press.is_press_active = false;
    g_cs_press.is_press_over_10min = false;
    g_cs_press.cur_down_max_force = 0;
    g_cs_press.total_down_max_force = 0;
    memset(g_cs_press.down_max_force, 0, sizeof(g_cs_press.down_max_force));
    for (i = 0; i < EVENT_STATISTICS_ARRAY_SIZE; i++) {
        g_cs_press.event_total_cnt[i] = 0;
        g_cs_press.suspend_abnormal_event_cnt[i] = 0;
    }
    memset(g_cs_press.mode_type_mismatch_cnt, 0, sizeof(g_cs_press.mode_type_mismatch_cnt));
    g_cs_press.none_mode_interrupt_cnt = 0;
}

static ssize_t cs_proc_health_monitor_write(struct file *file, const char __user *buf,
                                            size_t count, loff_t *offset)
{
    int tempdata = 0;
    char *kbuf = NULL;

    if (!buf || (count <= 0) || (count > 4)) {
        LOG_ERR("buff is null or argument err\n");
        goto exit_flag;
    }
    kbuf = kzalloc(count + 1, GFP_KERNEL);
    if (!kbuf) {
        goto exit_flag;
    }

    if (copy_from_user(kbuf, buf, count)) {
        goto exit_kfree;
    }

    kbuf[count] = '\0';
    LOG_INFO("%s: kbuf=%s, count=%zu\n", __func__, kbuf, count);
    if (!sscanf(kbuf, "%d", &tempdata)) {
        LOG_ERR("%s: sscanf error.\n", __func__);
        goto exit_kfree;
    }

    if (!tempdata) {
        cs_press_reset_all_statistics();
    }
exit_kfree:
    kfree(kbuf);
exit_flag:
    return count;
}

static ssize_t cs_proc_probe_status_write(struct file *file, const char __user *buf,
		size_t count, loff_t *offset)
{
    char kbuf[5] = { 0 };
    int tmp = 0;

    if (!buf) {
        LOG_ERR("buff is null!\n");
        return count;
    }
    if ((count <= 0) || (count > 4)) {
        LOG_ERR("argument err\n");
        return count;
    }

    if (copy_from_user(kbuf, buf, count)) {
        return count;
    }
    if (kstrtoint(kbuf, 10, &tmp)) {
        LOG_ERR("%s: kstrtoint error\n", __func__);
        return count;
    }
    g_cs_press.is_update_log = !!tmp;
    LOG_ERR("is_update_log is %d\n", g_cs_press.is_update_log);

    return count;
}


static ssize_t cs_proc_keep_irq_report_write(struct file *file, const char __user *buf,
		size_t count, loff_t *offset)
{
    char kbuf[5] = { 0 };
    int tmp = 0;

    if (!buf) {
        LOG_ERR("buff is null!\n");
        return count;
    }
    if ((count <= 0) || (count > 4)) {
        LOG_ERR("argument err\n");
        return count;
    }

    if (copy_from_user(kbuf, buf, count)) {
        return count;
    }
    if (kstrtoint(kbuf, 10, &tmp)) {
        LOG_ERR("%s: kstrtoint error\n", __func__);
        return count;
    }
    g_cs_press.keep_irq_report = !!tmp;
    LOG_ERR("keep_irq_report is %d\n", g_cs_press.keep_irq_report);

    cs_press_keep_irq_report(g_cs_press.keep_irq_report);

    return count;
}

static int cs_proc_game_key_mode_show(struct seq_file *m, void *v)
{
    char ret = 0;
    seq_printf(m, "%d", g_cs_press.camera_key_mode);
    return ret;
}

static int cs_proc_camera_key_config_show(struct seq_file *m, void *v)
{
    char ret = 0;
    trigger_strength_config strength_cfg;

#ifdef CAMERA_KEY
    camera_key_config_read(&strength_cfg);
    seq_printf(m, "tap_strength[%u,%u][%u,%u][%u,%u][%u,%u][%u,%u], area[%u,%u], judge_time[%u,%u]\n",
        strength_cfg.tap_1_down_strength * STRENGTH_PER_LEVEL,
        strength_cfg.tap_1_up_strength * STRENGTH_PER_LEVEL,
        strength_cfg.tap_2_down_strength * STRENGTH_PER_LEVEL,
        strength_cfg.tap_2_up_strength * STRENGTH_PER_LEVEL,
        strength_cfg.tap_3_down_strength * STRENGTH_PER_LEVEL,
        strength_cfg.tap_3_up_strength * STRENGTH_PER_LEVEL,
        strength_cfg.tap_4_down_strength * STRENGTH_PER_LEVEL,
        strength_cfg.tap_4_up_strength * STRENGTH_PER_LEVEL,
        strength_cfg.tap_5_down_strength * STRENGTH_PER_LEVEL,
        strength_cfg.tap_5_up_strength * STRENGTH_PER_LEVEL,
        strength_cfg.area_1, strength_cfg.area_2,
        strength_cfg.long_tap_judge_time * TIME_MS_PER_LEVEL,
        strength_cfg.muti_tap_judge_time * TIME_MS_PER_LEVEL);
#endif
    return ret;
}

static int cs_proc_game_key_config_show(struct seq_file *m, void *v)
{
    char ret = 0;

#ifdef CAMERA_KEY
    seq_printf(m, "tap_strength[%u,%u][%u,%u][%u,%u][%u,%u][%u,%u], area[%u,%u], judge_time[%u,%u]\n",
        g_cs_press.game_mode_strength_cfg.tap_1_down_strength * STRENGTH_PER_LEVEL,
        g_cs_press.game_mode_strength_cfg.tap_1_up_strength * STRENGTH_PER_LEVEL,
        g_cs_press.game_mode_strength_cfg.tap_2_down_strength * STRENGTH_PER_LEVEL,
        g_cs_press.game_mode_strength_cfg.tap_2_up_strength * STRENGTH_PER_LEVEL,
        g_cs_press.game_mode_strength_cfg.tap_3_down_strength * STRENGTH_PER_LEVEL,
        g_cs_press.game_mode_strength_cfg.tap_3_up_strength * STRENGTH_PER_LEVEL,
        g_cs_press.game_mode_strength_cfg.tap_4_down_strength * STRENGTH_PER_LEVEL,
        g_cs_press.game_mode_strength_cfg.tap_4_up_strength * STRENGTH_PER_LEVEL,
        g_cs_press.game_mode_strength_cfg.tap_5_down_strength * STRENGTH_PER_LEVEL,
        g_cs_press.game_mode_strength_cfg.tap_5_up_strength * STRENGTH_PER_LEVEL,
        g_cs_press.game_mode_strength_cfg.area_1, g_cs_press.game_mode_strength_cfg.area_2,
        g_cs_press.game_mode_strength_cfg.long_tap_judge_time * TIME_MS_PER_LEVEL,
        g_cs_press.game_mode_strength_cfg.muti_tap_judge_time * TIME_MS_PER_LEVEL);
#endif
    return ret;
}

static int cs_proc_quick_on_close_show(struct seq_file *m, void *v)
{
    char ret = 0;
    seq_printf(m, "%d", g_cs_press.quick_on_closed);
    return ret;
}

static int cs_proc_health_monitor_show(struct seq_file *m, void *v)
{
    seq_printf(m, "ic:CSA37F71\n");
    cs_press_wakeup_iic();
    cs_press_output_fw_version(m);
    cs_press_output_offset_info(m);
    cs_press_output_noise_var_info(m);
    cs_press_output_basic_stats(m);
    cs_press_output_reset_error_counts(m);
    seq_printf(m, "long_press_max_duration_ms:%lu\n", g_cs_press.long_press_max_duration_ms);
    if (g_cs_press.long_press_over_10min_cnt) {
        seq_printf(m, "long_press_over_10min_cnt:%lu\n", g_cs_press.long_press_over_10min_cnt);
    }
    cs_press_output_force_statistics(m);
    cs_press_output_event_statistics(m);
    cs_press_output_device_status(m);
    cs_press_output_mode_mismatch_stats(m);
    return 0;
}

static int cs_proc_probe_status_show(struct seq_file *m, void *v)
{
    char ret = 0;
    seq_printf(m, "%d", g_cs_press.is_update_log);
    return ret;
}

static int cs_proc_keep_irq_report_show(struct seq_file *m, void *v)
{
    char ret = 0;
    seq_printf(m, "%d", g_cs_press.keep_irq_report);
    return ret;
}

static int cs_proc_read_boot_version_open(struct inode *inode, struct file *filp)
{
    return single_open(filp,cs_read_boot_version_show,NULL);
}

static int cs_proc_auto_test_open(struct inode *inode, struct file *filp)
{
    return single_open(filp,cs_auto_test_show,NULL);
}

static int cs_proc_game_key_mode_open(struct inode *inode, struct file *filp)
{
    return single_open(filp, cs_proc_game_key_mode_show, pde_data(inode));
}

static int cs_proc_camera_key_config_open(struct inode *inode, struct file *filp)
{
    return single_open(filp, cs_proc_camera_key_config_show, pde_data(inode));
}

static int cs_proc_game_key_config_open(struct inode *inode, struct file *filp)
{
    return single_open(filp, cs_proc_game_key_config_show, pde_data(inode));
}

static int cs_proc_quick_on_close_open(struct inode *inode, struct file *filp)
{
    return single_open(filp, cs_proc_quick_on_close_show, pde_data(inode));
}

static int cs_proc_health_monitor_open(struct inode *inode, struct file *filp)
{
    return single_open(filp, cs_proc_health_monitor_show, pde_data(inode));
}

static int cs_proc_probe_status_open(struct inode *inode, struct file *filp)
{
    return single_open(filp, cs_proc_probe_status_show, pde_data(inode));
}

static int cs_proc_keep_irq_report_open(struct inode *inode, struct file *filp)
{
    return single_open(filp, cs_proc_keep_irq_report_show, pde_data(inode));
}

static int cs_proc_show(struct seq_file *m,void *v)
{
    return 0;
}

static int cs_proc_open(struct inode *inode, struct file *filp)
{
    return single_open(filp,cs_proc_show,NULL);
}

static ssize_t cs_proc_write(struct file *file, const char __user *buf,
                             size_t count, loff_t *offset)
{
    char *kbuf = NULL;

    kbuf = kzalloc(count, GFP_KERNEL);
    if (!kbuf) {
        goto exit;
    }
    if (copy_from_user(kbuf, buf, count)) {
        goto exit_kfree;
    }
exit_kfree:
    kfree(kbuf);
exit:
    return count;
}

/**
  * @brief    dts parse
  * @param
  * @retval
*/
int cs_parse_dts(struct i2c_client *pdev)
{
    int ret = -1;
#ifdef INT_SET_EN

    cs_press_irq_gpio = of_get_named_gpio((pdev->dev).of_node, "irq-gpio",0);
    if (!gpio_is_valid(cs_press_irq_gpio))
    {
        dev_err(&pdev->dev, "cs_press request_irq IRQ fail");
    }
    else
    {
        ret = gpio_request(cs_press_irq_gpio, "irq-gpio");
        if (ret)
        {
            dev_err(&pdev->dev, "cspress request_irq IRQ fail !, ret=%d.\n", ret);
        }
        ret = gpio_direction_input(cs_press_irq_gpio);
        msleep(50);
        cs_press_irq_num = gpio_to_irq(cs_press_irq_gpio);
        ret = request_irq(cs_press_irq_num,
          (irq_handler_t)cs_press_interrupt_handler,
          IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
          "CS_PRESS-eint", g_cs_press.device_irq);
        if (ret > 0) {
            ret = -1;
            dev_err(&pdev->dev, "cs_press request_irq IRQ fail !, ret=%d.\n", ret);
        }
    }
#endif
    g_cs_press.rst_gpio = of_get_named_gpio((pdev->dev).of_node, "reset-gpio", 0);
    if (!gpio_is_valid(g_cs_press.rst_gpio))
    {
        dev_err(&pdev->dev, "cs_press request_rst fail");
    } else {
        ret = gpio_request(g_cs_press.rst_gpio, "reset-gpio");
            if (ret)
            {
                dev_err(&pdev->dev, "cs_press request rst fail !, ret=%d.\n", ret);
            }
            ret = gpio_direction_output(g_cs_press.rst_gpio,0);
            dev_err(&pdev->dev, "gpio_direction_output set 0:rst_gpio=%d, ret=%d.\n", g_cs_press.rst_gpio, ret);
            msleep(50);
        gpio_set_value(g_cs_press.rst_gpio, 0);
    }

    g_cs_press.power_gpio = of_get_named_gpio((pdev->dev).of_node, "enable1v8_gpio", 0);
    if (!gpio_is_valid(g_cs_press.power_gpio))
    {
        dev_err(&pdev->dev, "cs_press enable1v8_gpio fail");
    } else {
        ret = gpio_request(g_cs_press.power_gpio, "enable1v8_gpio");
            if (ret)
            {
                dev_err(&pdev->dev, "cs_press request enable1v8_gpio fail !, ret=%d.\n", ret);
            }
            ret = gpio_direction_output(g_cs_press.power_gpio,0);
            msleep(50);
        gpio_set_value(g_cs_press.power_gpio, 1);
    }

    g_cs_press.vdd_2v8 = regulator_get(&pdev->dev, "vdd_2v8");
    if (g_cs_press.vdd_2v8 != NULL) {
        dev_err(&pdev->dev, "%s:vdd_2v8 is not NULL\n", __func__);
        ret = regulator_enable(g_cs_press.vdd_2v8);
        if (ret)
            dev_err(&pdev->dev, "%s: vdd_2v8 enable fail\n", __func__);
    } else {
            dev_err(&pdev->dev, "%s: vdd_2v8 is NULL\n", __func__);
    }
    LOG_ERR("end---\n");
    return 0;
}
/**
  * @brief    free resource from dts info
  * @param
  * @retval 0:success, -1: fail
  */
void cs_unregister_dts(void)
{
    /*GPIO unregister*/
    if (gpio_is_valid(g_cs_press.rst_gpio)) {
        gpio_free(g_cs_press.rst_gpio);
        LOG_DEBUG("reset gpio free\n");
    }
    #ifdef INT_SET_EN
    if (gpio_is_valid(cs_press_irq_gpio)) {
        free_irq(cs_press_irq_num,g_cs_press.device_irq);
        gpio_free(cs_press_irq_gpio);
        LOG_DEBUG("irq gpio free\n");
        g_cs_press.device_irq = NULL;
    }
    #endif
}

static const char proc_list[PROC_FOPS_NUM][PROC_NAME_LEN]={
    "fw_info",
    "fw_update_file",
    "read_boot_version",
    "auto_test",
    "game_key_mode",
    "camera_key_config",
    "game_key_config",
    "camera_key_report",
    "quick_on_close",
    "gpio_control",
    "local_fw_info",
    "health_info",
    "probe_status",
    "keep_irq_report",
};

#ifndef ALIENTEK
static const struct proc_ops proc_fops[PROC_FOPS_NUM] = {
#else
static const struct file_operations proc_fops[PROC_FOPS_NUM] = {
#endif
    FOPS_ARRAY(cs_proc_fw_info_open, NULL),    /*fw_info*/
    FOPS_ARRAY(cs_proc_open, cs_proc_fw_file_update_write),
    FOPS_ARRAY(cs_proc_read_boot_version_open, cs_proc_write),
    FOPS_ARRAY(cs_proc_auto_test_open, cs_proc_write),
    FOPS_ARRAY(cs_proc_game_key_mode_open, cs_proc_game_key_mode_write),
    FOPS_ARRAY(cs_proc_camera_key_config_open, cs_proc_camera_key_config_write),
    FOPS_ARRAY(cs_proc_game_key_config_open, cs_proc_game_key_config_write),
    FOPS_ARRAY(cs_proc_open, cs_proc_camera_key_report_write),
    FOPS_ARRAY(cs_proc_quick_on_close_open, cs_proc_quick_on_close_write),
    FOPS_ARRAY(cs_proc_open, cs_proc_gpio_control_write),
    FOPS_ARRAY(cs_proc_local_fw_info_open, NULL),    /*local_fw_info*/
    FOPS_ARRAY(cs_proc_health_monitor_open, cs_proc_health_monitor_write),
    FOPS_ARRAY(cs_proc_probe_status_open, cs_proc_probe_status_write),
    FOPS_ARRAY(cs_proc_keep_irq_report_open, cs_proc_keep_irq_report_write),
};
/**
  * @brief  cs_sys_create
  * @param
  * @retval
*/
static int cs_procfs_create(void)
{
    int ret = 0;
    int i = 0;
    struct proc_dir_entry *file;

    g_cs_press.p_proc_dir = proc_mkdir(CS_PROC_NAME, NULL);
    if (g_cs_press.p_proc_dir == NULL) {
        ret = -1;
        goto exit;
    }
    for (i = 0; i < PROC_FOPS_NUM; i++)
    {
        file = proc_create_data(proc_list[i], 0666, g_cs_press.p_proc_dir, &proc_fops[i], NULL);
        /*file = proc_create(proc_list[i],0644,NULL,&proc_fops[i]);*/
        if (file == NULL) {
            ret = -1;
            LOG_ERR("proc %s create failed",proc_list[i]);
            goto err_flag;
        }
    }
    return 0;
err_flag:
    remove_proc_entry(CS_PROC_NAME,NULL);
exit:
    return ret;
}

static void cs_procfs_delete(void)
{
    int i = 0;

    for (i = 0;i < PROC_FOPS_NUM;i++)
    {
        remove_proc_entry(proc_list[i], g_cs_press.p_proc_dir);
        LOG_DEBUG("proc %s is removed", proc_list[i]);
    }
    remove_proc_entry(CS_PROC_NAME,NULL);
}

#if IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER)
static void cs_press_panel_notifier_callback(enum panel_event_notifier_tag tag,
         struct panel_event_notification *notification, void *client_data)
{
    if (!notification) {
        LOG_ERR("Invalid notification\n");
        return;
    }
    if (notification->notif_type < DRM_PANEL_EVENT_FOR_TOUCH) {
        LOG_INFO("Notification type:%d, early_trigger:%d",
            notification->notif_type,
            notification->notif_data.early_trigger);
    } else {
        return;
    }

    if (g_cs_press.update_done) {
        if (notification->notif_type == DRM_PANEL_EVENT_UNBLANK && g_cs_press.is_suspended) {
            mutex_lock(&press_lock);
            cs_press_set_mode(g_cs_press.camera_key_mode);
            g_cs_press.is_suspended = false;
            mutex_unlock(&press_lock);
        } else if ((notification->notif_type == DRM_PANEL_EVENT_BLANK
                || notification->notif_type == DRM_PANEL_EVENT_BLANK_LP) && !g_cs_press.is_suspended) {
            mutex_lock(&press_lock);
            cs_press_set_mode(g_cs_press.camera_key_mode < GAME_KEY_DEFAULT_MODE ? CAMERA_KEY_SLEEP_MODE : GAME_KEY_SLEEP_MODE);
            g_cs_press.is_physical_tap_down = false;
            g_cs_press.is_suspended = true;
            mutex_unlock(&press_lock);
        }
        LOG_INFO("panel notification callback finish.\n");
    } else {
        LOG_INFO("device not ready.\n");
    }
}

#elif IS_ENABLED(CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY) || IS_ENABLED(CONFIG_OPLUS_DEVICE_INFO_MTK_PLATFORM)
static int cs_press_mtk_drm_notifier_callback(struct notifier_block *nb,
    unsigned long event, void *data)
{
    int *blank = (int *)data;

    LOG_INFO("mtk gki notifier event:%lu, blank:%d",
            event, *blank);

    if (g_cs_press.update_done) {
        if (*blank == MTK_DISP_BLANK_UNBLANK && g_cs_press.is_suspended) {
            mutex_lock(&press_lock);
            cs_press_set_mode(g_cs_press.camera_key_mode);
            g_cs_press.is_suspended = false;
            mutex_unlock(&press_lock);
        } else if (*blank == MTK_DISP_BLANK_POWERDOWN && !g_cs_press.is_suspended) {
            mutex_lock(&press_lock);
            cs_press_set_mode(g_cs_press.camera_key_mode < GAME_KEY_DEFAULT_MODE ? CAMERA_KEY_SLEEP_MODE : GAME_KEY_SLEEP_MODE);
            g_cs_press.is_physical_tap_down = false;
            g_cs_press.is_suspended = true;
            mutex_unlock(&press_lock);
        }
        LOG_INFO("mtk gki notifier callback finish.\n");
    } else {
        LOG_INFO("device not ready.\n");
    }

    return 0;
}

#else
/**
 * @brief Check if event is valid for processing
 * @param event: event type
 * @return true if valid, false otherwise
 */
static bool cs_press_is_valid_fb_event(unsigned long event)
{
#if IS_ENABLED(CONFIG_DRM_MSM) || IS_ENABLED(CONFIG_DRM_OPLUS_NOTIFY)
    return (event == MSM_DRM_EARLY_EVENT_BLANK || event == MSM_DRM_EVENT_BLANK);
#else
    return (event == FB_EARLY_EVENT_BLANK || event == FB_EVENT_BLANK);
#endif
}

/**
 * @brief Check if blank state indicates resume
 * @param blank: blank state value
 * @return true if resume, false otherwise
 */
static bool cs_press_is_resume_blank(int blank)
{
#if IS_ENABLED(CONFIG_DRM_MSM) || IS_ENABLED(CONFIG_DRM_OPLUS_NOTIFY)
    return (blank == MSM_DRM_BLANK_UNBLANK);
#else
    return (blank == FB_BLANK_UNBLANK);
#endif
}

/**
 * @brief Check if blank state indicates suspend
 * @param blank: blank state value
 * @return true if suspend, false otherwise
 */
static bool cs_press_is_suspend_blank(int blank)
{
#if IS_ENABLED(CONFIG_DRM_MSM) || IS_ENABLED(CONFIG_DRM_OPLUS_NOTIFY)
    return (blank == MSM_DRM_BLANK_BLANK);
#else
    return (blank == FB_BLANK_BLANK);
#endif
}

/**
 * @brief Handle resume event
 */
static void cs_press_handle_resume_event(void)
{
    mutex_lock(&press_lock);
    cs_press_set_mode(g_cs_press.camera_key_mode);
    g_cs_press.is_suspended = false;
    mutex_unlock(&press_lock);
}

/**
 * @brief Handle suspend event
 */
static void cs_press_handle_suspend_event(void)
{
    int sleep_mode = (g_cs_press.camera_key_mode < GAME_KEY_DEFAULT_MODE) ?
                     CAMERA_KEY_SLEEP_MODE : GAME_KEY_SLEEP_MODE;

    mutex_lock(&press_lock);
    cs_press_set_mode(sleep_mode);
    g_cs_press.is_physical_tap_down = false;
    g_cs_press.is_suspended = true;
    mutex_unlock(&press_lock);
}

static int fb_notifier_callback(struct notifier_block *self, unsigned long event, void *data)
{
    int *blank;
#if IS_ENABLED(CONFIG_DRM_MSM) || IS_ENABLED(CONFIG_DRM_OPLUS_NOTIFY)
    struct msm_drm_notifier *evdata = data;
#else
    struct fb_event *evdata = data;
#endif

    if (!cs_press_is_valid_fb_event(event)) {
        return 0;
    }

    if (!evdata || !evdata->data) {
        return 0;
    }

    blank = evdata->data;
    LOG_INFO("%s: event = %ld, blank = %d\n", __func__, event, *blank);

    if (!g_cs_press.update_done) {
        LOG_INFO("device not ready.\n");
        return 0;
    }

    if (cs_press_is_resume_blank(*blank) && g_cs_press.is_suspended) {
        cs_press_handle_resume_event();
    } else if (cs_press_is_suspend_blank(*blank) && !g_cs_press.is_suspended) {
        cs_press_handle_suspend_event();
    }

    return 0;
}
#endif

#if IS_ENABLED(CONFIG_DRM_OPLUS_PANEL_NOTIFY) || IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER)
struct drm_panel *cs_press_dev_get_panel(struct device_node *of_node)
{
    int i;
    int count;
    struct device_node *node;
    struct drm_panel *panel;
    struct device_node *np;
    char disp_node[32] = {0};

    np = of_find_node_by_name(NULL, "oplus,dsi-display-dev");
    if (!np) {
        LOG_INFO("[oplus,dsi-display-dev] is missing, try to find [panel] node \n");
        np = of_node;
        LOG_INFO("np full name = %s \n", np->full_name);
        strncpy(disp_node, "panel", sizeof("panel"));
    } else {
        LOG_ERR("[oplus,dsi-display-dev] node found \n");
        /* for primary panel */
        strncpy(disp_node, "oplus,dsi-panel-primary", sizeof("oplus,dsi-panel-primary"));
    }
    LOG_INFO("disp_node = %s \n", disp_node);

    count = of_count_phandle_with_args(np, disp_node, NULL);
    if (count <= 0) {
        LOG_ERR("can not find [%s] node in dts \n", disp_node);
        return NULL;
    }

    for (i = 0; i < count; i++) {
        node = of_parse_phandle(np, disp_node, i);
        panel = of_drm_find_panel(node);
        LOG_INFO("panel[%d] IS_ERR =%d \n", i, IS_ERR(panel));
        of_node_put(node);
        if (!IS_ERR(panel)) {
            LOG_ERR("Find available panel\n");
            return panel;
        }
    }

    return NULL;
}
#endif

int cs_press_register_notifier(void)
{
    int ret = 0;
#if IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER)
    void *cookie = NULL;
#endif
#if IS_ENABLED(CONFIG_DRM_OPLUS_PANEL_NOTIFY)
    g_cs_press.fb_notif.notifier_call = fb_notifier_callback;

    if (g_cs_press.active_panel)
        ret = drm_panel_notifier_register(g_cs_press.active_panel,
                &g_cs_press.fb_notif);
#elif IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER)
    if (g_cs_press.active_panel) {
        cookie = panel_event_notifier_register(PANEL_EVENT_NOTIFICATION_PRIMARY,
                PANEL_EVENT_NOTIFIER_CLIENT_TRI_STATE_KEY, g_cs_press.active_panel,
                &cs_press_panel_notifier_callback, &g_cs_press);

        if (!cookie) {
            LOG_ERR("Unable to register fb_notifier: %d\n", ret);
        } else {
            g_cs_press.notifier_cookie = cookie;
        }
    }

#elif IS_ENABLED(CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY) || IS_ENABLED(CONFIG_OPLUS_DEVICE_INFO_MTK_PLATFORM)
    g_cs_press.disp_notifier.notifier_call = cs_press_mtk_drm_notifier_callback;
    if (mtk_disp_notifier_register("Oplus_Press", &g_cs_press.disp_notifier)) {
        LOG_ERR("Failed to register disp notifier client!!\n");
    }

#elif IS_ENABLED(CONFIG_DRM_MSM) || IS_ENABLED(CONFIG_DRM_OPLUS_NOTIFY)
    g_cs_press.fb_notif.notifier_call = fb_notifier_callback;
    ret = msm_drm_register_client(&g_cs_press.fb_notif);

    if (ret) {
        LOG_ERR("Unable to register fb_notifier: %d\n", ret);
    }

#elif IS_ENABLED(CONFIG_FB)
    g_cs_press.fb_notif.notifier_call = fb_notifier_callback;
    ret = fb_register_client(&g_cs_press.fb_notif);

    if (ret) {
        LOG_ERR("Unable to register fb_notifier: %d\n", ret);
    }
#endif/*CONFIG_FB*/
    return ret;
}

int cs_press_unregister_notifier(void)
{
    int ret = 0;
#if IS_ENABLED(CONFIG_DRM_OPLUS_PANEL_NOTIFY)
    if (g_cs_press.active_panel && g_cs_press.fb_notif.notifier_call) {
        ret = drm_panel_notifier_unregister(g_cs_press.active_panel,
            &g_cs_press.fb_notif);
        if (ret) {
            LOG_ERR("Unable to unregister fb_notifier: %d\n", ret);
        }
    }
#elif IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER)
    if (g_cs_press.active_panel && g_cs_press.notifier_cookie) {
        panel_event_notifier_unregister(g_cs_press.notifier_cookie);
    }
#elif IS_ENABLED(CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY) || IS_ENABLED(CONFIG_OPLUS_DEVICE_INFO_MTK_PLATFORM)
    if (g_cs_press.disp_notifier.notifier_call) {
        mtk_disp_notifier_unregister(&g_cs_press.disp_notifier);
    }
#elif IS_ENABLED(CONFIG_DRM_MSM) || IS_ENABLED(CONFIG_DRM_OPLUS_NOTIFY)
    if (g_cs_press.fb_notif.notifier_call) {
        ret = msm_drm_unregister_client(&g_cs_press.fb_notif);

        if (ret) {
            LOG_ERR("Unable to register fb_notifier: %d\n", ret);
        }
    }
#elif IS_ENABLED(CONFIG_FB)
    if (g_cs_press.fb_notif.notifier_call) {
        ret = fb_unregister_client(&g_cs_press.fb_notif);

        if (ret) {
            LOG_ERR("Unable to unregister fb_notifier: %d\n", ret);
        }
    }
#endif/*CONFIG_FB*/
    return ret;
}

/********misc node for ndt*********/
static int csa37f71_open(struct inode *inode, struct file *filp)
{
    LOG_DEBUG("csa37f71_open\n");
    if (g_cs_press.client == NULL)
    {
        LOG_DEBUG("client is null\n");
        return -1;
    }
    return 0;
}

static ssize_t csa37f71_write(struct file *file, const char __user *buf,
        size_t count, loff_t *offset)
{
    int err;
    char *kbuf = NULL;
    char reg;
     /*****fw updating return****/
    if (get_device_updating_flag()) {
        LOG_ERR("updating\n");
        return -2;
    }

    kbuf = kzalloc(count, GFP_KERNEL);
    if (!kbuf)
    {
        LOG_DEBUG("kbuf is null\n");
        err = -ENOMEM;
        goto exit;
    }

    if (copy_from_user(&reg, buf, 1) || copy_from_user(kbuf, buf + 1, count))
    {
        LOG_DEBUG("copy from user err\n");
        err = -EFAULT;
        goto exit_kfree;
    }
    err = cs_press_iic_write(reg, kbuf, count);
    if (err >= 0)
    {
        err = 1;
    }

    exit_kfree:
    kfree(kbuf);
    exit:
    return err;
}

static ssize_t csa37f71_read(struct file *filp, char __user *buf, size_t count, loff_t *off)
{
    int err = 0;
    char *kbuf = NULL;
    char reg = 0;
    char err_code = 0;
     /*****fw updating return****/
    if (get_device_updating_flag()) {
        LOG_ERR("updating\n");
        return -2;
    }

    kbuf = kzalloc(count, GFP_KERNEL);
    if (!kbuf) {
        err = -ENOMEM;
        LOG_DEBUG("kbuf is null\n");
        goto exit;
    }
    /*get reg addr buf[0]*/
    if (copy_from_user(&reg, buf, 1)) {
        err = -EFAULT;
        LOG_DEBUG("copy from user err\n");
        goto exit_kfree;
    }

    if (reg == DEBUG_ENGINEER_TEST_REG) {
        if (g_cs_press.is_boot_ver_err) {
            err_code = ERROR_CODE_RST;
        } else if (!g_cs_press.irq_ok) {
            err_code = ERROR_CODE_IRQ;
        } else if (g_cs_press.fw_update_error < 0
                || g_cs_press.fw_update_error >= 0x80){
            err_code = ERROR_CODE_FW_UPDATE;
        } else {
           err = cs_press_iic_read(reg, kbuf, count);
            if (err < 0){
                err_code = ERROR_CODE_IIC;
            }
        }
        memset(kbuf, err_code, count);
    } else {
        err = cs_press_iic_read(reg, kbuf, count);
        if (err < 0){
            goto exit_kfree;
        }
    }

    if (copy_to_user(buf + 1, kbuf, count)) {
        LOG_DEBUG("copy from user err\n");
        err = -EFAULT;
    } else {
        err = 1;
    }
    exit_kfree:
    kfree(kbuf);
    exit:
    return err;
}

static int csa37f71_release(struct inode *inode, struct file *filp)
{
    return 0;
}
/*
 * @ misc device file operation
 */
static const struct file_operations csa37f71_fops = {
    .owner = THIS_MODULE,
    .open = csa37f71_open,
    .write = csa37f71_write,
    .read = csa37f71_read,
    .release = csa37f71_release,
};

static struct miscdevice csa37f71_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = MISC_DEVICE_NAME,
    .fops  = &csa37f71_fops,
};

static int csa37f71_probe(struct i2c_client *client)
{
    int ret = -1;
#if IS_ENABLED(CONFIG_DRM_OPLUS_PANEL_NOTIFY) || IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER)
    int retry = 0;
#endif

    LOG_DEBUG("probe init\n");
    g_cs_press.client = client;
    cs_parse_dts(g_cs_press.client);
    cs_press_struct_init();
/* ts check panel dt */
#if IS_ENABLED(CONFIG_DRM_OPLUS_PANEL_NOTIFY) || IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER)
    /* get spi of_node from spi_register_driver */
    for (retry = 0; retry < 10; retry++) {
        g_cs_press.active_panel = cs_press_dev_get_panel(g_cs_press.client->dev.of_node);
        if (g_cs_press.active_panel) {
            LOG_ERR("Success to get panel info\n");
            break;
        }
        msleep(500);
    }

    if (retry == 10) {
        LOG_ERR("ts check panel dt failed\n");
        return -EPROBE_DEFER; /* retry */
    }
#endif
    ret = misc_register(&csa37f71_misc); /*dev node*/
    if (ret) {
        LOG_DEBUG("misc_register err %d\n", ret);
    }
    g_cs_press.pinctrl = devm_pinctrl_get(&client->dev);
    if (IS_ERR_OR_NULL(g_cs_press.pinctrl)) {
        LOG_ERR("get pinctrl fail\n");
        //return -EINVAL;
    } else {
        g_cs_press.irq_pin_input = pinctrl_lookup_state(g_cs_press.pinctrl, "default");
        if (IS_ERR_OR_NULL(g_cs_press.irq_pin_input)) {
            LOG_ERR("Failed to get the state irq_pin_input pinctrl handle\n");
            //return -EINVAL;
        } else {
            pinctrl_select_state(g_cs_press.pinctrl, g_cs_press.irq_pin_input);
        }
    }

    g_cs_press.is_update_log = 0;
    cs_procfs_create();                 /*proc node*/
#ifdef INT_SET_EN
    eint_init();
#endif
    g_cs_press.camera_key_mode = GAME_KEY_DEFAULT_MODE;
    camera_key_config_read(&g_cs_press.strength_cfg);

    g_cs_press.suspend_lock = wakeup_source_register(NULL, "cs_press_wakelock");
    if (!g_cs_press.suspend_lock) {
        pr_err("wakeup source init failed.\n");
    }

    INIT_DELAYED_WORK(&g_cs_press.update_worker, update_work_func);
    schedule_delayed_work(&g_cs_press.update_worker, msecs_to_jiffies(20000));
    LOG_INFO("update_work_func start,delay 20s.\n");

    ret = cs_press_register_notifier();
    if (ret < 0) {
        LOG_ERR("cs_press_register_notifier failed\n");
    }
    LOG_ERR("end!\n");
    return 0;
}

static int csa37f71_suspend(struct device *dev)
{
    enable_irq_wake(cs_press_irq_num);
    LOG_DEBUG("suspend\n");
    return 0;
}

static int csa37f71_resume(struct device *dev)
{
    disable_irq_wake(cs_press_irq_num);
    LOG_DEBUG("resume\n");
    return 0;
}

static void csa37f71_remove(struct i2c_client *client)
{
    /* delete device */
    if (g_cs_press.client == NULL) {
        return;
    }
    LOG_DEBUG("csa37f71_remove\n");
    if (g_cs_press.suspend_lock) {
        wakeup_source_unregister(g_cs_press.suspend_lock);
    }
    cs_press_unregister_notifier();
    cs_unregister_dts();
    cs_procfs_delete();
#ifdef INT_SET_EN
    eint_exit();
#endif
    misc_deregister(&csa37f71_misc);
    g_cs_press.client = NULL;
}

/*
 * @traditional match list,use thie when not using dts
 */
static const struct i2c_device_id csa37f71_id[] = {
    {CS_PRESS_NAME, 0},
    {/*northing to be done*/},
};

/*
 * @dts match list
 */
static const struct of_device_id of_csa37f71_match[] = {
    {.compatible = "chipsea,csa37f71-game"},
    {/*northing to be done*/},
};

/**
 * @Support fast loading of hot swap devices
 */
MODULE_DEVICE_TABLE(i2c, csa37f71_id);

static const struct dev_pm_ops cs_press_pm_ops = {
    .suspend = csa37f71_suspend,
    .resume = csa37f71_resume,
};

/*i2c driver struct*/
static struct i2c_driver csa37f71_driver = {
    .probe = csa37f71_probe,
    .remove = csa37f71_remove,
    .driver = {
            .owner = THIS_MODULE,
            .name = CS_PRESS_NAME,
            .of_match_table = of_match_ptr(of_csa37f71_match),/*if use dts,use of_match_table to match*/
            .pm = &cs_press_pm_ops,
    },
    .id_table = csa37f71_id,
};

static int __init csa37f71_init(void)
{
    int ret = 0;
    ret = i2c_add_driver(&csa37f71_driver);
    return ret;
}

static void __exit csa37f71_exit(void)
{
    LOG_DEBUG("module exit\n");
    i2c_del_driver(&csa37f71_driver);
    cs_procfs_delete();
}

module_init(csa37f71_init);
module_exit(csa37f71_exit);
MODULE_LICENSE("GPL");
