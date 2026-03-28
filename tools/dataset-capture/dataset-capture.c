#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/videodev2.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define CLEAR(x) memset(&(x), 0, sizeof(x))

struct buffer {
    void *start;
    size_t length;
};

struct options {
    const char *device;
    const char *prefix;
    bool have_pixel;
    int pixel_x;
    int pixel_y;
    bool have_roi;
    int roi_x;
    int roi_y;
    int roi_w;
    int roi_h;
    int warmup_frames;
};

struct stats {
    uint8_t min_v;
    uint8_t max_v;
    int min_x;
    int min_y;
    int max_x;
    int max_y;
    double mean;
    uint8_t center_v;
    int center_x;
    int center_y;
    bool have_pixel;
    uint8_t pixel_v;
    int pixel_x;
    int pixel_y;
    bool have_roi;
    uint8_t roi_min;
    uint8_t roi_max;
    double roi_mean;
    int roi_x;
    int roi_y;
    int roi_w;
    int roi_h;
};

static int xioctl(int fd, unsigned long request, void *arg) {
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

static void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

static void die_msg(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(EXIT_FAILURE);
}

static void timestamp_string(char *buf, size_t n) {
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, n, "%Y%m%d_%H%M%S", &tm);
}

static void write_file(const char *path, const void *data, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) die("fopen");
    if (fwrite(data, 1, len, f) != len) die("fwrite");
    fclose(f);
}

static void write_pgm(const char *path, const uint8_t *img, int width, int height) {
    FILE *f = fopen(path, "wb");
    if (!f) die("fopen pgm");
    fprintf(f, "P5\n%d %d\n255\n", width, height);
    if (fwrite(img, 1, (size_t)width * height, f) != (size_t)width * height) {
        die("fwrite pgm");
    }
    fclose(f);
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

static void extract_yuyv_rows(uint8_t *dst, const uint8_t *frame,
                              int start_row, int rows, int bytes_per_line) {
    for (int y = 0; y < rows; y++) {
        const uint8_t *src_row = frame + (size_t)(start_row + y) * bytes_per_line;
        memcpy(dst + (size_t)y * bytes_per_line, src_row, (size_t)bytes_per_line);
    }
}

static void compute_stats(const uint8_t *img, int width, int height,
                          const struct options *opt, struct stats *st) {
    uint64_t sum = 0;
    st->min_v = 255;
    st->max_v = 0;
    st->min_x = st->min_y = 0;
    st->max_x = st->max_y = 0;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint8_t v = img[y * width + x];
            sum += v;
            if (v < st->min_v) {
                st->min_v = v;
                st->min_x = x;
                st->min_y = y;
            }
            if (v > st->max_v) {
                st->max_v = v;
                st->max_x = x;
                st->max_y = y;
            }
        }
    }

    st->mean = (double)sum / (double)(width * height);
    st->center_x = width / 2;
    st->center_y = height / 2;
    st->center_v = img[st->center_y * width + st->center_x];

    st->have_pixel = false;
    if (opt->have_pixel) {
        if (opt->pixel_x < 0 || opt->pixel_x >= width || opt->pixel_y < 0 || opt->pixel_y >= height) {
            die_msg("pixel out of range");
        }
        st->have_pixel = true;
        st->pixel_x = opt->pixel_x;
        st->pixel_y = opt->pixel_y;
        st->pixel_v = img[opt->pixel_y * width + opt->pixel_x];
    }

    st->have_roi = false;
    if (opt->have_roi) {
        int x0 = opt->roi_x;
        int y0 = opt->roi_y;
        int w = opt->roi_w;
        int h = opt->roi_h;

        if (w <= 0 || h <= 0) die_msg("ROI width/height must be positive");
        if (x0 < 0 || y0 < 0 || x0 + w > width || y0 + h > height) die_msg("ROI out of range");

        st->have_roi = true;
        st->roi_x = x0;
        st->roi_y = y0;
        st->roi_w = w;
        st->roi_h = h;

        uint64_t roi_sum = 0;
        st->roi_min = 255;
        st->roi_max = 0;

        for (int y = y0; y < y0 + h; y++) {
            for (int x = x0; x < x0 + w; x++) {
                uint8_t v = img[y * width + x];
                roi_sum += v;
                if (v < st->roi_min) st->roi_min = v;
                if (v > st->roi_max) st->roi_max = v;
            }
        }
        st->roi_mean = (double)roi_sum / (double)(w * h);
    }
}

static void write_meta_text(const char *path, const uint8_t *meta_y, const uint8_t *meta_yuyv,
                            int width, int rows) {
    FILE *f = fopen(path, "w");
    if (!f) die("fopen meta.txt");

    const int y_bytes = width * rows;
    const int raw_bytes = width * rows * 2;

    fprintf(f, "# HT-203U metadata dump\n");
    fprintf(f, "# width: %d\n", width);
    fprintf(f, "# rows: %d\n", rows);
    fprintf(f, "# meta_y_bytes: %d\n", y_bytes);
    fprintf(f, "# meta_yuyv_bytes: %d\n\n", raw_bytes);

    fprintf(f, "[meta_y_first_64]\n");
    for (int i = 0; i < y_bytes && i < 64; i++) {
        if ((i % 16) == 0) fprintf(f, "  ");
        fprintf(f, "%02X ", meta_y[i]);
        if ((i % 16) == 15) fprintf(f, "\n");
    }
    if ((y_bytes % 16) != 0) fprintf(f, "\n");
    fprintf(f, "\n");

    for (int r = 0; r < rows; r++) {
        const uint8_t *row_y = meta_y + (size_t)r * width;
        const uint8_t *row_raw = meta_yuyv + (size_t)r * width * 2;

        fprintf(f, "[row %d y_bytes]\n", r);
        for (int i = 0; i < width; i++) {
            if ((i % 16) == 0) fprintf(f, "  ");
            fprintf(f, "%02X ", row_y[i]);
            if ((i % 16) == 15) fprintf(f, "\n");
        }
        if ((width % 16) != 0) fprintf(f, "\n");
        fprintf(f, "\n");

        fprintf(f, "[row %d raw_bytes]\n", r);
        for (int i = 0; i < width * 2; i++) {
            if ((i % 16) == 0) fprintf(f, "  ");
            fprintf(f, "%02X ", row_raw[i]);
            if ((i % 16) == 15) fprintf(f, "\n");
        }
        if (((width * 2) % 16) != 0) fprintf(f, "\n");
        fprintf(f, "\n");

        fprintf(f, "[row %d u16_le]\n", r);
        for (int i = 0; i < width * 2; i += 2) {
            uint16_t v = (uint16_t)row_raw[i] | ((uint16_t)row_raw[i + 1] << 8);
            fprintf(f, "%04X", v);
            if ((i % 16) == 14) fprintf(f, "\n");
            else fprintf(f, " ");
        }
        if (((width * 2) % 16) != 0) fprintf(f, "\n");
        fprintf(f, "\n");

        fprintf(f, "[row %d u16_be]\n", r);
        for (int i = 0; i < width * 2; i += 2) {
            uint16_t v = ((uint16_t)row_raw[i] << 8) | (uint16_t)row_raw[i + 1];
            fprintf(f, "%04X", v);
            if ((i % 16) == 14) fprintf(f, "\n");
            else fprintf(f, " ");
        }
        if (((width * 2) % 16) != 0) fprintf(f, "\n");
        fprintf(f, "\n");
    }

    fclose(f);
}

static void write_summary_json(const char *path, const struct options *opt, const struct stats *st,
                               const uint8_t *meta_y, const uint8_t *meta_yuyv,
                               int width, int height, int meta_rows) {
    FILE *f = fopen(path, "w");
    if (!f) die("fopen summary.json");

    int meta_y_nonzero = 0;
    int meta_raw_nonzero = 0;
    for (int i = 0; i < width * meta_rows; i++) {
        if (meta_y[i] != 0) meta_y_nonzero++;
    }
    for (int i = 0; i < width * meta_rows * 2; i++) {
        if (meta_yuyv[i] != 0) meta_raw_nonzero++;
    }

    fprintf(f, "{\n");
    fprintf(f, "  \"device\": \"%s\",\n", opt->device);
    fprintf(f, "  \"width\": %d,\n", width);
    fprintf(f, "  \"height\": %d,\n", height);
    fprintf(f, "  \"min\": {\"value\": %u, \"x\": %d, \"y\": %d},\n",
            st->min_v, st->min_x, st->min_y);
    fprintf(f, "  \"max\": {\"value\": %u, \"x\": %d, \"y\": %d},\n",
            st->max_v, st->max_x, st->max_y);
    fprintf(f, "  \"mean\": %.6f,\n", st->mean);
    fprintf(f, "  \"center\": {\"x\": %d, \"y\": %d, \"value\": %u},\n",
            st->center_x, st->center_y, st->center_v);

    if (st->have_pixel) {
        fprintf(f, "  \"pixel\": {\"x\": %d, \"y\": %d, \"value\": %u},\n",
                st->pixel_x, st->pixel_y, st->pixel_v);
    } else {
        fprintf(f, "  \"pixel\": null,\n");
    }

    if (st->have_roi) {
        fprintf(f,
                "  \"roi\": {\"x\": %d, \"y\": %d, \"w\": %d, \"h\": %d, \"min\": %u, \"max\": %u, \"mean\": %.6f},\n",
                st->roi_x, st->roi_y, st->roi_w, st->roi_h,
                st->roi_min, st->roi_max, st->roi_mean);
    } else {
        fprintf(f, "  \"roi\": null,\n");
    }

    fprintf(f, "  \"meta\": {\n");
    fprintf(f, "    \"rows\": %d,\n", meta_rows);
    fprintf(f, "    \"y_nonzero\": %d,\n", meta_y_nonzero);
    fprintf(f, "    \"raw_nonzero\": %d,\n", meta_raw_nonzero);

    fprintf(f, "    \"y_prefix_hex\": \"");
    int y_prefix = width * meta_rows;
    if (y_prefix > 32) y_prefix = 32;
    for (int i = 0; i < y_prefix; i++) fprintf(f, "%02X", meta_y[i]);
    fprintf(f, "\",\n");

    fprintf(f, "    \"raw_prefix_hex\": \"");
    int raw_prefix = width * meta_rows * 2;
    if (raw_prefix > 64) raw_prefix = 64;
    for (int i = 0; i < raw_prefix; i++) fprintf(f, "%02X", meta_yuyv[i]);
    fprintf(f, "\"\n");

    fprintf(f, "  }\n");
    fprintf(f, "}\n");
    fclose(f);
}

static void usage(FILE *f, const char *argv0) {
    fprintf(f,
        "Usage: %s [options]\n"
        "Options:\n"
        "  --device PATH         Video device (default: /dev/video0)\n"
        "  --prefix NAME         Prefix for output files (default: timestamp)\n"
        "  --pixel X Y           Record one pixel value from extracted image\n"
        "  --roi X Y W H         Record ROI stats from extracted image\n"
        "  --warmup N            Frames to discard before capture (default: 6)\n"
        "  -h, --help            Show this help\n",
        argv0);
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

static void parse_args(int argc, char **argv, struct options *opt) {
    opt->device = "/dev/video0";
    opt->prefix = NULL;
    opt->have_pixel = false;
    opt->have_roi = false;
    opt->warmup_frames = 6;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--device")) {
            if (++i >= argc) die_msg("--device needs a path");
            opt->device = argv[i];
        } else if (!strcmp(argv[i], "--prefix")) {
            if (++i >= argc) die_msg("--prefix needs a value");
            opt->prefix = argv[i];
        } else if (!strcmp(argv[i], "--pixel")) {
            if (i + 2 >= argc) die_msg("--pixel needs X Y");
            opt->have_pixel = true;
            opt->pixel_x = parse_int(argv[++i], "pixel x");
            opt->pixel_y = parse_int(argv[++i], "pixel y");
        } else if (!strcmp(argv[i], "--roi")) {
            if (i + 4 >= argc) die_msg("--roi needs X Y W H");
            opt->have_roi = true;
            opt->roi_x = parse_int(argv[++i], "roi x");
            opt->roi_y = parse_int(argv[++i], "roi y");
            opt->roi_w = parse_int(argv[++i], "roi w");
            opt->roi_h = parse_int(argv[++i], "roi h");
        } else if (!strcmp(argv[i], "--warmup")) {
            if (++i >= argc) die_msg("--warmup needs N");
            opt->warmup_frames = parse_int(argv[i], "warmup frames");
            if (opt->warmup_frames < 1) die_msg("--warmup must be >= 1");
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(stdout, argv[0]);
            exit(EXIT_SUCCESS);
        } else {
            fprintf(stderr, "unknown option: %s\n", argv[i]);
            usage(stderr, argv[0]);
            exit(EXIT_FAILURE);
        }
    }
}

int main(int argc, char **argv) {
    struct options opt;
    parse_args(argc, argv, &opt);

    const int full_width = 256;
    const int full_height = 392;
    const int bytes_per_line = full_width * 2;  // YUYV

    const int top_half_start = 0;
    const int top_half_rows = 196;
    const int active_rows = 192;
    const int meta_rows = 4;

    int fd = open(opt.device, O_RDWR | O_NONBLOCK, 0);
    if (fd < 0) die("open");

    struct v4l2_capability cap;
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) die("VIDIOC_QUERYCAP");

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) die_msg("device does not support video capture");
    if (!(cap.capabilities & V4L2_CAP_STREAMING)) die_msg("device does not support streaming I/O");

    struct v4l2_format fmt;
    CLEAR(fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = full_width;
    fmt.fmt.pix.height = full_height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) die("VIDIOC_S_FMT");

    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV) die_msg("device did not accept YUYV");
    if ((int)fmt.fmt.pix.width != full_width || (int)fmt.fmt.pix.height != full_height) {
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

    char base[256];
    if (opt.prefix) {
        snprintf(base, sizeof(base), "%s", opt.prefix);
    } else {
        timestamp_string(base, sizeof(base));
    }

    for (int frame_no = 0; frame_no < opt.warmup_frames; frame_no++) {
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

        bool capture_this = (frame_no == opt.warmup_frames - 1);

        if (capture_this) {
            const uint8_t *frame = buffers[buf.index].start;

            char path_full[512];
            char path_region_yuyv[512];
            char path_img_raw[512];
            char path_img_pgm[512];
            char path_meta_y8[512];
            char path_meta_yuyv[512];
            char path_meta_txt[512];
            char path_summary[512];

            snprintf(path_full, sizeof(path_full), "%s_full_yuyv.raw", base);
            snprintf(path_region_yuyv, sizeof(path_region_yuyv), "%s_radiometric_region_yuyv.raw", base);
            snprintf(path_img_raw, sizeof(path_img_raw), "%s_radiometric_y8.raw", base);
            snprintf(path_img_pgm, sizeof(path_img_pgm), "%s_radiometric.pgm", base);
            snprintf(path_meta_y8, sizeof(path_meta_y8), "%s_meta_y8.raw", base);
            snprintf(path_meta_yuyv, sizeof(path_meta_yuyv), "%s_meta_yuyv.raw", base);
            snprintf(path_meta_txt, sizeof(path_meta_txt), "%s_meta.txt", base);
            snprintf(path_summary, sizeof(path_summary), "%s_summary.json", base);

            uint8_t *region_yuyv = malloc((size_t)top_half_rows * bytes_per_line);
            uint8_t *img_y = malloc((size_t)full_width * active_rows);
            uint8_t *meta_y = malloc((size_t)full_width * meta_rows);
            uint8_t *meta_raw = malloc((size_t)meta_rows * bytes_per_line);

            if (!region_yuyv || !img_y || !meta_y || !meta_raw) die("malloc");

            write_file(path_full, frame, buf.bytesused);

            extract_yuyv_rows(region_yuyv, frame, top_half_start, top_half_rows, bytes_per_line);
            extract_y_rows(img_y, frame, full_width, top_half_start, active_rows, bytes_per_line);
            extract_y_rows(meta_y, frame, full_width, top_half_start + active_rows, meta_rows, bytes_per_line);
            extract_yuyv_rows(meta_raw, frame, top_half_start + active_rows, meta_rows, bytes_per_line);

            write_file(path_region_yuyv, region_yuyv, (size_t)top_half_rows * bytes_per_line);
            write_file(path_img_raw, img_y, (size_t)full_width * active_rows);
            write_pgm(path_img_pgm, img_y, full_width, active_rows);
            write_file(path_meta_y8, meta_y, (size_t)full_width * meta_rows);
            write_file(path_meta_yuyv, meta_raw, (size_t)meta_rows * bytes_per_line);
            write_meta_text(path_meta_txt, meta_y, meta_raw, full_width, meta_rows);

            struct stats st;
            compute_stats(img_y, full_width, active_rows, &opt, &st);
            write_summary_json(path_summary, &opt, &st, meta_y, meta_raw, full_width, active_rows, meta_rows);

            fprintf(stderr, "wrote:\n");
            fprintf(stderr, "  %s\n", path_full);
            fprintf(stderr, "  %s\n", path_region_yuyv);
            fprintf(stderr, "  %s\n", path_img_raw);
            fprintf(stderr, "  %s\n", path_img_pgm);
            fprintf(stderr, "  %s\n", path_meta_y8);
            fprintf(stderr, "  %s\n", path_meta_yuyv);
            fprintf(stderr, "  %s\n", path_meta_txt);
            fprintf(stderr, "  %s\n", path_summary);

            if (opt.have_pixel) {
                fprintf(stderr, "pixel(%d,%d) = %u\n", st.pixel_x, st.pixel_y, st.pixel_v);
            }
            if (opt.have_roi) {
                fprintf(stderr, "roi(%d,%d,%d,%d): min=%u max=%u mean=%.3f\n",
                        st.roi_x, st.roi_y, st.roi_w, st.roi_h,
                        st.roi_min, st.roi_max, st.roi_mean);
            }
            fprintf(stderr, "global: min=%u@(%d,%d) max=%u@(%d,%d) mean=%.3f center=%u\n",
                    st.min_v, st.min_x, st.min_y,
                    st.max_v, st.max_x, st.max_y,
                    st.mean, st.center_v);

            free(region_yuyv);
            free(img_y);
            free(meta_y);
            free(meta_raw);
        }

        if (xioctl(fd, VIDIOC_QBUF, &buf) < 0) die("VIDIOC_QBUF");
    }

    if (xioctl(fd, VIDIOC_STREAMOFF, &type) < 0) die("VIDIOC_STREAMOFF");

    for (uint32_t i = 0; i < req.count; i++) {
        munmap(buffers[i].start, buffers[i].length);
    }
    free(buffers);
    close(fd);
    return 0;
}