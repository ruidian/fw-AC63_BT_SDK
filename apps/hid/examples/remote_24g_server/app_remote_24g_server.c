/*********************************************************************************************
    *   Filename        : x.c

    *   Description     :

    *   Author          :

    *   Email           :

    *   Last modifiled  : 2019-07-05 10:09

    *   Copyright:(c)JIELI  2011-2019  @ , All Rights Reserved.
*********************************************************************************************/
#include "system/app_core.h"
#include "system/includes.h"
#include "server/server_core.h"
#include "app_config.h"
#include "app_action.h"
#include "os/os_api.h"
#include "btcontroller_config.h"
#include "btctrler/btctrler_task.h"
#include "config/config_transport.h"
#include "btstack/avctp_user.h"
#include "btstack/btstack_task.h"
#include "bt_common.h"
#include "edr_hid_user.h"
/* #include "code_switch.h" */
/* #include "omsensor/OMSensor_manage.h" */
#include "le_common.h"
#include <stdlib.h>
#include "standard_hid.h"
#include "rcsp_bluetooth.h"
#include "app_charge.h"
#include "app_power_manage.h"
#include "app_chargestore.h"
#include "app_comm_bt.h"

#if(CONFIG_APP_REMOTE_24G_S)

#define LOG_TAG_CONST       REMOTE_S
#define LOG_TAG             "[REMOTE_S]"
#define LOG_ERROR_ENABLE
#define LOG_DEBUG_ENABLE
#define LOG_INFO_ENABLE
/* #define LOG_DUMP_ENABLE */
#define LOG_CLI_ENABLE
#include "debug.h"

#if TCFG_AUDIO_ENABLE
#include "tone_player.h"
#include "media/includes.h"
#include "key_event_deal.h"
extern void midi_paly_test(u32 key);
#endif/*TCFG_AUDIO_ENABLE*/

//2.4G模式: 0---ble, 非0---2.4G配对码
/* #define CFG_RF_24G_CODE_ID            (0) //<=24bits */
#define CFG_RF_24G_CODE_ID            (0x23) //<=24bits

//测试每个interval上行发一包数据
#define  HID_TEST_KEEP_SEND_EN        0//just for test keep data
//是否打开2.4G持续发送数据
#define  REMOTE_24G_KEEP_SEND_EN      1//just for 2.4gtest keep data
//选择物理层,设置之前先配置feature
#define  SELECT_PHY                   CONN_SET_1M_PHY//1M:CONN_SET_1M_PHY 2M:CONN_SET_2M_PHY CODED:CONN_SET_CODED_PHY
//选择CODED设置为S2 or S8
#define SELECT_CODED_S2_0R_S8         CONN_SET_PHY_OPTIONS_S2//S2:CONN_SET_PHY_OPTIONS_S2 S8:CONN_SET_PHY_OPTIONS_S8

#define trace_run_debug_val(x)   //log_info("\n## %s: %d,  0x%04x ##\n",__FUNCTION__,__LINE__,x)

//---------------------------------------------------------------------
#if SNIFF_MODE_RESET_ANCHOR
#define SNIFF_MODE_TYPE               SNIFF_MODE_ANCHOR
#define SNIFF_CNT_TIME                1/////<空闲?S之后进入sniff模式

#define SNIFF_MAX_INTERVALSLOT        16
#define SNIFF_MIN_INTERVALSLOT        16
#define SNIFF_ATTEMPT_SLOT            2
#define SNIFF_TIMEOUT_SLOT            1
#define SNIFF_CHECK_TIMER_PERIOD      100
#else

#define SNIFF_MODE_TYPE               SNIFF_MODE_DEF
#define SNIFF_CNT_TIME                5/////<空闲?S之后进入sniff模式

#define SNIFF_MAX_INTERVALSLOT        800
#define SNIFF_MIN_INTERVALSLOT        100
#define SNIFF_ATTEMPT_SLOT            4
#define SNIFF_TIMEOUT_SLOT            1
#define SNIFF_CHECK_TIMER_PERIOD      1000
#endif

//默认配置
static const edr_sniff_par_t remote_24g_sniff_param = {
    .sniff_mode = SNIFF_MODE_TYPE,
    .cnt_time = SNIFF_CNT_TIME,
    .max_interval_slots = SNIFF_MAX_INTERVALSLOT,
    .min_interval_slots = SNIFF_MIN_INTERVALSLOT,
    .attempt_slots = SNIFF_ATTEMPT_SLOT,
    .timeout_slots = SNIFF_TIMEOUT_SLOT,
    .check_timer_period = SNIFF_CHECK_TIMER_PERIOD,
};

typedef enum {
    HID_MODE_NULL = 0,
    HID_MODE_EDR,
    HID_MODE_BLE,
    HID_MODE_INIT = 0xff
} bt_mode_e;


static bt_mode_e bt_hid_mode;
static volatile u8 is_remote_24g_active = 0;//1-临界点,系统不允许进入低功耗，0-系统可以进入低功耗
static u16 g_auto_shutdown_timer = 0;
static void remote_24g_app_select_btmode(u8 mode);
static u8 remote_24g_phy_test_timer_id = 0;
//----------------------------------
static const u8 remote_24g_report_map[] = {
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x09, 0xE9,        //   Usage (Volume Increment)
    0x09, 0xEA,        //   Usage (Volume Decrement)
    0x09, 0xCD,        //   Usage (Play/Pause)
    0x09, 0xE2,        //   Usage (Mute)
    0x09, 0xB6,        //   Usage (Scan Previous Track)
    0x09, 0xB5,        //   Usage (Scan Next Track)
    0x09, 0xB3,        //   Usage (Fast Forward)
    0x09, 0xB4,        //   Usage (Rewind)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x10,        //   Report Count (16)
    0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,              // End Collection
    // 35 bytes
};
#define KEYBOARD_REPORT_ID          0x1
#define COUSTOM_CONTROL_REPORT_ID   0x2
#define MOUSE_POINT_REPORT_ID       0x3


const static u8  kb_hid_report_map[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, KEYBOARD_REPORT_ID,//   Report ID (1)
    0x05, 0x07,        //   Usage Page (Kbrd/Keypad)
    0x19, 0xE0,        //   Usage Minimum (0xE0)
    0x29, 0xE7,        //   Usage Maximum (0xE7)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x03,        //   Report Count (3)
    0x75, 0x01,        //   Report Size (1)
    0x05, 0x08,        //   Usage Page (LEDs)
    0x19, 0x01,        //   Usage Minimum (Num Lock)
    0x29, 0x03,        //   Usage Maximum (Scroll Lock)
    0x91, 0x02,        //   Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x95, 0x05,        //   Report Count (5)
    0x75, 0x01,        //   Report Size (1)
    0x91, 0x01,        //   Output (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x05, 0x07,        //   Usage Page (Kbrd/Keypad)
    0x19, 0x00,        //   Usage Minimum (0x00)
    0x2A, 0xFF, 0x00,  //   Usage Maximum (0xFF)
    0x81, 0x00,        //   Input (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,              // End Collection
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, COUSTOM_CONTROL_REPORT_ID,//   Report ID (3)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x01,        //   Report Count (1)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0x8C, 0x02,  //   Logical Maximum (652)
    0x19, 0x00,        //   Usage Minimum (Unassigned)
    0x2A, 0x8C, 0x02,  //   Usage Maximum (AC Send)
    0x81, 0x00,        //   Input (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,              // End Collection
//
    // Dummy mouse collection starts here
    //
    0x05, 0x01,                         // USAGE_PAGE (Generic Desktop)     0
    0x09, 0x02,                         // USAGE (Mouse)                    2
    0xa1, 0x01,                         // COLLECTION (Application)         4
    0x85, MOUSE_POINT_REPORT_ID,               //   REPORT_ID (Mouse)              6
    0x09, 0x01,                         //   USAGE (Pointer)                8
    0xa1, 0x00,                         //   COLLECTION (Physical)          10
    0x05, 0x09,                         //     USAGE_PAGE (Button)          12
    0x19, 0x01,                         //     USAGE_MINIMUM (Button 1)     14
    0x29, 0x02,                         //     USAGE_MAXIMUM (Button 2)     16
    0x15, 0x00,                         //     LOGICAL_MINIMUM (0)          18
    0x25, 0x01,                         //     LOGICAL_MAXIMUM (1)          20
    0x75, 0x01,                         //     REPORT_SIZE (1)              22
    0x95, 0x02,                         //     REPORT_COUNT (2)             24
    0x81, 0x02,                         //     INPUT (Data,Var,Abs)         26
    0x95, 0x06,                         //     REPORT_COUNT (6)             28
    0x81, 0x03,                         //     INPUT (Cnst,Var,Abs)         30
    0x05, 0x01,                         //     USAGE_PAGE (Generic Desktop) 32
    0x09, 0x30,                         //     USAGE (X)                    34
    0x09, 0x31,                         //     USAGE (Y)                    36
    0x15, 0x81,                         //     LOGICAL_MINIMUM (-127)       38
    0x25, 0x7f,                         //     LOGICAL_MAXIMUM (127)        40
    0x75, 0x08,                         //     REPORT_SIZE (8)              42
    0x95, 0x02,                         //     REPORT_COUNT (2)             44
    0x81, 0x06,                         //     INPUT (Data,Var,Rel)         46
    0xc0,                               //   END_COLLECTION                 48
    0xc0                                // END_COLLECTION                   49/50
};

// consumer key
#define CONSUMER_VOLUME_INC             0x0001
#define CONSUMER_VOLUME_DEC             0x0002
#define CONSUMER_PLAY_PAUSE             0x0004
#define CONSUMER_MUTE                   0x0008
#define CONSUMER_SCAN_PREV_TRACK        0x0010
#define CONSUMER_SCAN_NEXT_TRACK        0x0020
#define CONSUMER_SCAN_FRAME_FORWARD     0x0040
#define CONSUMER_SCAN_FRAME_BACK        0x0080

//----------------------------------
static const u16 hid_key_click_table[8] = {
    CONSUMER_PLAY_PAUSE,
    0,
    CONSUMER_SCAN_PREV_TRACK,
    0,
    CONSUMER_SCAN_NEXT_TRACK,
    0,
    0,
    0,
};

static const u16 hid_key_hold_table[8] = {
    0,
    0,
    CONSUMER_VOLUME_DEC,
    0,
    CONSUMER_VOLUME_INC,
    0,
    0,
    0,
};

//----------------------------------
/* static const edr_init_cfg_t hidvrc_edr_config = { */
/*     .page_timeout = 8000, */
/*     .super_timeout = 8000, */
/*     .io_capabilities = 3, */
/*     .authentication_req = 2, */
/*     .oob_data = 0, */
/*     .sniff_param = &remote_24g_sniff_param, */
/*     .class_type = BD_CLASS_REMOTE_CONTROL, */
/*     .report_map = kb_hid_report_map, */
/*     .report_map_size = sizeof(kb_hid_report_map), */
/* }; */

//----------------------------------
static const ble_init_cfg_t remote_24g_ble_config = {
    .same_address = 0,
    .appearance = BLE_APPEARANCE_GENERIC_REMOTE_CONTROL,
    .report_map = kb_hid_report_map,
    .report_map_size = sizeof(kb_hid_report_map),
};

extern void p33_soft_reset(void);
extern void ble_hid_key_deal_test(u16 key_msg);
extern void ble_module_enable(u8 en);
static void remote_24g_set_soft_poweroff(void);
/*************************************************************************************************/
/*!
 *  \brief      ble遥控车距离测试接口
 *
 *  \param      [out]
 *
 *  \return
 *
 *  \note
 */
/*************************************************************************************************/
u8 key_msg[] = {0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00};
void remote_24g_phy_test()
{
    static u8 flag_key = 0;
    static u8 flag_errornum = 0;
    u8 flag_send0, flag_send1;

    if (flag_key == 26) {
        flag_errornum = (((26 - flag_errornum) * 100) / 26);
        printf("succ probality :%d%%", flag_errornum);
        flag_errornum = 0;
        flag_key = 0;
    }
    key_msg[2] = 0x04 + flag_key;

    if (ble_hid_data_send(KEYBOARD_REPORT_ID, key_msg, 8) == -97) {
        /* r_printf("error!!!!!!!"); */
        flag_send0 = 1;
    } else {
        flag_send0 = 0;
    }

    memset(key_msg, 0x0, 8);
    if (ble_hid_data_send(KEYBOARD_REPORT_ID, key_msg, 8) == -97) {
        /* r_printf("error!!!!!!!"); */
        flag_send1 = 1;
    } else {
        flag_send1 = 0;
    }

    flag_key++;

    if (flag_send0 || flag_send1) {
        flag_errornum++;
        printf("send eroor %d ", flag_errornum);
        flag_send0 = 0;
        flag_send1 = 0;
    }
}

/*************************************************************************************************/
/*!
 *  \brief      删除auto关机
 *
 *  \param      [in]
 *
 *  \return
 *
 *  \note
 */
/*************************************************************************************************/
static void remote_24g_auto_shutdown_disable(void)
{
    log_info("----%s", __FUNCTION__);
    if (g_auto_shutdown_timer) {
        sys_timeout_del(g_auto_shutdown_timer);
    }
}

/*************************************************************************************************/
/*!
 *  \brief      测试一直发空键
 *
 *  \param      [in]
 *
 *  \return
 *
 *  \note
 */
/*************************************************************************************************/
#if HID_TEST_KEEP_SEND_EN
static u8 test_keep_send_start = 0;
extern int ble_hid_timer_handle;
extern int edr_hid_timer_handle;
extern int ble_hid_data_send(u8 report_id, u8 *data, u16 len);
void remote_24g_test_keep_send_data(void)
{
    static const u8 test_data_000[8] = {0, 0, 0, 0};
    void (*hid_data_send_pt)(u8 report_id, u8 * data, u16 len) = NULL;

    if (!test_keep_send_start) {
        return;
    }

    if (bt_hid_mode == HID_MODE_BLE) {
#if TCFG_USER_BLE_ENABLE
        hid_data_send_pt = ble_hid_data_send;
#endif
    }
    hid_data_send_pt(1, test_data_000, sizeof(test_data_000));
}

/*************************************************************************************************/
/*!
 *  \brief      初始化测试发送
 *
 *  \param      [in]
 *
 *  \return
 *
 *  \note
 */
/*************************************************************************************************/
void remote_24g_test_keep_send_init(void)
{
    if (bt_hid_mode == HID_MODE_BLE) {
#if TCFG_USER_BLE_ENABLE
        log_info("###keep test ble\n");
        ble_hid_timer_handle = sys_s_hi_timer_add((void *)0, remote_24g_test_keep_send_data, 10);
#endif
    } else {
#if TCFG_USER_EDR_ENABLE
        log_info("###keep test edr\n");
        edr_hid_timer_handle = sys_s_hi_timer_add((void *)0, remote_24g_test_keep_send_data, 10);
#endif
    }
}
#endif


/*************************************************************************************************/
/*!
 *  \brief      按键处理
 *
 *  \param      [in]
 *
 *  \return
 *
 *  \note
 */
/*************************************************************************************************/
static void remote_24g_app_key_deal_test(u8 key_type, u8 key_value)
{
    u16 key_msg = 0;

    /*Audio Test Demo*/
#if TCFG_AUDIO_ENABLE
    if (key_type == KEY_EVENT_CLICK && key_value == TCFG_ADKEY_VALUE0) {
        printf(">>>key0:open mic\n");
        //br23/25 mic test
        /* extern int audio_adc_open_demo(void); */
        /* audio_adc_open_demo(); */
        //br30 mic test
        /* extern void audio_adc_mic_demo(u8 mic_idx, u8 gain, u8 mic_2_dac); */
        /* audio_adc_mic_demo(1, 1, 1); */

        /*encode test*/
        /* extern int audio_mic_enc_open(int (*mic_output)(void *priv, void *buf, int len), u32 code_type); */
        /* audio_mic_enc_open(NULL, AUDIO_CODING_OPUS);//opus encode test */
        /* audio_mic_enc_open(NULL, AUDIO_CODING_SPEEX);//speex encode test  */



        /*midi test*/

        //printf(">>>key0:open midi\n");
        //	midi_paly_test(KEY_IR_NUM_0);

    }
    if (key_type == KEY_EVENT_CLICK && key_value == TCFG_ADKEY_VALUE1) {
        printf(">>>key1:tone_play_test\n");
        //br23/25 tone play test
        /* tone_play_by_path(TONE_NORMAL, 1); */
        /* tone_play_by_path(TONE_BT_CONN, 1); */
        //br30 tone play test
        /* tone_play(TONE_NUM_8, 1); */
        /* tone_play(TONE_SIN_NORMAL, 1); */

        //	printf(">>>key0:set  midi\n");
        //	midi_paly_test(KEY_IR_NUM_1);

    }

    if (key_type == KEY_EVENT_CLICK && key_value == TCFG_ADKEY_VALUE2) {
        //	printf(">>>key2:play  midi\n");
        //	midi_paly_test(KEY_IR_NUM_2);
    }

#endif/*TCFG_AUDIO_ENABLE*/

#if HID_TEST_KEEP_SEND_EN
    if (key_type == KEY_EVENT_LONG && key_value == TCFG_ADKEY_VALUE0) {
        test_keep_send_start = !test_keep_send_start;
        log_info("test_keep_send_start=%d\n", test_keep_send_start);
        return;
    }
#endif

    if (key_type == KEY_EVENT_CLICK) {
        key_msg = hid_key_click_table[key_value];
    } else if (key_type == KEY_EVENT_HOLD) {
        key_msg = hid_key_hold_table[key_value];
    }

    if (key_msg) {
        log_info("key_msg = %02x\n", key_msg);
        if (bt_hid_mode == HID_MODE_EDR) {
#if TCFG_USER_BLE_ENABLE
            ble_hid_key_deal_test(key_msg);
#endif
        }
        return;
    }

    if (key_type == KEY_EVENT_TRIPLE_CLICK
        && (key_value == TCFG_ADKEY_VALUE3 || key_value == TCFG_ADKEY_VALUE0)) {
        remote_24g_set_soft_poweroff();
        return;
    }

    if (key_type == KEY_EVENT_DOUBLE_CLICK && key_value == TCFG_ADKEY_VALUE0) {
#if (TCFG_USER_EDR_ENABLE && TCFG_USER_BLE_ENABLE)
        is_remote_24g_active = 1;
        if (HID_MODE_BLE == bt_hid_mode) {
            remote_24g_app_select_btmode(HID_MODE_EDR);
        } else {
            remote_24g_app_select_btmode(HID_MODE_BLE);
        }
        os_time_dly(WAIT_DISCONN_TIME_MS / 10); //for disconnect ok
        p33_soft_reset();
        while (1);
#endif
    }
}

/*************************************************************************************************/
/*!
 *  \brief      绑定信息 VM读写操作
 *
 *  \param      [in]rw_flag: 0-read vm,1--write
 *
 *  \return
 *
 *  \note
 */
/*************************************************************************************************/
typedef struct {
    u16 head_tag;
    u8  mode;
} hid_vm_cfg_t;

#define	HID_VM_HEAD_TAG (0x3AA3)
static void remote_24g_vm_deal(u8 rw_flag)
{
    hid_vm_cfg_t info;
    int ret;
    int vm_len = sizeof(hid_vm_cfg_t);

    log_info("-hid_info vm_do:%d\n", rw_flag);
    memset(&info, 0, vm_len);

    if (rw_flag == 0) {
        bt_hid_mode = HID_MODE_NULL;
        ret = syscfg_read(CFG_AAP_MODE_INFO, (u8 *)&info, vm_len);
        if (!ret) {
            log_info("-null--\n");
        } else {
            if (HID_VM_HEAD_TAG == info.head_tag) {
                log_info("-exist--\n");
                log_info_hexdump((u8 *)&info, vm_len);
                bt_hid_mode = info.mode;
            }
        }

        if (HID_MODE_NULL == bt_hid_mode) {
#if TCFG_USER_BLE_ENABLE
            bt_hid_mode = HID_MODE_BLE;
#else
            bt_hid_mode = HID_MODE_EDR;
#endif
        } else {
            if (!TCFG_USER_BLE_ENABLE) {
                bt_hid_mode = HID_MODE_EDR;
            }

            if (!TCFG_USER_EDR_ENABLE) {
                bt_hid_mode = HID_MODE_BLE;
            }

            if (bt_hid_mode != info.mode) {
                log_info("-write00--\n");
                info.mode = bt_hid_mode;
                syscfg_write(CFG_AAP_MODE_INFO, (u8 *)&info, vm_len);
            }
        }
    } else {
        info.mode = bt_hid_mode;
        info.head_tag = HID_VM_HEAD_TAG;
        syscfg_write(CFG_AAP_MODE_INFO, (u8 *)&info, vm_len);
        log_info("-write11--\n");
        log_info_hexdump((u8 *)&info, vm_len);
    }
}

/*************************************************************************************************/
/*!
 *  \brief      进入软关机
 *
 *  \param      [in]
 *
 *  \return
 *
 *  \note
 */
/*************************************************************************************************/
static void remote_24g_set_soft_poweroff(void)
{
    log_info("remote_24g_set_soft_poweroff\n");
    is_remote_24g_active = 1;

#if TCFG_USER_BLE_ENABLE
    btstack_ble_exit(0);
#endif

#if TCFG_USER_BLE_ENABLE
    //延时300ms，确保BT退出链路断开
    sys_timeout_add(NULL, power_set_soft_poweroff, WAIT_DISCONN_TIME_MS);
#else
    power_set_soft_poweroff();
#endif
}

static void remote_24g_timer_handle_test(void)
{
    log_info("not_bt");
}

/*************************************************************************************************/
/*!
 *  \brief      app 入口
 *
 *  \param      [in]
 *
 *  \return
 *
 *  \note
 */
/*************************************************************************************************/
extern void bt_pll_para(u32 osc, u32 sys, u8 low_power, u8 xosc);
extern void le_hogp_set_direct_adv_type(u8 type);
static void remote_24g_app_start()
{
    log_info("=======================================");
    log_info("-----------REMOTE SERVER DEMO----------");
    log_info("=======================================");
    log_info("app_file: %s", __FILE__);

    clk_set("sys", BT_NORMAL_HZ);

    //有蓝牙
#if (TCFG_USER_BLE_ENABLE)
    u32 sys_clk =  clk_get("sys");
    bt_pll_para(TCFG_CLOCK_OSC_HZ, sys_clk, 0, 0);

#if TCFG_USER_BLE_ENABLE
    rf_set_24g_hackable_coded(CFG_RF_24G_CODE_ID);
    btstack_ble_start_before_init(&remote_24g_ble_config, 0);
    le_hogp_set_direct_adv_type(ADV_DIRECT_IND_LOW);
#endif

    btstack_init();

#else
    //no bt,to for test
    sys_timer_add(NULL, remote_24g_timer_handle_test, 1000);
#endif

    /* 按键消息使能 */
    sys_key_event_enable();

#if (TCFG_HID_AUTO_SHUTDOWN_TIME)
    //无操作定时软关机
    g_auto_shutdown_timer = sys_timeout_add(NULL, remote_24g_set_soft_poweroff, TCFG_HID_AUTO_SHUTDOWN_TIME * 1000);
#endif
}


/*************************************************************************************************/
/*!
 *  \brief      app  状态处理
 *
 *  \param      [in]
 *
 *  \return
 *
 *  \note
 */
/*************************************************************************************************/
static int remote_24g_state_machine(struct application *app, enum app_state state, struct intent *it)
{
    switch (state) {
    case APP_STA_CREATE:
        break;
    case APP_STA_START:
        if (!it) {
            break;
        }
        switch (it->action) {
        case ACTION_REMOTE_24G_MAIN:
            remote_24g_app_start();
            break;
        }
        break;
    case APP_STA_PAUSE:
        break;
    case APP_STA_RESUME:
        break;
    case APP_STA_STOP:
        break;
    case APP_STA_DESTROY:
        log_info("APP_STA_DESTROY\n");
        break;
    }

    return 0;
}

/*************************************************************************************************/
/*!
 *  \brief      蓝牙HCI事件消息处理
 *
 *  \param      [in]
 *
 *  \return
 *
 *  \note
 */
/*************************************************************************************************/
static int remote_24g_bt_hci_event_handler(struct bt_event *bt)
{
    //对应原来的蓝牙连接上断开处理函数  ,bt->value=reason
    log_info("----%s reason %x %x", __FUNCTION__, bt->event, bt->value);

#if TCFG_USER_BLE_ENABLE
    bt_comm_ble_hci_event_handler(bt);
#endif

    return 0;
}

/*************************************************************************************************/
/*!
 *  \brief      蓝牙连接状态事件消息处理
 *
 *  \param      [in]
 *
 *  \return
 *
 *  \note
 */
/*************************************************************************************************/
static int remote_24g_bt_connction_status_event_handler(struct bt_event *bt)
{
    log_info("----%s %d", __FUNCTION__, bt->event);

    switch (bt->event) {
    case BT_STATUS_INIT_OK:
        /*
         * 蓝牙初始化完成
         */
        log_info("BT_STATUS_INIT_OK\n");

        remote_24g_vm_deal(0);//bt_hid_mode read for VM

//根据模式执行对应蓝牙的初始化
        if (bt_hid_mode == HID_MODE_BLE) {
#if TCFG_USER_BLE_ENABLE
            btstack_ble_start_after_init(0);
#endif
        }
        remote_24g_app_select_btmode(HID_MODE_INIT);//

#if HID_TEST_KEEP_SEND_EN
        remote_24g_test_keep_send_init();
#endif
        break;

    default:
        break;
    }

    return 0;
}
/*************************************************************************************************/
/*!
 *  \brief      BLE连接状态消息回调
 *
 *  \param      [in]
 *
 *  \return
 *
 *  \note
 */
/*************************************************************************************************/
static void remote_24g_hogp_ble_status_callback(ble_state_e status, u8 reason)
{
    log_info("----%s reason %x %x", __FUNCTION__, status, reason);

    switch (status) {
    case BLE_ST_IDLE:
        break;
    case BLE_ST_ADV:
        break;
    case BLE_ST_CONNECT:
        /*
         * 蓝牙连接完成
         */
        switch (SELECT_PHY) {
        case CONN_SET_1M_PHY:
            log_info("SELECT PHY IS :1M");
            break;
        case CONN_SET_2M_PHY:
            log_info("SELECT PHY IS: 2M");
            break;
        case CONN_SET_CODED_PHY:
            log_info("SELECT PHY IS: CODED");
            break;
        default:
            break;
        }
        ble_comm_set_connection_data_phy(reason, SELECT_PHY, SELECT_PHY, SELECT_CODED_S2_0R_S8);
        break;
    case BLE_ST_SEND_DISCONN:
        break;
    case BLE_ST_DISCONN:
        /*
         * 蓝牙断开连接
         */
#if REMOTE_24G_KEEP_SEND_EN
        log_info("CLOSE REMOTE_24G_KEEP_SEND_EN\n");
        sys_timeout_del(remote_24g_phy_test_timer_id);
        remote_24g_phy_test_timer_id = 0;
#endif
        break;
    case BLE_ST_NOTIFY_IDICATE:
        /*
         * 蓝牙设备已经连接允许发数据
         */
#if REMOTE_24G_KEEP_SEND_EN
        if (remote_24g_phy_test_timer_id == 0) {
            log_info("OPEN REMOTE_24G_KEEP_SEND_EN\n");
            //建立定时器持续发数据
            remote_24g_phy_test_timer_id = sys_timer_add(NULL, remote_24g_phy_test, 200);
        }
#endif
        break;

    default:
        break;
    }
}

/*************************************************************************************************/
/*!
 *  \brief      蓝牙公共消息处理
 *
 *  \param      [in]
 *
 *  \return
 *
 *  \note
 */
/*************************************************************************************************/
static int remote_24g_bt_common_event_handler(struct bt_event *bt)
{
    log_info("----%s reason %x %x", __FUNCTION__, bt->event, bt->value);

    switch (bt->event) {
    case COMMON_EVENT_EDR_REMOTE_TYPE:
        log_info(" COMMON_EVENT_EDR_REMOTE_TYPE,%d \n", bt->value);
        break;

    case COMMON_EVENT_BLE_REMOTE_TYPE:
        log_info(" COMMON_EVENT_BLE_REMOTE_TYPE,%d \n", bt->value);
        break;

    case COMMON_EVENT_SHUTDOWN_DISABLE:
        remote_24g_auto_shutdown_disable();
        break;

    default:
        break;

    }
    return 0;
}

/*************************************************************************************************/
/*!
 *  \brief      按键事件处理
 *
 *  \param      [in]
 *
 *  \return
 *
 *  \note
 */
/*************************************************************************************************/
static void remote_24g_key_event_handler(struct sys_event *event)
{
    /* u16 cpi = 0; */
    u8 event_type = 0;
    u8 key_value = 0;

    if (event->arg == (void *)DEVICE_EVENT_FROM_KEY) {
        event_type = event->u.key.event;
        key_value = event->u.key.value;
        log_info("app_key_evnet: %d,%d\n", event_type, key_value);
        remote_24g_app_key_deal_test(event_type, key_value);
    }
}

/*************************************************************************************************/
/*!
 *  \brief      app 线程事件处理
 *
 *  \param      [in]
 *
 *  \return
 *
 *  \note
 */
/*************************************************************************************************/
static int remote_24g_event_handler(struct application *app, struct sys_event *event)
{
#if (TCFG_HID_AUTO_SHUTDOWN_TIME)
    //重置无操作定时计数
    if (event->type != SYS_DEVICE_EVENT || DEVICE_EVENT_FROM_POWER != event->arg) { //过滤电源消息

        sys_timer_modify(g_auto_shutdown_timer, TCFG_HID_AUTO_SHUTDOWN_TIME * 1000);
    }
#endif

    switch (event->type) {
    case SYS_KEY_EVENT:

        remote_24g_key_event_handler(event);
        return 0;

    case SYS_BT_EVENT:
#if TCFG_USER_BLE_ENABLE
        if ((u32)event->arg == SYS_BT_EVENT_TYPE_CON_STATUS) {
            remote_24g_bt_connction_status_event_handler(&event->u.bt);
        } else if ((u32)event->arg == SYS_BT_EVENT_TYPE_HCI_STATUS) {
            remote_24g_bt_hci_event_handler(&event->u.bt);
        } else if ((u32)event->arg == SYS_BT_EVENT_BLE_STATUS) {
            remote_24g_hogp_ble_status_callback(event->u.bt.event, event->u.bt.value);
        } else if ((u32)event->arg == SYS_BT_EVENT_FORM_COMMON) {
            return remote_24g_bt_common_event_handler(&event->u.dev);
        }
#endif
        return 0;

    case SYS_DEVICE_EVENT:
        if ((u32)event->arg == DEVICE_EVENT_FROM_POWER) {
            return app_power_event_handler(&event->u.dev, remote_24g_set_soft_poweroff);
        }
#if TCFG_CHARGE_ENABLE
        else if ((u32)event->arg == DEVICE_EVENT_FROM_CHARGE) {
            app_charge_event_handler(&event->u.dev);
        }
#endif
        return 0;

    default:
        return 0;
    }

    return 0;
}

/*************************************************************************************************/
/*!
 *  \brief      切换蓝牙模式
 *
 *  \param      [in]
 *
 *  \return
 *
 *  \note
 */
/*************************************************************************************************/
static void remote_24g_app_select_btmode(u8 mode)
{
    if (mode != HID_MODE_INIT) {
        if (bt_hid_mode == mode) {
            return;
        }
        bt_hid_mode = mode;
    } else {
        //init start
    }

    log_info("###### %s: %d,%d\n", __FUNCTION__, mode, bt_hid_mode);

    if (bt_hid_mode == HID_MODE_BLE) {
        //ble
        log_info("---------app select ble--------\n");
        if (!STACK_MODULES_IS_SUPPORT(BT_BTSTACK_LE) || !BT_MODULES_IS_SUPPORT(BT_MODULE_LE)) {
            log_info("not surpport ble,make sure config !!!\n");
            ASSERT(0);
        }

#if TCFG_USER_BLE_ENABLE
        if (mode == HID_MODE_INIT) {
            ble_module_enable(1);
        }
#endif

    }
    remote_24g_vm_deal(1);
}

/*************************************************************************************************/
/*!
 *  \brief      注册控制是否进入sleep
 *
 *  \param      [in]
 *
 *  \return
 *
 *  \note
 */
/*************************************************************************************************/
//-----------------------
//system check go sleep is ok
static u8 remote_24g_app_idle_query(void)
{
    return !is_remote_24g_active;
}

REGISTER_LP_TARGET(app_remote_24g_lp_target) = {
    .name = "app_remote_24g_deal",
    .is_idle = remote_24g_app_idle_query,
};


static const struct application_operation app_remote_24g_ops = {
    .state_machine  = remote_24g_state_machine,
    .event_handler 	= remote_24g_event_handler,
};

/*
 * 注册模式
 */
REGISTER_APPLICATION(app_remote_24g) = {
    .name 	= "remote_24g",
    .action	= ACTION_REMOTE_24G_MAIN,
    .ops 	= &app_remote_24g_ops,
    .state  = APP_STA_DESTROY,
};


#endif

