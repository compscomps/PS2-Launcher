#ifndef HIDPAD_H
#define HIDPAD_H

/*
 * Generic USB HID gamepads: the pad's own report descriptor says where its buttons, sticks and hat are in each
 * report, so any controller that describes itself as a plain HID gamepad can be read without a driver of its own.
 * hidpad_parse() reads the descriptor once; hidpad_translate() turns a report into the PS2 pad's report.
 */

#include <stdint.h>
#include "ds34common.h"

#define HIDPAD_MAX_BUTTONS 16
#define HIDPAD_MAX_DESC    256

enum {
    HIDPAD_LAYOUT_GENERIC = 0, /* buttons in the usual HID gamepad order: A, B, X, Y, LB, RB, Back, Start, LS, RS */
    HIDPAD_LAYOUT_PS2 = 1,     /* PS2-to-USB adapters: Triangle, Circle, Cross, Square, L2, R2, L1, R1, Select, Start, L3, R3 */
};

typedef struct
{
    uint16_t bit;     /* where the field starts, in bits, from the first byte after the report ID */
    uint8_t size;     /* in bits; 0 = the pad has no such field */
    int32_t min, max; /* logical range */
} hidpad_field;

typedef struct
{
    uint8_t valid;     /* it describes a gamepad: at least 4 buttons and 2 axes */
    uint8_t report_id; /* 0 = the reports have no ID byte */
    uint8_t nbuttons;
    hidpad_field button[HIDPAD_MAX_BUTTONS];
    hidpad_field x, y, z, rx, ry, rz, hat;
} hidpad_map;

/* returns 1 when the descriptor describes a gamepad (map->valid) */
int hidpad_parse(const uint8_t *desc, int len, hidpad_map *map);

/* report: what the pad sent, with its ID byte if it has one. Returns 0 when it is not a report of this pad. */
int hidpad_translate(const hidpad_map *map, const uint8_t *report, int len, int layout, struct ds2report *out);

#endif
