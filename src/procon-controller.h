#include <linux/types.h>
#include <linux/hid.h>
#include <linux/input.h>

#ifndef __PROCON_CONTROLLER_H__
#define __PROCON_CONTROLLER_H__

#define PROCON_STICK_MAX 32767
#define PROCON_STICK_FUZZ 1000
#define PROCON_STICK_FLAT 2000

struct controller {
    struct input_dev *input;
    struct hid_device *handler;

    __u8 current_packet_num;
    unsigned int time_last_cmd_sent;
    
    __u8 player_id;
};

#endif