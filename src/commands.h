#ifndef __PROCON_COMMANDS_H__
#define __PROCON_COMMANDS_H__

#include <linux/types.h>

// Commands.
const static __u8 PROCON_CMD_COMMAND_AND_RUMBLE = 0x01;
const static __u8 PROCON_CMD_RUMBLE = 0x10;

// Subcommands.
const static __u8 PROCON_SUB_GET_CONTROLLER_STATE = 0x00;
const static __u8 PROCON_SUB_REQUEST_INFO = 0x02;
const static __u8 PROCON_SUB_SET_REPORT_MODE = 0x03;
const static __u8 PROCON_SUB_GET_TRIGGER_TIME = 0x04;
const static __u8 PROCON_SUB_SET_POWER_STATE = 0x08;
const static __u8 PROCON_SUB_READ_SPI = 0x10;
const static __u8 PROCON_SUB_SET_LIGHT = 0x30;
const static __u8 PROCON_SUB_SET_IMU = 0x40;
const static __u8 PROCON_SUB_SET_VIBRATION = 0x48;

// Controller lights.
const static __u8 PROCON_LED_FLASH_1 = 0b00010000;
const static __u8 PROCON_LED_FLASH_2 = 0b00110000;
const static __u8 PROCON_LED_FLASH_3 = 0b01110000;
const static __u8 PROCON_LED_FLASH_4 = 0b11110000;

const static __u8 PROCON_LED_ON_1 = 0b00000001;
const static __u8 PROCON_LED_ON_2 = 0b00000011;
const static __u8 PROCON_LED_ON_3 = 0b00000111;
const static __u8 PROCON_LED_ON_4 = 0b00001111;
const static __u8 PROCON_LED_OFF = 0b0;

#endif