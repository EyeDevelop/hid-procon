#include <linux/types.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/mutex.h>

#ifndef __PROCON_CONTROLLER_H__
#define __PROCON_CONTROLLER_H__

#define PROCON_STICK_MAX 32767
#define PROCON_STICK_FUZZ 300
#define PROCON_STICK_FLAT 1500

#define CALIBRATION_DEFAULT_CENTER 2000
#define CALIBRATION_DEFAULT_MIN 500
#define CALIBRATION_DEFAULT_MAX 3500

enum controller_type {
    LEFT_JOYCON = 1,
    RIGHT_JOYCON = 2,
    PROCON = 3,
};

struct controller_info {
    __u8 firmware_version_major;
    __u8 firmware_version_minor;
    enum controller_type controller_type;
    __u8 *controller_mac_addr; // Has length 6.
    __u8 low_power_mode;
    __u8 colour_mode;
};

struct controller {
    struct input_dev *input;
    struct hid_device *handler;
    struct controller_info *info;

    __s32 rs_center;
    __s32 rs_min;
    __s32 rs_max;

    __s32 ls_center;
    __s32 ls_min;
    __s32 ls_max;

    __u8 current_packet_num;
    unsigned int time_last_cmd_sent;
    
    __u8 controller_id;
    __u8 player_indicator;
    
    struct mutex lock;
    struct mutex proc_lock;
    struct mutex input_lock;
};

#endif