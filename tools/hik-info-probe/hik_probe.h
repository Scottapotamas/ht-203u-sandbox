#ifndef HIK_PROBE_H
#define HIK_PROBE_H

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <libusb-1.0/libusb.h>

typedef enum
{
    HIK_DEFAULT_VID               = 0x2bdf,
    HIK_DEFAULT_PID               = 0x0102,
    HIK_DEFAULT_INTERFACE         = 0,
    HIK_DEFAULT_UNIT              = 0x0a,
    HIK_DEFAULT_SELECTOR          = 0x01,
    HIK_THERMOMETRY_SELECTOR      = 0x03,
    HIK_DEFAULT_TIMEOUT_MS        = 5000,
    HIK_ENHANCEMENT_SELECTOR      = 0x02,
    HIK_PROTOCOL_SELECTOR         = 0x04,
    HIK_PRELUDE_SELECTOR          = 0x05,
    HIK_GET_CUR                   = 0x81,
    HIK_SET_CUR                   = 0x01,
    HIK_GET_LEN                   = 0x85,
    HIK_EXPECTED_INFO_BLOB_LEN    = 484,
    HIK_EXPECTED_ENHANCEMENT_LEN  = 24,
    HIK_EXPECTED_CALIBRATION_LEN  = 14336,
    HIK_CALIBRATION_HEADER_LEN    = 5,
    HIK_CALIBRATION_CHUNK_LEN     = 512,
    HIK_V4L2_SELECTOR2_BUFFER_LEN = 31,
    HIK_XU_GET_BMREQUESTTYPE      = 0xa1,
    HIK_XU_SET_BMREQUESTTYPE      = 0x21,
    HIK_MAX_REASONABLE_BLOB       = 16384,
    HIK_MAX_SCAN_PAYLOAD_BYTES    = 32,
    HIK_MAX_PROFILE_PAYLOAD_BYTES = 512,
    HIK_THERMOMETRY_TYPED_LEN     = 69,
    HIK_THERMOMETRY_STRUCT_LEN    = 0x104
} HikProbeConstant;

typedef enum
{
    HIK_PROFILE_READ = 0,
    HIK_PROFILE_WRITE,
    HIK_PROFILE_SCAN
} HikProfileKind;

typedef enum
{
    HIK_TRANSPORT_AUTO = 0,
    HIK_TRANSPORT_LIBUSB,
    HIK_TRANSPORT_V4L2
} HikTransportKind;

typedef enum
{
    HIK_READ_OUTPUT_INFO_BLOB = 0,
    HIK_READ_OUTPUT_ASCII_STRING,
    HIK_READ_OUTPUT_IMAGE_ENHANCEMENT,
    HIK_READ_OUTPUT_THERMOMETRY_BASIC,
    HIK_READ_OUTPUT_THERMOMETRY_CALIBRATION
} HikReadOutputKind;

typedef enum
{
    HIK_WRITE_SHAPE_FIXED = 0,
    HIK_WRITE_SHAPE_SCALAR,
    HIK_WRITE_SHAPE_ENHANCEMENT_PATCH,
    HIK_WRITE_SHAPE_THERMOMETRY_PATCH,
    HIK_WRITE_SHAPE_EXPERT_CORRECTION_REPLAY,
    HIK_WRITE_SHAPE_TEMPERATURE_CORRECT_REPLAY,
    HIK_WRITE_SHAPE_EXPERT_REGIONS_ZERO
} HikWriteShapeKind;

typedef enum
{
    /*
     * Byte 0 remains 0x01 across the captured 24-byte selector-0x02
     * enhancement payloads. It likely identifies the selector-0x02
     * sub-channel rather than a user-facing control.
     */
    HIK_ENHANCEMENT_BYTE_CHANNEL = 0,

    /* Dynamic Android matrix captures show byte 1 changes with the noise mode. */
    HIK_ENHANCEMENT_BYTE_NOISE_REDUCE_MODE = 1,

    /* Bytes 3 and 4 track the frame and interframe noise sliders. */
    HIK_ENHANCEMENT_BYTE_FRAME_NOISE_LEVEL      = 3,
    HIK_ENHANCEMENT_BYTE_INTERFRAME_NOISE_LEVEL = 4,

    /* Palette capture diffs isolate palette changes to byte 5. */
    HIK_ENHANCEMENT_BYTE_PALETTE = 5,

    /* LSE detail enable and level move at bytes 6 and 7 in the matrix traces. */
    HIK_ENHANCEMENT_BYTE_LSE_DETAIL_ENABLED = 6,
    HIK_ENHANCEMENT_BYTE_LSE_DETAIL_LEVEL   = 7,

    /* Bird-watching mode toggles byte 17 in the captured setter payloads. */
    HIK_ENHANCEMENT_BYTE_BIRD_WATCHING_MODE = 17,

    /* Highlight mode and level map to bytes 18 and 19. */
    HIK_ENHANCEMENT_BYTE_HIGHLIGHT_MODE  = 18,
    HIK_ENHANCEMENT_BYTE_HIGHLIGHT_LEVEL = 19,

    /* Hook-edge mode and level map to bytes 20 and 21. */
    HIK_ENHANCEMENT_BYTE_HOOK_EDGE_MODE  = 20,
    HIK_ENHANCEMENT_BYTE_HOOK_EDGE_LEVEL = 21,

    /*
     * Android getter traces usually report byte 23 as 1 while setter traces
     * often send 2, so the AGC interpretation is still slightly ambiguous.
     */
    HIK_ENHANCEMENT_BYTE_AGC_MODE = 23
} HikEnhancementByteOffset;

typedef struct
{
    const char *name;
    uint8_t     value;
} HikPaletteValue;

typedef struct
{
    const char       *name;
    HikProfileKind    kind;
    uint8_t           selector;
    uint16_t          expected_len;
    bool              auto_paired_init;
    HikReadOutputKind read_output_kind;
    uint8_t           prewrite_selector;
    uint8_t           prewrite_payload[4];
    size_t            prewrite_payload_len;
    uint8_t           payload[4];
    size_t            payload_len;
    HikWriteShapeKind write_shape;
    uint8_t           default_value;
    const char       *description;
} HikProfile;

typedef struct
{
    const char      *profile_name;
    bool             profile_set;
    uint16_t         vid;
    uint16_t         pid;
    uint8_t          interface_number;
    uint8_t          unit_id;
    uint8_t          selector;
    bool             selector_set;
    HikTransportKind transport;
    const char      *video_device;
    bool             value_set;
    uint8_t          value;
    bool             palette_set;
    uint8_t          palette;
    bool             noise_reduce_mode_set;
    uint8_t          noise_reduce_mode;
    bool             frame_noise_level_set;
    uint8_t          frame_noise_level;
    bool             interframe_noise_level_set;
    uint8_t          interframe_noise_level;
    bool             lse_detail_enabled_set;
    uint8_t          lse_detail_enabled;
    bool             lse_detail_level_set;
    uint8_t          lse_detail_level;
    bool             bird_watching_mode_set;
    uint8_t          bird_watching_mode;
    bool             highlight_mode_set;
    uint8_t          highlight_mode;
    bool             highlight_level_set;
    uint8_t          highlight_level;
    bool             hook_edge_mode_set;
    uint8_t          hook_edge_mode;
    bool             hook_edge_level_set;
    uint8_t          hook_edge_level;
    bool             agc_mode_set;
    uint8_t          agc_mode;
    bool             thermometry_enabled_set;
    uint8_t          thermometry_enabled;
    bool             thermometry_display_max_set;
    uint8_t          thermometry_display_max;
    bool             thermometry_display_min_set;
    uint8_t          thermometry_display_min;
    bool             thermometry_display_avg_set;
    uint8_t          thermometry_display_avg;
    bool             thermometry_temperature_unit_set;
    uint8_t          thermometry_temperature_unit;
    bool             thermometry_temperature_range_set;
    uint8_t          thermometry_temperature_range;
    bool             thermometry_calibration_coeff_enabled_set;
    uint8_t          thermometry_calibration_coeff_enabled;
    bool             thermometry_calibration_coeff_set;
    uint32_t         thermometry_calibration_coeff;
    bool             thermometry_external_optics_window_correction_set;
    uint32_t         thermometry_external_optics_window_correction;
    bool             thermometry_emissivity_set;
    uint32_t         thermometry_emissivity;
    bool             thermometry_distance_unit_set;
    uint8_t          thermometry_distance_unit;
    bool             thermometry_distance_set;
    uint32_t         thermometry_distance;
    bool             thermometry_reflective_enable_set;
    uint8_t          thermometry_reflective_enable;
    bool             thermometry_reflective_temperature_set;
    uint32_t         thermometry_reflective_temperature;
    bool             thermometry_info_display_position_set;
    uint8_t          thermometry_info_display_position;
    bool             thermometry_stream_overlay_set;
    uint8_t          thermometry_stream_overlay;
    bool             thermometry_alert_set;
    uint32_t         thermometry_alert;
    bool             thermometry_alarm_set;
    uint32_t         thermometry_alarm;
    bool             thermometry_external_optics_transmit_set;
    uint32_t         thermometry_external_optics_transmit;
    bool             thermometry_display_center_set;
    uint8_t          thermometry_display_center;
    bool             thermometry_backcolor_set;
    uint8_t          thermometry_backcolor;
    bool             thermometry_show_alarm_color_set;
    uint8_t          thermometry_show_alarm_color;
    unsigned int     timeout_ms;
    bool             auto_detach;
    bool             paired_init;
    bool             verbose;
    const char      *raw_out_path;
    bool             force_len_set;
    uint16_t         force_len;
    bool             scan_prewrite_selector_set;
    uint8_t          scan_prewrite_selector;
    bool             scan_target_selector_set;
    uint8_t          scan_target_selector;
    uint8_t          scan_prefix[HIK_MAX_SCAN_PAYLOAD_BYTES - 1];
    size_t           scan_prefix_len;
    bool             scan_range_set;
    uint8_t          scan_range_start;
    uint8_t          scan_range_end;
    bool             scan_read_cur;
} HikOptions;

typedef struct
{
    HikTransportKind                transport;
    libusb_context                 *usb_context;
    libusb_device_handle           *usb_handle;
    struct libusb_device_descriptor usb_descriptor;
    bool                            usb_descriptor_valid;
} HikSession;

typedef struct
{
    uint8_t  raw[4];
    int      raw_len;
    uint16_t decoded_length;
} HikLengthInfo;

extern const char HIK_DEFAULT_PROFILE_NAME[];

void hik_usage( FILE *stream, const char *argv0 );
void hik_options_init( HikOptions *opts );
bool hik_parse_args( int argc, char **argv, HikOptions *opts );
int  hik_validate_options( const HikOptions *opts,
                           const HikProfile *profile,
                           HikTransportKind  transport );

const HikProfile *hik_get_profiles( size_t *count_out );
const HikProfile *hik_find_profile( const char *name );
const HikProfile *hik_resolve_profile( const HikOptions *opts, HikProfile *manual_profile_out );
const char       *hik_palette_name( uint8_t value );
bool              hik_parse_palette_value( const char *text, uint8_t *out );
bool              hik_build_profile_payload( const HikProfile *profile,
                                             const HikOptions *opts,
                                             uint8_t          *payload_out,
                                             size_t           *payload_len_out );

const char      *hik_transport_name( HikTransportKind transport );
const char      *hik_transport_error( HikTransportKind transport, int rc );
HikTransportKind hik_resolve_transport( const HikOptions *opts, const HikProfile *profile );
int              hik_open_session( HikSession *session, const HikOptions *opts, HikTransportKind transport );
void             hik_close_session( HikSession *session, const HikOptions *opts );
void             hik_print_connection_summary( const HikSession *session, const HikOptions *opts );
int              hik_fetch_control_length( const HikSession *session,
                                           const HikOptions *opts,
                                           uint8_t           selector,
                                           HikLengthInfo    *length_out );
int              hik_fetch_current_value( const HikSession *session,
                                          const HikOptions *opts,
                                          uint8_t           selector,
                                          uint8_t          *buf,
                                          uint16_t          len );
int              hik_set_current_value( const HikSession *session,
                                        const HikOptions *opts,
                                        uint8_t           selector,
                                        const uint8_t    *buf,
                                        uint16_t          len );
uint16_t         hik_select_reported_length( uint16_t reported_len, uint16_t expected_len );
void             hik_print_length_report( const char *label, const HikLengthInfo *length_info );

bool   hik_enhancement_overrides_requested( const HikOptions *opts );
void   hik_print_enhancement_summary( const uint8_t *data, size_t len );
size_t hik_apply_enhancement_patch( uint8_t *buf, size_t len, const HikOptions *opts );

bool   hik_normalize_thermometry_payload( const uint8_t *data, size_t len, uint8_t *typed_out );
bool   hik_thermometry_overrides_requested( const HikOptions *opts );
void   hik_print_thermometry_summary( const uint8_t *data, size_t len );
size_t hik_apply_thermometry_patch( uint8_t *buf, size_t len, const HikOptions *opts );

int hik_execute_profile( const HikSession *session,
                         const HikOptions *opts,
                         const HikProfile *profile );

#endif
