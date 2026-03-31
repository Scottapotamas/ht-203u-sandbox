#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/videodev2.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define CLEAR(x) memset(&(x), 0, sizeof(x))

enum {
    FULL_WIDTH = 256,
    FULL_HEIGHT = 392,
    BYTES_PER_LINE = FULL_WIDTH * 2,
    TOP_HALF_START = 0,
    TOP_HALF_ROWS = 196,
    ACTIVE_ROWS = 192,
    META_ROWS = 4,
    IMAGE_PIXELS = FULL_WIDTH * ACTIVE_ROWS,
    META_RAW_BYTES = META_ROWS * BYTES_PER_LINE,
};

struct buffer {
    void *start;
    size_t length;
};

enum input_mode {
    INPUT_NONE = 0,
    INPUT_PREFIX,
    INPUT_DATASET,
    INPUT_DEVICE,
    INPUT_MONITOR,
};

enum decode_model {
    MODEL_Y8_RANGE_W20_W00 = 0,
    MODEL_Y8_RANGE_W25_W00,
    MODEL_Y8_SIGNED_W01_SPAN_W00_W20,
    MODEL_U16LE_LINEAR_4096_DIV29P5,
    MODEL_U16LE_LINEAR_4096_DIV32,
    MODEL_U16LE_LINEAR_4096_DIV29P5_W20DELTA,
    MODEL_U16LE_LINEAR_4096_DIV29P5_R1COMP,
    MODEL_COUNT,
};

struct options {
    enum input_mode mode;
    const char *prefix;
    const char *dataset_dir;
    const char *device;
    int warmup_frames;
    bool verbose;
    bool output_header;
    bool render;
    const char *render_dir;
    enum decode_model model;
    double interval_s;
    int count;
    const char *output_path;
};

struct footer_fields {
    uint16_t r0_w00;
    uint16_t r0_w01;
    uint16_t r0_w03;
    uint16_t r0_w20;
    uint16_t r0_w25;
    uint16_t r0_w30;
    uint16_t r0_w35;
    uint32_t r1_marker;
    uint32_t r1_u32_04;
    uint32_t r1_u32_08;
    uint32_t r1_u32_0c;
    uint32_t r1_u32_10;
    uint32_t r1_u32_14;
    int meta_y_nonzero;
    int meta_raw_nonzero;
};

struct raster_stats_u8 {
    uint8_t min_v;
    uint8_t max_v;
    double mean;
    uint8_t center_v;
};

struct raster_stats_u16 {
    uint16_t min_v;
    uint16_t max_v;
    double mean;
    uint16_t center_v;
};

struct temp_stats {
    int32_t min_centi_c;
    int32_t max_centi_c;
    double mean_centi_c;
    int32_t center_centi_c;
};

struct capture_data {
    char name[512];
    uint8_t image_y8[IMAGE_PIXELS];
    uint8_t active_raw[IMAGE_PIXELS * 2];
    uint16_t image_u16le[IMAGE_PIXELS];
    uint8_t meta_y[META_ROWS * FULL_WIDTH];
    uint8_t meta_raw[META_RAW_BYTES];
    struct footer_fields footer;
    struct raster_stats_u8 raster_stats;
    struct raster_stats_u16 raster16_stats;
};

static const char *model_names[MODEL_COUNT] = {
    "y8_range_w20_w00",
    "y8_range_w25_w00",
    "y8_signed_w01_span_w00_w20",
    "u16le_linear_4096_div29p5",
    "u16le_linear_4096_div32",
    "u16le_linear_4096_div29p5_w20delta",
    "u16le_linear_4096_div29p5_r1comp",
};

static int xioctl(int fd, unsigned long request, void *arg) {
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

static double parse_double(const char *s, const char *what) {
    char *end = NULL;
    double v = strtod(s, &end);
    if (!s[0] || (end && *end)) {
        fprintf(stderr, "invalid %s: %s\n", what, s);
        exit(EXIT_FAILURE);
    }
    return v;
}

static void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static void die_msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(EXIT_FAILURE);
}

static int parse_int(const char *s, const char *what) {
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (!s[0] || (end && *end)) {
        fprintf(stderr, "invalid %s: %s\n", what, s);
        exit(EXIT_FAILURE);
    }
    if (v < -2147483647L || v > 2147483647L) {
        fprintf(stderr, "out of range %s: %s\n", what, s);
        exit(EXIT_FAILURE);
    }
    return (int)v;
}

static FILE *open_output_path(const char *path) {
    if (!path || !strcmp(path, "-")) return stdout;
    FILE *f = fopen(path, "w");
    if (!f) die("fopen output");
    return f;
}

static int64_t timespec_to_ns(const struct timespec *ts) {
    return (int64_t)ts->tv_sec * 1000000000LL + (int64_t)ts->tv_nsec;
}

static void format_iso8601(char *buf, size_t n, const struct timespec *ts) {
    struct tm tm;
    localtime_r(&ts->tv_sec, &tm);
    snprintf(buf, n, "%04d-%02d-%02dT%02d:%02d:%02d.%03ld",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec, ts->tv_nsec / 1000000L);
}

static uint16_t u16_le_at(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t u32_le_at(const uint8_t *p) {
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

static size_t read_file_exact(const char *path, uint8_t *dst, size_t expected_len) {
    FILE *f = fopen(path, "rb");
    if (!f) die(path);
    size_t n = fread(dst, 1, expected_len + 1, f);
    if (ferror(f)) die(path);
    fclose(f);
    if (n != expected_len) {
        fprintf(stderr, "unexpected size for %s: got %zu expected %zu\n", path, n, expected_len);
        exit(EXIT_FAILURE);
    }
    return n;
}

static void read_file_prefix_exact(const char *path, uint8_t *dst, size_t prefix_len, size_t total_len) {
    FILE *f = fopen(path, "rb");
    if (!f) die(path);
    size_t n = fread(dst, 1, prefix_len, f);
    if (n != prefix_len) {
        fprintf(stderr, "unexpected prefix size for %s: got %zu expected %zu\n", path, n, prefix_len);
        exit(EXIT_FAILURE);
    }
    if (fseek(f, 0, SEEK_END) != 0) die(path);
    long end = ftell(f);
    if (end < 0) die(path);
    fclose(f);
    if ((size_t)end != total_len) {
        fprintf(stderr, "unexpected size for %s: got %ld expected %zu\n", path, end, total_len);
        exit(EXIT_FAILURE);
    }
}

static bool has_suffix(const char *s, const char *suffix) {
    size_t n = strlen(s);
    size_t m = strlen(suffix);
    return n >= m && strcmp(s + n - m, suffix) == 0;
}

static void join_path(char *dst, size_t dst_len, const char *a, const char *b) {
    if (!a[0] || !strcmp(a, ".")) {
        snprintf(dst, dst_len, "%s", b);
        return;
    }
    size_t n = strlen(a);
    if (a[n - 1] == '/') {
        snprintf(dst, dst_len, "%s%s", a, b);
    } else {
        snprintf(dst, dst_len, "%s/%s", a, b);
    }
}

static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void footer_decode(struct footer_fields *ff, const uint8_t *meta_y, const uint8_t *meta_raw) {
    memset(ff, 0, sizeof(*ff));
    ff->r0_w00 = u16_le_at(meta_raw + 0x00);
    ff->r0_w01 = u16_le_at(meta_raw + 0x02);
    ff->r0_w03 = u16_le_at(meta_raw + 0x06);
    ff->r0_w20 = u16_le_at(meta_raw + 0x28);
    ff->r0_w25 = u16_le_at(meta_raw + 0x32);
    ff->r0_w30 = u16_le_at(meta_raw + 0x3C);
    ff->r0_w35 = u16_le_at(meta_raw + 0x46);
    ff->r1_marker = u32_le_at(meta_raw + 0x200);
    ff->r1_u32_04 = u32_le_at(meta_raw + 0x204);
    ff->r1_u32_08 = u32_le_at(meta_raw + 0x208);
    ff->r1_u32_0c = u32_le_at(meta_raw + 0x20C);
    ff->r1_u32_10 = u32_le_at(meta_raw + 0x210);
    ff->r1_u32_14 = u32_le_at(meta_raw + 0x214);

    for (int i = 0; i < META_ROWS * FULL_WIDTH; i++) {
        if (meta_y[i] != 0) ff->meta_y_nonzero++;
    }
    for (int i = 0; i < META_RAW_BYTES; i++) {
        if (meta_raw[i] != 0) ff->meta_raw_nonzero++;
    }
}

static void compute_raster_stats(const uint8_t *img, struct raster_stats_u8 *st) {
    uint64_t sum = 0;
    st->min_v = 255;
    st->max_v = 0;
    for (int i = 0; i < IMAGE_PIXELS; i++) {
        uint8_t v = img[i];
        sum += v;
        if (v < st->min_v) st->min_v = v;
        if (v > st->max_v) st->max_v = v;
    }
    st->mean = (double)sum / (double)IMAGE_PIXELS;
    st->center_v = img[(ACTIVE_ROWS / 2) * FULL_WIDTH + (FULL_WIDTH / 2)];
}

static void compute_raster16_stats(const uint16_t *img, struct raster_stats_u16 *st) {
    uint64_t sum = 0;
    st->min_v = 65535;
    st->max_v = 0;
    for (int i = 0; i < IMAGE_PIXELS; i++) {
        uint16_t v = img[i];
        sum += v;
        if (v < st->min_v) st->min_v = v;
        if (v > st->max_v) st->max_v = v;
    }
    st->mean = (double)sum / (double)IMAGE_PIXELS;
    st->center_v = img[(ACTIVE_ROWS / 2) * FULL_WIDTH + (FULL_WIDTH / 2)];
}

static void decode_active_u16le(const uint8_t *active_raw, uint16_t *dst_u16) {
    for (int i = 0; i < IMAGE_PIXELS; i++) {
        dst_u16[i] = u16_le_at(active_raw + i * 2);
    }
}

static int decode_pixel_centi_c(enum decode_model model, const struct capture_data *cap, int index) {
    double out = 0.0;
    double w00 = (double)cap->footer.r0_w00;
    double w01 = (double)cap->footer.r0_w01;
    double w20 = (double)cap->footer.r0_w20;
    double w25 = (double)cap->footer.r0_w25;
    double w20_delta = w20 - w25;
    double raw8 = (double)cap->image_y8[index];
    double raw16 = (double)cap->image_u16le[index];
    double r1_10 = (double)cap->footer.r1_u32_10;

    switch (model) {
    case MODEL_Y8_RANGE_W20_W00:
        out = w20 + (raw8 * (w00 - w20) / 255.0);
        break;
    case MODEL_Y8_RANGE_W25_W00:
        out = w25 + (raw8 * (w00 - w25) / 255.0);
        break;
    case MODEL_Y8_SIGNED_W01_SPAN_W00_W20: {
        double span = (w00 - w20);
        out = w01 + ((raw8 - 128.0) * span / 255.0);
        break;
    }
    case MODEL_U16LE_LINEAR_4096_DIV29P5:
        out = (raw16 - 4096.0) * (100.0 / 29.5);
        break;
    case MODEL_U16LE_LINEAR_4096_DIV32:
        out = (raw16 - 4096.0) * (100.0 / 32.0);
        break;
    case MODEL_U16LE_LINEAR_4096_DIV29P5_W20DELTA:
        if (w25 > 0.0 && fabs(w20_delta) <= 64.0) {
            raw16 -= w20_delta * 0.8125;
        }
        out = (raw16 - 4096.0) * (100.0 / 29.5);
        break;
    case MODEL_U16LE_LINEAR_4096_DIV29P5_R1COMP:
        out = (raw16 - 4096.0) * (100.0 / 29.5);
        out -= (r1_10 - 25300.0) * 0.293;
        break;
    case MODEL_COUNT:
        die_msg("invalid decode model");
    }

    if (out < -10000.0) out = -10000.0;
    if (out > 55535.0) out = 55535.0;
    return (int)lround(out);
}

static void decode_image_to_u16(enum decode_model model, const struct capture_data *cap,
                                uint16_t *dst_u16, struct temp_stats *st) {
    int64_t sum = 0;
    st->min_centi_c =  2147483647;
    st->max_centi_c = -2147483647;
    for (int i = 0; i < IMAGE_PIXELS; i++) {
        int centi_c = decode_pixel_centi_c(model, cap, i);
        if (centi_c < st->min_centi_c) st->min_centi_c = centi_c;
        if (centi_c > st->max_centi_c) st->max_centi_c = centi_c;
        sum += centi_c;
        int stored = centi_c + 10000;
        if (stored < 0) stored = 0;
        if (stored > 65535) stored = 65535;
        dst_u16[i] = (uint16_t)stored;
    }
    st->mean_centi_c = (double)sum / (double)IMAGE_PIXELS;
    st->center_centi_c = decode_pixel_centi_c(model, cap, (ACTIVE_ROWS / 2) * FULL_WIDTH + (FULL_WIDTH / 2));
}

static void write_pgm_u16(const char *path, const uint16_t *img, int width, int height) {
    FILE *f = fopen(path, "wb");
    if (!f) die(path);
    fprintf(f, "P5\n%d %d\n65535\n", width, height);
    for (int i = 0; i < width * height; i++) {
        uint16_t v = img[i];
        uint8_t bytes[2];
        bytes[0] = (uint8_t)(v >> 8);
        bytes[1] = (uint8_t)(v & 0xFF);
        if (fwrite(bytes, 1, 2, f) != 2) die(path);
    }
    fclose(f);
}

static void write_preview_pgm(const char *path, const uint16_t *img, int width, int height) {
    uint16_t min_v = 65535;
    uint16_t max_v = 0;
    for (int i = 0; i < width * height; i++) {
        if (img[i] < min_v) min_v = img[i];
        if (img[i] > max_v) max_v = img[i];
    }

    FILE *f = fopen(path, "wb");
    if (!f) die(path);
    fprintf(f, "P5\n%d %d\n255\n", width, height);
    for (int i = 0; i < width * height; i++) {
        uint8_t out = 0;
        if (max_v > min_v) {
            out = (uint8_t)(((uint32_t)(img[i] - min_v) * 255U) / (uint32_t)(max_v - min_v));
        }
        if (fwrite(&out, 1, 1, f) != 1) die(path);
    }
    fclose(f);
}

static void render_capture(const struct capture_data *cap, const struct options *opt) {
    uint16_t decoded[IMAGE_PIXELS];
    struct temp_stats st;
    char path_u16[1024];
    char path_preview[1024];
    decode_image_to_u16(opt->model, cap, decoded, &st);

    snprintf(path_u16, sizeof(path_u16), "%s/%s_%s_u16.pgm",
             opt->render_dir, cap->name, model_names[opt->model]);
    snprintf(path_preview, sizeof(path_preview), "%s/%s_%s_preview.pgm",
             opt->render_dir, cap->name, model_names[opt->model]);

    write_pgm_u16(path_u16, decoded, FULL_WIDTH, ACTIVE_ROWS);
    write_preview_pgm(path_preview, decoded, FULL_WIDTH, ACTIVE_ROWS);
}

static void print_header(enum decode_model model) {
    printf("# model=%s\n", model_names[model]);
    printf("%-24s  %6s  %6s  %6s  %6s  %8s  %8s  %8s  %7s  %7s  %7s  %7s  %8s\n",
           "name", "w00", "w01", "w20", "w25", "code_min", "code_max",
           "code_mean", "tmin_C", "tmax_C", "tavg_C", "tctr_C", "r1_10");
}

static void print_capture_summary(const struct capture_data *cap, enum decode_model model, bool verbose) {
    uint16_t decoded[IMAGE_PIXELS];
    struct temp_stats st;
    decode_image_to_u16(model, cap, decoded, &st);

    printf("%-24s  %6u  %6u  %6u  %6u  %8u  %8u  %8.2f  %7.2f  %7.2f  %7.2f  %7.2f  %8u\n",
           cap->name,
           cap->footer.r0_w00, cap->footer.r0_w01, cap->footer.r0_w20, cap->footer.r0_w25,
           cap->raster16_stats.min_v, cap->raster16_stats.max_v, cap->raster16_stats.mean,
           st.min_centi_c / 100.0, st.max_centi_c / 100.0, st.mean_centi_c / 100.0,
           st.center_centi_c / 100.0, cap->footer.r1_u32_10);

    if (!verbose) return;

    printf("  raster:\n");
    printf("    y8:   min=%u max=%u mean=%.2f center=%u\n",
           cap->raster_stats.min_v, cap->raster_stats.max_v, cap->raster_stats.mean, cap->raster_stats.center_v);
    printf("    u16le:min=%u max=%u mean=%.2f center=%u\n",
           cap->raster16_stats.min_v, cap->raster16_stats.max_v, cap->raster16_stats.mean, cap->raster16_stats.center_v);
    printf("  footer:\n");
    printf("    row0: w00=%u (%.2f C?)  w01=%u (%.2f C?)  w03=0x%04X  w20=%u (%.2f C?)  w25=%u (%.2f C?)\n",
           cap->footer.r0_w00, cap->footer.r0_w00 / 100.0,
           cap->footer.r0_w01, cap->footer.r0_w01 / 100.0,
           cap->footer.r0_w03,
           cap->footer.r0_w20, cap->footer.r0_w20 / 100.0,
           cap->footer.r0_w25, cap->footer.r0_w25 / 100.0);
    printf("    row1: marker=0x%08" PRIX32 " dims=[%" PRIu32 ",%" PRIu32 ",%" PRIu32 "] u32_10=%" PRIu32 " u32_14=%" PRIu32 "\n",
           cap->footer.r1_marker,
           cap->footer.r1_u32_04, cap->footer.r1_u32_08, cap->footer.r1_u32_0c,
           cap->footer.r1_u32_10, cap->footer.r1_u32_14);
    printf("    nonzero: meta_y=%d meta_raw=%d\n",
           cap->footer.meta_y_nonzero, cap->footer.meta_raw_nonzero);
    printf("  candidate models:\n");
    for (int model = 0; model < MODEL_COUNT; model++) {
        decode_image_to_u16((enum decode_model)model, cap, decoded, &st);
        printf("    %-24s min=%7.2fC max=%7.2fC mean=%7.2fC center=%7.2fC\n",
               model_names[model],
               st.min_centi_c / 100.0, st.max_centi_c / 100.0,
               st.mean_centi_c / 100.0, st.center_centi_c / 100.0);
    }
}

static void load_from_prefix(const char *prefix, struct capture_data *cap) {
    char path_img[1024];
    char path_region[1024];
    char path_meta_y[1024];
    char path_meta_raw[1024];
    const char *bn = base_name(prefix);
    snprintf(cap->name, sizeof(cap->name), "%s", bn);

    snprintf(path_img, sizeof(path_img), "%s_radiometric_y8.raw", prefix);
    snprintf(path_region, sizeof(path_region), "%s_radiometric_region_yuyv.raw", prefix);
    snprintf(path_meta_y, sizeof(path_meta_y), "%s_meta_y8.raw", prefix);
    snprintf(path_meta_raw, sizeof(path_meta_raw), "%s_meta_yuyv.raw", prefix);

    read_file_exact(path_img, cap->image_y8, IMAGE_PIXELS);
    read_file_prefix_exact(path_region, cap->active_raw, IMAGE_PIXELS * 2, (size_t)TOP_HALF_ROWS * BYTES_PER_LINE);
    read_file_exact(path_meta_y, cap->meta_y, META_ROWS * FULL_WIDTH);
    read_file_exact(path_meta_raw, cap->meta_raw, META_RAW_BYTES);
    decode_active_u16le(cap->active_raw, cap->image_u16le);
    footer_decode(&cap->footer, cap->meta_y, cap->meta_raw);
    compute_raster_stats(cap->image_y8, &cap->raster_stats);
    compute_raster16_stats(cap->image_u16le, &cap->raster16_stats);
}

static void extract_y_rows(uint8_t *dst, const uint8_t *frame,
                           int width, int start_row, int rows, int bytes_per_line) {
    for (int y = 0; y < rows; y++) {
        const uint8_t *src_row = frame + (size_t)(start_row + y) * bytes_per_line;
        for (int x = 0; x < width; x++) {
            dst[y * width + x] = src_row[x * 2];
        }
    }
}

static void load_from_device(const struct options *opt, struct capture_data *cap) {
    snprintf(cap->name, sizeof(cap->name), "live_capture");

    int fd = open(opt->device, O_RDWR | O_NONBLOCK, 0);
    if (fd < 0) die("open");

    struct v4l2_capability cap_info;
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap_info) < 0) die("VIDIOC_QUERYCAP");
    if (!(cap_info.capabilities & V4L2_CAP_VIDEO_CAPTURE)) die_msg("device does not support video capture");
    if (!(cap_info.capabilities & V4L2_CAP_STREAMING)) die_msg("device does not support streaming I/O");

    struct v4l2_format fmt;
    CLEAR(fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = FULL_WIDTH;
    fmt.fmt.pix.height = FULL_HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) die("VIDIOC_S_FMT");
    if ((int)fmt.fmt.pix.width != FULL_WIDTH || (int)fmt.fmt.pix.height != FULL_HEIGHT) {
        fprintf(stderr, "unexpected format: %ux%u\n", fmt.fmt.pix.width, fmt.fmt.pix.height);
        exit(EXIT_FAILURE);
    }

    struct v4l2_requestbuffers req;
    CLEAR(req);
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0) die("VIDIOC_REQBUFS");
    if (req.count < 2) die_msg("insufficient V4L2 buffer memory");

    struct buffer *buffers = calloc(req.count, sizeof(*buffers));
    if (!buffers) die("calloc");

    for (uint32_t i = 0; i < req.count; i++) {
        struct v4l2_buffer buf;
        CLEAR(buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) die("VIDIOC_QUERYBUF");
        buffers[i].length = buf.length;
        buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        if (buffers[i].start == MAP_FAILED) die("mmap");
    }

    for (uint32_t i = 0; i < req.count; i++) {
        struct v4l2_buffer buf;
        CLEAR(buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) die("VIDIOC_QBUF");
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) die("VIDIOC_STREAMON");

    for (int frame_no = 0; frame_no <= opt->warmup_frames; frame_no++) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;

        int r = select(fd + 1, &fds, NULL, NULL, &tv);
        if (r == -1) die("select");
        if (r == 0) die_msg("select timeout");

        struct v4l2_buffer buf;
        CLEAR(buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (xioctl(fd, VIDIOC_DQBUF, &buf) < 0) die("VIDIOC_DQBUF");

        if (frame_no == opt->warmup_frames) {
            const uint8_t *frame = buffers[buf.index].start;
            extract_y_rows(cap->image_y8, frame, FULL_WIDTH, TOP_HALF_START, ACTIVE_ROWS, BYTES_PER_LINE);
            for (int y = 0; y < ACTIVE_ROWS; y++) {
                const uint8_t *src = frame + (size_t)(TOP_HALF_START + y) * BYTES_PER_LINE;
                memcpy(cap->active_raw + (size_t)y * BYTES_PER_LINE, src, BYTES_PER_LINE);
            }
            extract_y_rows(cap->meta_y, frame, FULL_WIDTH, TOP_HALF_START + ACTIVE_ROWS, META_ROWS, BYTES_PER_LINE);
            for (int y = 0; y < META_ROWS; y++) {
                const uint8_t *src = frame + (size_t)(TOP_HALF_START + ACTIVE_ROWS + y) * BYTES_PER_LINE;
                memcpy(cap->meta_raw + (size_t)y * BYTES_PER_LINE, src, BYTES_PER_LINE);
            }
        }
        if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) die("VIDIOC_QBUF");
    }

    if (xioctl(fd, VIDIOC_STREAMOFF, &type) < 0) die("VIDIOC_STREAMOFF");
    for (uint32_t i = 0; i < req.count; i++) {
        munmap(buffers[i].start, buffers[i].length);
    }
    free(buffers);
    close(fd);

    decode_active_u16le(cap->active_raw, cap->image_u16le);
    footer_decode(&cap->footer, cap->meta_y, cap->meta_raw);
    compute_raster_stats(cap->image_y8, &cap->raster_stats);
    compute_raster16_stats(cap->image_u16le, &cap->raster16_stats);
}

static void capture_one_frame_into(const struct options *opt, struct capture_data *cap,
                                   int fd, struct buffer *buffers, uint32_t buffer_count) {
    snprintf(cap->name, sizeof(cap->name), "live_capture");
    for (int frame_no = 0; frame_no <= opt->warmup_frames; frame_no++) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;

        int r = select(fd + 1, &fds, NULL, NULL, &tv);
        if (r == -1) die("select");
        if (r == 0) die_msg("select timeout");

        struct v4l2_buffer buf;
        CLEAR(buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (xioctl(fd, VIDIOC_DQBUF, &buf) < 0) die("VIDIOC_DQBUF");

        if (buf.index >= buffer_count) die_msg("buffer index out of range");
        if (frame_no == opt->warmup_frames) {
            const uint8_t *frame = buffers[buf.index].start;
            extract_y_rows(cap->image_y8, frame, FULL_WIDTH, TOP_HALF_START, ACTIVE_ROWS, BYTES_PER_LINE);
            for (int y = 0; y < ACTIVE_ROWS; y++) {
                const uint8_t *src = frame + (size_t)(TOP_HALF_START + y) * BYTES_PER_LINE;
                memcpy(cap->active_raw + (size_t)y * BYTES_PER_LINE, src, BYTES_PER_LINE);
            }
            extract_y_rows(cap->meta_y, frame, FULL_WIDTH, TOP_HALF_START + ACTIVE_ROWS, META_ROWS, BYTES_PER_LINE);
            for (int y = 0; y < META_ROWS; y++) {
                const uint8_t *src = frame + (size_t)(TOP_HALF_START + ACTIVE_ROWS + y) * BYTES_PER_LINE;
                memcpy(cap->meta_raw + (size_t)y * BYTES_PER_LINE, src, BYTES_PER_LINE);
            }
        }
        if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) die("VIDIOC_QBUF");
    }

    decode_active_u16le(cap->active_raw, cap->image_u16le);
    footer_decode(&cap->footer, cap->meta_y, cap->meta_raw);
    compute_raster_stats(cap->image_y8, &cap->raster_stats);
    compute_raster16_stats(cap->image_u16le, &cap->raster16_stats);
}

static void monitor_device(const struct options *opt) {
    int fd = open(opt->device, O_RDWR | O_NONBLOCK, 0);
    if (fd < 0) die("open");

    struct v4l2_capability cap_info;
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap_info) < 0) die("VIDIOC_QUERYCAP");
    if (!(cap_info.capabilities & V4L2_CAP_VIDEO_CAPTURE)) die_msg("device does not support video capture");
    if (!(cap_info.capabilities & V4L2_CAP_STREAMING)) die_msg("device does not support streaming I/O");

    struct v4l2_format fmt;
    CLEAR(fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = FULL_WIDTH;
    fmt.fmt.pix.height = FULL_HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) die("VIDIOC_S_FMT");

    struct v4l2_requestbuffers req;
    CLEAR(req);
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0) die("VIDIOC_REQBUFS");
    if (req.count < 2) die_msg("insufficient V4L2 buffer memory");

    struct buffer *buffers = calloc(req.count, sizeof(*buffers));
    if (!buffers) die("calloc");

    for (uint32_t i = 0; i < req.count; i++) {
        struct v4l2_buffer buf;
        CLEAR(buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (xioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) die("VIDIOC_QUERYBUF");
        buffers[i].length = buf.length;
        buffers[i].start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
        if (buffers[i].start == MAP_FAILED) die("mmap");
    }

    for (uint32_t i = 0; i < req.count; i++) {
        struct v4l2_buffer buf;
        CLEAR(buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) die("VIDIOC_QBUF");
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) die("VIDIOC_STREAMON");

    FILE *out = open_output_path(opt->output_path);
    fprintf(out, "# model=%s\n", model_names[opt->model]);
    fprintf(out, "# device=%s\n", opt->device);
    fprintf(out, "# interval_s=%.3f\n", opt->interval_s);
    fprintf(out, "sample,wallclock_local,elapsed_s,code_min,code_max,code_mean,code_center,temp_min_C,temp_max_C,temp_mean_C,temp_center_C");
    fprintf(out, ",r0_w00,r0_w01,r0_w03,r0_w20,r0_w25,r1_u32_10,r1_u32_14,delta_center_code,delta_r0_w20,delta_r1_u32_10\n");
    fflush(out);

    struct timespec mono_start;
    clock_gettime(CLOCK_MONOTONIC, &mono_start);
    int64_t next_sample_ns = timespec_to_ns(&mono_start);

    struct capture_data prev;
    bool have_prev = false;

    for (int sample = 0; opt->count == 0 || sample < opt->count; sample++) {
        struct capture_data cap;
        capture_one_frame_into(opt, &cap, fd, buffers, req.count);

        struct temp_stats st;
        uint16_t decoded[IMAGE_PIXELS];
        decode_image_to_u16(opt->model, &cap, decoded, &st);

        struct timespec wall_now;
        struct timespec mono_now;
        clock_gettime(CLOCK_REALTIME, &wall_now);
        clock_gettime(CLOCK_MONOTONIC, &mono_now);
        char tsbuf[64];
        format_iso8601(tsbuf, sizeof(tsbuf), &wall_now);
        double elapsed_s = (double)(timespec_to_ns(&mono_now) - timespec_to_ns(&mono_start)) / 1e9;

        int delta_center_code = have_prev ? (int)cap.raster16_stats.center_v - (int)prev.raster16_stats.center_v : 0;
        int delta_r0_w20 = have_prev ? (int)cap.footer.r0_w20 - (int)prev.footer.r0_w20 : 0;
        int delta_r1_u32_10 = have_prev ? (int)cap.footer.r1_u32_10 - (int)prev.footer.r1_u32_10 : 0;

        fprintf(out, "%d,%s,%.3f,%u,%u,%.2f,%u,%.2f,%.2f,%.2f,%.2f",
                sample, tsbuf, elapsed_s,
                cap.raster16_stats.min_v, cap.raster16_stats.max_v, cap.raster16_stats.mean, cap.raster16_stats.center_v,
                st.min_centi_c / 100.0, st.max_centi_c / 100.0, st.mean_centi_c / 100.0, st.center_centi_c / 100.0);
        fprintf(out, ",%u,%u,%u,%u,%u,%u,%u,%d,%d,%d\n",
                cap.footer.r0_w00, cap.footer.r0_w01, cap.footer.r0_w03, cap.footer.r0_w20, cap.footer.r0_w25,
                cap.footer.r1_u32_10, cap.footer.r1_u32_14,
                delta_center_code, delta_r0_w20, delta_r1_u32_10);
        fflush(out);

        prev = cap;
        have_prev = true;

        next_sample_ns += (int64_t)(opt->interval_s * 1e9);
        int64_t now_ns = timespec_to_ns(&mono_now);
        if (next_sample_ns > now_ns) {
            struct timespec sleep_ts;
            int64_t remain = next_sample_ns - now_ns;
            sleep_ts.tv_sec = (time_t)(remain / 1000000000LL);
            sleep_ts.tv_nsec = (long)(remain % 1000000000LL);
            nanosleep(&sleep_ts, NULL);
        } else {
            next_sample_ns = now_ns;
        }
    }

    if (out != stdout) fclose(out);
    if (xioctl(fd, VIDIOC_STREAMOFF, &type) < 0) die("VIDIOC_STREAMOFF");
    for (uint32_t i = 0; i < req.count; i++) {
        munmap(buffers[i].start, buffers[i].length);
    }
    free(buffers);
    close(fd);
}

static void usage(FILE *f, const char *argv0) {
    fprintf(f,
        "Usage: %s [input] [options]\n"
        "Input:\n"
        "  --prefix PATH        Decode one capture prefix\n"
        "  --dataset DIR        Decode every *_meta_yuyv.raw set in DIR\n"
        "  --device PATH        Capture one live frame from a V4L2 device\n"
        "  --monitor PATH       Monitor live drift from a V4L2 device, PATH or '-' for stdout\n"
        "Options:\n"
        "  --warmup N           Frames to discard before live capture (default: 6)\n"
        "  --interval SEC       Monitor sample interval (default: 5)\n"
        "  --count N            Monitor sample count, 0 = unlimited (default: 0)\n"
        "  --model NAME         Summary/render model\n"
        "  --verbose            Print detailed footer and model stats\n"
        "  --no-header          Omit summary table header\n"
        "  --render DIR         Write decoded u16 + preview PGM for selected model\n"
        "  -h, --help           Show this help\n",
        argv0);
}

static enum decode_model parse_model(const char *s) {
    for (int i = 0; i < MODEL_COUNT; i++) {
        if (!strcmp(s, model_names[i])) return (enum decode_model)i;
    }
    fprintf(stderr, "unknown model: %s\n", s);
    exit(EXIT_FAILURE);
}

static void parse_args(int argc, char **argv, struct options *opt) {
    memset(opt, 0, sizeof(*opt));
    opt->device = "/dev/video0";
    opt->warmup_frames = 6;
    opt->interval_s = 5.0;
    opt->count = 0;
    opt->output_header = true;
    opt->model = MODEL_U16LE_LINEAR_4096_DIV29P5;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--prefix")) {
            if (++i >= argc) die_msg("--prefix needs a path");
            opt->mode = INPUT_PREFIX;
            opt->prefix = argv[i];
        } else if (!strcmp(argv[i], "--dataset")) {
            if (++i >= argc) die_msg("--dataset needs a path");
            opt->mode = INPUT_DATASET;
            opt->dataset_dir = argv[i];
        } else if (!strcmp(argv[i], "--device")) {
            if (++i >= argc) die_msg("--device needs a path");
            opt->mode = INPUT_DEVICE;
            opt->device = argv[i];
        } else if (!strcmp(argv[i], "--monitor")) {
            if (++i >= argc) die_msg("--monitor needs a path or -");
            opt->mode = INPUT_MONITOR;
            opt->output_path = argv[i];
        } else if (!strcmp(argv[i], "--warmup")) {
            if (++i >= argc) die_msg("--warmup needs N");
            opt->warmup_frames = parse_int(argv[i], "warmup");
            if (opt->warmup_frames < 0) die_msg("--warmup must be >= 0");
        } else if (!strcmp(argv[i], "--interval")) {
            if (++i >= argc) die_msg("--interval needs seconds");
            opt->interval_s = parse_double(argv[i], "interval");
            if (opt->interval_s <= 0.0) die_msg("--interval must be > 0");
        } else if (!strcmp(argv[i], "--count")) {
            if (++i >= argc) die_msg("--count needs N");
            opt->count = parse_int(argv[i], "count");
            if (opt->count < 0) die_msg("--count must be >= 0");
        } else if (!strcmp(argv[i], "--model")) {
            if (++i >= argc) die_msg("--model needs a name");
            opt->model = parse_model(argv[i]);
        } else if (!strcmp(argv[i], "--verbose")) {
            opt->verbose = true;
        } else if (!strcmp(argv[i], "--no-header")) {
            opt->output_header = false;
        } else if (!strcmp(argv[i], "--render")) {
            if (++i >= argc) die_msg("--render needs a directory");
            opt->render = true;
            opt->render_dir = argv[i];
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(stdout, argv[0]);
            exit(EXIT_SUCCESS);
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            usage(stderr, argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    if (opt->mode == INPUT_NONE) {
        die_msg("choose one of --prefix, --dataset, or --device");
    }
}

static int compare_strings(const void *a, const void *b) {
    const char *const *sa = a;
    const char *const *sb = b;
    return strcmp(*sa, *sb);
}

static void ensure_dir_exists(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) die_msg("render path exists but is not a directory");
        return;
    }
    if (mkdir(path, 0777) < 0) die("mkdir render dir");
}

int main(int argc, char **argv) {
    struct options opt;
    parse_args(argc, argv, &opt);

    if (opt.render) ensure_dir_exists(opt.render_dir);

    if (opt.mode == INPUT_PREFIX) {
        if (opt.output_header) print_header(opt.model);
        struct capture_data cap;
        load_from_prefix(opt.prefix, &cap);
        print_capture_summary(&cap, opt.model, opt.verbose);
        if (opt.render) render_capture(&cap, &opt);
        return 0;
    }

    if (opt.mode == INPUT_DEVICE) {
        if (opt.output_header) print_header(opt.model);
        struct capture_data cap;
        load_from_device(&opt, &cap);
        print_capture_summary(&cap, opt.model, true);
        if (opt.render) render_capture(&cap, &opt);
        return 0;
    }

    if (opt.mode == INPUT_MONITOR) {
        monitor_device(&opt);
        return 0;
    }

    if (opt.output_header) print_header(opt.model);

    DIR *dir = opendir(opt.dataset_dir);
    if (!dir) die("opendir dataset");

    char **prefixes = NULL;
    size_t prefix_count = 0;
    size_t prefix_cap = 0;
    const char *suffix = "_meta_yuyv.raw";
    size_t suffix_len = strlen(suffix);

    for (;;) {
        struct dirent *ent = readdir(dir);
        if (!ent) break;
        if (!has_suffix(ent->d_name, suffix)) continue;

        size_t name_len = strlen(ent->d_name);
        size_t stem_len = name_len - suffix_len;
        char stem[512];
        memcpy(stem, ent->d_name, stem_len);
        stem[stem_len] = '\0';

        char prefix[1024];
        join_path(prefix, sizeof(prefix), opt.dataset_dir, stem);

        if (prefix_count == prefix_cap) {
            prefix_cap = prefix_cap ? prefix_cap * 2 : 16;
            prefixes = realloc(prefixes, prefix_cap * sizeof(*prefixes));
            if (!prefixes) die("realloc");
        }
        prefixes[prefix_count] = strdup(prefix);
        if (!prefixes[prefix_count]) die("strdup");
        prefix_count++;
    }
    closedir(dir);

    qsort(prefixes, prefix_count, sizeof(*prefixes), compare_strings);
    for (size_t i = 0; i < prefix_count; i++) {
        struct capture_data cap;
        load_from_prefix(prefixes[i], &cap);
        print_capture_summary(&cap, opt.model, opt.verbose);
        if (opt.render) render_capture(&cap, &opt);
        free(prefixes[i]);
    }
    free(prefixes);
    return 0;
}
