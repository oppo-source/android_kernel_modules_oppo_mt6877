#include "pogo_keyboard.h"
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/fb.h>
#include <linux/firmware.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <uapi/linux/sched/types.h>
#include <linux/pm.h>
#include <linux/pm_wakeirq.h>
#include <linux/serial_8250.h>
#include <linux/proc_fs.h>
#include <linux/input/mt.h>
#include <linux/pinctrl/consumer.h>
#include "owb.h"
#include <soc/oplus/dft/kernel_fb.h>
#ifndef CONFIG_REMOVE_OPLUS_FUNCTION
#include <soc/oplus/system/oplus_project.h>
#endif

#ifdef CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY
#include<mt-plat/mtk_boot_common.h>
#endif

#if IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER) && defined(CONFIG_OPLUS_POGOPIN_FUNCTION)
#include <linux/soc/qcom/panel_event_notifier.h>
#include <linux/msm_drm_notify.h>
#include <drm/drm_panel.h>
#endif

#if IS_ENABLED(CONFIG_DEVICE_MODULES_DRM_MEDIATEK) && defined(CONFIG_OPLUS_POGOPIN_FUNCTION)
#include "mtk_disp_notify.h"
#endif

#define KB_SN_HIDE_BIT_START   7
#define KB_SN_HIDE_BIT_LEN  12
#define KB_SN_HIDE_STAR_ASCII  42

//for TP DIST 15x25
#define ROWS 15
#define COLS 25

struct pogo_keyboard_data *pogo_keyboard_client = NULL;
//for trx test
static int test_fail_sum = 0;
static int test_count = 0;;
static unsigned char send_buf[UART_BUFFER_SIZE] = {0};
static unsigned char ack_buf[UART_BUFFER_SIZE] = {0};
static u8 send_len = 0;
static u8 ack_len = 0;
char TAG[60] = { 0 };
int kb_debug_level = 0;
atomic_t  kb_suspend_failed_count = ATOMIC_INIT(0);
atomic_t  kb_heart_in_suspend = ATOMIC_INIT(0);

static int sn_report_count = 0;
//for TP DIST 15x25
static short tp_data_dist[ROWS][COLS] = {0};

static DECLARE_WAIT_QUEUE_HEAD(waiter);
static DECLARE_WAIT_QUEUE_HEAD(read_waiter);

//static BLOCKING_NOTIFIER_HEAD(pogo_keyboard_notifier_list);

static int pogo_keyboard_event_process(unsigned char pogo_keyboard_event);
static irqreturn_t pogo_keyboard_irq_handler(int irq, void *data);
static struct file *pogo_keyboard_port_to_file(struct uart_port *port);
#ifdef CONFIG_POWER_CTRL_SUPPORT
static void pogo_keyboard_power_enable(int value);
#endif
static int pogo_keyboard_enable_uart_tx(int value);
static int pogo_keyboard_plat_remove(struct platform_device *device);
static int pogo_keyboard_plat_suspend(struct platform_device *device);
static int pogo_keyboard_plat_resume(struct platform_device *device);
static int pogo_keyboard_input_connect(void);
static void pogo_keyboard_input_disconnect(void);
static bool pogo_keyboard_key_up(void);
static bool pogo_keyboard_touch_up(void);

void pogo_keyboard_show_buf(void *buf, int count)
{
    int i = 0;
    char temp[513] = { 0 };
    int len = 0;
    char *pbuf = (char *)buf;
    if (count > UART_BUFFER_SIZE)
        count = UART_BUFFER_SIZE;
    for (i = 0; i < count; i++) {
        len += sprintf(temp + len, "%02x", pbuf[i]);
    }
    kb_debug("%s:show_buf  len:[%d]  %s\n", TAG, count, temp);
    memset(TAG, 0, sizeof(TAG));
}

// start heartbeat monitor timer only when keyboard is attached. value = 1 starts timer while 0 stops.
void pogo_keyboard_heartbeat_switch(int value)
{
    ktime_t ktime = ktime_set(0, 1000 * 1000 * 50); //50ms, note that heartbeat from keyboard is 100ms(may be adjusted in the future)

    if (pogo_keyboard_client == NULL) {
        return;
    }

    if (value == 1) {
        pm_wakeup_event(&pogo_keyboard_client->plat_dev->dev, 50 + 20);
        hrtimer_start(&pogo_keyboard_client->heartbeat_timer, ktime, HRTIMER_MODE_REL);
    } else if (value == 0) {
        hrtimer_cancel(&pogo_keyboard_client->heartbeat_timer);
    }
}

// start/stop keyboard attachement first detection timer. value = 1 starts timer while 0 stops.
void pogo_keyboard_plug_switch(int value)
{
    ktime_t ktime = ktime_set(POWERON_PLUG_CKECK_TIMER, 0); //5S

    if (pogo_keyboard_client == NULL || pogo_keyboard_client->plug_timer_one_time) {
        return;
    }

    if (value == 1) {
        pogo_keyboard_client->plug_timer_one_time = true;
        hrtimer_start(&pogo_keyboard_client->plug_timer, ktime, HRTIMER_MODE_REL);
    } else if (value == 0) {
        hrtimer_cancel(&pogo_keyboard_client->plug_timer);
    }
}

void pogo_keyboard_poweroff_timer_switch(bool state)
{
    ktime_t ktime = ktime_set(0, 1000 * 1000 * POWEROFF_TIMER_EXPIRY);

    if (pogo_keyboard_client == NULL) {
        return;
    }

    if (state) {
        pm_wakeup_event(&pogo_keyboard_client->plat_dev->dev, POWEROFF_TIMER_EXPIRY + 20);
        hrtimer_start(&pogo_keyboard_client->poweroff_timer, ktime, HRTIMER_MODE_REL);
    } else {
        hrtimer_cancel(&pogo_keyboard_client->poweroff_timer);
    }
}

void pogo_keyboard_plugin_check_timer_switch(bool state)
{
    ktime_t ktime = ktime_set(0, 1000 * 1000 * 20);

    if (pogo_keyboard_client == NULL) {
        return;
    }

    if (state) {
        pm_wakeup_event(&pogo_keyboard_client->plat_dev->dev, 40);
        hrtimer_start(&pogo_keyboard_client->plugin_check_timer, ktime, HRTIMER_MODE_REL);
    } else {
        hrtimer_cancel(&pogo_keyboard_client->plugin_check_timer);
    }
}

// api to send event to "pogo_keyboard_task".
static void pogo_keyboard_event_send(unsigned char value)
{
    struct pogo_keyboard_event send_event = { 0 };
    unsigned long flags;

    kb_info("new event:%d\n", value);

    spin_lock_irqsave(&pogo_keyboard_client->event_fifo_lock, flags);
    if (kfifo_avail(&pogo_keyboard_client->event_fifo) >= sizeof(struct pogo_keyboard_event)) {
        send_event.event = value;
        kfifo_in(&pogo_keyboard_client->event_fifo, &send_event, sizeof(struct pogo_keyboard_event));
        pogo_keyboard_client->flag = 1;
        wake_up_interruptible(&waiter);
    }
    spin_unlock_irqrestore(&pogo_keyboard_client->event_fifo_lock, flags);
}

//计算crc16
static unsigned short app_compute_crc16(unsigned short crc, unsigned char data, unsigned short polynomial)
{
    unsigned char i = 0;
    for (i = 0; i < 8; i++) {
        if ((((crc & 0x8000) >> 8) ^ (data & 0x80)) != 0) {
            crc <<= 1;         // shift left once
            crc ^= polynomial; // XOR with polynomial
        } else {
            crc <<= 1;         // shift left once
        }
        data <<= 1;            // Next data bit
    }
    return crc;
}

//获取crc16
unsigned short app_crc16_get(unsigned char *buf, unsigned short len, unsigned char crc_type)
{
    unsigned short i = 0;
    unsigned short crc = 0;
    unsigned short polynomial = 0;
    unsigned short crc_ibm_init_val = 0;

    if (!buf || len == 0 || len > UART_BUFFER_SIZE) {
        kb_err("Invalid input:buf=%p, len=%u\n", buf, len);
        return CRC_ERROR_VALUE;
    }
    if (pogo_keyboard_client && pogo_keyboard_client->crc_ibm_init_val) {
        crc_ibm_init_val = pogo_keyboard_client->crc_ibm_init_val;
    } else {
        crc_ibm_init_val = CRC_IBM_INIT_VAL;
    }
    polynomial = (crc_type == CRC_TYPE_IBM) ? POLYNOMIAL_IBM : POLYNOMIAL_CCITT;
    crc = (crc_type == CRC_TYPE_IBM) ? crc_ibm_init_val : CRC_CCITT_INIT_VAL;

    for (i = 0; i < len; i++) {
        crc = app_compute_crc16(crc, buf[i], polynomial);
    }
    if (crc_type == CRC_TYPE_IBM) {
        return crc;
    } else {
        return (unsigned short)(~crc);
    }
}

// put payloads into one wire bus protocol packet.
int uart_package_data(char *buf, int input_len, unsigned char *p_out, int *p_len)
{
    unsigned short i = 0;
    unsigned short need_len = 0;
    unsigned short crc16 = 0;
    unsigned char data_len = 0;

    if (!buf || !p_out || !p_len || input_len <= UART_PACKET_MIN_HEADER_SIZE) {
        kb_err("Invalid input parameters\n");
        return -EINVAL;
    }

    data_len = (unsigned char)buf[1];

    if (data_len > input_len - UART_PACKET_MIN_HEADER_SIZE) {
        kb_err("Data length %u exceeds input buffer size %d\n",
               data_len, input_len - UART_PACKET_MIN_HEADER_SIZE);
        return -EINVAL;
    }

    need_len = UART_PACKET_HEADER_SIZE + data_len + UART_PACKET_TAIL_SIZE;
    if (need_len > UART_BUFFER_SIZE) {
        kb_err("Packet too large: %u bytes, max allowed: %d\n",
               need_len, UART_BUFFER_SIZE);
        return -EINVAL;
    }

    // 头同步码
    for (i = 0; i < UART_PACKET_SYNC_HEAD_SIZE; i++) {
        p_out[i] = ONE_WIRE_BUS_PACKET_HEAD_SYNC_CODE;
    }
    // 起始码
    p_out[UART_PACKET_START_OFFSET] = ONE_WIRE_BUS_PACKET_XXX_START_CODE;
    // 源地址
    p_out[UART_PACKET_SRC_ADDR_OFFSET] = ONE_WIRE_BUS_PACKET_PAD_ADDR;
    // 目标地址
    p_out[UART_PACKET_DST_ADDR_OFFSET] = ONE_WIRE_BUS_PACKET_KEYBOARD_ADDR;
    // 主命令
    p_out[UART_PACKET_MAIN_CMD_OFFSET] = buf[0];
    // 总长度
    p_out[UART_PACKET_LENGTH_OFFSET] = data_len;
    // 子命令数据
    for (i = 0; i < data_len; i++) {
        p_out[UART_PACKET_DATA_OFFSET + i] = buf[i + UART_PACKET_MIN_HEADER_SIZE];
    }
    // 计算CRC16（从起始码到数据结束）
    crc16 = app_crc16_get(&p_out[UART_PACKET_START_OFFSET],
                         data_len + UART_PACKET_CRC_HEADER_SIZE, CRC_TYPE_IBM);
    // CRC16
    p_out[UART_PACKET_DATA_OFFSET + data_len] = (unsigned char)(crc16 >> 8);
    p_out[UART_PACKET_DATA_OFFSET + data_len + 1] = (unsigned char)(crc16 & 0x00ff);
    // 结束码
    p_out[UART_PACKET_DATA_OFFSET + data_len + 2] = ONE_WIRE_BUS_PACKET_XXX_END_CODE;
    // 尾同步码
    for (i = 0; i < UART_PACKET_SYNC_TAIL_SIZE; i++) {
        p_out[UART_PACKET_DATA_OFFSET + data_len + 3 + i] = ONE_WIRE_BUS_PACKET_TAIL_SYNC_CODE;
    }
    *p_len = need_len;
    snprintf(TAG, sizeof(TAG), "ciphertext");
    pogo_keyboard_show_buf(p_out, *p_len);
    return 0;
}

int uart_unpack_data(char *buf, int len, unsigned char *out_buf, int *out_len)
{
    if (len > 6) {
        *out_len = len - 6;//6:start+addr1+addr2 +  end+sync+sync
        memcpy(out_buf, &buf[3], *out_len);
    }
    return 0;
}

static int upload_pogopin_kevent_data(unsigned char *payload)
{
    struct kernel_packet_info *user_msg_info;
    char log_tag[] = KEVENT_LOG_TAG;
    char event_id_pogopin[] = KEVENT_EVENT_ID;
    int len;
    int retry = 3;
    int ret = 0;

    user_msg_info = (struct kernel_packet_info *)kmalloc(
			sizeof(char) * POGOPIN_TRIGGER_MSG_LEN, GFP_KERNEL);
    if (!user_msg_info) {
        kb_err("Allocation failed\n");
        return -ENOMEM;
    }

    memset(user_msg_info, 0, POGOPIN_TRIGGER_MSG_LEN);
    strncpy(user_msg_info->payload, payload, MAX_POGOPIN_PAYLOAD_LEN);
    user_msg_info->payload[MAX_POGOPIN_PAYLOAD_LEN - 1] = '\0';
    len = strlen(user_msg_info->payload);

    user_msg_info->type = 1;
    strncpy(user_msg_info->log_tag, log_tag, MAX_POGOPIN_EVENT_TAG_LEN);
    user_msg_info->log_tag[MAX_POGOPIN_EVENT_TAG_LEN - 1] = '\0';
    strncpy(user_msg_info->event_id, event_id_pogopin, MAX_POGOPIN_EVENT_ID_LEN);
    user_msg_info->event_id[MAX_POGOPIN_EVENT_ID_LEN - 1] = '\0';
    user_msg_info->payload_length = len + 1;

    do {
        ret = fb_kevent_send_to_user(user_msg_info);
        if (!ret)
            break;
        msleep(10);
    } while (retry --);

    kfree(user_msg_info);
    return ret;
}

static void handle_battery_info(char *buf, struct pogo_keyboard_data *client)
{
    kb_info("keyboard batt level:%d charge:%d state:0x%x\n",
           buf[4], (buf[5] >> 4) & 0x03, buf[5]);
    client->pogo_battery_power_level = (u8)buf[4];
}

static void handle_serial_number(char *buf, struct pogo_keyboard_data *client)
{
    int sn_len = (buf[3] > DEFAULT_SN_LEN) ? DEFAULT_SN_LEN : buf[3];
    int ret = memcmp(client->report_sn, &buf[4], sn_len);

    if (ret == 0 && sn_report_count >= 2) {
        kb_info("same keyboard SN ,not report\n");
        return;
    }

    if (ret != 0) {
        sn_report_count = 0;
    }

    memset(client->report_sn, 0, sizeof(client->report_sn));
    memcpy(client->report_sn, &buf[4], sn_len);
    pogo_keyboard_event_send(KEYBOARD_REPORT_SN_EVENT);
}

static void handle_kblog(char *buf, struct pogo_keyboard_data *client)
{
    if (client->crc_ibm_init_val == CRC_IBM_DUNHUANG) {//only dunhuang use
        if (buf[3] > KBLOG_LEN_MAX) {
            kb_err("log len too long!!!\n");
        } else {
            client->kblog_len = buf[3];
            memset(client->report_kblog, 0, sizeof(client->report_kblog));
            memcpy(client->report_kblog, &buf[4], client->kblog_len);
            pogo_keyboard_event_send(KEYBOARD_REPORT_KBLOG_EVENT);
        }
    }
}

static void handle_touchpad_status(char *buf, struct pogo_keyboard_data *client)
{
    client->touchpad_disable_state = buf[4];
    kb_info("touchpad_disable_state : %d\n", buf[4]);
}

static void handle_report_kbver(char *buf, struct pogo_keyboard_data *client)
{
    if (buf[3] < DEFAULT_KBVER_LEN || buf[3] > KBVER_LEN_MAX) {
        kb_err("get keyboard version is not right format!!!\n");
    } else {
        client->kbver_len = buf[3] - 1;
        memset(client->report_kbver, 0, sizeof(client->report_kbver));
        memcpy(client->report_kbver, &buf[4], client->kbver_len);
        pogo_keyboard_event_send(KEYBOARD_REPORT_KBVER_EVENT);
    }
}

static void handle_dfu_ota_start(char *buf, struct pogo_keyboard_data *client)
{
    if (client->pogopin_ota_dfu) {
        kb_info("dfu ota start...\n");
        client->max_disconnect_count = DFU_DISCONNECT_COUNT; //2s
    }
}

static void handle_dfu_ota_reset(char *buf, struct pogo_keyboard_data *client)
{
    if (client->pogopin_ota_dfu) {
        kb_info("dfu ota reset...\n");
        client->max_disconnect_count = DFU_RESET_DISCONNECT_COUNT;//20s
    }
}

static void handle_tp_ota_start(char *buf, struct pogo_keyboard_data *client)
{
    if (client->pogopin_ota_dfu) {
        kb_info("tp ota start...\n");
        client->tp_ota_status = OTA_STATUS_ACTIVE;
        client->max_disconnect_count = TP_OTA_START_DISCONNECT_COUNT;
    }
}

static void handle_tp_ota_end(char *buf, struct pogo_keyboard_data *client)
{
    if (client->pogopin_ota_dfu) {
        kb_info("tp ota end...\n");
        client->tp_ota_status = OTA_STATUS_INACTIVE;
        client->max_disconnect_count = DEFAULT_DISCONNECT_COUNT;
    }
}

static void handle_tp_dist(char *buf, struct pogo_keyboard_data *client)
{
    int i, j = 0;
    int col = 0;
    const int min_buf_size = 10 + ROWS * 2;

    if (UART_BUFFER_SIZE < min_buf_size) {
        kb_err("ROWS overflow!!!\n");
        return;
    }
    col = buf[6];
    if (col >= COLS) {
        kb_err("Invalid column index %d\n", col);
        return;
    }

    for (i = 0; i < ROWS; i++) {
        for (j = 0; j < COLS; j++) {
            if (j == col) {
                tp_data_dist[i][j] = (short)buf[8 + i * 2] << 8 | (short)buf[9 + i * 2];
            }
        }
    }

    if (col == COLS - 1) {
        pogo_keyboard_event_send(KEYBOARD_REPORT_TP_DIST_EVENT);
    }
}

static const CommandHandler cmd_handlers[] = {
    // battery
    {
        ONE_WIRE_BUS_PACKET_USER_GENERAL_ACK_CMD,
        ONE_WIRE_BUS_PACKET_USER_GENERAL_BATTERY_STATUS_CMD,
        NULL,
        0,
        handle_battery_info
    },
    // SN
    {
        ONE_WIRE_BUS_PACKET_USER_GENERAL_ACK_CMD,
        ONE_WIRE_BUS_PACKET_USER_GENERAL_SN_CMD,
        NULL,
        0,
        handle_serial_number
    },
    // KBLOG
    {
        ONE_WIRE_BUS_PACKET_USER_GENERAL_ACK_CMD,
        0x10,
        NULL,
        0,
        handle_kblog
    },
    // TP STATUS
    {
        ONE_WIRE_BUS_PACKET_USER_GENERAL_ACK_CMD,
        ONE_WIRE_BUS_PACKET_USER_GENERAL_TP_STATUS_CMD,
        (uint8_t[]){0x3b, 0x03, 0x0c, 0x01},
        4,
        handle_touchpad_status
    },
    // report kbver
    {
        ONE_WIRE_BUS_PACKET_USER_PASSTHROUGH_ACK_CMD,
        ONE_WIRE_BUS_PACKET_USER_PASSTHROUGH_KBVER_CMD,
        NULL,
        0,
        handle_report_kbver
    },
    // OTA
    {
        ONE_WIRE_BUS_PACKET_USER_PASSTHROUGH_CMD,
        ONE_WIRE_BUS_PACKET_USER_PASSTHROUGH_DFU_OTA_CMD,
        (uint8_t[]){0x38, 0x04, 0x0a, 0x02, 0x06, 0x01, 0x05, 0xd5},
        8,
        handle_dfu_ota_start
    },
    {
        ONE_WIRE_BUS_PACKET_USER_PASSTHROUGH_ACK_CMD,
        ONE_WIRE_BUS_PACKET_USER_PASSTHROUGH_DFU_OTA_CMD,
        (uint8_t[]){0x39, 0x05, 0x0a, 0x03, 0x60, 0x04, 0x01, 0x9b, 0x5a},
        9,
        handle_dfu_ota_reset
    },
    {
        ONE_WIRE_BUS_PACKET_USER_GENERAL_ACK_CMD,
        ONE_WIRE_BUS_PACKET_USER_GENERAL_TP_OTA_DFU_CMD,
        (uint8_t[]){0x3b, 0x03, 0x18, 0x01, 0x01},
        5,
        handle_tp_ota_start
    },
    {
        ONE_WIRE_BUS_PACKET_USER_GENERAL_ACK_CMD,
        ONE_WIRE_BUS_PACKET_USER_GENERAL_TP_OTA_STATUS_CMD,
        (uint8_t[]){0x3b, 0x03, 0x16, 0x01, 0x01},
        5,
        handle_tp_ota_end
    },
    //TP DEBUG
    {
        ONE_WIRE_BUS_PACKET_USER_GENERAL_ACK_CMD,
        ONE_WIRE_BUS_PACKET_USER_GENERAL_TP_DIST_CMD,
        (uint8_t[]){0x3b, 0x24, 0x1A, 0x22, 0x06, 0x21},
        6,
        handle_tp_dist
    },
    // END
    {
        0, 0, NULL, 0, NULL
    }
};

// handle uart received data. NOTE: called by uart rx interrupt handler.
static int pogo_keyboard_mod_data_process(char *buf, int len)
{
    int value = buf[0];
    pogo_keyboard_client->disconnect_count = 0;
    switch (value) {
        case ONE_WIRE_BUS_PACKET_GENRRAL_KEY_CMD:
            pogo_keyboard_input_report(&buf[1]);
            break;
        case ONE_WIRE_BUS_PACKET_MEDIA_KEY_CMD:
            pogo_keyboard_mm_input_report(&buf[1]);
            break;
        case ONE_WIRE_BUS_PACKET_TOUCHPAD_CMD:
            touchpad_input_report(&buf[1]);
            break;
        case ONE_WIRE_BUS_PACKET_SYNC_UPLOAD_CMD:
            // packet format: 0x2F + 0x04(len=4) + 1-byte sub-cmd + 3-byte data.
            if ((pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_CONNECT_STATUS) == 0) {
                //获取MAC地址及品牌标志
                if (buf[2] == 0x01 && buf[3] == 0x02) {
                    pogo_keyboard_client->keyboard_brand = buf[4];
                    memcpy((unsigned char *)&pogo_keyboard_client->mac_addr, &buf[5], 6);
                    if (pogo_keyboard_client->pogopin_touch_support && buf[1] > 9) {
                        pogo_keyboard_client->touchpad_disable_state = buf[11];
                    }
                } else if (buf[2] == 0x05 && buf[3] == 0x02) {
                    pogo_keyboard_client->keyboard_brand = buf[5];
                    memcpy((unsigned char *)&pogo_keyboard_client->mac_addr, &buf[6], 6);
                    if (pogo_keyboard_client->pogopin_touch_support && buf[1] > 10) {
                        pogo_keyboard_client->touchpad_disable_state = buf[12];
                    }
                }

                kb_info("plug in\n");
                pogo_keyboard_client->plug_in_count = 0; // reset heartbeat counter.
                if (pogo_keyboard_client->pogopin_ota_dfu && pogo_keyboard_client->tp_ota_status == OTA_STATUS_INACTIVE) {
                    pogo_keyboard_client->max_disconnect_count = DEFAULT_DISCONNECT_COUNT;
                    pogo_keyboard_client->max_plug_in_disconnect_count = DEFAULT_PLUGIN_DISCONNECT_COUNT;// reset heartbeat_hrtimer to 2s
                }
                pogo_keyboard_event_send(KEYBOARD_PLUG_IN_EVENT);

            } else if ((pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_CONNECT_STATUS) != 0) {
                //     sub-cmd = 0x01: power-up sync packet, 3-byte data = 0xCC.
                //     sub-cmd = 0x05: heartbeat packet, 3-byte data = 0xAA 0xBB 0BIT:CAPSLOCK|1BIT:NUMLOCK|2BIT:MUTE|3BIT:MIC|4BIT:FNLOCK.
                if (buf[2] == 0x01) { //by power on data for after plug out quick plug in connect status not update
                    //获取MAC地址及品牌标志
                    if (buf[3] == 0x02) {
                        pogo_keyboard_client->keyboard_brand = buf[4];
                        memcpy((unsigned char *)&pogo_keyboard_client->mac_addr, &buf[5], 6);
                        if (pogo_keyboard_client->pogopin_touch_support && buf[1] > 9) {
                            pogo_keyboard_client->touchpad_disable_state = buf[11];
                        }
                    }
                    pogo_keyboard_client->pogo_keyboard_status &= ~KEYBOARD_CONNECT_STATUS;
                    pogo_keyboard_client->plug_in_count = 0;
                    if (pogo_keyboard_client->pogopin_ota_dfu && pogo_keyboard_client->tp_ota_status == OTA_STATUS_INACTIVE) {
                        pogo_keyboard_client->max_disconnect_count = DEFAULT_DISCONNECT_COUNT;
                        pogo_keyboard_client->max_plug_in_disconnect_count = DEFAULT_PLUGIN_DISCONNECT_COUNT;// reset heartbeat_hrtimer to 2s
                    }
                    kb_info("quick plug out and quick plug in\n");
                    pogo_keyboard_event_send(KEYBOARD_PLUG_IN_EVENT);
                } else if (buf[2] == 0x05 && buf[3] == 0x02) {
                    if (pogo_keyboard_client->pogopin_touch_support && (buf[1] > 10) &&
                        (pogo_keyboard_client->pogo_report_touch_status == 1)) {
                        pogo_keyboard_client->pogo_report_touch_status = 0;
                        pogo_keyboard_client->touchpad_disable_state = buf[12];
                        pogo_keyboard_event_send(KEYBOARD_REPORT_TOUCH_STATUS_EVENT);
                    }
                    if ((buf[4] & 0x01) != ((pogo_keyboard_client->pogo_keyboard_status >> 2) & 0x01)) { //by heartbeet data sync capslock status
                        //sync capslock led status if status reported from keyboard differs from host.
                        //note definition: KEYBOARD_CAPSLOCK_ON_STATUS  (1<<2)
                        kb_info("sync capslock:%02x\n", buf[5]);
                        if ((pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_LCD_ON_STATUS) != 0) {//send led cmd to kb only if lcd is on.
                            if (((pogo_keyboard_client->pogo_keyboard_status >> 2) & 0x01) == 0x01)
                                pogo_keyboard_event_send(KEYBOARD_CAPSLOCK_ON_EVENT);
                            else
                                pogo_keyboard_event_send(KEYBOARD_CAPSLOCK_OFF_EVENT);
                        }
                    }
                    if (pogo_keyboard_client->pogopin_touch_support)
                        pogo_keyboard_touch_up();
                }

                if (buf[2] == 0x05 && (pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_LCD_ON_STATUS) == 0) {
                    pogo_keyboard_client->sync_lcd_state_cnt++;
                    if(pogo_keyboard_client->sync_lcd_state_cnt >= SYNC_LCD_STATE_CNT_MAX) {
                        pogo_keyboard_client->sync_lcd_state_cnt = 0;
                        atomic_set(&kb_heart_in_suspend, 1);
                        pogo_keyboard_event_send(KEYBOARD_HOST_LCD_OFF_EVENT);
                    }
                }
                if (buf[2] == 0x07 && buf[3] == 0xe0) {
                    pogo_keyboard_client->nfc_status = 1;
                    pogo_keyboard_event_send(KEYBOARD_REPORT_NFC_STA);
                    kb_info("nfc card near!!!\n");
                }
                if (buf[2] == 0x08 && buf[3] == 0xe1) {
                    pogo_keyboard_client->nfc_status = 0;
                    pogo_keyboard_event_send(KEYBOARD_REPORT_NFC_STA);
                    kb_info("nfc card far!!!\n");
                }
            }
            break;
        case ONE_WIRE_BUS_PACKET_PARAM_SET_ACK_CMD:
        case ONE_WIRE_BUS_PACKET_OTA_ACK_CMD:
        case ONE_WIRE_BUS_PACKET_I2C_READ_ACK_CMD:
        case ONE_WIRE_BUS_PACKET_USER_GENERAL_ACK_CMD:
        case ONE_WIRE_BUS_PACKET_OTA_CMD:
        case ONE_WIRE_BUS_PACKET_PARAM_SET_CMD:
        case ONE_WIRE_BUS_PACKET_USER_GENERAL_CMD:
        case ONE_WIRE_BUS_PACKET_USER_PASSTHROUGH_CMD:
        case ONE_WIRE_BUS_PACKET_USER_PASSTHROUGH_ACK_CMD:
            if (pogo_keyboard_client->read_flag == 1) {
                kb_info("value:0x%2x cmd:0x%2x \n", value, pogo_keyboard_client->write_buf[0]);
                // judgment below is based on the assumption that response cmd code = send cmd code + 1. not universal.
                if (value == (int)pogo_keyboard_client->write_buf[0] || value == (int)pogo_keyboard_client->write_buf[0] + 1) {
                    pogo_keyboard_client->read_flag = 0;
                    wake_up_interruptible(&read_waiter);
                }
            }

            for (size_t i = 0; i < ARRAY_SIZE(cmd_handlers); i++) {
                const CommandHandler *h = &cmd_handlers[i];
                if (value == h->main_cmd && buf[2] == h->sub_cmd) {
                    if (h->ack == NULL ||
                        (h->ack_len > 0 && memcmp(buf, h->ack, h->ack_len) == 0)) {
                        h->handler(buf, pogo_keyboard_client);
                        break;
                    }
                }
            }
            break;
        default:
            kb_err("invalid cmd:0x%2x!!!", value);
            return -EINVAL;
    }
    return 0;
}

bool pogo_keyboard_data_is_valid(char *buf, int len)
{
    char flag = 0;
    unsigned short crc16 = 0;
    if (len < 5) {
        kb_err("keyboard data is not valid!!!\n");
        return false;
    }
    flag = buf[2];
    crc16 = (buf[len - 5] << 8) | buf[len - 4];
    if (flag == ONE_WIRE_BUS_PACKET_KEYBOARD_ADDR || flag == ONE_WIRE_BUS_PACKET_PAD_ADDR) {
        if (crc16 == app_crc16_get(buf, len - 5, CRC_TYPE_IBM)) {
            return true;
        } else {
            kb_info("flag:%2x crc16:%4x cal_crc:%4x\n", flag, crc16, app_crc16_get(buf, len - 5, CRC_TYPE_IBM));
            return false;
        }
    } else {
        kb_info("flag:%2x\n", flag);
        return false;
    }
}

int pogo_keyboard_store_recv_data(char *buf, int len)
{
    int ret = 0;
    if (buf[0] == pogo_keyboard_client->write_buf[0]) {
        memcpy(pogo_keyboard_client->write_check_buf, buf, len);
        pogo_keyboard_client->write_len = len;
        ret = len;
        snprintf(TAG, sizeof(TAG), "write_check_buf");
        pogo_keyboard_show_buf(pogo_keyboard_client->write_check_buf, len);
    } else if (buf[0] == (int)pogo_keyboard_client->write_buf[0] + 1) {
        memcpy(pogo_keyboard_client->read_buf, buf, len);
        pogo_keyboard_client->read_len = len;
        ret = len;
        snprintf(TAG, sizeof(TAG), "read_buf");
        pogo_keyboard_show_buf(pogo_keyboard_client->read_buf, len);
    }
    return ret;
}

// handle uart received data, called by uart rx interrupt handler.
int pogo_keyboard_recv(char *buf, int len)
{
    static bool recv_start_flag = false;
    static bool recv_decode_flag = false;
    static unsigned char head_sync_code_cnt = 0;
    static unsigned char rec_temp_buf_index = 0;
    static char recv_buf[UART_BUFFER_SIZE] = { 0 };
    char data_buf[UART_BUFFER_SIZE] = { 0 };
    int out_len = 0;
    char rx_data;
    int i = 0;
    int ret = 0;
    unsigned char recv_decode_cnt = 0;

    if (len <= 0) {
        kb_err("invalid len!\n");
        return -EINVAL;
    }
    snprintf(TAG, sizeof(TAG), "%s ", __func__);
    pogo_keyboard_show_buf(buf, len);

    // the process below is based on the assumption that only one packet per transfer!!
    for (i = 0; i < len; i++) {
        rx_data = buf[i];

        if (rx_data == ONE_WIRE_BUS_PACKET_HEAD_SYNC_CODE) {
            head_sync_code_cnt++;
        } else if (rx_data == ONE_WIRE_BUS_PACKET_XXX_START_CODE || rx_data == ONE_WIRE_BUS_PACKET_XXX_REPEAT_CODE) {
            //收到起始码后，再判断起始码之前有没有收到连续的头同步码
            if (head_sync_code_cnt >= 4) {
                rec_temp_buf_index = 0;
                recv_start_flag = true;
            }
            head_sync_code_cnt = 0;
        } else {
            head_sync_code_cnt = 0;
        }
        if (recv_start_flag == true) {
            recv_buf[rec_temp_buf_index++] = rx_data;
            if (rec_temp_buf_index >= 3
                && recv_buf[rec_temp_buf_index - 1] == ONE_WIRE_BUS_PACKET_TAIL_SYNC_CODE
                && recv_buf[rec_temp_buf_index - 2] == ONE_WIRE_BUS_PACKET_TAIL_SYNC_CODE
                && recv_buf[rec_temp_buf_index - 3] == ONE_WIRE_BUS_PACKET_XXX_END_CODE) {
                //当收到0xFE,0xAA,0xAA字段时，认为接收到完整一包了，并开始进行包处理
                recv_start_flag = false;
                recv_decode_flag = true;
                recv_decode_cnt++;
                if(recv_decode_cnt > 3) {
                    recv_decode_flag = false;
                    break;
                }
            }
        }
        if (rec_temp_buf_index >= UART_BUFFER_SIZE) {
            rec_temp_buf_index = 0;
            recv_start_flag = false;
        }
        if (recv_decode_flag == true) {
            recv_decode_flag = false;
            if (rec_temp_buf_index > 6 &&
                pogo_keyboard_data_is_valid(recv_buf, rec_temp_buf_index)) {
                uart_unpack_data(recv_buf, rec_temp_buf_index, data_buf, &out_len);
                pogo_keyboard_store_recv_data(data_buf, out_len);
                ret = pogo_keyboard_mod_data_process(data_buf, out_len);
            }
            rec_temp_buf_index = 0;
        }
    }
    return ret;
}

int pogo_keyboard_recv_callback(char *buf, int len)
{
    return pogo_keyboard_recv(buf, len);
}

int pogo_keyboard_get_valid_data(char *buf, int len, unsigned char *out_buf, int *out_len)
{
    int count = 0;
    count = len - 11 - 4; //11: sync*8+start+addr1+addr2  4:sync*4
    memcpy(out_buf, &buf[11], count);
    *out_len = count;
    return count;
}

ssize_t tty_write_ex(const char *buf, size_t count)
{
    ssize_t ret = 0;
    if (pogo_keyboard_client->file_client == NULL) {
        kb_err("err: file_client is null!\n");
        return -1;
    }
    ret = pogo_tty_write(pogo_keyboard_client->file_client, buf, count, NULL);
    if (ret < 0) {
        kb_err("err:%zd!\n", ret);
        return -1;
    }
    return ret;
}

int pogo_keyboard_write(void *buf, int len)
{
    int ret = 0;
    int out_len = 0;
    int count = 3;
    void *pbuf = buf;
    char write_buf[255] = { 0 };
    int write_len = 0;
    int i = 0;

    memset(pogo_keyboard_client->write_buf, 0, sizeof(pogo_keyboard_client->write_buf));
    ret = uart_package_data(pbuf, len, write_buf, &write_len);
    if (ret) {
        kb_err("ret:%d\n", ret);
        return ret;
    }
    pogo_keyboard_get_valid_data(write_buf, write_len, pogo_keyboard_client->write_buf, &out_len);
    pogo_keyboard_client->read_flag = 1;
    for (i = 0; i < count; i++) {
        ret = tty_write_ex(write_buf, write_len);
        if (ret > 0) {
            break;
        }
    }
    if (i >= count) {
        kb_err("ret:%d i:%d err\n", ret, i);
        return ret;
    }

    wait_event_interruptible_timeout(read_waiter, pogo_keyboard_client->read_flag != 1, HZ / 20);//timeout 50ms

    if (pogo_keyboard_client->read_flag == 1) {
        kb_debug("read_flag:%d\n", pogo_keyboard_client->read_flag);
    }

    if (pogo_keyboard_client->write_len <= 0) {
        kb_err("read_flag:%d  write_len:%d\n", pogo_keyboard_client->read_flag, pogo_keyboard_client->write_len);
        pogo_keyboard_client->read_flag = 0;
        return -1;
    }

    if (pogo_keyboard_client->write_buf[0] == pogo_keyboard_client->write_check_buf[0] &&
        pogo_keyboard_client->write_buf[out_len - 2] == pogo_keyboard_client->write_check_buf[out_len - 2]) { //compare cmd and crc
        ret = 0;
    } else {
        ret = -1;
        kb_err("cmd or crc not same, write_buf:0x%2x,0x%2x, write_check_buf:0x%2x,0x%2x\n",
            pogo_keyboard_client->write_buf[0], pogo_keyboard_client->write_check_buf[0],
            pogo_keyboard_client->write_buf[out_len - 2], pogo_keyboard_client->write_check_buf[out_len - 2]);
    }

    pogo_keyboard_client->write_len = 0;
    memset(pogo_keyboard_client->write_check_buf, 0, UART_BUFFER_SIZE);
    return ret;
}

int pogo_keyboard_read(void *buf, int *r_len)
{
    pogo_keyboard_client->read_flag = 1;
    wait_event_interruptible_timeout(read_waiter, pogo_keyboard_client->read_flag != 1, HZ / 10);//timeout 100ms
    if (pogo_keyboard_client->read_flag == 1) {
        kb_debug("read_flag:%d\n", pogo_keyboard_client->read_flag);
    }
    if (pogo_keyboard_client->read_len <= 0) {
        kb_err("read_len:%d  err\n", pogo_keyboard_client->read_len);
        pogo_keyboard_client->read_flag = 0;
        return -1;
    }
    memcpy(buf, pogo_keyboard_client->read_buf, pogo_keyboard_client->read_len);
    *r_len = pogo_keyboard_client->read_len;

    memset(pogo_keyboard_client->read_buf, 0, sizeof(pogo_keyboard_client->read_buf));
    pogo_keyboard_client->read_len = 0;
    memset(pogo_keyboard_client->read_buf, 0, UART_BUFFER_SIZE);

    return 0;
}

int pogo_keyboard_write_and_read(void *w_buf, int w_len, void *r_buf, int *r_len)
{
    int ret = 0;
    int i = 0;
    int count = 3;
    pm_stay_awake(&pogo_keyboard_client->plat_dev->dev);

    for (i = 0; i < count; i++) {
        ret = pogo_keyboard_write(w_buf, w_len);
        if (ret) {
            mdelay(50);
            continue;
        }

        if (r_buf == NULL) {
            break;
        }

        ret = pogo_keyboard_read(r_buf, r_len);
        if (ret == 0) {
            break;
        }
    }
    pm_relax(&pogo_keyboard_client->plat_dev->dev);
    if (i >= count) {
        kb_err("ret:%d i:%d err\n", ret, i);
        return ret;
    }
    kb_debug("ret:%d i:%d ok\n", ret, i);
    return 0;
}

//read touchpad version from keyboard.
int  pogo_keyboard_tp_ver(void)
{
    char ver_reg[] = { ONE_WIRE_BUS_PACKET_I2C_WRITE_CMD, 0x01, 0x07 };

    char temp[100] = { 0 };
    int read_len = 0;
    int ret = 0;

    ret = pogo_keyboard_write_and_read(ver_reg, sizeof(ver_reg), temp, &read_len);
    if (ret < 0) {
        kb_err("ret:%d \n", ret);
        return ret;
    }
    pogo_keyboard_show_buf(temp, read_len);
    return 0;
}

//read keyboard version.
int  pogo_keyboard_ver(void)
{
    char ver_reg[] = { ONE_WIRE_BUS_PACKET_USER_PASSTHROUGH_CMD, 0x03, 0x08,0x01, 0x01 };
    char buf[] = { ONE_WIRE_BUS_PACKET_USER_PASSTHROUGH_ACK_CMD };
    char temp[100] = { 0 };
    int read_len = 0;
    int count = 3;
    int ret = 0;
    int i = 0;
    for (i = 0; i < count; i++) {
        ret = pogo_keyboard_write_and_read(ver_reg, sizeof(ver_reg), temp, &read_len);
        if (ret < 0) {
            kb_err("ret:%d \n", ret);
            continue;
        }
        if (memcmp(temp, buf, sizeof(buf)) == 0) {
            break;
        }
    }

    if (i >= count) {
        kb_err("ret:%d \n", ret);
        return -1;
    }
    return 0;

}

//TRX data test
int  pogo_keyboard_trx_test(char *write_buf, u8 write_len, char *read_buf, u8 read_len)
{
    char temp[UART_BUFFER_SIZE] = { 0 };
    int len = 0;
    int count = 3;
    int ret = 0;
    int i = 0;
    if (write_len > UART_BUFFER_SIZE || read_len > UART_BUFFER_SIZE) {
        kb_err("set data out of range: %d %d\n", write_len, read_len);
        return -1;
    }

    for (i = 0; i < count; i++) {
        ret = pogo_keyboard_write_and_read(write_buf, write_len, temp, &len);
        if (ret < 0) {
            kb_err("ret:%d \n", ret);
            continue;
        }
        if (memcmp(temp, read_buf, read_len) == 0) {
            break;
        }
    }

    if (i >= count) {
        kb_err("ret:%d \n", ret);
        return -1;
    }
    return 0;

}

static int pogo_keyboard_set_touch_status(bool state)
{
    char write_buf[] = {ONE_WIRE_BUS_PACKET_USER_GENERAL_CMD, 0x03, 0x11, 0x01, 0x00};
    char buf[] = {ONE_WIRE_BUS_PACKET_USER_GENERAL_ACK_CMD, 0x03, 0x11, 0x01, 0x01};
    char buf2[] = {ONE_WIRE_BUS_PACKET_USER_GENERAL_ACK_CMD, 0x03, 0x0c, 0x01, 0x01};
    char temp[128] = {0};
    int read_len = 0;
    int count = 3;
    int ret = 0, i = 0;

    if (state) {
        //set tp disable
        write_buf[4] = 0x01;
        buf2[4] = 0x01;
    } else {
        //set tp enable
        write_buf[4] = 0x00;
        buf2[4] = 0x00;
    }

    for (i = 0; i < count; i++) {
        ret = pogo_keyboard_write_and_read(write_buf, sizeof(write_buf), temp, &read_len);
        if (ret < 0) {
            kb_err("ret:%d \n", ret);
            continue;
        }
        if ((memcmp(temp, buf2, sizeof(buf2)) == 0) || (memcmp(temp, buf, sizeof(buf)) == 0)) {
            break;
        }
    }
    if (i >= count) {
        kb_err("ret:%d \n", ret);
        return -1;
    }
    return 0;
}

static int pogo_keyboard_set_touch_gesture(bool state)
{
    char write_buf[] = {ONE_WIRE_BUS_PACKET_USER_GENERAL_CMD, 0x03, 0x17, 0x01, 0x00};
    char buf[] = {ONE_WIRE_BUS_PACKET_USER_GENERAL_ACK_CMD, 0x03, 0x17, 0x01, 0x01};
    char temp[128] = {0};
    int read_len = 0;
    int count = 3;
    int ret = 0, i = 0;

    if (state) {
        //set touch gesture enable
        write_buf[4] = 0x01;
    } else {
        //set touch gesture disable
        write_buf[4] = 0x00;
    }

    for (i = 0; i < count; i++) {
        ret = pogo_keyboard_write_and_read(write_buf, sizeof(write_buf), temp, &read_len);
        if (ret < 0) {
            kb_err("ret:%d \n", ret);
            continue;
        }
        if (memcmp(temp, buf, sizeof(buf)) == 0) {
            break;
        }
    }
    if (i >= count) {
        kb_err("ret:%d \n", ret);
        return -1;
    }
    return 0;
}

int pogo_keyboard_get_charge_current(void)
{
    char info_reg[] = {ONE_WIRE_BUS_PACKET_USER_GENERAL_CMD, 0x03, 0x09, 0x01, 0x01};
    char buf[] = {ONE_WIRE_BUS_PACKET_USER_GENERAL_ACK_CMD, 0x06, 0x09, 0x04};
    char temp[128] = {0};
    int read_len = 0;
    int count = 3;
    int ret = 0, i = 0;
    int charge_current = -1;

    for (i = 0; i < count; i++) {
        ret = pogo_keyboard_write_and_read(info_reg, sizeof(info_reg), temp, &read_len);
        if (ret < 0) {
            kb_err("ret:%d \n", ret);
            continue;
        }
        if (memcmp(temp, buf, sizeof(buf)) == 0) {
            break;
        }
    }
    if (i >= count) {
        kb_err("ret:%d \n", ret);
        return charge_current;
    }
    charge_current = (temp[7] << 8) + temp[6];
    kb_debug("current=%dmA\n", charge_current);
    return charge_current;
}

static int pogo_keyboard_set_touch_debug(bool state)
{
    char write_buf[] = {ONE_WIRE_BUS_PACKET_USER_GENERAL_CMD, 0x03, 0x19, 0x01, 0x00};
    char buf[] = {ONE_WIRE_BUS_PACKET_USER_GENERAL_ACK_CMD, 0x03, 0x19, 0x01, 0x01};
    char temp[128] = {0};
    int read_len = 0;
    int count = 3;
    int ret = 0, i = 0;

    if (state) {
        //set touch debug enable
        write_buf[4] = 0x01;
    } else {
        //set touch debug disable
        write_buf[4] = 0x00;
    }

    for (i = 0; i < count; i++) {
        ret = pogo_keyboard_write_and_read(write_buf, sizeof(write_buf), temp, &read_len);
        if (ret < 0) {
            kb_err("ret:%d \n", ret);
            continue;
        }
        if (memcmp(temp, buf, sizeof(buf)) == 0) {
            break;
        }
    }
    if (i >= count) {
        kb_err("ret:%d \n", ret);
        return -1;
    }
    return 0;
}

// output leds control cmd to keyboard.
static int pogo_keyboard_set_led(char event)
{
    int ret = 0;
    char led_buf[] = { ONE_WIRE_BUS_PACKET_PARAM_SET_CMD, 0x06, 0x0D, 0x04, 0x00, 0x00, 0x00, 0x00 };

    if (pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_CAPSLOCK_ON_STATUS) {
        led_buf[4] = 0x01;
    }
    if (pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_MUTEDISABLE_ON_STATUS) {
        led_buf[6] = 0x01;
    }
    if (pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_MICDISABLE_ON_STATUS) {
        led_buf[7] = 0x01;
    }

    mdelay(15);
    switch (event) {
        case  KEYBOARD_HOST_LCD_ON_EVENT:
            ret = pogo_keyboard_write(led_buf, sizeof(led_buf));
            if (ret) {
                kb_err("ret:%d \n", ret);
                return ret;
            }
            break;
        case KEYBOARD_HOST_LCD_OFF_EVENT:
            memset(&led_buf[4], 0, 4);
            ret = pogo_keyboard_write(led_buf, sizeof(led_buf));
            if (ret) {
                kb_err("ret:%d \n", ret);
                return ret;
            }
            break;
        case KEYBOARD_PLUG_IN_EVENT:
            ret = pogo_keyboard_write(led_buf, sizeof(led_buf));
            if (ret) {
                kb_err("ret:%d \n", ret);
                return ret;
            }
            break;
        case KEYBOARD_PLUG_OUT_EVENT:  /* for pre development test*/
            memset(&led_buf[4], 0, 4);
            ret = pogo_keyboard_write(led_buf, sizeof(led_buf));
            if (ret) {
                kb_err("ret:%d \n", ret);
                return ret;
            }
            break;

        case KEYBOARD_CAPSLOCK_ON_EVENT:
            led_buf[4] = 0x01;
            ret = pogo_keyboard_write(led_buf, sizeof(led_buf));
            if (ret) {
                kb_err("ret:%d \n", ret);
                return ret;
            }
            break;
        case KEYBOARD_CAPSLOCK_OFF_EVENT:
            led_buf[4] = 0x00;
            ret = pogo_keyboard_write(led_buf, sizeof(led_buf));
            if (ret) {
                kb_err("ret:%d \n", ret);
                return ret;
            }
            break;
        case KEYBOARD_MUTEDISABLE_ON_EVENT:
            led_buf[6] = 0x01;
            ret = pogo_keyboard_write(led_buf, sizeof(led_buf));
            if (ret) {
                kb_err("ret:%d \n", ret);
                return ret;
            }
            break;
        case KEYBOARD_MUTEDISABLE_OFF_EVENT:
            led_buf[6] = 0x00;
            ret = pogo_keyboard_write(led_buf, sizeof(led_buf));
            if (ret) {
                kb_err("ret:%d \n", ret);
                return ret;
            }
            break;
        case KEYBOARD_MICDISABLE_ON_EVENT:
            led_buf[7] = 0x01;
            ret = pogo_keyboard_write(led_buf, sizeof(led_buf));
            if (ret) {
                kb_err("ret:%d \n", ret);
                return ret;
            }
            break;
        case KEYBOARD_MICDISABLE_OFF_EVENT:
            led_buf[7] = 0x00;
            ret = pogo_keyboard_write(led_buf, sizeof(led_buf));
            if (ret) {
                kb_err("ret:%d \n", ret);
                return ret;
            }
            break;
        default:
            kb_err("no event to do err\r\n");
            break;
    }
    return ret;
}

// sync host lcd/screen on/off(sleep/wakeup state) state to keyboard. state=1 means wakeup while 0 means going to sleep.
int pogo_keyboard_set_lcd_state(bool state)
{
    int ret = 0;
    char write_buf[] = { ONE_WIRE_BUS_PACKET_USER_GENERAL_CMD, 0x03, 0x02, 0x01, 0x01 };
    char buf[] = { ONE_WIRE_BUS_PACKET_USER_GENERAL_ACK_CMD, 0x04, 0x02, 0x02, 0x01};
    char read_buf[255] = { 0 };
    int read_len = 0;
    int i = 0;

    if (state) {
        write_buf[4] = 0x00;
        buf[4] = 0x00;
    } else {
        write_buf[4] = 0x01;
        buf[4] = 0x01;
    }
    for (i = 0; i < 3; i++) {
        ret = pogo_keyboard_write_and_read(write_buf, sizeof(write_buf), read_buf, &read_len);
        if (ret) {
            continue;
        }
        if (memcmp(read_buf, buf, sizeof(buf)) == 0) {

            if (read_buf[5] == 1)
                break;

        }
    }
    if (i >= 3) {
        kb_err("ret:0x%02x status:%d\n", ret, read_buf[5]);
        return ret;
    }
    return 0;
}

static int pogo_keyboard_get_sn(void)
{
    char sn_reg[] = {ONE_WIRE_BUS_PACKET_USER_GENERAL_CMD, 0x04, 0x0b, 0x02, 0x02, 0x14};
    char buf[] = { ONE_WIRE_BUS_PACKET_USER_GENERAL_ACK_CMD };
    char temp[100] = { 0 };
    int read_len = 0;
    int count = 3;
    int ret = 0;
    int i = 0;
    for (i = 0; i < count; i++) {
        ret = pogo_keyboard_write_and_read(sn_reg, sizeof(sn_reg), temp, &read_len);
        if (ret < 0) {
            kb_err("ret:%d \n", ret);
            continue;
        }
        if (memcmp(temp, buf, sizeof(buf)) == 0 && temp[2] == 0x0b) {
            break;
        }
    }

    if (i >= count) {
        kb_err("ret:%d \n", ret);
        return -1;
    }
    return 0;

}

void pogo_keyboard_led_report(int key_value)
{
    char value = 0;
    if (!(key_value == KEY_CAPSLOCK || key_value == KEY_MUTE || key_value == KEY_MICDISABLE))
        return;
    // NOTE: currently caps lock led is controlled by upper system layer while nothing is done here.
    // should other leds follow the same? need further consideration in the future.
    if (key_value == KEY_CAPSLOCK) {
        return;
    } else if (key_value == KEY_MUTE) {
        if ((pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_MUTEDISABLE_ON_STATUS) == 0) {
            pogo_keyboard_client->pogo_keyboard_status |= KEYBOARD_MUTEDISABLE_ON_STATUS;
        } else {
            pogo_keyboard_client->pogo_keyboard_status &= (~KEYBOARD_MUTEDISABLE_ON_STATUS);
        }
        value = pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_MUTEDISABLE_ON_STATUS;
        input_event(pogo_keyboard_client->input_pogo_keyboard, EV_LED, LED_MUTE, !!value);
        kb_debug("key_value:%d value:%d\n", key_value, value);

    } else if (key_value == KEY_MICDISABLE) {
        if ((pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_MICDISABLE_ON_STATUS) == 0) {
            pogo_keyboard_client->pogo_keyboard_status |= KEYBOARD_MICDISABLE_ON_STATUS;
        } else {
            pogo_keyboard_client->pogo_keyboard_status &= (~KEYBOARD_MICDISABLE_ON_STATUS);
        }
        value = pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_MICDISABLE_ON_STATUS;
        input_event(pogo_keyboard_client->input_pogo_keyboard, EV_LED, LED_MIC_MUTE, !!value);
        kb_debug("key_value:%d value:%d\n", key_value, value);
    }

}

void pogo_keyboard_led_process(int code, int value)
{
    unsigned char event = 0;

    kb_debug(" type:led code:0x%02x value:0x%02x\n", code, value);
    switch (code) {
        case LED_CAPSL:
            if (value == 1) {
                event = KEYBOARD_CAPSLOCK_ON_EVENT;
                pogo_keyboard_client->pogo_keyboard_status |= KEYBOARD_CAPSLOCK_ON_STATUS;
            } else {
                event = KEYBOARD_CAPSLOCK_OFF_EVENT;
                pogo_keyboard_client->pogo_keyboard_status &= (~KEYBOARD_CAPSLOCK_ON_STATUS);
            }
            break;
        case LED_MUTE:
            if (value == 1) {
                event = KEYBOARD_MUTEDISABLE_ON_EVENT;
                pogo_keyboard_client->pogo_keyboard_status |= KEYBOARD_MUTEDISABLE_ON_STATUS;
            } else {
                event = KEYBOARD_MUTEDISABLE_OFF_EVENT;
                pogo_keyboard_client->pogo_keyboard_status &= (~KEYBOARD_MUTEDISABLE_ON_STATUS);
            }
            break;
        case LED_MIC_MUTE:
            if (value == 1) {
                event = KEYBOARD_MICDISABLE_ON_EVENT;
                pogo_keyboard_client->pogo_keyboard_status |= KEYBOARD_MICDISABLE_ON_STATUS;
            } else {
                event = KEYBOARD_MICDISABLE_OFF_EVENT;
                pogo_keyboard_client->pogo_keyboard_status &= (~KEYBOARD_MICDISABLE_ON_STATUS);
            }
            break;
        default:
            kb_info(" type:led code:0x%2x value:0x%2x no support!\n", code, value);
            return;
    }
    if ((pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_LCD_ON_STATUS) == 0)
        return;

    pogo_keyboard_event_send(event);
}

// monitor lcd/screen on/off state.
void pogo_keyboard_sync_lcd_state(bool lcd_on_state)
{
    if (lcd_on_state) {
        kb_info("pogo_keyboard goto wakeup\n");
        pogo_keyboard_event_send(KEYBOARD_HOST_LCD_ON_EVENT);
    } else {
        kb_info("pogo_keyboard goto sleep\n");
        pogo_keyboard_event_send(KEYBOARD_HOST_LCD_OFF_EVENT);
    }

    return;
}

static int pogo_keyboard_enable_uart_tx(int value)
{
    if (pogo_keyboard_client->file_client == NULL)
        return -1;
    if (value == 1) {
        if (gpio_get_value(pogo_keyboard_client->tx_en_gpio) == 0) {
            kb_info("%d\n", value);
            //gpio_direction_output(pogo_keyboard_client->tx_en_gpio, 1);
            gpio_set_value(pogo_keyboard_client->tx_en_gpio, 1);
            udelay(450);//for set mode valid
        }
    } else if (value == 0) {
        if (gpio_get_value(pogo_keyboard_client->tx_en_gpio) == 1) {
            kb_info("%d\n", value);
            udelay(300); //for set mode valid
            gpio_set_value(pogo_keyboard_client->tx_en_gpio, 0);
            //gpio_direction_output(pogo_keyboard_client->tx_en_gpio, 0);
        }
    }
    return 0;
}

bool pogo_keyboard_is_support(struct uart_port *port)
{
    bool ret = false;
    if (!port || !pogo_keyboard_client)
        return false;
    if (!strncmp(port->name, pogo_keyboard_client->tty_name, strlen(pogo_keyboard_client->tty_name))) {
        ret = true;
    }
    return ret;
}

static int pogo_keyboard_get_dts_info(struct platform_device *pdev)
{
    struct device_node *node = NULL;
    struct platform_device *device = pdev;
    u32 temp_data = 0;
    int ret = 0;

    kb_info("start\n");
    node = device->dev.of_node;
    if (!node) {
        kb_err("of node is not null\n");
        return -EINVAL;
    }

    ret = of_property_read_string(node, "tty-name-string", (const char **)&pogo_keyboard_client->tty_name);
    if (ret) {
        kb_err("read tty name err: %d\n", ret);
        return ret;
    }

    if (of_get_property(node, "touchpad-xy-max", NULL)) {
        pogo_keyboard_client->pogopin_touch_support = true;
        ret = of_property_read_u32_index(node, "touchpad-xy-max", 0, &pogo_keyboard_client->touchpad_x_max);
        if (ret) {
            kb_err("read touchpad-xy-max err: %d\n", ret);
            return ret;
        }
        ret = of_property_read_u32_index(node, "touchpad-xy-max", 1, &pogo_keyboard_client->touchpad_y_max);
        if (ret) {
            kb_err("read touchpad-xy-max err: %d\n", ret);
            return ret;
        }
        ret = of_property_read_u32_index(node, "touchpad-xy-resolution", 0, &pogo_keyboard_client->touchpad_x_resolution);
        if (ret) {
            kb_err("read touchpad-x-resolution err: %d, set default 0.\n", ret);
            pogo_keyboard_client->touchpad_x_resolution = 0;
        }
        ret = of_property_read_u32_index(node, "touchpad-xy-resolution", 1, &pogo_keyboard_client->touchpad_y_resolution);
        if (ret) {
            kb_err("read touchpad-y-resolution err: %d, set default 0.\n", ret);
            pogo_keyboard_client->touchpad_y_resolution = 0;
        }
        if (of_get_property(node, "touchpad-gesture-ignore", NULL)) {
            pogo_keyboard_client->touchpad_gesture_ignore = 1;
        }
    }

    pogo_keyboard_client->pogo_battery_support = of_property_read_bool(node, "pogopin-battery-support");
    kb_info("pogo_keyboard_client->pogo_battery_support: %d\n", pogo_keyboard_client->pogo_battery_support);

    if (of_get_property(node, "id-product", NULL)) {
        ret = of_property_read_u32_index(node, "id-product", 0, &temp_data);
        if (ret) {
            kb_err("read id-product err: %d\n", ret);
            return ret;
        }
        pogo_keyboard_client->pogo_id_product = (unsigned short)temp_data;
    }

    if (of_get_property(node, "crc-ibm-init-val", NULL)) {
        ret = of_property_read_u32_index(node, "crc-ibm-init-val", 0, &temp_data);
        if (ret) {
            kb_err("read crc-ibm-init-val err: %d\n", ret);
            return ret;
        }
        pogo_keyboard_client->crc_ibm_init_val = (unsigned short)temp_data;
        pogo_keyboard_client->get_crc_ibm_from_dts = true;
    }

    if (of_get_property(node, "pogopin-kb-fw-support", NULL)) {
        kb_info("pogopin-kb-fw-support\n");
        pogo_keyboard_client->pogopin_fw_support = true;

        if (of_get_property(node, "pogopin-kb-ota-dfu", NULL)) {
            kb_info("pogopin-kb-ota-dfu\n");
            pogo_keyboard_client->pogopin_ota_dfu = true;
            ret = of_property_read_u32_index(node, "ota-customize-datas", 0, &temp_data);
            if (ret) {
                kb_err("read dfu_fwinfo_start_addr err: %d\n", ret);
                return ret;
            }
            pogo_keyboard_client->dfu_fwinfo_start_addr = temp_data;
        } else {
            ret = of_property_read_u32_index(node, "ota-customize-datas", 0, &temp_data);
            if (ret) {
                kb_err("read ota_start_addr err: %d\n", ret);
                return ret;
            }
            pogo_keyboard_client->ota_start_addr = temp_data;

            ret = of_property_read_u32_index(node, "ota-customize-datas", 1, &temp_data);
            if (ret) {
                kb_err("ota_get_version_addr err: %d\n", ret);
                return ret;
            }
            pogo_keyboard_client->ota_get_version_addr = temp_data;

            ret = of_property_read_u32_index(node, "ota-customize-datas", 2, &temp_data);
            if (ret) {
                kb_err("read ota_send_data_start_addr err: %d\n", ret);
                return ret;
            }
            pogo_keyboard_client->ota_send_data_start_addr = temp_data;
        }

        ret = of_property_read_string(node, "ota-firmware-name", (const char **)&pogo_keyboard_client->ota_firmware_name);
        if (ret) {
            kb_err("read ota_firmware_name err: %d\n", ret);
            return ret;
        }
    }

    pogo_keyboard_client->pinctrl = devm_pinctrl_get(&device->dev);
    if (IS_ERR(pogo_keyboard_client->pinctrl)) {
        kb_err("devm_pinctrl_get  err\n");
        return -1;
    }

    pogo_keyboard_client->uart_rx_set = pinctrl_lookup_state(pogo_keyboard_client->pinctrl, "uart_rx_set");
    if (IS_ERR(pogo_keyboard_client->uart_rx_set)) {
        kb_err("pinctrl_lookup_state uart_rx_set err\n");
        return -1;
    }
    pogo_keyboard_client->uart_rx_clear = pinctrl_lookup_state(pogo_keyboard_client->pinctrl, "uart_rx_clear");
    if (IS_ERR(pogo_keyboard_client->uart_rx_clear)) {
        kb_err("pinctrl_lookup_state uart_rx_clear err\n");
        return -1;
    }
    ret = pinctrl_select_state(pogo_keyboard_client->pinctrl, pogo_keyboard_client->uart_rx_set);
    kb_info("pinctrl_select_state:%d\n", ret);

#ifdef CONFIG_BOARD_V4_SUPPORT
    pogo_keyboard_client->uart_wake_gpio_pin = pinctrl_lookup_state(pogo_keyboard_client->pinctrl, "uart_wake_gpio");
    if (IS_ERR(pogo_keyboard_client->uart_wake_gpio_pin)) {
        kb_err("pinctrl_lookup_state uart_wake_gpio_pin err\n");
        return -1;
    }
    pinctrl_select_state(pogo_keyboard_client->pinctrl, pogo_keyboard_client->uart_wake_gpio_pin);

    pogo_keyboard_client->uart_wake_gpio = of_get_named_gpio(node, "uart-wake-gpio", 0);
    if (!gpio_is_valid(pogo_keyboard_client->uart_wake_gpio)) {
        kb_err("uart_wake_gpio is not valid %d\n", pogo_keyboard_client->uart_wake_gpio);
        return -EINVAL;
    }
    ret = gpio_request_one(pogo_keyboard_client->uart_wake_gpio, GPIOF_DIR_IN, "uart_wake_gpio");
    if (ret) {
        kb_err("gpio_request_one err:%d\n", ret);
        return ret;
    }

    pogo_keyboard_client->uart_wake_gpio_irq = gpio_to_irq(pogo_keyboard_client->uart_wake_gpio);
    device_init_wakeup(&pogo_keyboard_client->plat_dev->dev, true);
    ret = dev_pm_set_wake_irq(&pogo_keyboard_client->plat_dev->dev, pogo_keyboard_client->uart_wake_gpio_irq);
    if (ret) {
        kb_err("dev_pm_set_wake_irq failed : %d\n", pogo_keyboard_client->uart_wake_gpio_irq);
        return ret;
    }
    ret = devm_request_threaded_irq(&device->dev, pogo_keyboard_client->uart_wake_gpio_irq, NULL,
        pogo_keyboard_irq_handler, IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "keyboard_wake_irq", NULL);
    if (ret < 0) {
        kb_err("request irq failed : %d\n", pogo_keyboard_client->uart_wake_gpio_irq);
        return -EINVAL;
    }

    pogo_keyboard_client->tx_en_gpio = of_get_named_gpio(node, "uart-tx-en-gpio", 0);
    if (!gpio_is_valid(pogo_keyboard_client->uart_wake_gpio)) {
        kb_err("uart_wake_gpio is not valid %d\n", pogo_keyboard_client->uart_wake_gpio);
        return -EINVAL;
    }
    kb_info("uart_wake_gpio:%d tx_en_gpio:%d uart_wake_gpio_irq:%d\n", pogo_keyboard_client->uart_wake_gpio,
        pogo_keyboard_client->tx_en_gpio, pogo_keyboard_client->uart_wake_gpio_irq);
    gpio_direction_output(pogo_keyboard_client->tx_en_gpio, 0);

#endif

    if (of_get_property(node, "pogopin-power-supply", NULL)) {
        kb_info("get vcc from ldo\n");
        pogo_keyboard_client->get_vcc_from_ldo = true;
        if (!pogo_keyboard_client->vcc_reg) {
            pogo_keyboard_client->vcc_reg = devm_regulator_get(&device->dev, "pogopin-power");
            if (IS_ERR(pogo_keyboard_client->vcc_reg)) {
                ret = PTR_ERR(pogo_keyboard_client->vcc_reg);
                kb_err("Failed to get regulator vcc:%d\n", ret);
                pogo_keyboard_client->vcc_reg = NULL;
                return ret;
            } else {
                kb_info("vcc regulator get success!!!\n");
            }
        }
    } else {
        pogo_keyboard_client->pogo_power_enable = pinctrl_lookup_state(pogo_keyboard_client->pinctrl, "pogo_power_enable");
        if (IS_ERR(pogo_keyboard_client->pogo_power_enable)) {
            kb_err("pinctrl_lookup_state pogo_power_enable err\n");
            return -1;
        }
        pogo_keyboard_client->pogo_power_disable = pinctrl_lookup_state(pogo_keyboard_client->pinctrl, "pogo_power_disable");
        if (IS_ERR(pogo_keyboard_client->pogo_power_disable)) {
            kb_err("pinctrl_lookup_state pogo_power_disable err\n");
            return -1;
        }
        pinctrl_select_state(pogo_keyboard_client->pinctrl, pogo_keyboard_client->pogo_power_disable);
    }

    kb_info("ret: %d ok\n", ret);
    return 0;
}

static char *pogo_keyboard_get_keyboard_name(void)
{
    int ret = 0, name_cnt = 0;

    if (pogo_keyboard_client == NULL) {
        kb_err("pogo_keyboard_client is null\n");
        return NULL;
    }

    if (pogo_keyboard_client->keyboard_brand == 0) {
        kb_err("keyboard_brand is empty\n");
        return NULL;
    }

    if (pogo_keyboard_client->is_confidential != true) {
        name_cnt = of_property_count_strings(pogo_keyboard_client->plat_dev->dev.of_node, "keyboard-name-strings");
        if (pogo_keyboard_client->keyboard_brand > name_cnt) {
            kb_err("keyboard_brand out of range\n");
            return NULL;
        }
        ret = of_property_read_string_index(pogo_keyboard_client->plat_dev->dev.of_node, "keyboard-name-strings",
        pogo_keyboard_client->keyboard_brand - 1, (const char **)&pogo_keyboard_client->keyboard_name);
        if (ret) {
            kb_err("read keyboard name err: %d\n", ret);
            return NULL;
        }
    } else {
        name_cnt = of_property_count_strings(pogo_keyboard_client->plat_dev->dev.of_node, "keyboard-name-strings-enc");
        if (name_cnt < 0) {
            kb_info("keyboard-name-strings-enc not set in dts,use default strings\n");
            pogo_keyboard_client->keyboard_name =
                    (pogo_keyboard_client->keyboard_brand - 1) ? "OnePlus Keyboard" : "OPPO Pad Keyboard";
        } else {
            if (pogo_keyboard_client->keyboard_brand > name_cnt) {
                kb_err("keyboard_brand out of range\n");
                return NULL;
            }
            ret = of_property_read_string_index(pogo_keyboard_client->plat_dev->dev.of_node, "keyboard-name-strings-enc",
            pogo_keyboard_client->keyboard_brand - 1, (const char **)&pogo_keyboard_client->keyboard_name);
            if (ret) {
                kb_err("read keyboard name err: %d\n", ret);
                return NULL;
            }
        }
    }
    return pogo_keyboard_client->keyboard_name;
}

static char *pogo_keyboard_get_keyboard_ble_name(void)
{
    int ret = 0, name_cnt = 0;

    if (pogo_keyboard_client == NULL) {
        kb_err("pogo_keyboard_client is null\n");
        return NULL;
    }

    if (pogo_keyboard_client->keyboard_brand == 0) {
        kb_err("keyboard_brand error\n");
        return NULL;
    }

    if (pogo_keyboard_client->is_confidential != true) {
        name_cnt = of_property_count_strings(pogo_keyboard_client->plat_dev->dev.of_node, "keyboard-ble-name-strings");
        if (pogo_keyboard_client->keyboard_brand > name_cnt) {
            kb_err("keyboard_brand out of range\n");
            return NULL;
        }
        ret = of_property_read_string_index(pogo_keyboard_client->plat_dev->dev.of_node, "keyboard-ble-name-strings",
        pogo_keyboard_client->keyboard_brand - 1, (const char **)&pogo_keyboard_client->keyboard_ble_name);
        if (ret) {
            kb_err("read keyboard name err: %d\n", ret);
            return NULL;
        }
    } else {
        name_cnt = of_property_count_strings(pogo_keyboard_client->plat_dev->dev.of_node, "keyboard-ble-name-strings-enc");
        if (name_cnt < 0) {
            kb_info("keyboard-ble-name-strings-enc not set in dts,use default strings\n");
            pogo_keyboard_client->keyboard_ble_name =
                    (pogo_keyboard_client->keyboard_brand - 1) ? "OnePlus Keyboard" : "OPPO Pad Keyboard";
        } else {
            if (pogo_keyboard_client->keyboard_brand > name_cnt) {
                kb_err("keyboard_brand out of range\n");
                return NULL;
            }
            ret = of_property_read_string_index(pogo_keyboard_client->plat_dev->dev.of_node, "keyboard-ble-name-strings-enc",
            pogo_keyboard_client->keyboard_brand - 1, (const char **)&pogo_keyboard_client->keyboard_ble_name);
            if (ret) {
                kb_err("read keyboard name err: %d\n", ret);
                return NULL;
            }
        }
    }
    return pogo_keyboard_client->keyboard_ble_name;
}

#ifdef CONFIG_POWER_CTRL_SUPPORT
// turn on/off vcc for keyboard.
static void pogo_keyboard_power_enable(int value)
{
    int ret = 0;
    if (pogo_keyboard_client == NULL)
        return;

    if (pogo_keyboard_client->get_vcc_from_ldo) {
        if (value == 1) {
            if (pogo_keyboard_client->vcc_reg && !regulator_is_enabled(pogo_keyboard_client->vcc_reg)) {
                ret = regulator_enable(pogo_keyboard_client->vcc_reg);
                if(!ret) {
                    kb_info("enable vcc success!!!\n");
                } else {
                    kb_info("enable vcc failed!!!\n");
                }
            } else {
                kb_info("vcc is on or null, keep!!!\n");
            }
        } else if (value == 0) {
            if (pogo_keyboard_client->vcc_reg && regulator_is_enabled(pogo_keyboard_client->vcc_reg)) {
                ret = regulator_disable(pogo_keyboard_client->vcc_reg);
                if(!ret) {
                    kb_info("disable vcc success!!!\n");
                } else {
                    kb_info("disable vcc failed!!!\n");
                }
            } else {
                kb_info("vcc is off or null, keep!!!\n");
            }
        }
    } else {
        if (value == 1) {
            pinctrl_select_state(pogo_keyboard_client->pinctrl, pogo_keyboard_client->pogo_power_enable);
            kb_info("enable value:%d\n", value);
        } else if (value == 0) {
            pinctrl_select_state(pogo_keyboard_client->pinctrl, pogo_keyboard_client->pogo_power_disable);
            kb_info("enable value:%d\n", value);
        }
    }
}
#endif

#ifdef CONFIG_BOARD_V4_SUPPORT
// handler for uart rx wakup interrupt(press key while pad is in sleep) and keyboard attachment detection.
static irqreturn_t pogo_keyboard_irq_handler(int irq, void *data)
{
    int value;

    // process only when keyboard is not connected.
    if ((pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_CONNECT_STATUS) == 0) {
        /*low level:plug in irq handler*/

        kb_info("\n");
        // keyboard will drive a low signal through TRX data pin by keyboard hardware design.
        value = gpio_get_value(pogo_keyboard_client->uart_wake_gpio);
        if (value == 0) {
            kb_info("disable_irq_nosync\n");
            disable_irq_nosync(irq);
            pogo_keyboard_client->check_disconnect_count = 0;
            pogo_keyboard_client->check_connect_count = 0;
            pogo_keyboard_plugin_check_timer_switch(true);
        }
    }
    return IRQ_HANDLED;
}
#else
static irqreturn_t pogo_keyboard_rx_gpio_irq_handler(int irq, void *data)
{
    kb_info("\n");
    disable_irq_wake(pogo_keyboard_client->uart_rx_gpio_irq);
    pogo_keyboard_event_send(KEYBOARD_HOST_RX_UART_EVENT);
    return IRQ_HANDLED;
}
#endif

static ssize_t test_mode_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    char *p = NULL;
    char *s = NULL;
    char *k = NULL;
    //size_t litmit 2048
    char tmp[UART_BUFFER_SIZE * 7] = {0};
    char seps[] = ",\t\n";
    int i = 0;
    unsigned long value[2] = {0};
    bool power_en = 0;

    if (count > UART_BUFFER_SIZE * 7)
        return 0;
    memcpy(tmp, buf, count);
    s = tmp;
    while (i < 2 && s != NULL) {
        p = strsep(&s, seps);
        value[i++] = simple_strtoul(p, NULL, 10);
        kb_info("value[%i]:%lu\n", i-1, value[i-1]);
    }
    switch (value[0]) {
        case 1:
            if (pogo_keyboard_client &&
                (pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_CONNECT_STATUS))
            pogo_keyboard_ver();
            break;
        case 2:
            if (pogo_keyboard_client &&
                (pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_CONNECT_STATUS))
            pogo_keyboard_tp_ver();
            break;
        case 3:
            if (pogo_keyboard_client == NULL ||
                (pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_CONNECT_STATUS) == 0)
                return count;
            test_count = value[1];
            test_fail_sum = 0;
            if (s != NULL) {
                i = 0;
                p = strsep(&s, seps);
                while (i < UART_BUFFER_SIZE && p != NULL) {
                    k = strsep(&p, " ");
                    if (strlen(k) == 2) {
                        send_buf[i++] = (unsigned char)simple_strtoul(k, NULL, 16);
                        send_len = i;
                    } else if (strlen(k) > 2) {
                        kb_err("input data format not right!!! E: aa bb 11 22\n");
                        return count;
                    } else {
                        kb_err("ignore space\n");
                        continue;
                    }
                }
            }
            if (s != NULL) {
                i = 0;
                p = strsep(&s, seps);
                while (i < UART_BUFFER_SIZE && p != NULL) {
                    k = strsep(&p, " ");
                    if (strlen(k) == 2) {
                        ack_buf[i++] = (unsigned char)simple_strtoul(k, NULL, 16);
                        ack_len = i;
                    } else if (strlen(k) > 2) {
                        kb_err("input data format not right!!! E: aa bb 11 22\n");
                        return count;
                    } else {
                        kb_err("ignore space\n");
                        continue;
                    }
                }
            }
            schedule_work(&pogo_keyboard_client->kpd_trx_test_work);
            break;
        case 4:
            power_en = value[1] ? 0 : 1;
            pogo_keyboard_power_enable(power_en);
            break;
        case 5:
            pogo_keyboard_client->max_disconnect_count = value[1];
            kb_info("max_disconnect_count:%d\n", pogo_keyboard_client->max_disconnect_count);
            break;
        case 6:
            pogo_keyboard_client->max_plug_in_disconnect_count = value[1];
            kb_info("max_plug_in_disconnect_count:%d\n", pogo_keyboard_client->max_plug_in_disconnect_count);
            break;
        default:
            break;
    }
    return count;
}

static ssize_t test_mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    return snprintf(buf, PROC_PAGE_LEN - 1, "test_fail_sum : %d\n", test_fail_sum);
}

static DEVICE_ATTR(test_mode, S_IRUGO | S_IWUSR, test_mode_show, test_mode_store);

static struct attribute *pogo_keyboard_attributes[] = {
    &dev_attr_test_mode.attr,
    NULL
};

static struct attribute_group pogo_keyboard_attribute_group = {
    .attrs = pogo_keyboard_attributes
};

static void pogo_keyboard_report_tp_dist(void)
{
    char temp[COLS * 7 + 5 + 1 ] = {0}; // 每个元素6字符(十进制) + 1空格 + 5头显示+ 终止符
    int len = 0;
    int i, j = 0;
    kb_info("--------tp_debug_s--------\n");
    for (i = 0; i < ROWS; i++) {
        len = 0;
        memset(temp, 0, sizeof(temp));
        len += scnprintf(temp + len, sizeof(temp) - len, "TX%2d:", i);
        for (j = 0; j < COLS; j++) {
            len += scnprintf(temp + len, sizeof(temp) - len, "%6d", tp_data_dist[i][j]);
            if (j < COLS - 1) {
                len += scnprintf(temp + len, sizeof(temp) - len, " ");
            }
        }
        len += scnprintf(temp + len, sizeof(temp) - len, "\n");
        kb_info("%s", temp);
    }
    kb_info("--------tp_debug_e--------\n");
}

static int pogo_keyboard_input_connect(void)
{
    int ret = 0;
    if (!pogo_keyboard_client->input_pogo_keyboard) {
        ret = pogo_keyboard_input_init(pogo_keyboard_get_keyboard_name());
        if (ret) {
            kb_err("pogo_keyboard_input_init err:%d\n", ret);
            return ret;
        }
    }

    if (pogo_keyboard_client->pogopin_touch_support && !pogo_keyboard_client->input_touchpad) {
        ret = touchpad_input_init();
        if (ret) {
            kb_err("touchpad_input_init err:%d\n", ret);
            return ret;
        }
    }
    return 0;
}

static void pogo_keyboard_input_disconnect(void)
{
    if (pogo_keyboard_client->input_touchpad) {

        input_unregister_device(pogo_keyboard_client->input_touchpad);
        //input_free_device(pogo_keyboard_client->input_touchpad);
        kb_info("input_unregister_device \n");
        pogo_keyboard_client->input_touchpad = NULL;
    }
    if (pogo_keyboard_client->input_pogo_keyboard) {
        input_unregister_device(pogo_keyboard_client->input_pogo_keyboard);
        //input_free_device(pogo_keyboard_client->input_pogo_keyboard);
        kb_info("input_unregister_device \n");
        pogo_keyboard_client->input_pogo_keyboard = NULL;
    }
    return;
}

static void pogo_keyboard_report_toggle_key(void)
{
    if (!pogo_keyboard_client || !pogo_keyboard_client->input_pogo_keyboard)
        return;
    kb_debug("\n");
    input_report_key(pogo_keyboard_client->input_pogo_keyboard, KEY_TOUCHPAD_TOGGLE, 1);
    input_sync(pogo_keyboard_client->input_pogo_keyboard);
    mdelay(10);
    input_report_key(pogo_keyboard_client->input_pogo_keyboard, KEY_TOUCHPAD_TOGGLE, 0);
    input_sync(pogo_keyboard_client->input_pogo_keyboard);
}

static bool pogo_keyboard_key_up(void)
{
    bool ret = false;
    if (!pogo_keyboard_client || !pogo_keyboard_client->input_pogo_keyboard)
        return ret;

    if (pogo_keyboard_client->is_down) {
        input_report_key(pogo_keyboard_client->input_pogo_keyboard, pogo_keyboard_client->down_code, 0);
        input_sync(pogo_keyboard_client->input_pogo_keyboard);
        memset(pogo_keyboard_client->old, 0, sizeof(pogo_keyboard_client->old));
        pogo_keyboard_client->is_down = false;
        kb_info("key code %d up\n", pogo_keyboard_client->down_code);
        ret = true;
    }
    if (pogo_keyboard_client->is_mmdown) {
        input_report_key(pogo_keyboard_client->input_pogo_keyboard, pogo_keyboard_client->down_mmcode, 0);
        input_sync(pogo_keyboard_client->input_pogo_keyboard);
        memset(pogo_keyboard_client->mm_old, 0, sizeof(pogo_keyboard_client->mm_old));
        pogo_keyboard_client->is_mmdown = false;
        kb_info("key mmcode %d up\n", pogo_keyboard_client->down_mmcode);
        ret = true;
    }
    return ret;
}

static bool pogo_keyboard_touch_up(void)
{
    bool ret = false;
    int i = 0;
    if (!pogo_keyboard_client || !pogo_keyboard_client->input_touchpad)
        return ret;

    if (pogo_keyboard_client->touch_down) { //finger all up
        kb_info("touch_down %d\n", pogo_keyboard_client->touch_down);
        input_mt_report_slot_state(pogo_keyboard_client->input_touchpad, MT_TOOL_FINGER, false);
        reset_tool_buttons(pogo_keyboard_client->input_touchpad);
        for (i = 0; i < TOUCH_FINGER_MAX; i++) {
            if (BIT(i) & pogo_keyboard_client->touch_down) {
                input_mt_slot(pogo_keyboard_client->input_touchpad, i);
                input_report_abs(pogo_keyboard_client->input_touchpad, ABS_MT_TOUCH_MINOR, 0);
                input_report_abs(pogo_keyboard_client->input_touchpad, ABS_MT_TOUCH_MAJOR, 0);
                input_mt_report_slot_state(pogo_keyboard_client->input_touchpad, MT_TOOL_FINGER, false);
                kb_info("finger up id:%d\n", i);
            }
        }
        input_report_key(pogo_keyboard_client->input_touchpad, BTN_TOUCH, 0);
        input_sync(pogo_keyboard_client->input_touchpad);
        pogo_keyboard_client->touch_temp = 0;
        pogo_keyboard_client->touch_down = 0;
        pogo_keyboard_client->prev_finger_count = 0;
        kb_info("finger all up\n");
        ret = true;
    }
    return ret;
}

static void pogo_keyboard_connect_send_uevent(void)
{
    char status_string[32], mac_string[32], name_string[64], sn_string[32];
    char *envp[] = { status_string, mac_string, name_string, sn_string, NULL };
    char *keyboard_ble_name = NULL;
    int ret = 0;

    if (!pogo_keyboard_client || !pogo_keyboard_client->pogo ||
        !pogo_keyboard_client->pogo->uevent_dev) {
        kb_err("pogo_keyboard_client or pogo or uevent_dev is NULL \n");
        return;
    }
    snprintf(status_string, sizeof(status_string), "pogopin_status=%d",
        pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_CONNECT_STATUS);

    snprintf(mac_string, sizeof(mac_string), "mac_addr=%012llx", pogo_keyboard_client->mac_addr);
    keyboard_ble_name = pogo_keyboard_get_keyboard_ble_name();
    if (keyboard_ble_name == NULL) {
        keyboard_ble_name = KEYBOARD_NAME;
    }
    snprintf(name_string, sizeof(name_string), "keyboard_ble_name=%s", keyboard_ble_name);

    snprintf(sn_string, sizeof(sn_string), "report_sn=%s", pogo_keyboard_client->report_sn);

    ret = kobject_uevent_env(&pogo_keyboard_client->pogo->uevent_dev->kobj, KOBJ_CHANGE, envp);
    if (ret) {
        kb_err("kobject_uevent_fail, ret = %d", ret);
    }

    kb_info("send uevent:%s %s %s %s.\n",
        status_string, mac_string, name_string, sn_string);
}

static void pogo_keyboard_connect_send_nfc_uevent(void)
{
    char nfc_string[32];
    char *envp[] = { nfc_string, NULL };
    int ret = 0;

    if (!pogo_keyboard_client || !pogo_keyboard_client->nfc ||
        !pogo_keyboard_client->nfc->uevent_dev) {
        kb_err("pogo_keyboard_client or nfc or uevent_dev is NULL \n");
        return;
    }

    snprintf(nfc_string, sizeof(nfc_string), "nfc_sta=%d", pogo_keyboard_client->nfc_status);

    ret = kobject_uevent_env(&pogo_keyboard_client->nfc->uevent_dev->kobj, KOBJ_CHANGE, envp);
    if (ret) {
        kb_err("kobject_uevent_fail, ret = %d", ret);
    }

    kb_info("send uevent:%s.\n", nfc_string);
}

static void pogo_keyboard_connect_send_uart_uevent(bool status)
{
    char uart_string[32];
    char *envp[] = { uart_string, NULL };
    int ret = 0;

    if (!pogo_keyboard_client || !pogo_keyboard_client->pogo_uart ||
        !pogo_keyboard_client->pogo_uart->uevent_dev) {
        kb_err("pogo_keyboard_client or pogo_uart or uevent_dev is NULL \n");
        return;
    }

    if (status) {
        snprintf(uart_string, sizeof(uart_string), "ACTION=add");
        ret = kobject_uevent_env(&pogo_keyboard_client->pogo_uart->uevent_dev->kobj, KOBJ_ADD, envp);
        if (ret) {
            kb_err("kobject_uevent add fail, ret = %d", ret);
        }
        kb_info("send uevent uart add, %s\n", uart_string);
    } else {
        snprintf(uart_string, sizeof(uart_string), "ACTION=remove");
        ret = kobject_uevent_env(&pogo_keyboard_client->pogo_uart->uevent_dev->kobj, KOBJ_REMOVE, envp);
        if (ret) {
            kb_err("kobject_uevent add fail, ret = %d", ret);
        }
        kb_info("send uevent uart remove, %s\n", uart_string);
    }
}

static void handle_plug_in(unsigned char pogo_keyboard_event)
{
    int ret = 0;

    if (!pogo_keyboard_client) {
        kb_err("pogo_keyboard_client is NULL \n");
        return;
    }
    if ((pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_CONNECT_STATUS) == 0 &&
        atomic_read(&pogo_keyboard_client->vcc_on)) {
        pogo_keyboard_client->pogo_keyboard_status |= KEYBOARD_CONNECT_STATUS;

        ret = pogo_keyboard_input_connect();
        if (ret) {
            kb_err("pogo_keyboard_input_connect err!\n");
        } else {
            pogo_keyboard_key_up();
            if (pogo_keyboard_client->pogopin_touch_support)
                pogo_keyboard_touch_up();
        }
        pogo_keyboard_get_sn();
        pogo_keyboard_client->keypad_pluginout_state = 1;//for factory test detect
        pogo_keyboard_heartbeat_switch(1);
        pogo_keyboard_client->poweroff_timer_check_count = 0;
        pogo_keyboard_client->sync_lcd_state_cnt = 0;
        pogo_keyboard_ver();
        if (pogo_keyboard_client->pogopin_fw_support) {
            pogo_keyboard_client->kpd_fw_status = FW_UPDATE_READY;
            pogo_keyboard_client->fw_update_progress = 0;
        }
        pogo_keyboard_connect_send_uevent();

    }
}

static void handle_plug_out(unsigned char pogo_keyboard_event)
{
    int ret = 0;

    if (!pogo_keyboard_client) {
        kb_err("pogo_keyboard_client is NULL \n");
        return;
    }
    if ((pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_CONNECT_STATUS) != 0) {
        pogo_keyboard_enable_uart_tx(0);
        if (pogo_keyboard_client->pogopin_touch_support)
            ret = pogo_keyboard_touch_up();
        if (pogo_keyboard_key_up() || ret)
            mdelay(10);
        pogo_keyboard_input_disconnect();
        pogo_keyboard_client->pogo_keyboard_status &= ~KEYBOARD_CONNECT_STATUS;
        pogo_keyboard_connect_send_uevent();
        pogo_keyboard_client->keypad_pluginout_state = 0;//for factory test detect
    }
}

static void handle_host_lcd_on(unsigned char pogo_keyboard_event)
{
    int ret = 0;

    if (!pogo_keyboard_client) {
        kb_err("pogo_keyboard_client is NULL \n");
        return;
    }
    pogo_keyboard_client->pogo_keyboard_status |= KEYBOARD_LCD_ON_STATUS;
    if (pogo_keyboard_client->pogopin_touch_support)
        pogo_keyboard_client->pogo_report_touch_status = 1;
    if ((pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_CONNECT_STATUS) != 0) {

        ret = pogo_keyboard_set_lcd_state(true);
        if (ret) {
            kb_err("pogo_keyboard_set_lcd_state err!\n");
        }
    }
    pogo_keyboard_heartbeat_switch(true);
    pogo_keyboard_key_up();
    if (pogo_keyboard_client->pogopin_touch_support) {
        pogo_keyboard_touch_up();
    }
}

static void handle_host_lcd_off(unsigned char pogo_keyboard_event)
{
    int ret = 0;

    if (!pogo_keyboard_client) {
        kb_err("pogo_keyboard_client is NULL \n");
        return;
    }
    if ((pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_CONNECT_STATUS) != 0) {
        if (!pogo_keyboard_client->pogopin_fw_support ||
            pogo_keyboard_client->kpd_fw_status != FW_UPDATE_START ) {
            ret = pogo_keyboard_set_lcd_state(false);
            if (ret) {
                kb_err("pogo_keyboard_set_lcd_state err!\n");
                if (atomic_read(&kb_heart_in_suspend) == 1) {
                    atomic_inc(&kb_suspend_failed_count);
                    kb_err("kb_suspend_failed_count:%d\n", atomic_read(&kb_suspend_failed_count));
                }
            }
        } else {
            kb_err("pogo keyboard ota started, keep keyboard wake!\n");
        }
        pogo_keyboard_heartbeat_switch(false);
    }
    pogo_keyboard_client->pogo_keyboard_status &= ~KEYBOARD_LCD_ON_STATUS;
    pogo_keyboard_client->sync_lcd_state_cnt = 0;
    atomic_set(&kb_heart_in_suspend, 0);
}

static void handle_leds(unsigned char pogo_keyboard_event)
{
    if (!pogo_keyboard_client) {
        kb_err("pogo_keyboard_client is NULL \n");
        return;
    }
    if ((pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_CONNECT_STATUS) != 0) {
        pogo_keyboard_set_led(pogo_keyboard_event);
    }
}

static void handle_host_rx_gpio(unsigned char pogo_keyboard_event)
{
    if (!pogo_keyboard_client) {
        kb_err("pogo_keyboard_client is NULL \n");
        return;
    }
    enable_irq_wake(pogo_keyboard_client->uart_wake_gpio_irq);
}

static void handle_host_rx_uart(unsigned char pogo_keyboard_event)
{
    if (!pogo_keyboard_client) {
        kb_err("pogo_keyboard_client is NULL \n");
        return;
    }
    disable_irq_wake(pogo_keyboard_client->uart_wake_gpio_irq);
}

static void handle_power_on(unsigned char pogo_keyboard_event)
{
    if (!pogo_keyboard_client) {
        kb_err("pogo_keyboard_client is NULL \n");
        return;
    }
    if (atomic_read(&pogo_keyboard_client->vcc_on)) {
        pogo_keyboard_event_send(KEYBOARD_REPORT_UART_OPEN_EVENT);
        mdelay(10);
        pogo_keyboard_heartbeat_switch(1);
#ifdef CONFIG_POWER_CTRL_SUPPORT
        pogo_keyboard_power_enable(1);
#endif
        pogo_keyboard_client->disconnect_count = 0;
        pogo_keyboard_client->plug_in_count = 0;
    }
}

static void handle_power_off(unsigned char pogo_keyboard_event)
{
    if (!pogo_keyboard_client) {
        kb_err("pogo_keyboard_client is NULL \n");
        return;
    }
    if (!atomic_read(&pogo_keyboard_client->vcc_on)) {
#ifdef CONFIG_POWER_CTRL_SUPPORT
        pogo_keyboard_power_enable(0);
#endif
        if ((pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_CONNECT_STATUS) != 0) {
            pogo_keyboard_event_send(KEYBOARD_PLUG_OUT_EVENT);
        }
    }
    if (pogo_keyboard_client->pogopin_ota_dfu) {
        pogo_keyboard_client->tp_ota_status = OTA_STATUS_INACTIVE;
        pogo_keyboard_client->max_disconnect_count = DEFAULT_DISCONNECT_COUNT;
        pogo_keyboard_client->max_plug_in_disconnect_count = DEFAULT_PLUGIN_DISCONNECT_COUNT;
    }
    kb_debug("KEYBOARD_POWER_OFF_EVENT %d\n", pogo_keyboard_client->poweroff_timer_check_count);
    if (pogo_keyboard_client->poweroff_timer_check_count < POWEROFF_TIMER_CHECK_MAX) {
        pogo_keyboard_client->poweroff_disconnect_count = 0;
        pogo_keyboard_client->poweroff_connect_count = 0;
        pogo_keyboard_client->poweroff_timer_check_count++;
        pogo_keyboard_poweroff_timer_switch(true);
    } else {
        kb_info("enable_irq\n");
        enable_irq(pogo_keyboard_client->uart_wake_gpio_irq);
        pogo_keyboard_event_send(KEYBOARD_REPORT_UART_CLOSE_EVENT);
    }
}

static void handle_report_sn(unsigned char pogo_keyboard_event)
{
    int ret = 0;
    char report[MAX_POGOPIN_PAYLOAD_LEN];
    char hidcode[KB_SN_HIDE_BIT_LEN];

    if (!pogo_keyboard_client) {
        kb_err("pogo_keyboard_client is NULL \n");
        return;
    }
    memset(report, 0, sizeof(report));
    memset(hidcode, KB_SN_HIDE_STAR_ASCII, sizeof(hidcode));
    snprintf(report, MAX_POGOPIN_PAYLOAD_LEN - 1, "$$sn@@%s", pogo_keyboard_client->report_sn);
    kb_info("keyboard sn:%s\n", report);
    memcpy(&report[KB_SN_HIDE_BIT_START], hidcode, sizeof(hidcode));
    ret = upload_pogopin_kevent_data(report);
    if (ret) {
        kb_err("pogopin report sn err\n");
    }
    if (sn_report_count < 2) {
        sn_report_count ++;
    }
}

static void handle_report_touch_status(unsigned char pogo_keyboard_event)
{
    pogo_keyboard_report_toggle_key();
}

static void handle_report_kbmcu_ver(unsigned char pogo_keyboard_event)
{
    if (!pogo_keyboard_client) {
        kb_err("pogo_keyboard_client is NULL \n");
        return;
    }
    pogo_keyboard_client->kpdmcu_mcu_version =
        (pogo_keyboard_client->report_kbver[8] & 0x0f) << 8 |
        (pogo_keyboard_client->report_kbver[10] & 0x0f) << 4 |
        (pogo_keyboard_client->report_kbver[12] & 0x0f);
    kb_info("keyboard version: 0x%04x\n", pogo_keyboard_client->kpdmcu_mcu_version);
    if (pogo_keyboard_client->kpdmcu_mcu_version < pogo_keyboard_client->kpdmcu_fw_data_ver) {
        pogo_keyboard_client->is_kpdmcu_need_fw_update = true;
    } else {
        pogo_keyboard_client->is_kpdmcu_need_fw_update = false;
    }
    //1.0.7_TP_A_0C not support tp ota
    if ((pogo_keyboard_client->pogopin_ota_dfu) &&
        (pogo_keyboard_client->report_kbver[pogo_keyboard_client->kbver_len - 1] == 0x43)) {
        pogo_keyboard_client->is_kpdmcu_need_fw_update = true;
    }
}

static void handle_report_kblog(unsigned char pogo_keyboard_event)
{
    char report[MAX_POGOPIN_PAYLOAD_LEN];

    if (!pogo_keyboard_client) {
        kb_err("pogo_keyboard_client is NULL \n");
        return;
    }
    memset(report, 0, sizeof(report));
    snprintf(report, MAX_POGOPIN_PAYLOAD_LEN - 1, "%s", pogo_keyboard_client->report_kblog);
    kb_info("keyboard log:%s\n", report);
}

static void handle_report_nfc_sta(unsigned char pogo_keyboard_event)
{
    pogo_keyboard_connect_send_nfc_uevent();
}

static void handle_report_uart_open(unsigned char pogo_keyboard_event)
{
    pogo_keyboard_connect_send_uart_uevent(1);
}

static void handle_report_uart_close(unsigned char pogo_keyboard_event)
{
    pogo_keyboard_connect_send_uart_uevent(0);
}

static void handle_report_tp_dist(unsigned char pogo_keyboard_event)
{
    pogo_keyboard_report_tp_dist();
}

static void handle_tp_debug(unsigned char pogo_keyboard_event)
{
    bool dist_enable = 0;
    dist_enable = kb_debug_level & (1 << 1) ? 1 : 0;
    pogo_keyboard_set_touch_debug(dist_enable);
}

static void handle_default(void)
{
    kb_err("no event do!\n");
}

typedef void (*event_handler_t)(unsigned char pogo_keyboard_event);
static const event_handler_t event_handlers[] = {
    [KEYBOARD_PLUG_IN_EVENT] = handle_plug_in,
    [KEYBOARD_PLUG_OUT_EVENT] = handle_plug_out,
    [KEYBOARD_HOST_LCD_ON_EVENT] = handle_host_lcd_on,
    [KEYBOARD_HOST_LCD_OFF_EVENT] = handle_host_lcd_off,
    [KEYBOARD_CAPSLOCK_ON_EVENT] = handle_leds,
    [KEYBOARD_CAPSLOCK_OFF_EVENT] = handle_leds,
    [KEYBOARD_MUTEDISABLE_ON_EVENT] = handle_leds,
    [KEYBOARD_MUTEDISABLE_OFF_EVENT] = handle_leds,
    [KEYBOARD_MICDISABLE_ON_EVENT] = handle_leds,
    [KEYBOARD_MICDISABLE_OFF_EVENT] = handle_leds,
    [KEYBOARD_HOST_RX_GPIO_EVENT] = handle_host_rx_gpio,
    [KEYBOARD_HOST_RX_UART_EVENT] = handle_host_rx_uart,
    [KEYBOARD_POWER_ON_EVENT] = handle_power_on,
    [KEYBOARD_POWER_OFF_EVENT] = handle_power_off,
    [KEYBOARD_REPORT_SN_EVENT] = handle_report_sn,
    [KEYBOARD_REPORT_TOUCH_STATUS_EVENT] = handle_report_touch_status,
    [KEYBOARD_REPORT_KBVER_EVENT] = handle_report_kbmcu_ver,
    [KEYBOARD_REPORT_KBLOG_EVENT] = handle_report_kblog,
    [KEYBOARD_REPORT_NFC_STA] = handle_report_nfc_sta,
    [KEYBOARD_REPORT_UART_OPEN_EVENT] = handle_report_uart_open,
    [KEYBOARD_REPORT_UART_CLOSE_EVENT] = handle_report_uart_close,
    [KEYBOARD_REPORT_TP_DIST_EVENT] = handle_report_tp_dist,
    [KEYBOARD_REPORT_TP_DEBUG_EVENT] = handle_tp_debug,
};

static int pogo_keyboard_event_process(unsigned char pogo_keyboard_event)
{
    if (!pogo_keyboard_client) {
        kb_err("pogo_keyboard_client is NULL\n");
        return 0;
    }

    kb_info("pogo_keyboard_event:%d  pogo_keyboard_status:0x%02x\n",
        pogo_keyboard_event, pogo_keyboard_client->pogo_keyboard_status);

    if (pogo_keyboard_event < sizeof(event_handlers) / sizeof(event_handlers[0]) &&
        event_handlers[pogo_keyboard_event] != NULL) {
        event_handlers[pogo_keyboard_event](pogo_keyboard_event);
    } else {
        handle_default();
    }
    return 0;
}

static int pogo_keyboard_event_handler(void *unused)
{
    struct pogo_keyboard_event new_event = { 0 };
    int ret = 0;

    do {
        if (kfifo_is_empty(&pogo_keyboard_client->event_fifo)) {
            kb_info("waiter\n");
            wait_event_interruptible(waiter, pogo_keyboard_client->flag != 0);
        } else {
            ret = kfifo_out(&pogo_keyboard_client->event_fifo, &new_event, sizeof(struct pogo_keyboard_event));
            if (ret == sizeof(struct pogo_keyboard_event)) {
                pogo_keyboard_event_process(new_event.event);
            } else {
                kb_err("kfifo_out err!\n");
            }
            pogo_keyboard_client->flag = 0;
        }
    } while (!kthread_should_stop());

    kb_info("touch_event_handler exit\n");
    return 0;
}

int pogo_keyboard_write_callback(void *param, int enable)
{
    if (enable == 1) {
        pogo_keyboard_enable_uart_tx(1);
    } else if (enable == 0) {
        pogo_keyboard_enable_uart_tx(0);
    } else {
        kb_err("param err!\n");
        return -1;
    }
    return 0;
}

struct file *pogo_keyboard_port_to_file(struct uart_port *port)
{
    struct uart_port *uart_port = port;
    struct uart_state *state = uart_port->state;
    struct tty_struct *tty = state->port.itty;
    struct tty_file_private *priv;
    struct file *filp = NULL;
    spin_lock(&tty->files_lock);
    list_for_each_entry(priv, &tty->tty_files, list)
    {
        if (priv != NULL) {
            if (!strncmp(port->name, pogo_keyboard_client->tty_name,
                strlen(pogo_keyboard_client->tty_name))) {
                filp = priv->file;
            }
        }
    }
    spin_unlock(&tty->files_lock);
    return filp;
}

// called only once by system uart driver when uart port is opened/ready to detect keyboard attachment.
int pogo_keyboard_init_callback(void *port, int type)
{
    struct file *filp;

    if (!port)
        return -1;
    if (type == 1) {
        pogo_keyboard_client->port = (struct uart_port *)port;
        filp = pogo_keyboard_port_to_file(pogo_keyboard_client->port);
        if (pogo_keyboard_client->file_client == NULL && filp != NULL) {
            pogo_keyboard_client->file_client = filp;
            pogo_keyboard_plug_switch(1);
        }
        kb_info("start %p\n", pogo_keyboard_client->file_client);
    } else if (type == 0) {
        pogo_keyboard_client->file_client = NULL;
        pogo_keyboard_client->port = NULL;
        kb_info("end\n");
    } else {
        kb_err("param err!\n");
        return -1;
    }
    return 0;
}

#if IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER) && defined(CONFIG_OPLUS_POGOPIN_FUNCTION)
static void pogo_keyboard_drm_notifier_callback(enum panel_event_notifier_tag tag,
    struct panel_event_notification *notification, void *priv)
{
    if (!notification) {
        kb_err("Invalid notification\n");
        return;
    }

    switch (notification->notif_type) {
        case DRM_PANEL_EVENT_UNBLANK:
            kb_info("event:%d pogo_keyboard goto wakeup\n", notification->notif_data.early_trigger);
            if (!notification->notif_data.early_trigger) {
                pogo_keyboard_sync_lcd_state(true);
            }
            break;
        case DRM_PANEL_EVENT_BLANK:
            kb_info("event:%d pogo_keyboard goto sleep\n", notification->notif_data.early_trigger);
            if (!notification->notif_data.early_trigger) {
                pogo_keyboard_sync_lcd_state(false);
            }
            break;
        case DRM_PANEL_EVENT_BLANK_LP:
            break;
        case DRM_PANEL_EVENT_FPS_CHANGE:
            break;
        default:
            break;
    }
}

static int pogo_keyboard_register_drm_notify(void)
{
    int i = 0, count = 0;
    struct device_node *np = NULL;
    struct device_node *node = NULL;
    struct drm_panel *panel = NULL;
    void *cookie = NULL;

    np = of_find_node_by_name(NULL, "oplus,dsi-display-dev");
    if (!np) {
        kb_err("device tree info. missing\n");
        return 0;
    }
    count = of_count_phandle_with_args(np, "oplus,dsi-panel-primary", NULL);
    if (count <= 0) {
        kb_err("primary panel no found\n");
        return 0;
    }
    for (i = 0; i < count; i++) {
        node = of_parse_phandle(np, "oplus,dsi-panel-primary", i);
        panel = of_drm_find_panel(node);
        of_node_put(node);
        if (!IS_ERR(panel)) {
            pogo_keyboard_client->active_panel = panel;
            kb_err("find active_panel\n");
            break;
        }
    }

    if (i >= count) {
        kb_err("can't find active panel\n");
        return -ENODEV;
    }

    cookie = panel_event_notifier_register(
        PANEL_EVENT_NOTIFICATION_PRIMARY,
        PANEL_EVENT_NOTIFIER_CLIENT_POGOPIN,
        panel, &pogo_keyboard_drm_notifier_callback,
        NULL);
    if (!cookie) {
        kb_err("Unable to register pogo_keyboard_panel_notifier\n");
        return -EINVAL;
    } else {
        pogo_keyboard_client->notifier_cookie = cookie;
        kb_info("success register pogo_keyboard_panel_notifier\n");
    }
    return 0;
}
#endif

#if IS_ENABLED(CONFIG_DEVICE_MODULES_DRM_MEDIATEK) && defined(CONFIG_OPLUS_POGOPIN_FUNCTION)
static int pogo_keyboard_mtk_disp_notifier_callback(struct notifier_block *nb,
    unsigned long value, void *v)
{
    struct pogo_keyboard_data *pogo_data =
        container_of(nb, struct pogo_keyboard_data, disp_notifier);
    int *data = (int *)v;

    if (pogo_data && v) {
        if (value == MTK_DISP_EVENT_BLANK) {
            if (*data == MTK_DISP_BLANK_UNBLANK) {
                pogo_keyboard_sync_lcd_state(true);
            }
        } else if (value == MTK_DISP_EARLY_EVENT_BLANK) {
            if (*data == MTK_DISP_BLANK_POWERDOWN) {
                pogo_keyboard_sync_lcd_state(false);
            }

        }
    } else {
        kb_err("pogo_data or v is NULL!!!\n");
        return -1;
    }
    return 0;
}
#endif

static int pogo_keyboard_lcd_event_register(void)
{
    int ret = 0;

#if IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER) && defined(CONFIG_OPLUS_POGOPIN_FUNCTION)
    ret = pogo_keyboard_register_drm_notify();
#endif

#if IS_ENABLED(CONFIG_DEVICE_MODULES_DRM_MEDIATEK) && defined(CONFIG_OPLUS_POGOPIN_FUNCTION)
    pogo_keyboard_client->disp_notifier.notifier_call = pogo_keyboard_mtk_disp_notifier_callback;
    mtk_disp_notifier_register("pogo_keyboard", &pogo_keyboard_client->disp_notifier);
#endif
    return ret;
}

static void pogo_keyboard_lcd_event_unregister(void)
{
    if (!pogo_keyboard_client->lcd_notify_reg) {
        return;
    }

#if IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER) && defined(CONFIG_OPLUS_POGOPIN_FUNCTION)
    if (pogo_keyboard_client->active_panel && pogo_keyboard_client->notifier_cookie)
        panel_event_notifier_unregister(pogo_keyboard_client->notifier_cookie);
#endif

#if IS_ENABLED(CONFIG_DEVICE_MODULES_DRM_MEDIATEK) && defined(CONFIG_OPLUS_POGOPIN_FUNCTION)
    mtk_disp_notifier_unregister(&pogo_keyboard_client->disp_notifier);
#endif
}

#define LCD_REG_RETRY_COUNT_MAX		100
#define LCD_REG_RETRY_DELAY_MS		100
static void pogo_keyboard_lcd_notify_reg_work(struct work_struct *work)
{
    static int retry_count = 0;
    int ret = 0;

    if (retry_count >= LCD_REG_RETRY_COUNT_MAX)
        return;

    ret = pogo_keyboard_lcd_event_register();
    if (ret < 0) {
        retry_count++;
        kb_info("lcd panel not ready, count=%d\n", retry_count);
        schedule_delayed_work(&pogo_keyboard_client->lcd_notify_reg_work,
            msecs_to_jiffies(LCD_REG_RETRY_DELAY_MS));
        return;
    }
    retry_count = 0;
    pogo_keyboard_client->lcd_notify_reg = true;
}

#ifdef CONFIG_OPLUS_POGOPIN_FUNCTION
extern struct pogo_keyboard_operations *get_pogo_keyboard_operations(void);
#else
struct pogo_keyboard_operations *get_pogo_keyboard_operations(void)
{
    return NULL;
}
#endif

static void pogo_keyboard_register_callback(void)
{
    struct pogo_keyboard_operations *client = get_pogo_keyboard_operations();

    if (client == NULL)
        return;
    if (client->init == NULL) {
        client->init = pogo_keyboard_init_callback;
        client->write = pogo_keyboard_write_callback;
        client->recv = pogo_keyboard_recv_callback;
        client->resume = pogo_keyboard_plat_resume;
        client->suspend = pogo_keyboard_plat_suspend;
        client->remove = pogo_keyboard_plat_remove;
        client->check = pogo_keyboard_is_support;
    }
}

static void pogo_keyboard_unregister_callback(void)
{
    struct pogo_keyboard_operations *client = get_pogo_keyboard_operations();

    if (client == NULL)
        return;
    if (client->init) {
        client->init = NULL;
        client->write = NULL;
        client->recv = NULL;
        client->resume = NULL;
        client->suspend = NULL;
        client->remove = NULL;
        client->check = NULL;
    }
}

// timer for first keyboard attachment detection after system init. run only once.
static enum hrtimer_restart keyboard_core_plug_hrtimer(struct hrtimer *timer)
{
    int value = 0;

    if (!pogo_keyboard_client || pogo_keyboard_client->file_client == NULL) {
        return HRTIMER_NORESTART;
    }
    // will get 0 if keyboard is attached(trx pin low).
    value = gpio_get_value(pogo_keyboard_client->uart_wake_gpio);

    kb_info("gpio read value = %d\r\n", value);
    if (value == 0) {
        if (!atomic_read(&pogo_keyboard_client->vcc_on)) {
            atomic_set(&pogo_keyboard_client->vcc_on, 1);
            kb_info("%d disable_irq_nosync\n", value);
            pogo_keyboard_event_send(KEYBOARD_POWER_ON_EVENT);
            disable_irq_nosync(pogo_keyboard_client->uart_wake_gpio_irq);
            if (pogo_keyboard_client->pogopin_ota_dfu) {
                pogo_keyboard_client->max_disconnect_count = DFU_RESET_DISCONNECT_COUNT;//20s
            }
        }

    } else {
        kb_info("not keyboard plugin,close uart!!!\n");
        pogo_keyboard_event_send(KEYBOARD_REPORT_UART_CLOSE_EVENT);
    }

    return HRTIMER_NORESTART;
}

// timer for monitoring keyboard heartbeat report periodically.
static enum hrtimer_restart keyboard_core_heartbeat_hrtimer(struct hrtimer *timer)
{
    if (!pogo_keyboard_client) {
        kb_err("pogo_keyboard_client is NULL \n");
        return HRTIMER_NORESTART;
    }
    if (pogo_keyboard_client->disconnect_count >= pogo_keyboard_client->max_disconnect_count &&
        pogo_keyboard_client->plug_in_count >= pogo_keyboard_client->max_plug_in_disconnect_count) {
        if ((pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_CONNECT_STATUS) ||
            atomic_read(&pogo_keyboard_client->vcc_on)) {
            atomic_set(&pogo_keyboard_client->vcc_on, 0);
            kb_info("plug out for disconnect_count = %d\n", pogo_keyboard_client->disconnect_count);
            pogo_keyboard_event_send(KEYBOARD_POWER_OFF_EVENT);
        }
        pogo_keyboard_client->disconnect_count = 0;
        pogo_keyboard_client->plug_in_count = 0;
    } else {
        pogo_keyboard_client->disconnect_count++;
        if ((pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_CONNECT_STATUS) ||
            atomic_read(&pogo_keyboard_client->vcc_on)) {
            pogo_keyboard_heartbeat_switch(1); // restart the timer.
        }
    }

    if (pogo_keyboard_client->plug_in_count <= pogo_keyboard_client->max_plug_in_disconnect_count)
        pogo_keyboard_client->plug_in_count++;

    return HRTIMER_NORESTART;
}

static enum hrtimer_restart keyboard_core_poweroff_hrtimer(struct hrtimer *timer)
{
    int value = 0;

    if (!pogo_keyboard_client) {
        kb_err("pogo_keyboard_client is NULL \n");
        return HRTIMER_NORESTART;
    }

    value = gpio_get_value(pogo_keyboard_client->uart_wake_gpio);
    if (value == 0) {
        pogo_keyboard_client->poweroff_disconnect_count = 0;
        pogo_keyboard_client->poweroff_connect_count++;
    } else {
        pogo_keyboard_client->poweroff_connect_count = 0;
        pogo_keyboard_client->poweroff_disconnect_count++;
    }

    if (pogo_keyboard_client->poweroff_disconnect_count >= POWEROFF_DISCONNECT_MAX) {
        kb_info("no restart and enable irq\n");
        enable_irq(pogo_keyboard_client->uart_wake_gpio_irq);
        pogo_keyboard_event_send(KEYBOARD_REPORT_UART_CLOSE_EVENT);
        return HRTIMER_NORESTART;
    }

    if (pogo_keyboard_client->poweroff_connect_count >= POWEROFF_CONNECT_MAX) {
        kb_info("power on! %d %d\n", pogo_keyboard_client->pogo_keyboard_status,
                atomic_read(&pogo_keyboard_client->vcc_on));
        if ((pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_CONNECT_STATUS) == 0) {
            if (!atomic_read(&pogo_keyboard_client->vcc_on)) {
                atomic_set(&pogo_keyboard_client->vcc_on, 1);
                // signal main event task to do the following attachement procedure.
                pogo_keyboard_event_send(KEYBOARD_POWER_ON_EVENT);
                pm_wakeup_event(&pogo_keyboard_client->plat_dev->dev, 2500);
                return HRTIMER_NORESTART;
            }
        }
    }

    pogo_keyboard_poweroff_timer_switch(true);
    return HRTIMER_NORESTART;
}

static enum hrtimer_restart keyboard_core_plugin_check_hrtimer(struct hrtimer *timer)
{
    int value = 0;

    if (!pogo_keyboard_client) {
        kb_err("pogo_keyboard_client is NULL \n");
        return HRTIMER_NORESTART;
    }

    if(pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_CONNECT_STATUS)
    {
        kb_info("keyboard connected, exit\n");
        return HRTIMER_NORESTART;
    }

    value = gpio_get_value(pogo_keyboard_client->uart_wake_gpio);
    if(value == 0) {
        pogo_keyboard_client->check_disconnect_count = 0;
        pogo_keyboard_client->check_connect_count++;
    } else {
        pogo_keyboard_client->check_connect_count = 0;
        pogo_keyboard_client->check_disconnect_count++;
    }

    if(pogo_keyboard_client->check_disconnect_count >= PLUGIN_CHECK_CHECK_MAX/2) {
        kb_info("no restart and enable irq\n");
        enable_irq(pogo_keyboard_client->uart_wake_gpio_irq);
        return HRTIMER_NORESTART;
    }

    if(pogo_keyboard_client->check_connect_count >= PLUGIN_CHECK_CHECK_MAX) {
        kb_info("power on! %d %d\n", pogo_keyboard_client->pogo_keyboard_status,
                atomic_read(&pogo_keyboard_client->vcc_on));
        if((pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_CONNECT_STATUS) == 0){
            if (!atomic_read(&pogo_keyboard_client->vcc_on)){
                atomic_set(&pogo_keyboard_client->vcc_on, 1);
                // signal main event task to do the following attachement procedure.
                pogo_keyboard_event_send(KEYBOARD_POWER_ON_EVENT);
                pm_wakeup_event(&pogo_keyboard_client->plat_dev->dev, 500);
                if (pogo_keyboard_client->pogopin_ota_dfu && pogo_keyboard_client->dfu_boot == 1) {
                    pogo_keyboard_client->max_plug_in_disconnect_count = DFU_PLUGIN_DISCONNECT_COUNT;//8s
                    pogo_keyboard_client->dfu_boot = 0;
                }
            }
        }
        return HRTIMER_NORESTART;
    }

    pogo_keyboard_plugin_check_timer_switch(true);
    return HRTIMER_NORESTART;
}

static int pogo_keyboard_start_up_init(void)
{
    pogo_keyboard_client->pogo_keyboard_status |= KEYBOARD_LCD_ON_STATUS;
    kb_info("pogo_keyboard_status:0x%02x\n", pogo_keyboard_client->pogo_keyboard_status);
    atomic_set(&pogo_keyboard_client->vcc_on, 0);
/*
//max numbers of interval for plug-out detection.
//heartbeat interval from keyboard is 100ms.
//keyboard initialization takes about 400ms and after that first heartbeat packet is reported.
//that's to say during keyboard plug-in stage host will treat keyboard plug-out if host cannot receive heartbeat packet within 400ms.
//but host only wait 200ms after keyboard attechment is finished.
//note the host timer is 50ms.
*/
    pogo_keyboard_client->max_disconnect_count = DEFAULT_DISCONNECT_COUNT;
    pogo_keyboard_client->max_plug_in_disconnect_count = DEFAULT_PLUGIN_DISCONNECT_COUNT;
    hrtimer_init(&pogo_keyboard_client->plug_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    pogo_keyboard_client->plug_timer.function = keyboard_core_plug_hrtimer;

    hrtimer_init(&pogo_keyboard_client->heartbeat_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    pogo_keyboard_client->heartbeat_timer.function = keyboard_core_heartbeat_hrtimer;

    hrtimer_init(&pogo_keyboard_client->poweroff_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    pogo_keyboard_client->poweroff_timer.function = keyboard_core_poweroff_hrtimer;

    hrtimer_init(&pogo_keyboard_client->plugin_check_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    pogo_keyboard_client->plugin_check_timer.function = keyboard_core_plugin_check_hrtimer;

    return 0;
}

static ssize_t proc_tty_name_read(struct file *file, char __user *user_buf,
    size_t count, loff_t *ppos)
{
    int ret = 0;

    if (pogo_keyboard_client && pogo_keyboard_client->tty_name) {
        ret = simple_read_from_buffer(user_buf, count, ppos,
                pogo_keyboard_client->tty_name, strlen(pogo_keyboard_client->tty_name));
    }
    return ret;
}

static const struct proc_ops proc_tty_name_ops = {
    .proc_read = proc_tty_name_read,
    .proc_open = simple_open,
    .proc_lseek = default_llseek,
};

static ssize_t proc_crc_ibm_read(struct file *file, char __user *user_buf,
    size_t count, loff_t *ppos)
{
    int ret = 0;
    char buf[8] = {0};

    if (pogo_keyboard_client && pogo_keyboard_client->crc_ibm_init_val) {
        snprintf(buf, sizeof(buf), "%d", pogo_keyboard_client->crc_ibm_init_val);
        ret = simple_read_from_buffer(user_buf, count, ppos, buf, strlen(buf));

    }
    return ret;
}

static const struct proc_ops proc_crc_ibm_ops = {
    .proc_read = proc_crc_ibm_read,
    .proc_open = simple_open,
    .proc_lseek = default_llseek,
};

static ssize_t proc_touchpad_state_read(struct file *file, char __user *user_buf,
    size_t count, loff_t *ppos)
{
    int ret = 0;
    char buf[8] = {0};

    if (pogo_keyboard_client) {
        snprintf(buf, sizeof(buf), "%u", pogo_keyboard_client->touchpad_disable_state);
        ret = simple_read_from_buffer(user_buf, count, ppos, buf, strlen(buf));
    }

    return ret;
}

static ssize_t proc_battery_power_level_read(struct file *file, char __user *user_buf,
    size_t count, loff_t *ppos)
{
    int ret = 0;
    char buf[8] = {0};

    if (pogo_keyboard_client && (pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_CONNECT_STATUS)) {
        snprintf(buf, sizeof(buf), "%u", pogo_keyboard_client->pogo_battery_power_level);
        ret = simple_read_from_buffer(user_buf, count, ppos, buf, strlen(buf));
    }
    return ret;
}

static ssize_t proc_battery_charge_current_read(struct file *file, char __user *user_buf,
    size_t count, loff_t *ppos)
{
    int ret = 0;
    int pogo_battery_charge_current = 0;
    char buf[8] = {0};

    if (pogo_keyboard_client && (pogo_keyboard_client->pogo_keyboard_status & KEYBOARD_CONNECT_STATUS)) {
        pogo_battery_charge_current = pogo_keyboard_get_charge_current();
        if (pogo_battery_charge_current < 0) {
            kb_err("send cmd to keyboard to read charge current fail\n");
        }
    }
    snprintf(buf, sizeof(buf), "%d", pogo_battery_charge_current);
    ret = simple_read_from_buffer(user_buf, count, ppos, buf, strlen(buf));

    return ret;
}

static const struct proc_ops proc_battery_power_level_ops = {
    .proc_read = proc_battery_power_level_read,
    .proc_open = simple_open,
    .proc_lseek = default_llseek,
};
static const struct proc_ops proc_battery_charge_current_ops = {
    .proc_read = proc_battery_charge_current_read,
    .proc_open = simple_open,
    .proc_lseek = default_llseek,
};
static const struct proc_ops proc_touchpad_state_ops = {
    .proc_read = proc_touchpad_state_read,
    .proc_open = simple_open,
    .proc_lseek = default_llseek,
};

static ssize_t proc_touchpad_disable_read(struct file *file, char __user *user_buf,
    size_t count, loff_t *ppos)
{
    int ret = 0;
    char buf[8] = {0};

    if (pogo_keyboard_client) {
        snprintf(buf, sizeof(buf), "%u", pogo_keyboard_client->touchpad_disable_state);
        ret = simple_read_from_buffer(user_buf, count, ppos, buf, strlen(buf));
    }

    return ret;
}

static ssize_t proc_touchpad_disable_write(struct file *file, const char __user *buf, size_t count, loff_t *lo)
{
    char write_data[2] = { 0 };

    if (count > 2) {
        kb_err("count: %zd > 2\n", count);
        return count;
    }

    if (copy_from_user(&write_data, buf, count)) {
        kb_err("read proc input error.\n");
        return count;
    }

    if(!pogo_keyboard_client)
        return count;

    if(write_data[0] == '0'){
        pogo_keyboard_set_touch_status(0);
        kb_info("enable touchpad\n");
    } else {
        pogo_keyboard_set_touch_status(1);
        kb_info("disable touchpad\n");
    }

    return count;
}

static const struct proc_ops proc_touchpad_disable_ops = {
    .proc_read = proc_touchpad_disable_read,
    .proc_write = proc_touchpad_disable_write,
    .proc_lseek = default_llseek,
};

static ssize_t proc_touchpad_gesture_read(struct file *file, char __user *user_buf,
    size_t count, loff_t *ppos)
{
    int ret = 0;
    char buf[8] = {0};

    if (pogo_keyboard_client) {
        snprintf(buf, sizeof(buf), "%u", pogo_keyboard_client->touchpad_gesture_state);
        ret = simple_read_from_buffer(user_buf, count, ppos, buf, strlen(buf));
    }

    return ret;
}

static ssize_t proc_touchpad_gesture_write(struct file *file, const char __user *buf, size_t count, loff_t *lo)
{
    char write_data[2] = { 0 };

    if (count > 2) {
        kb_err("count: %zd > 2\n", count);
        return count;
    }

    if (copy_from_user(&write_data, buf, count)) {
        kb_err("read proc input error.\n");
        return count;
    }

    if(!pogo_keyboard_client)
        return count;

    if(write_data[0] == '0'){
        pogo_keyboard_set_touch_gesture(0);
        pogo_keyboard_client->touchpad_gesture_state = 0;
        kb_info("disable touch gesture\n");
    } else {
        pogo_keyboard_set_touch_gesture(1);
        pogo_keyboard_client->touchpad_gesture_state = 1;
        kb_info("enable touch gesture\n");
    }

    return count;
}

static const struct proc_ops proc_touchpad_gesture_ops = {
    .proc_read = proc_touchpad_gesture_read,
    .proc_write = proc_touchpad_gesture_write,
    .proc_lseek = default_llseek,
};

static ssize_t proc_keypad_state_read(struct file *file, char __user *user_buf,
    size_t count, loff_t *ppos)
{
    int ret = 0;
    char buf[8] = {0};

    if (pogo_keyboard_client) {
        snprintf(buf, sizeof(buf), "%u", pogo_keyboard_client->keypad_pluginout_state);
        ret = simple_read_from_buffer(user_buf, count, ppos, buf, strlen(buf));
    }

    return ret;
}

static ssize_t proc_keypad_state_write(struct file *file, const char __user *buf, size_t count, loff_t *lo)
{
    char write_data[2] = { 0 };

    if (count > 2) {
        kb_err("count: %zd > 2\n", count);
        return count;
    }

    if (copy_from_user(&write_data, buf, count)) {
        kb_err("read proc input error.\n");
        return count;
    }

    if(!pogo_keyboard_client)
        return count;

    if(write_data[0] == '1'){
        pogo_keyboard_connect_send_uevent();
    } else {
        kb_err("input error.\n");
    }

    return count;
}

static const struct proc_ops proc_keypad_state_ops = {
    .proc_read = proc_keypad_state_read,
    .proc_write = proc_keypad_state_write,
    .proc_lseek = default_llseek,
};

static ssize_t proc_kpdmcu_fw_update_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    uint8_t ret = 0;
    char page[4] = {0};

    if (pogo_keyboard_client) {
        snprintf(page, 3, "%d\n", pogo_keyboard_client->kpd_fw_status);
        ret = simple_read_from_buffer(buf, count, ppos, page, strlen(page));
    }

    return ret;
}

static ssize_t proc_kpdmcu_fw_update_write(struct file *file, const char __user *buf, size_t count, loff_t *lo)
{
    char write_data[2] = { 0 };

    if (count > 2) {
        kb_err("count: %zd > 2\n", count);
        return count;
    }

    if (copy_from_user(&write_data, buf, count)) {
        kb_err("read proc input error.\n");
        return count;
    }

    if(!pogo_keyboard_client || pogo_keyboard_client->kpd_fw_status == FW_UPDATE_START)
        return count;

    if(write_data[0] == '1'){
        pogo_keyboard_client->kpdmcu_fw_update_force = false;
        schedule_work(&pogo_keyboard_client->kpdmcu_fw_update_work);
    } else if(write_data[0] == '2') {
        pogo_keyboard_client->kpdmcu_fw_update_force = true;
        schedule_work(&pogo_keyboard_client->kpdmcu_fw_update_work);
    } else {
        pogo_keyboard_client->kpdmcu_fw_update_force = false;
    }

    return count;
}

static const struct proc_ops proc_kpdmcu_fw_update_ops = {
    .proc_read = proc_kpdmcu_fw_update_read,
    .proc_write = proc_kpdmcu_fw_update_write,
    .proc_lseek = default_llseek,
};

static ssize_t proc_kpdmcu_fw_check_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    uint8_t ret = 0;
    char page[PROC_PAGE_LEN] = {0};
    bool need_update_fw = false;
    u32 fw_count = 0;
    int index = 0;

    if (pogo_keyboard_client) {
        if (pogo_keyboard_client->pogopin_fw_support) {
            need_update_fw = pogo_keyboard_client->is_kpdmcu_need_fw_update;

            if(need_update_fw)
                fw_count += pogo_keyboard_client->kpdmcu_fw_cnt;

            if ((pogo_keyboard_client->pogopin_ota_dfu) &&
                (pogo_keyboard_client->kpdmcu_update_end) &&
                (pogo_keyboard_client->kpd_fw_status == FW_UPDATE_SUC)) {
                pogo_keyboard_client->kpd_fw_status = FW_UPDATE_READY;
                pogo_keyboard_client->fw_update_progress = 0;
            }
            index = snprintf(page, PROC_PAGE_LEN - 1, "%s:%04x:%d:%d:%d\n",
                    pogo_keyboard_client->report_kbver,
                    pogo_keyboard_client->kpdmcu_fw_data_ver, fw_count,
                    need_update_fw, pogo_keyboard_client->fw_update_progress / FW_PERCENTAGE_100);
        } else {
            index = snprintf(page, PROC_PAGE_LEN - 1, "%s\n",
                    pogo_keyboard_client->report_kbver);
        }
        ret = simple_read_from_buffer(buf, count, ppos, page, index);
    }

    return ret;
}

static ssize_t proc_kpdmcu_fw_check_write(struct file *file, const char __user *buf, size_t count, loff_t *lo)
{
    char write_data[2] = { 0 };
    if (pogo_keyboard_client->pogopin_fw_support) {
        if (count > 2) {
            kb_err("count: %zd > 2\n", count);
            return count;
        }

        if (copy_from_user(&write_data, buf, count)) {
            kb_err("read proc input error!!!\n");
            return count;
        }

        if(write_data[0] == '1' && pogo_keyboard_client != NULL)
            schedule_delayed_work(&pogo_keyboard_client->kpdmcu_fw_data_version_work, 0);
    }
    return count;
}

static const struct proc_ops proc_kpdmcu_fw_check_ops = {
    .proc_read = proc_kpdmcu_fw_check_read,
    .proc_write = proc_kpdmcu_fw_check_write,
    .proc_lseek = default_llseek,
};

static ssize_t proc_debug_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	uint8_t ret = 0;
	char page[PROC_PAGE_LEN] = {0};
    int index = 0;

	index = snprintf(page, PROC_PAGE_LEN - 1, "%d", kb_debug_level);
	ret = simple_read_from_buffer(buf, count, ppos, page, index);

	return ret;
}

static ssize_t proc_debug_write(struct file *file, const char __user *buf, size_t count, loff_t *lo)
{
    char write_data[3] = { 0 };
    int tmp = 0;

    if (count < 1 || count > 2) {
        kb_err("Invalid input size: %zd (1-2bytes)\n", count);
        return -EINVAL;
    }

    if (copy_from_user(&write_data, buf, count)) {
        kb_err("Failed to copy %zd bytes from user space\n", count);
        return -EINVAL;
    }
    write_data[count] = '\0';

    if (pogo_keyboard_client == NULL) {
        kb_err("pogo_keyboard_client is NULL!!!\n");
        return -ENODEV;
    }
    if (kstrtoint(write_data, 10, &tmp)) {
        kb_err("Invalid integer format:'%s'\n", write_data);
        return -EINVAL;
    }
    kb_debug_level = tmp;
    kb_info("setting kb_debug_level=%d\n", kb_debug_level);
    pogo_keyboard_event_send(KEYBOARD_REPORT_TP_DEBUG_EVENT);
    return count;
}

static const struct proc_ops proc_debug_ops = {
    .proc_read = proc_debug_read,
    .proc_write = proc_debug_write,
    .proc_lseek = default_llseek,
};

static ssize_t proc_trace_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    uint8_t ret = 0;
    char page[PROC_PAGE_LEN] = {0};
    int index = 0;
    int value = atomic_read(&kb_suspend_failed_count);

    index = snprintf(page, PROC_PAGE_LEN - 1, "kb_suspend_failed:%d\n", value);
    ret = simple_read_from_buffer(buf, count, ppos, page, index);

    return ret;
}

static ssize_t proc_trace_write(struct file *file, const char __user *buf, size_t count, loff_t *lo)
{
    char write_data[3] = { 0 };
    int tmp = 0;

    if (count < 1 || count > 2) {
        kb_err("Invalid input size: %zd (1-2bytes)\n", count);
        return -EINVAL;
    }

    if (copy_from_user(&write_data, buf, count)) {
        kb_err("Failed to copy %zd bytes from user space\n", count);
        return -EINVAL;
    }
    write_data[count] = '\0';

    if (1 == sscanf(write_data, "%d", &tmp) && tmp == 0) {
        atomic_set(&kb_suspend_failed_count, 0);
    } else {
        kb_err("invalid operation\n");
    }

    return count;
}

static const struct proc_ops proc_trace_ops = {
    .proc_read = proc_trace_read,
    .proc_write = proc_trace_write,
    .proc_lseek = default_llseek,
};

//for trx test
static void kpd_trx_test_thread(struct work_struct *work)
{
    int ret = 0;
    int i = 0;
    if(pogo_keyboard_client == NULL) {
        kb_err("pogo_keyboard_client is NULL\n");
        return;
    }

    if (pogo_keyboard_client->pogopin_wakelock)
        __pm_stay_awake(pogo_keyboard_client->pogopin_wakelock);
    mutex_lock(&pogo_keyboard_client->mutex);

    for (i = 0; i < test_count; i++) {
        ret = pogo_keyboard_trx_test(send_buf, send_len, ack_buf, ack_len);
        if (ret != 0) {
            kb_err("i:%d err\r\n", i);
            test_fail_sum++;
        }
    }
    if (test_fail_sum != 0) {
        kb_err("fail! test_fail_sum:%d\r\n", test_fail_sum);
    }

    mutex_unlock(&pogo_keyboard_client->mutex);
    if (pogo_keyboard_client->pogopin_wakelock)
        __pm_relax(pogo_keyboard_client->pogopin_wakelock);
}

static int pogo_keyboard_proc_create(const char *name, umode_t mode, struct proc_dir_entry *parent,
        const struct proc_ops *proc_ops, void *data)
{
    struct proc_dir_entry *prEntry_tmp = NULL;
    int ret = 0;
    if (data == NULL) {
        prEntry_tmp = proc_create(name, mode, parent, proc_ops);
        if (prEntry_tmp == NULL) {
            kb_err("couldn't create %s entry\n", name);
            ret = -ENOMEM;
        }
    } else {
        prEntry_tmp = proc_create_data(name, mode, parent, proc_ops, data);
        if (prEntry_tmp == NULL) {
            kb_err("couldn't create %s entry\n", name);
            ret = -ENOMEM;
        }
    }
    return ret;
}

static int pogo_keyboard_init_proc(void)
{
    int ret = 0;
    struct proc_dir_entry *prEntry_keyboard = NULL;

    prEntry_keyboard = proc_mkdir("pogopin", NULL);
    if (prEntry_keyboard == NULL) {
        kb_err("couldn't create entry\n");
        ret = -ENOMEM;
    }

    pogo_keyboard_proc_create("tty_name", 0444, prEntry_keyboard, &proc_tty_name_ops, NULL);

    if (pogo_keyboard_client->get_crc_ibm_from_dts) {
        pogo_keyboard_proc_create("crc_ibm_init_val", 0444, prEntry_keyboard, &proc_crc_ibm_ops, NULL);
    }

    if (pogo_keyboard_client->pogopin_touch_support) {
        pogo_keyboard_proc_create("kbd_touch_status", 0444, prEntry_keyboard, &proc_touchpad_state_ops, NULL);
        pogo_keyboard_proc_create("kbd_touch_disable", 0664, prEntry_keyboard, &proc_touchpad_disable_ops, NULL);
        if (!pogo_keyboard_client->touchpad_gesture_ignore) {
            pogo_keyboard_proc_create("kbd_touch_gesture", 0664, prEntry_keyboard, &proc_touchpad_gesture_ops, NULL);
        }
    }

    if (pogo_keyboard_client->pogo_battery_support) {
        pogo_keyboard_proc_create("kbd_battery_power_level", 0444, prEntry_keyboard, &proc_battery_power_level_ops, NULL);
        pogo_keyboard_proc_create("kbd_battery_charge_current", 0444, prEntry_keyboard, &proc_battery_charge_current_ops, NULL);
    }
    /*for factory test detect*/
    pogo_keyboard_proc_create("kbd_keypad_status", 0664, prEntry_keyboard, &proc_keypad_state_ops, NULL);
    pogo_keyboard_proc_create("kpdmcu_fw_check", 0666, prEntry_keyboard, &proc_kpdmcu_fw_check_ops, pogo_keyboard_client);

    if (pogo_keyboard_client->pogopin_fw_support) {
        pogo_keyboard_proc_create("kpdmcu_fw_update", 0666, prEntry_keyboard, &proc_kpdmcu_fw_update_ops, pogo_keyboard_client);
    }

    /*for tp debug*/
    pogo_keyboard_proc_create("kbd_debug", 0664, prEntry_keyboard, &proc_debug_ops, NULL);
    /*for kb trace*/
    pogo_keyboard_proc_create("kbd_trace", 0664, prEntry_keyboard, &proc_trace_ops, NULL);
    return ret;
}

static inline char *get_name(const char *str1, const char *str2)
{
    size_t len1 = strlen(str1);
    size_t len2 = strlen(str2);
    size_t total_len = len1 + len2 + 1;
    char *result = NULL;

    result = (char *)kzalloc(total_len * sizeof(char), GFP_KERNEL);
    if (!result) {
        kb_err("kzalloc err\n");
        return NULL;
    }

    memcpy(result, str1, len1);
    memcpy(result + len1, str2, len2 + 1);
    return result;
}

static struct pogo_uevent *pogo_keyboard_init_device_uevent(const char *name)
{
    struct pogo_uevent *udev = NULL;
    dev_t devt;
    int ret = 0;

    udev = kzalloc(sizeof(*udev), GFP_KERNEL);
    if (!udev) {
        kb_err("Failed to allocate memory for uevent\n");
        goto err_alloc;
    }

    udev->class_name = get_name("pogopin", name);
    if (!udev->class_name) {
        kb_err("Failed to get class name\n");
        goto err_class_name;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
    udev->uevent_class = class_create(udev->class_name);
#else
    udev->uevent_class = class_create(THIS_MODULE, udev->class_name);
#endif
    if (IS_ERR(udev->uevent_class)) {
        kb_err("Failed to create class (err=%ld)\n", PTR_ERR(udev->uevent_class));
        goto err_class_create;
    }

    udev->cdev_name = get_name("keyboard", name);
    if (!udev->cdev_name) {
        kb_err("Failed to get cdev name\n");
        goto err_cdev_name;
    }

    ret = alloc_chrdev_region(&devt, 0, 1, udev->cdev_name);
    if (ret < 0) {
        kb_err("Failed to allocate chrdev region (err=%d)\n", ret);
        goto err_chrdev_alloc;
    }

    udev->dev_name = get_name(KEYBOARD_NAME, name);
    if (!udev->dev_name) {
        kb_err("Failed to get device name\n");
        goto err_dev_name;
    }

    udev->uevent_dev = device_create(udev->uevent_class, NULL, devt, NULL, "%s", udev->dev_name);
    if (IS_ERR(udev->uevent_dev)) {
        kb_err("Failed to create device (err=%ld)\n", PTR_ERR(udev->uevent_dev));
        goto err_device_create;
    }

    udev->uevent_dev->devt = devt;
    return udev;

err_device_create:
    kfree(udev->dev_name);
err_dev_name:
    unregister_chrdev_region(devt, 1);
err_chrdev_alloc:
    kfree(udev->cdev_name);
err_cdev_name:
    class_destroy(udev->uevent_class);
err_class_create:
    kfree(udev->class_name);
err_class_name:
    kfree(udev);
err_alloc:
    return NULL;
}

static void pogo_keyboard_deinit_device_uevent(struct pogo_uevent *udev)
{
    if (udev && udev->uevent_class && udev->uevent_dev && udev->uevent_dev->devt) {
        device_destroy(udev->uevent_class, udev->uevent_dev->devt);
        unregister_chrdev_region(udev->uevent_dev->devt, 1);
        class_destroy(udev->uevent_class);
    }
}

#ifdef CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY
bool inline is_ftm_boot_mode(void)
{
    int boot_mode = 0;
    boot_mode = get_boot_mode();

    return (boot_mode == META_BOOT || boot_mode == FACTORY_BOOT) ? true : false;
}
#endif

int pogo_keyboard_plat_probe(struct platform_device *device)
{
    int ret = 0;

#ifdef CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY
    if (is_ftm_boot_mode()) {
        kb_info("now is ftm mode, return not probe \n");
        return ret;
    } else {
        kb_info("now is noraml mode, continue\n");
    }
#endif

    pogo_keyboard_client = kzalloc(sizeof(*pogo_keyboard_client), GFP_KERNEL);
    if (!pogo_keyboard_client) {
        kb_err("kzalloc err\n");
        return -ENOMEM;
    }
    mutex_init(&pogo_keyboard_client->mutex);

    ret = kfifo_alloc(&pogo_keyboard_client->event_fifo,
        sizeof(struct pogo_keyboard_event) * POGO_KEYBOARD_EVENT_MAX, GFP_KERNEL);
    if (ret) {
        kb_err("kfifo_alloc fail\n");
        goto err_kfifo_alloc;
    }
    spin_lock_init(&pogo_keyboard_client->event_fifo_lock);

    pogo_keyboard_client->pogo_keyboard_task = kthread_run(pogo_keyboard_event_handler, 0, "pogo_keyboard_task");
    if (IS_ERR_OR_NULL(pogo_keyboard_client->pogo_keyboard_task)) {
        kb_err("kthread_run err\n");
        pogo_keyboard_client->pogo_keyboard_task = NULL;
    }
    pogo_keyboard_client->plat_dev = device;
#ifndef CONFIG_REMOVE_OPLUS_FUNCTION
    pogo_keyboard_client->is_confidential = is_confidential();  //get confidential status.
#else
    pogo_keyboard_client->is_confidential = false;
#endif
    pogo_keyboard_start_up_init();
    ret = pogo_keyboard_get_dts_info(device);
    if (ret != 0) {
        goto err_dts_info;
    }
    ret = pogo_keyboard_input_wakeup_init();
    if (ret != 0) {
        goto err_wakeup_init;
    }

    ret = sysfs_create_group(&device->dev.kobj, &pogo_keyboard_attribute_group);
    if (ret != 0) {
        kb_err("sysfs_create_group err\n");
        goto err_create_group;
    }

    pogo_keyboard_client->pogo = pogo_keyboard_init_device_uevent("");

    if (pogo_keyboard_client->pogo == NULL) {
        kb_err("pogo_keyboard_init_device_uevent err\n");
        goto err_init_uevent;
    }

    pogo_keyboard_client->nfc = pogo_keyboard_init_device_uevent("_nfc");

    if (pogo_keyboard_client->nfc == NULL) {
        kb_err("pogo_keyboard_init_device_uevent_nfc err\n");
        goto err_init_uevent_nfc;
    }

    pogo_keyboard_client->pogo_uart = pogo_keyboard_init_device_uevent("_uart");

    if (pogo_keyboard_client->pogo_uart == NULL) {
        kb_err("pogo_keyboard_init_device_uevent_battery err\n");
        goto err_init_uevent_uart;
    }

    ret = pogo_keyboard_init_proc();
    if (ret != 0) {
        kb_err("pogo_keyboard_init_proc err\n");
        goto err_init_proc;
    }

    pogo_keyboard_register_callback();

    INIT_DELAYED_WORK(&pogo_keyboard_client->lcd_notify_reg_work, pogo_keyboard_lcd_notify_reg_work);
    schedule_delayed_work(&pogo_keyboard_client->lcd_notify_reg_work, 0);

    //for trx test
    INIT_WORK(&pogo_keyboard_client->kpd_trx_test_work, kpd_trx_test_thread);

    if (pogo_keyboard_client->pogopin_fw_support) {
        pogo_keyboard_client->kpdmcu_fw_update_force = false;
        pogo_keyboard_client->is_kpdmcu_need_fw_update = false;
        pogo_keyboard_client->kpdmcu_update_end = false;
        pogo_keyboard_client->kpdmcu_fw_cnt = 0;
        pogo_keyboard_client->kpd_fw_status = FW_UPDATE_READY;
        pogo_keyboard_client->fw_update_progress = 0;
        INIT_DELAYED_WORK(&pogo_keyboard_client->kpdmcu_fw_data_version_work, kpdmcu_fw_data_version_thread);
        schedule_delayed_work(&pogo_keyboard_client->kpdmcu_fw_data_version_work, round_jiffies_relative(msecs_to_jiffies(30 * 1000)));
        INIT_WORK(&pogo_keyboard_client->kpdmcu_fw_update_work, kpdmcu_fw_update_thread);
    }

    kb_info("ok\n");

    return 0;

err_init_proc:
    pogo_keyboard_deinit_device_uevent(pogo_keyboard_client->pogo_uart);
err_init_uevent_uart:
    pogo_keyboard_deinit_device_uevent(pogo_keyboard_client->nfc);
err_init_uevent_nfc:
    pogo_keyboard_deinit_device_uevent(pogo_keyboard_client->pogo);
err_init_uevent:
    sysfs_remove_group(&device->dev.kobj, &pogo_keyboard_attribute_group);
err_create_group:
err_wakeup_init:
err_dts_info:
err_kfifo_alloc:
    kfree(pogo_keyboard_client);

    return ret;
}

static int pogo_keyboard_plat_remove(struct platform_device *device)
{
    pogo_keyboard_unregister_callback();
    if (!pogo_keyboard_client) {
        kb_info("pogo_keyboard_client is NULL\n");
        return 0;
    }
    if (pogo_keyboard_client->input_wakeup) {
        input_unregister_device(pogo_keyboard_client->input_wakeup);
        input_free_device(pogo_keyboard_client->input_wakeup);
        kb_info("unregister input_wakeup\n");
        pogo_keyboard_client->input_wakeup = NULL;
    }
    if (pogo_keyboard_client->input_touchpad) {

        input_unregister_device(pogo_keyboard_client->input_touchpad);
        input_free_device(pogo_keyboard_client->input_touchpad);
        kb_info("unregister input_touchpad\n");
        pogo_keyboard_client->input_touchpad = NULL;
    }
    if (pogo_keyboard_client->input_pogo_keyboard) {
        input_unregister_device(pogo_keyboard_client->input_pogo_keyboard);
        input_free_device(pogo_keyboard_client->input_pogo_keyboard);
        kb_info("unregister input_pogo_keyboard\n");
        pogo_keyboard_client->input_pogo_keyboard = NULL;
    }

    sysfs_remove_group(&device->dev.kobj, &pogo_keyboard_attribute_group);

    pogo_keyboard_lcd_event_unregister();
    pogo_keyboard_deinit_device_uevent(pogo_keyboard_client->nfc);
    pogo_keyboard_deinit_device_uevent(pogo_keyboard_client->pogo);
    pogo_keyboard_deinit_device_uevent(pogo_keyboard_client->pogo_uart);
    kfree(pogo_keyboard_client);

    kb_info("\n");
    return 0;
}

static int pogo_keyboard_plat_suspend(struct platform_device *device)
{
    kb_info("\n");
    return 0;
}

static int pogo_keyboard_plat_resume(struct platform_device *device)
{
    kb_info("\n");
    return 0;
}

static void pogo_keyboard_plat_shutdown(struct platform_device *device)
{
    kb_info("\n");
}


static const struct of_device_id pogo_keyboard_plat_of_match[] = {
    {.compatible = KEYBOARD_CORE_NAME,},
    {},
};
static struct platform_driver pogo_keyboard_plat_driver = {
    .probe = pogo_keyboard_plat_probe,
    .remove = pogo_keyboard_plat_remove,
    .shutdown = pogo_keyboard_plat_shutdown,
    .driver = {
        .name = KEYBOARD_CORE_NAME,
        .owner = THIS_MODULE,
        .of_match_table = pogo_keyboard_plat_of_match,
    }
};


static int __init pogo_keyboard_mod_init(void)
{
    int ret = 0;
    ret = platform_driver_register(&pogo_keyboard_plat_driver);
    if (ret) {
        kb_err("platform_driver_register err\n");
        return ret;
    }
    kb_info("\n");
    return 0;
}

static void __exit pogo_keyboard_mod_exit(void)
{
    platform_driver_unregister(&pogo_keyboard_plat_driver);
    kb_info("\n");
}

module_init(pogo_keyboard_mod_init);
module_exit(pogo_keyboard_mod_exit);

MODULE_AUTHOR("Tinno Team Inc");
MODULE_DESCRIPTION("Tinno Keyboard Driver v1.0");
MODULE_LICENSE("GPL");
