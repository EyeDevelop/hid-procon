#include <linux/hid.h>
#include <linux/input.h>
#include <linux/types.h>
#include <linux/kernel.h>

#include "procon-controller.h"
#include "procon-input.h"

int create_input_device(struct controller *c) {
    char *name;
    static const size_t name_len = 45;

    name = devm_kzalloc(&c->handler->dev, 45, GFP_KERNEL);
    if (name == NULL) {
        return -ENOMEM;
    }

    c->input = devm_input_allocate_device(&c->handler->dev);
    if (c->input == NULL) {
        return -ENOMEM;
    }

    switch (c->handler->product) {
        case 0x2006:
            snprintf(name, name_len, "Nintendo Switch Left JoyCon [controller %d]", c->controller_id);
            break;
        
        case 0x2007:
            snprintf(name, name_len, "Nintendo Switch Right JoyCon [controller %d]", c->controller_id);
            break;

        case 0x2009:
            snprintf(name, name_len, "Nintendo Switch ProCon [controller %d]", c->controller_id);
            break;

        default:
            snprintf(name, name_len, "Unknown ProCon device [controller %d]", c->controller_id);
            break;
    }

    // Setup basic information of the controller.
    c->input->id.bustype = c->handler->bus;
    c->input->id.product = c->handler->product;
    c->input->id.vendor = c->handler->vendor;
    c->input->id.version = c->handler->version;
    c->input->uniq = c->handler->uniq;
    c->input->name = name;

    // Setup controller capabilities.
    // D-Pad
    input_set_abs_params(c->input, ABS_HAT0X, -1, 1, 0, 0);
    input_set_abs_params(c->input, ABS_HAT0Y, -1, 1, 0, 0);

    // Buttons on the right
    input_set_capability(c->input, EV_KEY, BTN_NORTH);
    input_set_capability(c->input, EV_KEY, BTN_WEST);
    input_set_capability(c->input, EV_KEY, BTN_SOUTH);
    input_set_capability(c->input, EV_KEY, BTN_EAST);

    // Buttons in the middle.
    input_set_capability(c->input, EV_KEY, BTN_START);
    input_set_capability(c->input, EV_KEY, BTN_SELECT);
    input_set_capability(c->input, EV_KEY, BTN_MODE);

    // Triggers and bumpers.
    input_set_capability(c->input, EV_KEY, BTN_TR);
    input_set_capability(c->input, EV_KEY, BTN_TL);
    input_set_capability(c->input, EV_KEY, BTN_TR2);
    input_set_capability(c->input, EV_KEY, BTN_TL2);

    // Stick buttons.
    input_set_capability(c->input, EV_KEY, BTN_THUMBL);
    input_set_capability(c->input, EV_KEY, BTN_THUMBR);

    // Analog joysticks.
    input_set_abs_params(c->input, ABS_X, -PROCON_STICK_MAX, PROCON_STICK_MAX, PROCON_STICK_FUZZ, PROCON_STICK_FLAT);
    input_set_abs_params(c->input, ABS_Y, -PROCON_STICK_MAX, PROCON_STICK_MAX, PROCON_STICK_FUZZ, PROCON_STICK_FLAT);

    input_set_abs_params(c->input, ABS_RX, -PROCON_STICK_MAX, PROCON_STICK_MAX, PROCON_STICK_FUZZ, PROCON_STICK_FLAT);
    input_set_abs_params(c->input, ABS_RY, -PROCON_STICK_MAX, PROCON_STICK_MAX, PROCON_STICK_FUZZ, PROCON_STICK_FLAT);

    return input_register_device(c->input);
}