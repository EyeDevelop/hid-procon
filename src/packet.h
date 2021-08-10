#include <linux/types.h>
#include <linux/string.h>

#ifndef __PROCON_PACKET_H__
#define __PROCON_PACKET_H__

#define PACKET_RUMBLE_LENGTH 8
#define PACKET_ARG_LENGTH 53

// Defines a packet as sent to the switch.
// Is always 0x40 bytes long.
struct packet {
    __u8 command; // 0x01 for command. 0x10 for rumble.
    __u8 packet_num; // Incremented every packet. Loops from 0x0 to 0xF, then back around.
    __u8 rumble_data[PACKET_RUMBLE_LENGTH];
    __u8 subcommand;
    __u8 arguments[PACKET_ARG_LENGTH];
};

struct button_info {
    // Buttons.
    _Bool y;
    _Bool x;
    _Bool b;
    _Bool a;
    _Bool minus;
    _Bool plus;
    _Bool tr;
    _Bool tl;
    _Bool home;
    _Bool capture;

    // Triggers and bumpers.
    _Bool zr;
    _Bool zl;
    _Bool sr;
    _Bool sl;
    _Bool r;
    _Bool l;

    // D-pad.
    __s8 dpad_vertical;
    __s8 dpad_horizontal;
};

struct analog_stick_info {
    // Left stick.
    __s16 left_horizontal;
    __s16 left_vertical;

    // Right stick.
    __s16 right_horizontal;
    __s16 right_vertical;
};

struct input_response {
    __u8 report_id;
    __u8 timer;
    __u8 battery_and_connection_type;
    struct button_info button_data;
    struct analog_stick_info stick_data;
    __u8 vibrator;
    __u8 subcommand_ack;
    __u8 subcommand_id;
    __u8 subcommand_reply[35];
};

// Functions.
void init_packet(struct packet *p, __u8 command, __u8 subcommand, __u8 *args, size_t args_len);

void packet_add_rumble(struct packet *p);

int decode_message(struct input_response *resp, __u8 *resp_data, size_t len);

#endif