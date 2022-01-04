#include <linux/string.h>

#include "packet.h"
#include "procon-controller.h"

void init_packet(struct packet *p, const __u8 command, const __u8 subcommand, const __u8 *args, size_t args_len) {
    __u8 neutral[PACKET_RUMBLE_LENGTH] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

    // Set the command information.
    p->command = command;
    p->subcommand = subcommand;
    
    // Copy the arguments into the packet.
    args_len = args_len > PACKET_ARG_LENGTH ? PACKET_ARG_LENGTH : args_len;
    for (size_t i = 0; i < args_len; i++) {
        p->arguments[i] = args[i];
    }

    // Make the rest zero bytes.
    for (size_t i = args_len; i < PACKET_ARG_LENGTH; i++) {
        p->arguments[i] = 0;
    }

    // Set the rumble data to nothing.
    memcpy(p->rumble_data, neutral, PACKET_RUMBLE_LENGTH);
}

void packet_add_rumble(struct packet *p) {
    __u8 neutral[PACKET_RUMBLE_LENGTH] = {0x00, 0x01, 0x40, 0x40, 0x00, 0x00, 0x00, 0x00};
    memcpy(p->rumble_data, neutral, PACKET_RUMBLE_LENGTH);
}

__s16 scale_and_clamp_single_stick(const __s16 stick_info) {
    __s32 val = ((stick_info - 2000) * PROCON_STICK_MAX) / 1500;

    if (val > PROCON_STICK_MAX) {
        return PROCON_STICK_MAX;
    }

    if (val < -PROCON_STICK_MAX) {
        return -PROCON_STICK_MAX;
    }

    return (__s16) val;
}

void scale_and_clamp(struct analog_stick_info *data) {
    data->left_horizontal = scale_and_clamp_single_stick(data->left_horizontal);
    data->left_vertical = -scale_and_clamp_single_stick(data->left_vertical);
    
    data->right_horizontal = scale_and_clamp_single_stick(data->right_horizontal);
    data->right_vertical = -scale_and_clamp_single_stick(data->right_vertical);
}

int decode_advanced_input_report(struct input_response *resp, const __u8 *resp_data, const size_t len) {
    const __u8 *left_data;
    const __u8 *right_data;

    if (len < 49) {
        return 1;
    }

    // Copy over the basic data.
    resp->report_id = resp_data[0];
    resp->timer = resp_data[1];
    resp->battery_and_connection_type = resp_data[2];
    resp->vibrator = resp_data[12];

    if (resp->report_id == 0x21) {
        resp->subcommand_ack = resp_data[13];
        resp->subcommand_id = resp_data[14];
        memcpy(resp->subcommand_reply, resp_data + 15, sizeof(resp->subcommand_reply));
    } else {
        resp->subcommand_ack = 0x00;
    }

    // Decode the button data.
    resp->button_data.y = resp_data[3] & 0x01;
    resp->button_data.x = resp_data[3] & 0x02;
    resp->button_data.b = resp_data[3] & 0x04;
    resp->button_data.a = resp_data[3] & 0x08;
    resp->button_data.sr = resp_data[3] & 0x10;
    resp->button_data.sl = resp_data[3] & 0x20;
    resp->button_data.r = resp_data[3] & 0x40;
    resp->button_data.zr = resp_data[3] & 0x80;

    resp->button_data.minus = resp_data[4] & 0x01;
    resp->button_data.plus = resp_data[4] & 0x02;
    resp->button_data.tr = resp_data[4] & 0x04;
    resp->button_data.tl = resp_data[4] & 0x08;
    resp->button_data.home = resp_data[4] & 0x10;
    resp->button_data.capture = resp_data[4] & 0x20;

    resp->button_data.dpad_vertical = (resp_data[5] & 0x01) ^ -((resp_data[5] & 0x02) >> 1);
    resp->button_data.dpad_horizontal = ((resp_data[5] & 0x04) >> 2) ^ -((resp_data[5] & 0x08) >> 3);

    // resp->button_data.sr = resp_data[5] & 0x10;
    // resp->button_data.sl = resp_data[5] & 0x20;
    resp->button_data.l = resp_data[5] & 0x40;
    resp->button_data.zl = resp_data[5] & 0x80;

    // Decode the stick data.
    left_data = resp_data + 6;
    right_data = resp_data + 9;

    resp->stick_data.left_horizontal = left_data[0] | ((left_data[1] & 0xF) << 8);
    resp->stick_data.left_vertical = (left_data[1] >> 4) | (left_data[2] << 4);
    resp->stick_data.right_horizontal = right_data[0] | ((right_data[1] & 0xF) << 8);
    resp->stick_data.right_vertical = (right_data[1] >> 4) | (right_data[2] << 4);

    // Clamp the stick data after applying the scaling function.
    scale_and_clamp(&resp->stick_data);

    return 0;
}

int decode_simple_input_report(struct input_response *resp, const __u8 *resp_data, const size_t len) {
    const __u8 *left_data;
    const __u8 *right_data;

    if (len < 11) {
        return 1;
    }

    // Copy basic data.
    resp->report_id = resp_data[0];

    // Decode the button data.
    resp->button_data.dpad_vertical = (resp_data[1] & 0x01) ^ -((resp_data[1] & 0x08) >> 3);
    resp->button_data.dpad_horizontal = ((resp_data[1] & 0x02) >> 1) ^ -((resp_data[1] & 0x04) >> 2);

    resp->button_data.sl = resp_data[1] & 0x10;
    resp->button_data.sr = resp_data[1] & 0x20;

    resp->button_data.minus = resp_data[2] & 0x01;
    resp->button_data.plus = resp_data[2] & 0x02;
    resp->button_data.tl = resp_data[2] & 0x04;
    resp->button_data.tr = resp_data[2] & 0x08;
    resp->button_data.home = resp_data[2] & 0x10;
    resp->button_data.capture = resp_data[2] & 0x20;
    resp->button_data.l = resp_data[2] & 0x40;
    resp->button_data.zl = resp_data[2] & 0x80;

    // Decode the stick data.
    left_data = resp_data + 4;
    right_data = resp_data + 8;

    resp->stick_data.left_horizontal = left_data[0] | (left_data[1] << 8);
    resp->stick_data.left_vertical = left_data[2] | (left_data[3] << 8);

    resp->stick_data.right_horizontal = right_data[0] | (right_data[1] << 8);
    resp->stick_data.right_vertical = right_data[2] | (right_data[3] << 8);

    return 0;
}

int decode_device_information(struct controller_info *resp, const __u8 *data, const size_t len) {
    if (len < 12) {
        return 1;
    }

    // Copy the data.
    resp->firmware_version_major = data[0];
    resp->firmware_version_minor = data[1];

    resp->controller_type = (enum controller_type) data[2];
    memcpy(resp->controller_mac_addr, data + 4, 6 * sizeof(__u8));
    resp->colour_mode = data[11];

    return 0;
}

int decode_spi_read(__u8 *buf, const __u8 *data, const size_t len) {
    __u8 address[4] = {0};
    __u8 buf_len = len > 0x1d ? 0x1d : len;

    memcpy(address, data, 4 * sizeof(__u8));
    buf_len = data[4] > buf_len ? buf_len : data[4];

    memcpy(buf, data + 5, buf_len);

    return 0;
}

int decode_message(struct input_response *resp, const __u8 *resp_data, const size_t len) {
    // Decode the report.
    if (resp_data[0] == 0x21 || resp_data[0] == 0x30) {
        return decode_advanced_input_report(resp, resp_data, len);
    } else if (resp_data[0] == 0x3F) {
        return decode_simple_input_report(resp, resp_data, len);
    }

    return 0;
}

char *format_controller_type(const enum controller_type type) {
    char *ret = kzalloc(13, GFP_KERNEL);
    if (ret == NULL) {
        pr_err("Cannot allocate memory for controller type!\n");
        return NULL;
    }

    switch (type) {
        case LEFT_JOYCON:
        snprintf(ret, 13, "Left JoyCon");
        break;

        case RIGHT_JOYCON:
        snprintf(ret, 13, "Right JoyCon");
        break;

        case PROCON:
        snprintf(ret, 13, "ProCon");
        break;

        default:
        snprintf(ret, 13, "Unknown");
        break;
    }

    return ret;
}

char *format_lpm(const __u8 low_power_mode) {
    char *ret = kzalloc(9, GFP_KERNEL);
    if (ret == NULL) {
        pr_err("Cannot allocate memory for LPM setting!\n");
        return NULL;
    }

    if (low_power_mode == 1) {
        snprintf(ret, 9, "enabled");
    } else {
        snprintf(ret, 9, "disabled");
    }

    return ret;
}

char *format_colour_mode(const __u8 colour_mode) {
    char *ret = kzalloc(8, GFP_KERNEL);
    if (ret == NULL) {
        pr_err("Cannot allocate memory for colour mode!\n");
        return NULL;
    }

    switch(colour_mode) {
        case 0x1:
        snprintf(ret, 8, "SPI");
        break;

        default:
        snprintf(ret, 8, "default");
        break;
    }

    return ret;
}