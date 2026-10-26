#include "hidpad.h"

#define PAGE_GENERIC_DESKTOP 0x01
#define PAGE_BUTTON          0x09

static void clear_bytes(void *p, int n)
{
    uint8_t *b = p;
    while (n-- > 0)
        *b++ = 0;
}

static void set_field(hidpad_field *f, uint32_t bit, uint32_t size, int32_t min, int32_t max)
{
    if (f->size != 0)
        return; /* the first one wins */
    f->bit = (uint16_t)bit;
    f->size = (uint8_t)size;
    f->min = min;
    f->max = max;
}

int hidpad_parse(const uint8_t *d, int len, hidpad_map *map)
{
    uint32_t usage_page = 0, rsize = 0, rcount = 0, offset = 0;
    int32_t lmin = 0, lmax = 0;
    int cur_id = 0, sel_id = -1;
    uint32_t usages[32], pages[32], umin = 0, umax = 0;
    int nusages = 0, have_range = 0;
    int i = 0;

    clear_bytes(map, sizeof(*map));

    while (i < len) {
        uint8_t b = d[i];
        int size, type, tag, k;
        uint32_t uv = 0;
        int32_t sv;

        if (b == 0xFE) { /* long item: skipped */
            if (i + 1 >= len)
                break;
            i += 3 + d[i + 1];
            continue;
        }

        size = b & 3;
        if (size == 3)
            size = 4;
        type = (b >> 2) & 3;
        tag = b >> 4;
        if (i + 1 + size > len)
            break;
        for (k = 0; k < size; k++)
            uv |= (uint32_t)d[i + 1 + k] << (8 * k);
        sv = (int32_t)uv;
        if (size > 0 && size < 4 && (uv & (1u << (8 * size - 1))))
            sv = (int32_t)(uv | ~((1u << (8 * size)) - 1));
        i += 1 + size;

        if (type == 1) { /* global */
            switch (tag) {
                case 0:
                    usage_page = uv;
                    break;
                case 1:
                    lmin = sv;
                    break;
                case 2:
                    lmax = (lmin >= 0 && sv < 0) ? (int32_t)uv : sv; /* 0xFF with a size of 1 byte means 255 */
                    break;
                case 7:
                    rsize = uv;
                    break;
                case 8:
                    cur_id = (int)uv;
                    break;
                case 9:
                    rcount = uv;
                    break;
            }
        } else if (type == 2) { /* local */
            switch (tag) {
                case 0:
                    if (nusages < 32) {
                        pages[nusages] = (size == 4) ? (uv >> 16) : usage_page;
                        usages[nusages++] = (size == 4) ? (uv & 0xFFFF) : uv;
                    }
                    break;
                case 1:
                    umin = uv;
                    have_range = 1;
                    break;
                case 2:
                    umax = uv;
                    have_range = 1;
                    break;
            }
        } else if (type == 0) { /* main */
            if (tag == 8) {     /* input */
                uint32_t n;

                if (rsize > 0 && rsize <= 16 && rcount > 0) {
                    if (sel_id < 0 && !(uv & 1))
                        sel_id = cur_id;
                    if (sel_id < 0 || cur_id == sel_id) {
                        for (n = 0; n < rcount; n++) {
                            uint32_t page = usage_page, usage = 0;
                            int known = 0;

                            if (nusages > 0) {
                                int u = (n < (uint32_t)nusages) ? (int)n : nusages - 1;
                                usage = usages[u];
                                page = pages[u];
                                known = 1;
                            } else if (have_range && umin + n <= umax) {
                                usage = umin + n;
                                known = 1;
                            }

                            if (!(uv & 1) && (uv & 2) && known) { /* data, variable */
                                if (page == PAGE_BUTTON && usage >= 1 && usage <= HIDPAD_MAX_BUTTONS) {
                                    set_field(&map->button[usage - 1], offset, rsize, lmin, lmax);
                                    if (usage > map->nbuttons)
                                        map->nbuttons = (uint8_t)usage;
                                } else if (page == PAGE_GENERIC_DESKTOP) {
                                    switch (usage) {
                                        case 0x30: set_field(&map->x, offset, rsize, lmin, lmax); break;
                                        case 0x31: set_field(&map->y, offset, rsize, lmin, lmax); break;
                                        case 0x32: set_field(&map->z, offset, rsize, lmin, lmax); break;
                                        case 0x33: set_field(&map->rx, offset, rsize, lmin, lmax); break;
                                        case 0x34: set_field(&map->ry, offset, rsize, lmin, lmax); break;
                                        case 0x35: set_field(&map->rz, offset, rsize, lmin, lmax); break;
                                        case 0x39: set_field(&map->hat, offset, rsize, lmin, lmax); break;
                                    }
                                }
                            }
                            offset += rsize;
                        }
                    }
                } else if (rsize > 16 && rcount > 0 && (sel_id < 0 || cur_id == sel_id)) {
                    offset += rsize * rcount; /* wide field: not used, but it takes room */
                }
            }
            /* every main item ends the local items */
            nusages = 0;
            have_range = 0;
            umin = umax = 0;
        }
    }

    if (sel_id > 0)
        map->report_id = (uint8_t)sel_id;
    map->report_bits = (uint16_t)offset;
    map->valid = (map->nbuttons >= 4 && ((map->x.size && map->y.size) || map->hat.size)) ? 1 : 0;
    return map->valid;
}

static uint32_t get_bits(const uint8_t *d, int len, uint32_t bit, uint32_t size)
{
    uint32_t v = 0, i;

    for (i = 0; i < size; i++) {
        uint32_t p = bit + i;
        if ((int)(p >> 3) >= len)
            return 0;
        if (d[p >> 3] & (1u << (p & 7)))
            v |= 1u << i;
    }
    return v;
}

static int32_t get_value(const uint8_t *d, int len, const hidpad_field *f)
{
    uint32_t u = get_bits(d, len, f->bit, f->size);

    if (f->min < 0 && (u & (1u << (f->size - 1))))
        return (int32_t)(u | ~((1u << f->size) - 1));
    return (int32_t)u;
}

static int pressed(const hidpad_map *m, const uint8_t *d, int len, int n)
{
    return n < HIDPAD_MAX_BUTTONS && m->button[n].size != 0 && get_bits(d, len, m->button[n].bit, 1) != 0;
}

/* 0..255, 128 = centre */
static uint8_t axis(const uint8_t *d, int len, const hidpad_field *f)
{
    int32_t v, range, n;

    if (f->size == 0)
        return 0x7F;
    range = f->max - f->min;
    if (range <= 0)
        return 0x7F;
    v = get_value(d, len, f);
    n = ((v - f->min) * 255) / range;
    if (n < 0)
        n = 0;
    if (n > 255)
        n = 255;
    if (n >= 120 && n <= 135) /* the dead zone of a worn stick */
        n = 0x7F;
    return (uint8_t)n;
}

/* -1 = not pressed, else 0 = up, 1 = up-right ... 7 = up-left */
static int hat(const uint8_t *d, int len, const hidpad_field *f)
{
    int32_t v;

    if (f->size == 0)
        return -1;
    v = get_value(d, len, f);
    if (v < f->min || v > f->max)
        return -1;
    v -= f->min;
    if (f->max - f->min == 7)
        return (int)v;
    if (f->max - f->min == 3)
        return (int)(v * 2);
    return -1;
}

int hidpad_translate(const hidpad_map *m, const uint8_t *report, int len, int layout, struct ds2report *out)
{
    static const uint16_t ps2_order[12] = {DS2ButtonTriangle, DS2ButtonCircle, DS2ButtonCross, DS2ButtonSquare,
                                           DS2ButtonL2, DS2ButtonR2, DS2ButtonL1, DS2ButtonR1,
                                           DS2ButtonSelect, DS2ButtonStart, DS2ButtonL3, DS2ButtonR3};
    static const uint16_t generic_order[12] = {DS2ButtonCross, DS2ButtonCircle, DS2ButtonSquare, DS2ButtonTriangle,
                                               DS2ButtonL1, DS2ButtonR1, DS2ButtonSelect, DS2ButtonStart,
                                               DS2ButtonL3, DS2ButtonR3, DS2ButtonL2, DS2ButtonR2};
    static const uint8_t hat_dirs[8] = {
        (DS2ButtonUp) >> 4, (DS2ButtonUp | DS2ButtonRight) >> 4, (DS2ButtonRight) >> 4, (DS2ButtonDown | DS2ButtonRight) >> 4,
        (DS2ButtonDown) >> 4, (DS2ButtonDown | DS2ButtonLeft) >> 4, (DS2ButtonLeft) >> 4, (DS2ButtonUp | DS2ButtonLeft) >> 4};
    const uint16_t *order = layout == HIDPAD_LAYOUT_PS2 ? ps2_order : generic_order;
    const uint8_t *d = report;
    uint16_t b = 0;
    int n, h;
    const hidpad_field *rx, *ry;

    if (!m->valid)
        return 0;
    if (m->report_id != 0) {
        if (len < 1 || report[0] != m->report_id)
            return 0;
        d = report + 1;
        len -= 1;
    }

    for (n = 0; n < 12; n++) {
        if (pressed(m, d, len, n))
            b |= order[n];
    }
    h = hat(d, len, &m->hat);
    if (h >= 0)
        b |= (uint16_t)hat_dirs[h] << 4;

    out->nButtonState = (uint16_t)~b;

    out->LeftStickX = axis(d, len, &m->x);
    out->LeftStickY = axis(d, len, &m->y);
    if (layout == HIDPAD_LAYOUT_PS2) { /* PS2 adapters: the right stick is Z / Rz, the other way round */
        rx = &m->rz;
        ry = &m->z;
    } else if (m->rx.size && m->ry.size) {
        rx = &m->rx;
        ry = &m->ry;
    } else {
        rx = &m->z;
        ry = &m->rz;
    }
    out->RightStickX = axis(d, len, rx);
    out->RightStickY = axis(d, len, ry);

    out->PressureRight = (b & DS2ButtonRight) ? 0xFF : 0;
    out->PressureLeft = (b & DS2ButtonLeft) ? 0xFF : 0;
    out->PressureUp = (b & DS2ButtonUp) ? 0xFF : 0;
    out->PressureDown = (b & DS2ButtonDown) ? 0xFF : 0;
    out->PressureTriangle = (b & DS2ButtonTriangle) ? 0xFF : 0;
    out->PressureCircle = (b & DS2ButtonCircle) ? 0xFF : 0;
    out->PressureCross = (b & DS2ButtonCross) ? 0xFF : 0;
    out->PressureSquare = (b & DS2ButtonSquare) ? 0xFF : 0;
    out->PressureL1 = (b & DS2ButtonL1) ? 0xFF : 0;
    out->PressureR1 = (b & DS2ButtonR1) ? 0xFF : 0;
    out->PressureL2 = (b & DS2ButtonL2) ? 0xFF : 0;
    out->PressureR2 = (b & DS2ButtonR2) ? 0xFF : 0;
    return 1;
}

/* Z, Rz, X, Y and one more byte, the hat in 4 bits, 12 buttons, 2 vendor bits: 9 bytes a report */
static const uint8_t ps2adapter_desc[] = {
    0x05, 0x01, 0x09, 0x04, 0xA1, 0x01, 0xA1, 0x02, 0x75, 0x08, 0x95, 0x05, 0x15, 0x00, 0x26, 0xFF, 0x00,
    0x35, 0x00, 0x46, 0xFF, 0x00, 0x09, 0x32, 0x09, 0x35, 0x09, 0x30, 0x09, 0x31, 0x09, 0x00, 0x81, 0x02,
    0x75, 0x04, 0x95, 0x01, 0x25, 0x07, 0x46, 0x3B, 0x01, 0x65, 0x14, 0x09, 0x39, 0x81, 0x42,
    0x65, 0x00, 0x75, 0x01, 0x95, 0x0C, 0x25, 0x01, 0x45, 0x01, 0x05, 0x09, 0x19, 0x01, 0x29, 0x0C, 0x81, 0x02,
    0x06, 0x00, 0xFF, 0x09, 0x01, 0x95, 0x02, 0x81, 0x02, 0xC0,
    0xA1, 0x02, 0x75, 0x08, 0x95, 0x07, 0x46, 0xFF, 0x00, 0x26, 0xFF, 0x00, 0x09, 0x02, 0x91, 0x02, 0xC0, 0xC0};

int hidpad_builtin_ps2adapter(hidpad_map *map)
{
    return hidpad_parse(ps2adapter_desc, (int)sizeof(ps2adapter_desc), map);
}

int hidpad_report_bytes(const hidpad_map *map)
{
    if (!map->valid || map->report_bits == 0)
        return 0;
    return (map->report_bits + 7) / 8 + (map->report_id ? 1 : 0);
}
