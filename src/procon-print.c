#include <linux/printk.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include "packet.h"
#include "procon-print.h"

void print_response(struct input_response data, __u8 *raw_data, size_t len) {
    if (data.subcommand_ack >> 7) {
        pr_info("Subcommand response for id: %02x.\n", data.subcommand_id);
        print_byte_array(data.subcommand_reply, sizeof(data.subcommand_reply));
    }

    else if (data.report_id != 0x30 && data.report_id != 0x3F && data.report_id != 0x21) {
        pr_info("Received:\n");
        print_byte_array(raw_data, len);
    }
}

void print_byte_array(__u8 *data, size_t len) {
    size_t current_byte = 0;
    size_t index = 0;
    char buf[31];

    buf[30] = '\x00';

    while (current_byte < len) {
        snprintf(buf + index, 31, "%02x ", data[current_byte++]);
        index += 3;

        if (index >= 30 || current_byte >= len) {
            pr_info("%s\n", buf);
            index = 0;
        }
    }
}