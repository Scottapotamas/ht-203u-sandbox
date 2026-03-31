#ifndef HTI_USB_VIEW_H
#define HTI_USB_VIEW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <libusb-1.0/libusb.h>

enum
{
    HTI_FRAME_WIDTH         = 256,
    HTI_FRAME_HEIGHT        = 192,
    HTI_FULL_HEIGHT         = 392,
    HTI_BYTES_PER_LINE      = HTI_FRAME_WIDTH * 2,
    HTI_META_ROWS           = 4,
    HTI_IMAGE_PIXELS        = HTI_FRAME_WIDTH * HTI_FRAME_HEIGHT,
    HTI_ACTIVE_BYTES        = HTI_IMAGE_PIXELS * 2,
    HTI_META_RAW_BYTES      = HTI_META_ROWS * HTI_BYTES_PER_LINE,
    HTI_FULL_FRAME_BYTES    = HTI_FULL_HEIGHT * HTI_BYTES_PER_LINE,
    HTI_MAX_TRANSPORT_BYTES = 262144,
    HTI_DEFAULT_TIMEOUT_MS  = 5000,
};

enum hti_frame_layout
{
    HTI_LAYOUT_UNKNOWN = 0,
    HTI_LAYOUT_STACKED_RAW,
    HTI_LAYOUT_DUAL_ACTIVE,
    HTI_LAYOUT_VENDOR_FRAMED,
};

enum hti_correction_mode
{
    HTI_CORRECTION_RAW = 0,
    HTI_CORRECTION_W20DELTA,
    HTI_CORRECTION_W20_PLUS_R1,
    HTI_CORRECTION_LOWER_AFFINE,
    HTI_CORRECTION_COUNT,
};

enum hti_ffc_state
{
    HTI_FFC_NONE = 0,
    HTI_FFC_TRIGGERED,
    HTI_FFC_REPLAY,
    HTI_FFC_RECOVERED,
};

enum hti_lower_affine_kind
{
    HTI_LOWER_AFFINE_NONE = 0,
    HTI_LOWER_AFFINE_MINMAX,
    HTI_LOWER_AFFINE_MINMEAN,
};

enum hti_palette_mode
{
    HTI_PALETTE_IRON = 0,
    HTI_PALETTE_GRAY,
    HTI_PALETTE_COUNT,
};

enum hti_overlay_mode
{
    HTI_OVERLAY_NONE = 0,
    HTI_OVERLAY_MINIMAL,
    HTI_OVERLAY_DEBUG,
    HTI_OVERLAY_COUNT,
};

struct hti_footer_fields
{
    bool     valid;
    uint16_t r0_w00;
    uint16_t r0_w01;
    uint16_t r0_w03;
    uint16_t r0_w20;
    uint16_t r0_w25;
    uint32_t r1_u32_10;
    uint32_t r1_u32_14;
};

struct hti_display_footer_fields
{
    bool     valid;
    uint16_t marker0;
    uint16_t marker1;
    uint16_t w2;
    uint16_t w5;
    uint16_t w8;
    uint16_t w9;
};

struct hti_transport_frame
{
    char     name[128];
    uint8_t  bytes[HTI_MAX_TRANSPORT_BYTES];
    size_t   length;
    uint64_t sequence;
};

struct hti_frame
{
    char                             name[128];
    enum hti_frame_layout            layout;
    uint16_t                         radiometric[HTI_IMAGE_PIXELS];
    uint8_t                          display_raw[HTI_ACTIVE_BYTES];
    bool                             has_display_raw;
    uint8_t                          meta_raw[HTI_META_RAW_BYTES];
    bool                             has_meta_raw;
    struct hti_footer_fields         footer;
    struct hti_display_footer_fields display_footer;
    uint64_t                         capture_monotonic_ns;
    uint64_t                         frame_delta_ns;
    bool                             replay_detected;
    bool                             footer_changed;
    enum hti_ffc_state               ffc_state;
    bool                             ffc_manual;
    bool                             quantitative_valid;
    uint64_t                         ffc_window_ns;
    uint64_t                         stable_run_ns;
    uint64_t                         previous_stable_run_ns;
    bool                             lower_affine_valid;
    enum hti_lower_affine_kind       lower_affine_kind;
    double                           lower_affine_scale;
    double                           lower_affine_bias;
    double                           lower_affine_target_min_c;
    double                           lower_affine_target_max_c;
    double                           lower_affine_target_mean_c;
    double                           lower_affine_target_center_c;
};

struct hti_temperature_stats
{
    double min_c;
    double max_c;
    double center_c;
    double mean_c;
    double offset_c;
    int    min_x;
    int    min_y;
    int    max_x;
    int    max_y;
};

struct hti_display_range
{
    double min_c;
    double max_c;
};

struct hti_stream_request
{
    uint8_t  format_index;
    uint8_t  frame_index;
    uint32_t frame_interval_100ns;
    int      timeout_ms;
};

struct hti_usb_device
{
    libusb_context       *ctx;
    libusb_device_handle *handle;
    uint16_t              vid;
    uint16_t              pid;
    uint8_t               control_interface;
    uint8_t               streaming_interface;
    uint8_t               extension_unit;
    uint8_t               bulk_endpoint;
    int                   timeout_ms;
    uint32_t              negotiated_frame_bytes;
    uint32_t              negotiated_payload_bytes;
    uint64_t              frame_sequence;
    bool                  control_detached;
    bool                  streaming_detached;
    uint8_t               packet_buf[65536];
};

struct hti_options
{
    const char               *prefix;
    const char               *save_dir;
    enum hti_correction_mode  correction;
    enum hti_palette_mode     palette;
    enum hti_overlay_mode     overlay;
    bool                      headless;
    bool                      verbose;
    bool                      manual_scale;
    double                    manual_min_c;
    double                    manual_max_c;
    int                       frames;
    int                       trigger_after_frames;
    bool                      trigger_before_start;
    int                       window_scale;
    struct hti_stream_request stream_request;
};

struct hti_renderer
{
    void *window;
    void *renderer;
    void *texture;
};

static inline uint16_t hti_read_le16( const uint8_t *p )
{
    return (uint16_t)p[0] | ( (uint16_t)p[1] << 8 );
}

static inline uint32_t hti_read_le32( const uint8_t *p )
{
    return (uint32_t)p[0]
           | ( (uint32_t)p[1] << 8 )
           | ( (uint32_t)p[2] << 16 )
           | ( (uint32_t)p[3] << 24 );
}

static inline void hti_write_le16( uint8_t *p, uint16_t value )
{
    p[0] = (uint8_t)( value & 0xffu );
    p[1] = (uint8_t)( ( value >> 8 ) & 0xffu );
}

static inline void hti_write_le32( uint8_t *p, uint32_t value )
{
    p[0] = (uint8_t)( value & 0xffu );
    p[1] = (uint8_t)( ( value >> 8 ) & 0xffu );
    p[2] = (uint8_t)( ( value >> 16 ) & 0xffu );
    p[3] = (uint8_t)( ( value >> 24 ) & 0xffu );
}

const char              *hti_frame_layout_name( enum hti_frame_layout layout );
const char              *hti_correction_name( enum hti_correction_mode correction );
enum hti_correction_mode hti_next_correction( enum hti_correction_mode correction );
const char              *hti_palette_name( enum hti_palette_mode palette );
enum hti_palette_mode    hti_next_palette( enum hti_palette_mode palette );
const char              *hti_ffc_state_name( enum hti_ffc_state state );
const char              *hti_lower_affine_kind_name( enum hti_lower_affine_kind kind );
double                   hti_lower_footer_min_c( const struct hti_frame *frame );
double                   hti_lower_footer_max_c( const struct hti_frame *frame );
double                   hti_lower_footer_mean_c( const struct hti_frame *frame );
double                   hti_lower_footer_center_c( const struct hti_frame *frame );

int  hti_usb_open( struct hti_usb_device *dev, uint16_t vid, uint16_t pid,
                   const struct hti_stream_request *request, bool verbose );
void hti_usb_close( struct hti_usb_device *dev );
int  hti_usb_read_frame( struct hti_usb_device *dev, struct hti_transport_frame *frame );
int  hti_usb_trigger_shutter( struct hti_usb_device *dev );
int  hti_usb_trigger_shutter_standalone( uint16_t vid, uint16_t pid, int timeout_ms, bool verbose );

int hti_decode_transport_frame( const struct hti_transport_frame *src, struct hti_frame *dst );
int hti_load_saved_prefix( const char                 *prefix,
                           struct hti_transport_frame *transport,
                           struct hti_frame           *frame );

double hti_temperature_at( const struct hti_frame  *frame,
                           enum hti_correction_mode correction,
                           int                      index );
void   hti_prepare_frame( struct hti_frame *frame );
void   hti_compute_stats( const struct hti_frame       *frame,
                          enum hti_correction_mode      correction,
                          struct hti_temperature_stats *stats );
void   hti_compute_display_range( const struct hti_temperature_stats *stats,
                                  bool                                manual_scale,
                                  double                              manual_min_c,
                                  double                              manual_max_c,
                                  struct hti_display_range           *range );

void hti_palette_map( enum hti_palette_mode palette, double normalized,
                      uint8_t *r, uint8_t *g, uint8_t *b );

int  hti_renderer_init( struct hti_renderer *renderer, int window_scale );
void hti_renderer_shutdown( struct hti_renderer *renderer );
void hti_renderer_present( struct hti_renderer                *renderer,
                           const struct hti_frame             *frame,
                           const struct hti_temperature_stats *stats,
                           const struct hti_options           *opt,
                           const struct hti_display_range     *range,
                           bool                                paused );

int hti_save_frame( const char                         *directory,
                    const struct hti_transport_frame   *transport,
                    const struct hti_frame             *frame,
                    const struct hti_temperature_stats *stats,
                    enum hti_correction_mode            correction,
                    enum hti_palette_mode               palette,
                    const struct hti_display_range     *range );

#endif
