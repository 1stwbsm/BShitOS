/* PNG and baseline JPEG decoders. Pictures are scaled while decoding. */
#include "bs.h"
#include "render.h"

#define PIXEL_POOL (192 * 1024) /* 768 KB of pixels */
#define MAX_PICTURES 16
#define MAX_DISPLAY_W 600
#define MAX_DISPLAY_H 600
#define MAX_ROW_BYTES (16 * 1024)
#define WINDOW_SIZE 32768u

typedef unsigned char u8;

struct picture render_pictures[MAX_PICTURES];
int render_picture_count;

static unsigned int pixel_pool[PIXEL_POOL];
static int pixels_used;

/* Where the picture being decoded is going. */
static unsigned int *target;
static int source_w, source_h, target_w, target_h;

void render_images_reset(void)
{
    render_picture_count = 0;
    pixels_used = 0;
}

/* Chooses the on-screen size: two source pixels per screen unit by default. */
static int begin_picture(int width, int height, int wanted_w, int wanted_h)
{
    int w, h, need;

    if (width <= 0 || height <= 0 || render_picture_count == MAX_PICTURES)
        return -1;
    if (wanted_w > 0) {
        w = wanted_w / 2;
        h = wanted_h > 0 ? wanted_h / 2 : height * w / width;
    } else {
        w = width / 2;
        h = height / 2;
    }
    if (w < 1)
        w = 1;
    if (h < 1)
        h = 1;
    if (w > MAX_DISPLAY_W) {
        h = h * MAX_DISPLAY_W / w;
        w = MAX_DISPLAY_W;
    }
    if (h > MAX_DISPLAY_H) {
        w = w * MAX_DISPLAY_H / h;
        h = MAX_DISPLAY_H;
    }
    if (w < 1)
        w = 1;
    if (h < 1)
        h = 1;

    need = w * h;
    if (pixels_used + need > PIXEL_POOL)
        return -1;
    target = pixel_pool + pixels_used;
    source_w = width;
    source_h = height;
    target_w = w;
    target_h = h;
    return 0;
}

static int finish_picture(void)
{
    struct picture *p = &render_pictures[render_picture_count];

    p->pixels = target;
    p->width = target_w;
    p->height = target_h;
    pixels_used += target_w * target_h;
    return render_picture_count++;
}

/* ---- Inflate (RFC 1951) ---------------------------------------------------- */

struct huffman {
    short count[16], symbol[288];
};

static const u8 *in;
static int in_length, in_position, bit_buffer, bit_count;
static u8 window[WINDOW_SIZE];
static unsigned int window_position;
static void (*sink)(u8 value);
static int inflate_failed;

static int need_bits(int n)
{
    int value = bit_buffer;

    while (bit_count < n) {
        if (in_position >= in_length) {
            inflate_failed = 1;
            return 0;
        }
        value |= (int)in[in_position++] << bit_count;
        bit_count += 8;
    }
    bit_buffer = value >> n;
    bit_count -= n;
    return value & ((1 << n) - 1);
}

static void emit_byte(u8 value)
{
    window[window_position++ & (WINDOW_SIZE - 1)] = value;
    sink(value);
}

static void build(struct huffman *h, const u8 *lengths, int n)
{
    short offsets[16];

    memset(h->count, 0, sizeof(h->count));
    for (int i = 0; i < n; i++)
        h->count[lengths[i]]++;
    h->count[0] = 0;
    offsets[1] = 0;
    for (int i = 1; i < 15; i++)
        offsets[i + 1] = (short)(offsets[i] + h->count[i]);
    for (int i = 0; i < n; i++)
        if (lengths[i])
            h->symbol[offsets[lengths[i]]++] = (short)i;
}

static int decode(const struct huffman *h)
{
    int code = 0, first = 0, index = 0;

    for (int length = 1; length <= 15; length++) {
        int count;

        code |= need_bits(1);
        if (inflate_failed)
            return -1;
        count = h->count[length];
        if (code - count < first)
            return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static int inflate_codes(const struct huffman *literals, const struct huffman *distances)
{
    static const short base_length[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const short extra_length[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const short base_distance[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const short extra_distance[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

    for (;;) {
        int symbol = decode(literals);

        if (symbol < 0)
            return -1;
        if (symbol < 256) {
            emit_byte((u8)symbol);
        } else if (symbol == 256) {
            return 0;
        } else {
            int length, distance;

            symbol -= 257;
            if (symbol >= 29)
                return -1;
            length = base_length[symbol] + need_bits(extra_length[symbol]);
            symbol = decode(distances);
            if (symbol < 0 || symbol >= 30)
                return -1;
            distance = base_distance[symbol] + need_bits(extra_distance[symbol]);
            if (inflate_failed)
                return -1;
            while (length--)
                emit_byte(window[(window_position - (unsigned int)distance) & (WINDOW_SIZE - 1)]);
        }
    }
}

static int inflate_stream(const u8 *data, int length, void (*output)(u8))
{
    static struct huffman literals, distances;
    int last;

    in = data;
    in_length = length;
    in_position = bit_buffer = bit_count = 0;
    window_position = 0;
    sink = output;
    inflate_failed = 0;

    do {
        int type;

        last = need_bits(1);
        type = need_bits(2);
        if (inflate_failed)
            return -1;

        if (type == 0) {
            int size;

            bit_buffer = bit_count = 0;
            if (in_position + 4 > in_length)
                return -1;
            size = in[in_position] | in[in_position + 1] << 8;
            in_position += 4;
            if (in_position + size > in_length)
                return -1;
            while (size--)
                emit_byte(in[in_position++]);
        } else if (type == 1) {
            u8 lengths[288 + 32];
            int i = 0;

            for (; i < 144; i++) lengths[i] = 8;
            for (; i < 256; i++) lengths[i] = 9;
            for (; i < 280; i++) lengths[i] = 7;
            for (; i < 288; i++) lengths[i] = 8;
            build(&literals, lengths, 288);
            memset(lengths, 5, 30);
            build(&distances, lengths, 30);
            if (inflate_codes(&literals, &distances) < 0)
                return -1;
        } else if (type == 2) {
            static const u8 order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
            u8 lengths[320];
            int literal_count = need_bits(5) + 257, distance_count = need_bits(5) + 1, code_count = need_bits(4) + 4;
            int index = 0;

            if (inflate_failed || literal_count > 286 || distance_count > 30)
                return -1;
            memset(lengths, 0, sizeof(lengths));
            for (int i = 0; i < code_count; i++)
                lengths[order[i]] = (u8)need_bits(3);
            build(&literals, lengths, 19);

            while (index < literal_count + distance_count) {
                int symbol = decode(&literals);

                if (symbol < 0)
                    return -1;
                if (symbol < 16) {
                    lengths[index++] = (u8)symbol;
                } else {
                    int previous = 0, repeat;

                    if (symbol == 16) {
                        if (index == 0)
                            return -1;
                        previous = lengths[index - 1];
                        repeat = 3 + need_bits(2);
                    } else if (symbol == 17) {
                        repeat = 3 + need_bits(3);
                    } else {
                        repeat = 11 + need_bits(7);
                    }
                    if (index + repeat > literal_count + distance_count)
                        return -1;
                    while (repeat--)
                        lengths[index++] = (u8)previous;
                }
            }
            {
                static u8 saved[320];

                memcpy(saved, lengths, sizeof(saved));
                build(&literals, saved, literal_count);
                build(&distances, saved + literal_count, distance_count);
            }
            if (inflate_codes(&literals, &distances) < 0)
                return -1;
        } else {
            return -1;
        }
    } while (!last);
    return 0;
}

/* ---- PNG ---------------------------------------------------------------------- */

static int png_type, png_depth, png_channels, png_stride, png_bytes_per_pixel;
static int png_row, png_column;
static u8 png_filter, png_have_filter;
static u8 png_current[MAX_ROW_BYTES], png_previous[MAX_ROW_BYTES];
static u8 png_palette[256][3], png_palette_alpha[256];
static int png_last_target_row;

static int blend(int value, int alpha, int shift)
{
    return (value * alpha + (int)((render_background >> shift) & 255) * (255 - alpha)) / 255;
}

static int paeth(int a, int b, int c)
{
    int p = a + b - c;
    int pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;

    if (pa <= pb && pa <= pc)
        return a;
    return pb <= pc ? b : c;
}

static unsigned int png_pixel(int x)
{
    int r, g, b, a = 255;

    if (png_type == 3) {
        int index;

        if (png_depth == 8) {
            index = png_current[x];
        } else {
            int bits = x * png_depth;

            index = (png_current[bits >> 3] >> (8 - png_depth - (bits & 7))) & ((1 << png_depth) - 1);
        }
        r = png_palette[index][0];
        g = png_palette[index][1];
        b = png_palette[index][2];
        a = png_palette_alpha[index];
    } else if (png_type == 0 || png_type == 4) {
        if (png_depth < 8) {
            int bits = x * png_depth, max = (1 << png_depth) - 1;

            r = g = b = ((png_current[bits >> 3] >> (8 - png_depth - (bits & 7))) & max) * 255 / max;
        } else {
            r = g = b = png_current[x * png_bytes_per_pixel];
            if (png_type == 4)
                a = png_current[x * png_bytes_per_pixel + png_bytes_per_pixel / 2];
        }
    } else {
        int unit = png_depth / 8;

        r = png_current[x * png_bytes_per_pixel];
        g = png_current[x * png_bytes_per_pixel + unit];
        b = png_current[x * png_bytes_per_pixel + 2 * unit];
        if (png_type == 6)
            a = png_current[x * png_bytes_per_pixel + 3 * unit];
    }
    if (a != 255) {
        r = blend(r, a, 16);
        g = blend(g, a, 8);
        b = blend(b, a, 0);
    }
    /* Zero means "transparent" to nothing here, but keep real black visible. */
    return (unsigned int)r << 16 | (unsigned int)g << 8 | (unsigned int)b;
}

static void png_finish_row(void)
{
    int row_target = png_row * target_h / source_h;

    for (int i = 0; i < png_stride; i++) {
        int a = i >= png_bytes_per_pixel ? png_current[i - png_bytes_per_pixel] : 0;
        int b = png_previous[i];
        int c = i >= png_bytes_per_pixel ? png_previous[i - png_bytes_per_pixel] : 0;
        int add = 0;

        switch (png_filter) {
        case 1: add = a; break;
        case 2: add = b; break;
        case 3: add = (a + b) / 2; break;
        case 4: add = paeth(a, b, c); break;
        default: break;
        }
        png_current[i] = (u8)(png_current[i] + add);
    }

    if (row_target != png_last_target_row && row_target < target_h) {
        for (int x = 0; x < target_w; x++)
            target[row_target * target_w + x] = png_pixel(x * source_w / target_w);
        png_last_target_row = row_target;
    }

    memcpy(png_previous, png_current, (unsigned int)png_stride);
    png_row++;
    png_column = 0;
    png_have_filter = 0;
}

static void png_byte(u8 value)
{
    if (png_row >= source_h)
        return;
    if (!png_have_filter) {
        png_filter = value;
        png_have_filter = 1;
        return;
    }
    png_current[png_column++] = value;
    if (png_column == png_stride)
        png_finish_row();
}

static unsigned int be32(const u8 *p)
{
    return (unsigned int)p[0] << 24 | (unsigned int)p[1] << 16 | (unsigned int)p[2] << 8 | p[3];
}

static int decode_png(u8 *data, int length, int wanted_w, int wanted_h)
{
    int position = 8, idat_end = 0, width = 0, height = 0, began = 0;
    u8 *idat = 0;

    memset(png_palette_alpha, 255, sizeof(png_palette_alpha));

    while (position + 12 <= length) {
        unsigned int chunk = be32(data + position);
        const u8 *type = data + position + 4;
        u8 *body = data + position + 8;

        if (chunk > (unsigned int)(length - position - 12))
            return -1;

        if (memcmp_bytes(type, "IHDR", 4) == 0) {
            width = (int)be32(body);
            height = (int)be32(body + 4);
            png_depth = body[8];
            png_type = body[9];
            if (body[12] != 0 || png_depth == 16)
                return -1; /* interlaced or 16-bit */
            png_channels = png_type == 2 ? 3 : png_type == 4 ? 2 : png_type == 6 ? 4 : 1;
            png_bytes_per_pixel = png_channels * png_depth / 8;
            if (png_bytes_per_pixel < 1)
                png_bytes_per_pixel = 1;
            png_stride = (width * png_channels * png_depth + 7) / 8;
            if (png_stride > MAX_ROW_BYTES)
                return -1;
            if (begin_picture(width, height, wanted_w, wanted_h) < 0)
                return -1;
            began = 1;
        } else if (memcmp_bytes(type, "PLTE", 4) == 0) {
            for (unsigned int i = 0; i * 3 + 2 < chunk && i < 256; i++) {
                png_palette[i][0] = body[i * 3];
                png_palette[i][1] = body[i * 3 + 1];
                png_palette[i][2] = body[i * 3 + 2];
            }
        } else if (memcmp_bytes(type, "tRNS", 4) == 0 && png_type == 3) {
            for (unsigned int i = 0; i < chunk && i < 256; i++)
                png_palette_alpha[i] = body[i];
        } else if (memcmp_bytes(type, "IDAT", 4) == 0) {
            /* Close the gaps so the compressed data is one run. */
            if (!idat)
                idat = body;
            for (unsigned int i = 0; i < chunk; i++)
                idat[idat_end + (int)i] = body[i];
            idat_end += (int)chunk;
        } else if (memcmp_bytes(type, "IEND", 4) == 0) {
            break;
        }
        position += 12 + (int)chunk;
    }

    if (!began || !idat || idat_end < 2)
        return -1;

    png_row = png_column = 0;
    png_have_filter = 0;
    png_last_target_row = -1;
    memset(png_previous, 0, sizeof(png_previous));
    memset(target, 0xFF, (unsigned int)(target_w * target_h) * 4);
    inflate_stream(idat + 2, idat_end - 2, png_byte); /* a truncated image still shows */
    return finish_picture();
}

/* ---- JPEG (baseline) ------------------------------------------------------------ */

#define JPEG_COMPONENTS 3

struct jpeg_table {
    u8 count[17];
    u8 symbols[256];
    int min_code[17], max_code[17], index[17];
};

static struct jpeg_table dc_tables[4], ac_tables[4];
static unsigned short quant[4][64];
static struct {
    int id, h, v, q, dc, ac, predictor;
} jc[JPEG_COMPONENTS];
static int jpeg_component_count, jpeg_h_max, jpeg_v_max, jpeg_restart;
static const u8 *jp;
static int jp_left;
static unsigned int jbits;
static int jbit_count;
static int jpeg_failed;

static const u8 zigzag[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

static int jpeg_bit(void)
{
    if (jbit_count == 0) {
        int byte;

        if (jp_left <= 0) {
            jpeg_failed = 1;
            return 0;
        }
        byte = *jp++;
        jp_left--;
        if (byte == 0xFF) {
            if (jp_left > 0 && *jp == 0) {
                jp++;
                jp_left--;
            } else {
                jpeg_failed = 1; /* a marker where data should be */
                return 0;
            }
        }
        jbits = (unsigned int)byte;
        jbit_count = 8;
    }
    jbit_count--;
    return (int)(jbits >> jbit_count) & 1;
}

static int jpeg_receive(int n)
{
    int value = 0;

    while (n--)
        value = value << 1 | jpeg_bit();
    return value;
}

static int jpeg_extend(int value, int bits)
{
    return bits && value < (1 << (bits - 1)) ? value - (1 << bits) + 1 : value;
}

static int jpeg_decode_symbol(const struct jpeg_table *t)
{
    int code = 0;

    for (int length = 1; length <= 16; length++) {
        code = code << 1 | jpeg_bit();
        if (jpeg_failed)
            return -1;
        if (t->max_code[length] >= 0 && code <= t->max_code[length] && code >= t->min_code[length])
            return t->symbols[t->index[length] + code - t->min_code[length]];
    }
    jpeg_failed = 1;
    return -1;
}

static void jpeg_build_table(struct jpeg_table *t)
{
    int code = 0, k = 0;

    for (int length = 1; length <= 16; length++) {
        if (t->count[length]) {
            t->index[length] = k;
            t->min_code[length] = code;
            code += t->count[length];
            k += t->count[length];
            t->max_code[length] = code - 1;
        } else {
            t->max_code[length] = -1;
        }
        code <<= 1;
    }
}

/* cos(k*pi/16) in 1/1024ths. */
static int cosine(int k)
{
    static const short quarter[9] = {1024, 1004, 946, 851, 724, 569, 392, 200, 0};

    k %= 32;
    if (k > 16)
        k = 32 - k;
    return k <= 8 ? quarter[k] : -quarter[16 - k];
}

static void jpeg_idct(const int *in_block, u8 *out)
{
    int middle[64];

    for (int x = 0; x < 8; x++) {
        for (int y = 0; y < 8; y++) {
            int sum = 0;

            for (int u = 0; u < 8; u++) {
                int c = u == 0 ? 724 : 1024;

                sum += (in_block[u * 8 + x] * c >> 10) * cosine((2 * y + 1) * u) >> 10;
            }
            middle[y * 8 + x] = sum;
        }
    }
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            int sum = 0;

            for (int u = 0; u < 8; u++) {
                int c = u == 0 ? 724 : 1024;

                sum += (middle[y * 8 + u] * c >> 10) * cosine((2 * x + 1) * u) >> 10;
            }
            sum = (sum >> 2) + 128;
            out[y * 8 + x] = (u8)(sum < 0 ? 0 : sum > 255 ? 255 : sum);
        }
    }
}

static int jpeg_block(int c, u8 *out)
{
    int block[64];
    int size, value, k = 1;
    const unsigned short *q = quant[jc[c].q];

    memset(block, 0, sizeof(block));
    size = jpeg_decode_symbol(&dc_tables[jc[c].dc]);
    if (size < 0)
        return -1;
    value = jpeg_extend(jpeg_receive(size), size);
    jc[c].predictor += value;
    block[0] = jc[c].predictor * q[0];

    while (k < 64) {
        int symbol = jpeg_decode_symbol(&ac_tables[jc[c].ac]);
        int run, bits;

        if (symbol < 0)
            return -1;
        run = symbol >> 4;
        bits = symbol & 15;
        if (bits == 0) {
            if (run != 15)
                break;
            k += 16;
            continue;
        }
        k += run;
        if (k > 63)
            return -1;
        block[zigzag[k]] = jpeg_extend(jpeg_receive(bits), bits) * q[k];
        k++;
    }
    if (jpeg_failed)
        return -1;
    jpeg_idct(block, out);
    return 0;
}

static u8 clamp8(int v)
{
    return (u8)(v < 0 ? 0 : v > 255 ? 255 : v);
}

static int jpeg_scan(void)
{
    static u8 planes[JPEG_COMPONENTS][4 * 4 * 64];
    int mcu_w = 8 * jpeg_h_max, mcu_h = 8 * jpeg_v_max;
    int mcu_x_count = (source_w + mcu_w - 1) / mcu_w, mcu_y_count = (source_h + mcu_h - 1) / mcu_h;
    int restart_count = jpeg_restart;

    for (int mcu_y = 0; mcu_y < mcu_y_count; mcu_y++) {
        for (int mcu_x = 0; mcu_x < mcu_x_count; mcu_x++) {
            if (jpeg_restart && restart_count == 0) {
                jbit_count = 0;
                while (jp_left >= 2 && !(jp[0] == 0xFF && jp[1] >= 0xD0 && jp[1] <= 0xD7)) {
                    jp++;
                    jp_left--;
                }
                if (jp_left >= 2) {
                    jp += 2;
                    jp_left -= 2;
                }
                for (int c = 0; c < jpeg_component_count; c++)
                    jc[c].predictor = 0;
                restart_count = jpeg_restart;
            }

            for (int c = 0; c < jpeg_component_count; c++) {
                for (int by = 0; by < jc[c].v; by++) {
                    for (int bx = 0; bx < jc[c].h; bx++) {
                        u8 block[64];

                        if (jpeg_block(c, block) < 0)
                            return 0; /* keep what was decoded */
                        for (int y = 0; y < 8; y++)
                            memcpy(planes[c] + (by * 8 + y) * 32 + bx * 8, block + y * 8, 8);
                    }
                }
            }
            restart_count--;

            for (int py = 0; py < mcu_h; py++) {
                int sy = mcu_y * mcu_h + py, ty;

                if (sy >= source_h)
                    break;
                ty = sy * target_h / source_h;
                if ((ty + 1) * source_h / target_h - 1 != sy && ty * source_h / target_h != sy)
                    continue;
                for (int px = 0; px < mcu_w; px++) {
                    int sx = mcu_x * mcu_w + px, tx, values[JPEG_COMPONENTS];

                    if (sx >= source_w)
                        break;
                    tx = sx * target_w / source_w;
                    if (tx * source_w / target_w != sx)
                        continue;
                    for (int c = 0; c < jpeg_component_count; c++) {
                        int cy = py * jc[c].v / jpeg_v_max, cx = px * jc[c].h / jpeg_h_max;

                        values[c] = planes[c][cy * 32 + cx];
                    }
                    if (jpeg_component_count == 1) {
                        target[ty * target_w + tx] = (unsigned int)values[0] * 0x010101u;
                    } else {
                        int y = values[0], cb = values[1] - 128, cr = values[2] - 128;
                        int r = y + (359 * cr >> 8), g = y - ((88 * cb + 183 * cr) >> 8), b = y + (454 * cb >> 8);

                        target[ty * target_w + tx] = (unsigned int)clamp8(r) << 16 | (unsigned int)clamp8(g) << 8 | clamp8(b);
                    }
                }
            }
        }
    }
    return 0;
}

static int decode_jpeg(const u8 *data, int length, int wanted_w, int wanted_h)
{
    int position = 2, began = 0;

    jpeg_restart = 0;
    jpeg_component_count = 0;

    while (position + 4 <= length) {
        int marker, size;
        const u8 *body;

        if (data[position] != 0xFF)
            return -1;
        marker = data[position + 1];
        if (marker == 0xFF) {
            position++;
            continue;
        }
        if (marker == 0xD9)
            break;
        size = data[position + 2] << 8 | data[position + 3];
        body = data + position + 4;
        if (size < 2 || position + 2 + size > length)
            return -1;

        if (marker == 0xDB) {
            const u8 *p = body, *end = body + size - 2;

            while (p < end) {
                int precision = *p >> 4, id = *p & 3;

                p++;
                for (int i = 0; i < 64; i++) {
                    quant[id][i] = precision ? (unsigned short)(p[0] << 8 | p[1]) : *p;
                    p += precision ? 2 : 1;
                }
            }
        } else if (marker == 0xC4) {
            const u8 *p = body, *end = body + size - 2;

            while (p < end) {
                int cls = *p >> 4, id = *p & 3, total = 0;
                struct jpeg_table *t = cls ? &ac_tables[id] : &dc_tables[id];

                p++;
                t->count[0] = 0;
                for (int i = 1; i <= 16; i++) {
                    t->count[i] = *p++;
                    total += t->count[i];
                }
                if (total > 256 || p + total > end)
                    return -1;
                memcpy(t->symbols, p, (unsigned int)total);
                p += total;
                jpeg_build_table(t);
            }
        } else if (marker == 0xC0 || marker == 0xC1) {
            int height = body[1] << 8 | body[2], width = body[3] << 8 | body[4];

            if (body[0] != 8 || body[5] > JPEG_COMPONENTS || body[5] == 2)
                return -1;
            jpeg_component_count = body[5];
            jpeg_h_max = jpeg_v_max = 1;
            for (int i = 0; i < jpeg_component_count; i++) {
                jc[i].id = body[6 + i * 3];
                jc[i].h = body[7 + i * 3] >> 4;
                jc[i].v = body[7 + i * 3] & 15;
                jc[i].q = body[8 + i * 3] & 3;
                if (jc[i].h < 1 || jc[i].h > 4 || jc[i].v < 1 || jc[i].v > 4)
                    return -1;
                if (jc[i].h > jpeg_h_max)
                    jpeg_h_max = jc[i].h;
                if (jc[i].v > jpeg_v_max)
                    jpeg_v_max = jc[i].v;
            }
            if (begin_picture(width, height, wanted_w, wanted_h) < 0)
                return -1;
            began = 1;
        } else if (marker >= 0xC2 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
            return -1; /* progressive and other unsupported kinds */
        } else if (marker == 0xDD) {
            jpeg_restart = body[0] << 8 | body[1];
        } else if (marker == 0xDA) {
            int count = body[0];

            if (!began || count != jpeg_component_count)
                return -1;
            for (int i = 0; i < count; i++) {
                for (int c = 0; c < jpeg_component_count; c++) {
                    if (jc[c].id == body[1 + i * 2]) {
                        jc[c].dc = body[2 + i * 2] >> 4 & 3;
                        jc[c].ac = body[2 + i * 2] & 3;
                        jc[c].predictor = 0;
                    }
                }
            }
            jp = body + size - 2;
            jp_left = length - (position + 2 + size);
            jbit_count = 0;
            jpeg_failed = 0;
            memset(target, 0xFF, (unsigned int)(target_w * target_h) * 4);
            jpeg_scan();
            return finish_picture();
        }
        position += 2 + size;
    }
    return -1;
}

int render_decode_image(const unsigned char *data, int length, int wanted_width, int wanted_height)
{
    static const u8 png_signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

    if (length > 8 && memcmp_bytes(data, png_signature, 8) == 0)
        return decode_png((u8 *)data, length, wanted_width, wanted_height);
    if (length > 4 && data[0] == 0xFF && data[1] == 0xD8)
        return decode_jpeg(data, length, wanted_width, wanted_height);
    return -1;
}
