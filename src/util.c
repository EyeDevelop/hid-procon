#include <linux/printk.h>
#include <linux/hid.h>
#include <linux/types.h>
#include <linux/string.h>

#include "util.h"

char *format_mac_addr(const __u8 *mac_data) {
    char *ret = kzalloc(18, GFP_KERNEL);
    if (ret == NULL) {
        pr_err("Cannot allocate memory for MAC address!\n");
        return NULL;
    }

    if (mac_data == NULL) {
        pr_err("MAC address not ready yet!\n");
        return NULL;
    }

    snprintf(ret, 18, "%02x:%02x:%02x:%02x:%02x:%02x", mac_data[0], mac_data[1], mac_data[2], mac_data[3], mac_data[4], mac_data[5]);
    return ret;
}