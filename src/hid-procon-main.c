#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/hid.h>
#include <linux/input.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/jiffies.h>

#include "hids.h"
#include "commands.h"
#include "packet.h"
#include "procon-print.h"
#include "procon-controller.h"
#include "procon-input.h"

static unsigned int MAX_SUBCMD_RATE_MS = 50;
static bool free_players[] = {true, true, true, true};

void enforce_baudrate(struct controller *c) {
    unsigned int current_ms = jiffies_to_msecs(jiffies);
	unsigned int delta_ms = current_ms - c->time_last_cmd_sent;

	while (delta_ms < MAX_SUBCMD_RATE_MS) {
		mdelay(5);
        current_ms = jiffies_to_msecs(jiffies);
		delta_ms = current_ms - c->time_last_cmd_sent;
	}

	c->time_last_cmd_sent = current_ms;
}

int get_next_free_player(void) {
    for (int i = 0; i < sizeof(free_players); i++) {
        if (free_players[i]) {
            free_players[i] = false;
            return i + 1;
        }
    }

    return -1;
}

__u8 get_player_led(__u8 player_id) {
    switch (player_id) {
        case 1:
        return PROCON_LED_ON_1;

        case 2:
        return PROCON_LED_ON_2;

        case 3:
        return PROCON_LED_ON_3;

        case 4:
        return PROCON_LED_ON_4;
    }

    return PROCON_LED_FLASH_4;
}

int send_message_raw(struct hid_device *hdev, __u8 *data, size_t len) {
    __u8 *buf;
    int ret;

    // Try to allocate enough memory to send the message.
    buf = kmemdup(data, len, GFP_KERNEL);
    if (buf == NULL) {
        return -ENOMEM;
    }

    // Send the message.
    ret = hid_hw_output_report(hdev, buf, len);

    // Free the buffer.
    kfree(buf);

    if (ret < 0) {
        pr_warn("Failed to send message to device: %d\n", ret);
    }

    return ret;
}

int send_message(struct controller *c, struct packet *p) {
    size_t len = 0x40;

    // Set the packet number.
    p->packet_num = c->current_packet_num++;
    if (c->current_packet_num > 0x0F) {
        c->current_packet_num = 0x00;
    }

    // If the packet is a rumble only command, truncate the message to 11 bytes.
    if (p->command == PROCON_CMD_RUMBLE) {
        len = 0xB;
    }

    // Wait until we can send.
    enforce_baudrate(c);

    // Convert the packet to a u8 array and send the raw message.
    return send_message_raw(c->handler, (__u8*) p, 0x40);
}

int set_player_light(struct controller *c, __u8 led_information) {
    int ret;
    struct packet p;

    __u8 light_arg[] = {led_information};
    init_packet(&p, PROCON_CMD_COMMAND_AND_RUMBLE, PROCON_SUB_SET_LIGHT, light_arg, sizeof(light_arg));
    
    ret = send_message(c, &p);
    if (ret < 0) {
        return ret;
    }

    return 0;
}

int procon_init_device(struct hid_device *hdev, const struct hid_device_id *id) {
    int ret;

    struct controller *c;
    int player_id = get_next_free_player();

    __u8 handshake[] = {0x80, 0x02};
    __u8 baudrate_increase[] = {0x80, 0x03};
    __u8 report_mode_args[] = {0x30};

    __u8 disable_arg[] = {0x00};
    __u8 enable_arg[] = {0x01};

    struct packet p;

    // Check if there is a player slot available.
    if (player_id == -1) {
        pr_err("There are already 4 controllers connected!");
        ret = -1;
        goto err_ret;
    }

    // Probe has started.
    pr_info("Probe started for: %s [%x:%x].\n", hdev->name, id->vendor, id->product);

    // Try to parse the HID information.
    ret = hid_parse(hdev);
    if (ret < 0) {
        pr_err("Could not parse HID device.\n");
        goto err_ret;
    }

    // Use hidraw to get the inputs from the controller. Initialise hidraw.
    ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
    if (ret < 0) {
        pr_err("Could not start hidraw.\n");
        goto err_ret;
    }

    ret = hid_hw_open(hdev);
    if (ret < 0) {
        pr_err("Failed to open device with hidraw.\n");
        goto err_stop;
    }
    hid_device_io_start(hdev);

    // Initialise controller struct.
    c = devm_kzalloc(&hdev->dev, sizeof(struct controller), GFP_KERNEL);
    if (c == NULL) {
        free_players[player_id - 1] = true;
        return -ENOMEM;
    }

    c->player_id = player_id;
    c->current_packet_num = 0;
    c->handler = hdev;
    hid_set_drvdata(hdev, c);

    // Perform handshake.
    ret = send_message_raw(hdev, handshake, sizeof(handshake));
    mdelay(MAX_SUBCMD_RATE_MS);

    // Increase baudrate
    ret = send_message_raw(hdev, baudrate_increase, sizeof(baudrate_increase));
    mdelay(MAX_SUBCMD_RATE_MS);

    // Handshake again.
    ret = send_message_raw(hdev, handshake, sizeof(handshake));
    mdelay(MAX_SUBCMD_RATE_MS);

    // First ask the controller for its info.
    pr_info("Asking controller for info...\n");
    init_packet(&p, PROCON_CMD_COMMAND_AND_RUMBLE, PROCON_SUB_REQUEST_INFO, NULL, 0);
    
    ret = send_message(c, &p);
    if (ret < 0) {
        pr_err("Failed to ask for info: %d.\n", ret);
        goto err_close;
    }

    // Set the shipment low power state.
    pr_info("Setting controller low power state...\n");
    init_packet(&p, PROCON_CMD_COMMAND_AND_RUMBLE, PROCON_SUB_SET_POWER_STATE, disable_arg, sizeof(disable_arg));

    ret = send_message(c, &p);
    if (ret < 0) {
        pr_err("Failed to set low power state: %d.\n", ret);
        goto err_close;
    }

    // Set input report mode to full reporting mode.
    pr_info("Setting input report mode...\n");
    init_packet(&p, PROCON_CMD_COMMAND_AND_RUMBLE, PROCON_SUB_SET_REPORT_MODE, report_mode_args, sizeof(report_mode_args));
    packet_add_rumble(&p);

    ret = send_message(c, &p);
    if (ret < 0) {
        pr_err("Failed to set input report mode: %d.\n", ret);
        goto err_close;
    }

    // Send a vibrate command.
    init_packet(&p, PROCON_CMD_RUMBLE, 0, NULL, 0);
    packet_add_rumble(&p);
    ret = send_message(c, &p);

    // Disable the IMU.
    pr_info("Disabling IMU...\n");
    init_packet(&p, PROCON_CMD_COMMAND_AND_RUMBLE, PROCON_SUB_SET_IMU, disable_arg, sizeof(disable_arg));

    ret = send_message(c, &p);
    if (ret < 0) {
        pr_err("Could not disable IMU: %d.\n", ret);
        goto err_close;
    }

    // Enable vibration.
    pr_info("Disabling vibration...\n");
    init_packet(&p, PROCON_CMD_COMMAND_AND_RUMBLE, PROCON_SUB_SET_VIBRATION, disable_arg, sizeof(disable_arg));

    ret = send_message(c, &p);
    if (ret < 0) {
        pr_err("Could not disable vibration: %d.\n", ret);
        goto err_close;
    }

    // Set the player light.
    pr_info("Setting player %d light...\n", c->player_id);
    set_player_light(c, get_player_led(c->player_id));

    // Create input device for the controller.
    ret = create_input_device(c);
    if (ret < 0) {
        pr_err("Could not create input device: %d.\n", ret);
        goto err_close;
    }

    return 0;

err_close:
    hid_hw_close(hdev);
err_stop:
    hid_hw_stop(hdev);
    free_players[player_id - 1] = true;
err_ret:
    return ret;
}

int procon_event(struct hid_device *hdev, struct hid_report *report, __u8 *raw_data, int size) {
    struct input_response resp;
    struct controller *c;

    // Get the controller from the device.
    c = hid_get_drvdata(hdev);

    // Decode the controller message.
    decode_message(&resp, raw_data, size);

    // Full input report. Pass this to the input device.
    if (c->input != NULL && (resp.report_id == 0x30 || resp.report_id == 0x21)) {
        // D-Pad
        input_report_abs(c->input, ABS_HAT0X, resp.button_data.dpad_horizontal);
        input_report_abs(c->input, ABS_HAT0Y, resp.button_data.dpad_vertical);

        // Buttons on the right
        input_report_key(c->input, BTN_NORTH, resp.button_data.y);
        input_report_key(c->input, BTN_WEST, resp.button_data.x);
        input_report_key(c->input, BTN_SOUTH, resp.button_data.b);
        input_report_key(c->input, BTN_EAST, resp.button_data.a);

        // Buttons in the middle.
        input_report_key(c->input, BTN_START, resp.button_data.plus);
        input_report_key(c->input, BTN_SELECT, resp.button_data.minus);
        input_report_key(c->input, BTN_MODE, resp.button_data.home);

        // Bumpers and triggers.
        input_report_key(c->input, BTN_TR, resp.button_data.r);
        input_report_key(c->input, BTN_TL, resp.button_data.l);
        input_report_key(c->input, BTN_TR2, resp.button_data.zr);
        input_report_key(c->input, BTN_TL2, resp.button_data.zl);

        // Stick buttons.
        input_report_key(c->input, BTN_THUMBL, resp.button_data.tl);
        input_report_key(c->input, BTN_THUMBR, resp.button_data.tr);

        // Analog joysticks.
        input_report_abs(c->input, ABS_X, resp.stick_data.left_horizontal);
        input_report_abs(c->input, ABS_Y, resp.stick_data.left_vertical);

        input_report_abs(c->input, ABS_RX, resp.stick_data.right_horizontal);
        input_report_abs(c->input, ABS_RY, resp.stick_data.right_vertical);

        input_sync(c->input);
    }
    
    return 0;
}

void procon_remove_device(struct hid_device *hdev) {
    struct controller *c = (struct controller*) hid_get_drvdata(hdev);

    pr_info("Device removed: %s [%x:%x] [player %d].\n", hdev->name, hdev->vendor, hdev->product, c->player_id);

    if (c != NULL && c->player_id > 0 && c->player_id <= 4) {
        free_players[c->player_id - 1] = true;
    }

    hid_hw_close(hdev);
    hid_hw_stop(hdev);
}

static const struct hid_device_id procon_devices[] = {
    { HID_BLUETOOTH_DEVICE(VENDOR_NINTENDO, DEVICE_PROCON) },
    { }
};
MODULE_DEVICE_TABLE(hid, procon_devices);

static struct hid_driver procon_hid_driver = {
    .name = "procon",
    .id_table = procon_devices,
    .probe = procon_init_device,
    .remove = procon_remove_device,
    .raw_event = procon_event,
};
module_hid_driver(procon_hid_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Johannes Goor");
MODULE_DESCRIPTION("Kernel driver for the Nintendo Switch Pro Controller.");