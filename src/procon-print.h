#include "packet.h"

#ifndef __PROCON_PRINT_H__
#define __PROCON_PRINT_H__

void print_response(struct input_response data, __u8 *raw_data, size_t len);

void print_byte_array(__u8 *data, size_t len);

#endif