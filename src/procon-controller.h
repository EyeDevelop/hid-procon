#include <linux/types.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/mutex.h>

#include "packet.h"

#ifndef __PROCON_CONTROLLER_H__
#define __PROCON_CONTROLLER_H__

#define PROCON_STICK_MAX 32767
#define PROCON_STICK_FUZZ 300
#define PROCON_STICK_FLAT 1500

struct controller {
    struct input_dev *input;
    struct hid_device *handler;
    struct controller_info *info;

    __u8 current_packet_num;
    unsigned int time_last_cmd_sent;
    
    __u8 controller_id;
    __u8 player_indicator;
    
    struct mutex lock;
    struct mutex proc_lock;
    struct mutex input_lock;
};

#endif