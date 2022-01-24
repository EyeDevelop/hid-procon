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
#include <linux/proc_fs.h>
#include <linux/mutex.h>

#include "hids.h"
#include "commands.h"
#include "packet.h"
#include "procon-print.h"
#include "procon-controller.h"
#include "procon-input.h"
#include "util.h"

#define MAX_CONTROLLER_SUPPORT 8
#define MAX_LIGHT_SUPPORT 4
#define CHARS_NEEDED_FOR_CONTROLLER_ID 1

static unsigned int MAX_SUBCMD_RATE_MS = 70;

static bool controller_spots[MAX_CONTROLLER_SUPPORT] = {};
struct controller *connected_controllers[MAX_CONTROLLER_SUPPORT] = {};

struct proc_dir_entry *procon_proc_dir = NULL;
struct proc_dir_entry *procon_player_dirs[MAX_CONTROLLER_SUPPORT] = {};

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

int get_next_free_controller(void) {
    for (int i = 0; i < MAX_CONTROLLER_SUPPORT; i++) {
        if (controller_spots[i]) {
            controller_spots[i] = false;
            return i;
        }
    }

    return -1;
}

__u8 get_player_led_arg(__u8 player_id) {
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
    int ret = 0;

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

    // Wait a bit longer if the mutex is locked.
    mutex_lock(&c->lock);

    ret = send_message_raw(c->handler, (__u8*) p, len);

    // Unlock the mutex and return.
    mutex_unlock(&c->lock);
    return ret;
}

int set_player_led(struct controller *c, __u8 led_information) {
    int ret;
    struct packet p;

    __u8 light_arg[1] = {led_information};
    init_packet(&p, PROCON_CMD_COMMAND_AND_RUMBLE, PROCON_SUB_SET_LIGHT, light_arg, sizeof(light_arg));
    
    ret = send_message(c, &p);
    if (ret < 0) {
        return ret;
    }

    return 0;
}

int procon_proc_init(struct inode *file_info, struct file *file) {
    int controller_id = -1;
    const unsigned char *path = file->f_path.dentry->d_parent->d_name.name;
    struct controller *c;

    // Try to get the connected controller id form the proc file path.
    // This should work, since the proc files are created in the form:
    // /proc/procon/player<n>/<file>
    if (sscanf(path, "controller%d", &controller_id) == 0) {
        pr_err("Could not get controller id from path: %s\n", path);
        return 1;
    }

    // Check if player within limits.
    if (controller_id >= MAX_CONTROLLER_SUPPORT || controller_id < 0) {
        pr_err("Wrong controller id (%d) in file: %s\n", controller_id, path);
        return 1;
    }

    if (connected_controllers[controller_id] == NULL) {
        pr_err("Controller %d no longer connected!\n", controller_id);
        return 1;
    }

    c = connected_controllers[controller_id];
    mutex_lock(&c->proc_lock);

    // Set private data to be used in further functions.
    file->private_data = connected_controllers[controller_id];

    return 0;
}

int procon_proc_exit(struct inode *file_info, struct file *file) {
    struct controller* c;

    if (file->private_data == NULL) {
        pr_err("Private data not set!\n");
        return 1;
    }

    c = (struct controller*) file->private_data;

    mutex_unlock(&c->proc_lock);
    return 0;
}

ssize_t procon_proc_get_player_led(struct file *file, char __user *buffer, size_t len, loff_t *offset) {
    struct controller *c;
    char player_indicator[3];

    if ((int) (*offset) >= sizeof(player_indicator)) {
        return 0;
    }

    if (file->private_data == NULL) {
        pr_err("Private data not set!\n");
        return 0;
    }

    c = (struct controller*) file->private_data;
    snprintf(player_indicator, sizeof(player_indicator), "%d\n", c->player_indicator);

    if (copy_to_user(buffer, player_indicator, sizeof(player_indicator))) {
        pr_err("Failed writing player_id!\n");
        return 0;
    }

    *offset = sizeof(player_indicator);
    return sizeof(player_indicator);
}

ssize_t procon_proc_set_player_led(struct file *file, const char __user *buffer, size_t len, loff_t *offset) {
    struct controller *c;
    char player_indicator[2];
    int player_led = 0;

    if ((int) (*offset) >= sizeof(player_indicator)) {
        return 0;
    }

    if (file->private_data == NULL) {
        pr_err("Private data not set!\n");
        return 0;
    }

    c = (struct controller*) file->private_data;
    
    if (copy_from_user(player_indicator, buffer, sizeof(player_indicator))) {
        pr_err("Failed reading from user!\n");
        return 0;
    }

    if (sscanf(player_indicator, "%d", &player_led) == 0) {
        return 0;
    }

    if (player_led < 0 || player_led > MAX_LIGHT_SUPPORT) {
        return 0;
    }

    if (set_player_led(c, get_player_led_arg(player_led))) {
        pr_err("Could not set LED.\n");
        return sizeof(player_indicator);
    }

    c->player_indicator = player_led;

    *offset = sizeof(player_indicator);
    return sizeof(player_indicator);
}

void procon_proc_create_player_indicator(int controller_id) {
    static struct proc_ops ops = {
        .proc_open = procon_proc_init,
        .proc_release = procon_proc_exit,
        .proc_read = procon_proc_get_player_led,
        .proc_write = procon_proc_set_player_led,
    };

    struct proc_dir_entry *proc_entry;

    proc_entry = proc_create("led", 0666, procon_player_dirs[controller_id], &ops);
    if (proc_entry == NULL) {
        pr_err("Cannot create led proc file!\n");
        return;
    }
}

ssize_t procon_proc_get_low_power_mode(struct file *file, char __user *buffer, size_t len, loff_t *offset) {
    struct controller *c;
    char low_power_mode[3];

    if ((int) (*offset) >= sizeof(low_power_mode)) {
        return 0;
    }

    if (file->private_data == NULL) {
        pr_err("Private data not set!\n");
        return 0;
    }

    c = (struct controller*) file->private_data;
    snprintf(low_power_mode, sizeof(low_power_mode), "%d\n", c->info->low_power_mode);

    if (copy_to_user(buffer, low_power_mode, sizeof(low_power_mode))) {
        pr_err("Failed writing low power mode!\n");
        return 0;
    }

    *offset = sizeof(low_power_mode);
    return sizeof(low_power_mode);
}

ssize_t procon_proc_set_low_power_mode(struct file *file, const char __user *buffer, size_t len, loff_t *offset) {
    struct controller *c;
    __u8 lpm_set_args[1] = {0};
    __u8 lpm_read_args[] = {0x00, 0x50, 0x00, 0x00, 0x01};
    int enable = 0;
    char buf[2] = {0};
    struct packet p;

    if ((int) (*offset) >= sizeof(buf)) {
        return 0;
    }

    if (file->private_data == NULL) {
        pr_err("Private data not set!\n");
        return 0;
    }

    c = (struct controller*) file->private_data;

    // Copy the user buffer to our buffer and convert to an unsigned integer.
    if (copy_from_user(buf, buffer, sizeof(buf))) {
        pr_err("Cannot read data from user!\n");
        return 0;
    }

    // Check if user actually sent an int.
    if (sscanf(buf, "%d", &enable) == 0) {
        return 0;
    }

    lpm_set_args[0] = enable == 1 ? 0x1 : 0x0;

    // Send new lower power mode to controller.
    init_packet(&p, PROCON_CMD_COMMAND_AND_RUMBLE, PROCON_SUB_SET_POWER_STATE, lpm_set_args, sizeof(lpm_set_args));
    send_message(c, &p);

    // Also request new controller info.
    init_packet(&p, PROCON_CMD_COMMAND_AND_RUMBLE, PROCON_SUB_READ_SPI, lpm_read_args, sizeof(lpm_read_args));
    send_message(c, &p);

    *offset = sizeof(buf);
    return sizeof(buf);
}

void procon_proc_create_low_power_mode(int controller_id) {
    static struct proc_ops ops = {
        .proc_open = procon_proc_init,
        .proc_release = procon_proc_exit,
        .proc_read = procon_proc_get_low_power_mode,
        .proc_write = procon_proc_set_low_power_mode,
    };

    struct proc_dir_entry *proc_entry;

    proc_entry = proc_create("lpm", 0666, procon_player_dirs[controller_id], &ops);
    if (proc_entry == NULL) {
        pr_err("Cannot create low power mode proc file!\n");
        return;
    }
}

ssize_t procon_proc_get_controller_info(struct file *file, char __user *buffer, size_t len, loff_t *offset) {
    struct controller *c;
    const char fmt[] = "Device information\n\nPlayer LED: %d\nFirmware: %d.%d\nType: %s\nMAC: %s\nLPM: %s\nCM: %s\n";

    // Ret has size of the base format, plus the extremes for each of the format values.
    // CHARS_NEEDED_FOR_CONTROLLER_ID, for Player LED.
    // 7 (xxx.xxx), for major.minor
    // 12 (Right JoyCon), for type
    // 17, for MAC address
    // 8 (disabled), for LPM
    // 7 (default), for CM.
    // 1 for \00.
    char ret[sizeof(fmt) + CHARS_NEEDED_FOR_CONTROLLER_ID + 7 + 12 + 17 + 8 + 7 + 1] = {0};
    char *mac;
    char *controller_type;
    char *lpm;
    char *colour_mode;

    if ((int) (*offset) >= sizeof(ret)) {
        return 0;
    }

    if (file->private_data == NULL) {
        pr_err("Private data not set!\n");
        return 0;
    }

    c = (struct controller*) file->private_data;

    mac = format_mac_addr(c->info->controller_mac_addr);
    if (mac == NULL) {
        return 0;
    }

    controller_type = format_controller_type(c->info->controller_type);
    if (controller_type == NULL) {
        return 0;
    }

    lpm = format_lpm(c->info->low_power_mode);
    if (lpm == NULL) {
        return 0;
    }

    colour_mode = format_colour_mode(c->info->colour_mode);
    if (colour_mode == NULL) {
        return 0;
    }

    snprintf(ret, sizeof(ret), fmt, c->player_indicator, c->info->firmware_version_major, c->info->firmware_version_minor, controller_type, mac, lpm, colour_mode);

    // Free all used variables.
    kfree(mac);
    kfree(controller_type);
    kfree(lpm);
    kfree(colour_mode);

    if(copy_to_user(buffer, ret, sizeof(ret))) {
        pr_err("Failed writing device information!\n");
        return 0;
    }
    
    *offset = sizeof(ret);
    return sizeof(ret);
}

void procon_proc_create_info(int controller_id) {
    static struct proc_ops ops = {
        .proc_open = procon_proc_init,
        .proc_release = procon_proc_exit,
        .proc_read = procon_proc_get_controller_info,
    };

    struct proc_dir_entry *proc_entry;

    proc_entry = proc_create("info", 0444, procon_player_dirs[controller_id], &ops);
    if (proc_entry == NULL) {
        pr_err("Cannot create device info proc file!\n");
        return;
    }
}

int procon_init_device(struct hid_device *hdev, const struct hid_device_id *id) {
    int ret;

    struct controller *c;
    int controller_id = get_next_free_controller();

    __u8 handshake[] = {0x80, 0x02};
    __u8 baudrate_increase[] = {0x80, 0x03};
    __u8 lpm_read_args[] = {0x00, 0x50, 0x00, 0x00, 0x01};
    __u8 report_mode_args[] = {0x30};

    __u8 disable_arg[] = {0x00};
    __u8 enable_arg[] = {0x01};

    struct packet p;
    char* controller_name;

    // Check if there is a controller slot available.
    if (controller_id == -1) {
        pr_err("Driver does not support more than %d controllers!", MAX_CONTROLLER_SUPPORT);
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
    mutex_init(&c->lock);
    mutex_init(&c->proc_lock);
    mutex_init(&c->input_lock);
    c->info = devm_kzalloc(&hdev->dev, sizeof(struct controller_info), GFP_KERNEL);
    c->info->controller_mac_addr = devm_kzalloc(&hdev->dev, 6 * sizeof(__u8), GFP_KERNEL);

    if (c == NULL || c->info == NULL || c->info->controller_mac_addr == NULL) {
        controller_spots[controller_id] = true;
        return -ENOMEM;
    }

    c->controller_id = controller_id;
    c->player_indicator = 0;
    c->current_packet_num = 0;
    c->handler = hdev;
    hid_set_drvdata(hdev, c);
    connected_controllers[controller_id] = c;

    // Perform handshake.
    mutex_lock(&c->lock);
    send_message_raw(hdev, handshake, sizeof(handshake));
    mdelay(MAX_SUBCMD_RATE_MS);

    // Increase baudrate
    send_message_raw(hdev, baudrate_increase, sizeof(baudrate_increase));
    mdelay(MAX_SUBCMD_RATE_MS);

    // Handshake again.
    send_message_raw(hdev, handshake, sizeof(handshake));
    mdelay(MAX_SUBCMD_RATE_MS);
    mutex_unlock(&c->lock);

    // First ask the controller for its info.
    init_packet(&p, PROCON_CMD_COMMAND_AND_RUMBLE, PROCON_SUB_REQUEST_INFO, NULL, 0);
    
    ret = send_message(c, &p);
    if (ret < 0) {
        pr_err("Failed to ask for info: %d.\n", ret);
        goto err_close;
    }

    // Fetch LPM mode.
    init_packet(&p, PROCON_CMD_COMMAND_AND_RUMBLE, PROCON_SUB_READ_SPI, lpm_read_args, sizeof(lpm_read_args));

    ret = send_message(c, &p);
    if (ret < 0) {
        pr_err("Failed to read LPM information: %d.\n", ret);
        goto err_close;
    }

    // Set input report mode to full reporting mode.
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
    send_message(c, &p);

    // Disable the IMU.
    init_packet(&p, PROCON_CMD_COMMAND_AND_RUMBLE, PROCON_SUB_SET_IMU, disable_arg, sizeof(disable_arg));

    ret = send_message(c, &p);
    if (ret < 0) {
        pr_err("Could not disable IMU: %d.\n", ret);
        goto err_close;
    }

    // Enable vibration.
    init_packet(&p, PROCON_CMD_COMMAND_AND_RUMBLE, PROCON_SUB_SET_VIBRATION, disable_arg, sizeof(disable_arg));

    ret = send_message(c, &p);
    if (ret < 0) {
        pr_err("Could not disable vibration: %d.\n", ret);
        goto err_close;
    }

    // Set the player light.
    set_player_led(c, get_player_led_arg(c->player_indicator));

    // Create input device for the controller.
    ret = create_input_device(c);
    if (ret < 0) {
        pr_err("Could not create input device: %d.\n", ret);
        goto err_close;
    }

    // Create a proc folder for settings for this device.
    controller_name = devm_kzalloc(&hdev->dev, 12, GFP_KERNEL);
    if (controller_name == NULL) {
        return -ENOMEM;
    }

    snprintf(controller_name, 12, "controller%d", controller_id);

    if (procon_proc_dir == NULL) {
        pr_warn("Not creating proc entries, parent is null.\n");
        return 0;
    }

    procon_player_dirs[controller_id] = proc_mkdir(controller_name, procon_proc_dir);
    if (procon_player_dirs[controller_id] == NULL) {
        pr_warn("Not creating proc entires, player folder is null.\n");
        return 0;
    }

    procon_proc_create_player_indicator(controller_id);
    procon_proc_create_info(controller_id);
    procon_proc_create_low_power_mode(controller_id);

    pr_info("Device %s [%02x:%02x] successfully connected as id controller%d!\n", hdev->name, hdev->vendor, hdev->product, controller_id);

    return 0;

err_close:
    hid_hw_close(hdev);
err_stop:
    hid_hw_stop(hdev);
    controller_spots[controller_id] = true;
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

    if (resp.subcommand_id == 0x02) {
        mutex_lock(&c->lock);
        decode_device_information(c->info, resp.subcommand_reply, 12);
        mutex_unlock(&c->lock);
    } else if (resp.subcommand_id == 0x10) {
        __u8 buf[0x1d] = {0};
        decode_spi_read(buf, resp.subcommand_reply, 0x1d);

        mutex_lock(&c->lock);
        c->info->low_power_mode = buf[0] == 0x1 ? 1 : 0;
        mutex_unlock(&c->lock);
    }

    mutex_lock(&c->input_lock);

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

    mutex_unlock(&c->input_lock);
    
    return 0;
}

void procon_remove_device(struct hid_device *hdev) {
    struct controller *c = (struct controller*) hid_get_drvdata(hdev);

    if (c == NULL) {
        goto close;
    }

    // Remove proc entries.
    if (c->controller_id >= 0 && c->controller_id < MAX_CONTROLLER_SUPPORT && procon_player_dirs[c->controller_id] != NULL) {
        proc_remove(procon_player_dirs[c->controller_id]);
        controller_spots[c->controller_id] = true;
    }

    pr_info("Device removed: %s [%02x:%02x] [controller%d].\n", hdev->name, hdev->vendor, hdev->product, c->controller_id);

close:
    hid_hw_close(hdev);
    hid_hw_stop(hdev);
}

static const struct hid_device_id procon_devices[] = {
    { HID_BLUETOOTH_DEVICE(VENDOR_NINTENDO, DEVICE_PROCON) },
    { HID_BLUETOOTH_DEVICE(VENDOR_NINTENDO, DEVICE_JOYCON_RIGHT) },
    { HID_BLUETOOTH_DEVICE(VENDOR_NINTENDO, DEVICE_JOYCON_LEFT) },
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

// Initialisation of the driver.
static int __init procon_hid_driver_init(void) {
    int ret = 0;

    // Register the driver with HID.
    ret = hid_register_driver(&procon_hid_driver);
    if (ret != 0) {
        pr_crit("Cannot register device with HID: %d\n", ret);
        return ret;
    }

    // Set all controller slots to available.
    for (int i = 0; i < MAX_CONTROLLER_SUPPORT; i++) {
        controller_spots[i] = true;
        connected_controllers[i] = NULL;
        procon_player_dirs[i] = NULL;
    }

    // Create some proc entries for user-space controller management.
    procon_proc_dir = proc_mkdir("procon", NULL);

    pr_info("Ready to play!");

    return 0;
}

// Exit of the driver.
static void __exit procon_hid_driver_exit(void) {
    if (procon_proc_dir) {
        proc_remove(procon_proc_dir);
    }

    hid_unregister_driver(&(procon_hid_driver));
}

module_init(procon_hid_driver_init);
module_exit(procon_hid_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Johannes Goor");
MODULE_DESCRIPTION("Kernel driver for the Nintendo Switch Pro Controller.");