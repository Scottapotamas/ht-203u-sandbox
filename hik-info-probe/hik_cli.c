#include "hik_probe.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static bool parse_u16( const char *text, uint16_t *out )
{
    char         *end   = NULL;
    unsigned long value = strtoul( text, &end, 0 );

    if( text[0] == '\0' || end == text || *end != '\0' || value > UINT16_MAX )
    {
        return false;
    }

    *out = (uint16_t)value;
    return true;
}

static bool parse_u8( const char *text, uint8_t *out )
{
    uint16_t value = 0;

    if( !parse_u16( text, &value ) || value > UINT8_MAX )
    {
        return false;
    }

    *out = (uint8_t)value;
    return true;
}

static bool parse_uint( const char *text, unsigned int *out )
{
    char         *end   = NULL;
    unsigned long value = strtoul( text, &end, 0 );

    if( text[0] == '\0' || end == text || *end != '\0' || value > UINT32_MAX )
    {
        return false;
    }

    *out = (unsigned int)value;
    return true;
}

static bool parse_u8_range( const char *text, uint8_t *start_out, uint8_t *end_out )
{
    const char *colon = strchr( text, ':' );
    char        start_buf[32];
    uint8_t     start = 0;
    uint8_t     end   = 0;

    if( colon == NULL )
    {
        return false;
    }

    if( (size_t)( colon - text ) >= sizeof( start_buf ) )
    {
        return false;
    }

    memcpy( start_buf, text, (size_t)( colon - text ) );
    start_buf[colon - text] = '\0';

    if( !parse_u8( start_buf, &start ) || !parse_u8( colon + 1, &end ) )
    {
        return false;
    }

    if( start > end )
    {
        return false;
    }

    *start_out = start;
    *end_out   = end;
    return true;
}

static bool parse_hex_blob( const char *text,
                            uint8_t    *buf,
                            size_t      buf_capacity,
                            size_t     *len_out )
{
    char   compact[( HIK_MAX_SCAN_PAYLOAD_BYTES * 2 ) + 1];
    size_t compact_len = 0;

    for( size_t i = 0; text[i] != '\0'; ++i )
    {
        unsigned char c = (unsigned char)text[i];

        if( isspace( c ) || c == ',' || c == ':' || c == '-' )
        {
            continue;
        }
        if( !isxdigit( c ) )
        {
            return false;
        }
        if( compact_len + 1 >= sizeof( compact ) )
        {
            return false;
        }
        compact[compact_len++] = (char)c;
    }

    if( compact_len == 0 || ( compact_len % 2 ) != 0 )
    {
        return false;
    }

    *len_out = compact_len / 2;
    if( *len_out > buf_capacity )
    {
        return false;
    }

    for( size_t i = 0; i < *len_out; ++i )
    {
        char          byte_text[3];
        char         *end   = NULL;
        unsigned long value = 0;

        byte_text[0] = compact[i * 2];
        byte_text[1] = compact[i * 2 + 1];
        byte_text[2] = '\0';
        value        = strtoul( byte_text, &end, 16 );
        if( end == byte_text || *end != '\0' || value > UINT8_MAX )
        {
            return false;
        }
        buf[i] = (uint8_t)value;
    }

    return true;
}

static bool parse_transport( const char *text, HikTransportKind *out )
{
    if( strcmp( text, "auto" ) == 0 )
    {
        *out = HIK_TRANSPORT_AUTO;
        return true;
    }
    if( strcmp( text, "libusb" ) == 0 )
    {
        *out = HIK_TRANSPORT_LIBUSB;
        return true;
    }
    if( strcmp( text, "v4l2" ) == 0 )
    {
        *out = HIK_TRANSPORT_V4L2;
        return true;
    }
    return false;
}

void hik_usage( FILE *stream, const char *argv0 )
{
    size_t            profile_count = 0;
    const HikProfile *profiles      = hik_get_profiles( &profile_count );

    fprintf( stream,
             "Usage: %s [options]\n"
             "\n"
             "Reproduce a small set of confirmed UVC extension-unit controls from\n"
             "the paired Android USB capture. Default profile: %s.\n"
             "\n"
             "Options:\n"
             "  --profile NAME      Named operation profile (default: %s)\n"
             "  --transport KIND    Transport: auto, libusb, or v4l2 (default: auto)\n"
             "  --video-device PATH V4L2 device for UVCIOC_CTRL_QUERY writes, e.g. /dev/video0\n"
             "  --value N           Scalar value for brightness/contrast profiles (0-255)\n"
             "  --palette NAME|N    Palette for image-enhancement-set, e.g. white-hot or 0x02\n"
             "  --noise-reduce-mode N        Enhancement byte 1\n"
             "  --frame-noise-level N        Enhancement byte 3\n"
             "  --interframe-noise-level N   Enhancement byte 4\n"
             "  --lse-detail-enabled 0|1     Enhancement byte 6\n"
             "  --lse-detail-level N         Enhancement byte 7\n"
             "  --bird-watching-mode 0|1     Enhancement byte 17\n"
             "  --highlight-mode N           Enhancement byte 18\n"
             "  --highlight-level N          Enhancement byte 19\n"
             "  --hook-edge-mode N           Enhancement byte 20\n"
             "  --hook-edge-level N          Enhancement byte 21\n"
             "  --agc-mode N                 Enhancement byte 23\n"
             "  --vid HEX           USB vendor ID (default: 0x%04x)\n"
             "  --pid HEX           USB product ID (default: 0x%04x)\n"
             "  --interface N       VideoControl interface number (default: %u)\n"
             "  --unit HEX          UVC extension unit ID (default: 0x%02x)\n"
             "  --selector HEX      Control selector (default: 0x%02x)\n"
             "  --timeout-ms N      Control transfer timeout (default: %u)\n"
             "  --raw-out PATH      Save the raw blob to PATH\n"
             "  --force-len N       Override GET_LEN-derived size for experimental reads/writes\n"
             "  --scan-prewrite-selector HEX  Selector written during selector-state-scan\n"
             "  --scan-target-selector HEX    Selector read during selector-state-scan\n"
             "  --scan-prefix-hex HEXBYTES    Fixed prefix before the swept scan byte\n"
             "  --scan-range A:B             Inclusive scan byte range, e.g. 0:15 or 0x00:0xff\n"
             "  --scan-read-cur              Also issue GET_CUR for each scan candidate\n"
             "  --no-auto-detach    Do not auto-detach any bound kernel driver\n"
             "  --no-paired-init    Skip the selector-0x04/0x05 preamble seen\n"
             "                      before the successful blob read in the paired capture\n"
             "  --verbose           Enable libusb debug logging\n"
             "  -h, --help          Show this help\n",
             argv0,
             HIK_DEFAULT_PROFILE_NAME,
             HIK_DEFAULT_PROFILE_NAME,
             HIK_DEFAULT_VID,
             HIK_DEFAULT_PID,
             HIK_DEFAULT_INTERFACE,
             HIK_DEFAULT_UNIT,
             HIK_DEFAULT_SELECTOR,
             HIK_DEFAULT_TIMEOUT_MS );

    fputs( "\nProfiles:\n", stream );
    for( size_t i = 0; i < profile_count; ++i )
    {
        fprintf( stream, "  %-18s %s\n", profiles[i].name, profiles[i].description );
    }
}

void hik_options_init( HikOptions *opts )
{
    *opts = (HikOptions) {
        .profile_name           = HIK_DEFAULT_PROFILE_NAME,
        .vid                    = HIK_DEFAULT_VID,
        .pid                    = HIK_DEFAULT_PID,
        .interface_number       = HIK_DEFAULT_INTERFACE,
        .unit_id                = HIK_DEFAULT_UNIT,
        .selector               = HIK_DEFAULT_SELECTOR,
        .transport              = HIK_TRANSPORT_AUTO,
        .timeout_ms             = HIK_DEFAULT_TIMEOUT_MS,
        .auto_detach            = true,
        .paired_init            = true,
        .scan_prewrite_selector = HIK_PRELUDE_SELECTOR,
        .scan_target_selector   = HIK_ENHANCEMENT_SELECTOR,
    };
}

bool hik_parse_args( int argc, char **argv, HikOptions *opts )
{
    for( int i = 1; i < argc; ++i )
    {
        const char *arg = argv[i];

        if( strcmp( arg, "-h" ) == 0 || strcmp( arg, "--help" ) == 0 )
        {
            hik_usage( stdout, argv[0] );
            exit( 0 );
        }
        if( strcmp( arg, "--profile" ) == 0 && i + 1 < argc )
        {
            opts->profile_name = argv[++i];
            opts->profile_set  = true;
            continue;
        }
        if( strcmp( arg, "--transport" ) == 0 && i + 1 < argc )
        {
            if( !parse_transport( argv[++i], &opts->transport ) )
            {
                fprintf( stderr, "error: invalid --transport value: %s\n", argv[i] );
                return false;
            }
            continue;
        }
        if( strcmp( arg, "--video-device" ) == 0 && i + 1 < argc )
        {
            opts->video_device = argv[++i];
            continue;
        }
        if( strcmp( arg, "--value" ) == 0 && i + 1 < argc )
        {
            if( !parse_u8( argv[++i], &opts->value ) )
            {
                fprintf( stderr, "error: invalid --value: %s\n", argv[i] );
                return false;
            }
            opts->value_set = true;
            continue;
        }
        if( strcmp( arg, "--palette" ) == 0 && i + 1 < argc )
        {
            if( !hik_parse_palette_value( argv[++i], &opts->palette ) )
            {
                fprintf( stderr, "error: invalid --palette: %s\n", argv[i] );
                return false;
            }
            opts->palette_set = true;
            continue;
        }
        if( strcmp( arg, "--noise-reduce-mode" ) == 0 && i + 1 < argc )
        {
            if( !parse_u8( argv[++i], &opts->noise_reduce_mode ) )
            {
                fprintf( stderr, "error: invalid --noise-reduce-mode: %s\n", argv[i] );
                return false;
            }
            opts->noise_reduce_mode_set = true;
            continue;
        }
        if( strcmp( arg, "--frame-noise-level" ) == 0 && i + 1 < argc )
        {
            if( !parse_u8( argv[++i], &opts->frame_noise_level ) )
            {
                fprintf( stderr, "error: invalid --frame-noise-level: %s\n", argv[i] );
                return false;
            }
            opts->frame_noise_level_set = true;
            continue;
        }
        if( strcmp( arg, "--interframe-noise-level" ) == 0 && i + 1 < argc )
        {
            if( !parse_u8( argv[++i], &opts->interframe_noise_level ) )
            {
                fprintf( stderr, "error: invalid --interframe-noise-level: %s\n", argv[i] );
                return false;
            }
            opts->interframe_noise_level_set = true;
            continue;
        }
        if( strcmp( arg, "--lse-detail-enabled" ) == 0 && i + 1 < argc )
        {
            if( !parse_u8( argv[++i], &opts->lse_detail_enabled ) )
            {
                fprintf( stderr, "error: invalid --lse-detail-enabled: %s\n", argv[i] );
                return false;
            }
            opts->lse_detail_enabled_set = true;
            continue;
        }
        if( strcmp( arg, "--lse-detail-level" ) == 0 && i + 1 < argc )
        {
            if( !parse_u8( argv[++i], &opts->lse_detail_level ) )
            {
                fprintf( stderr, "error: invalid --lse-detail-level: %s\n", argv[i] );
                return false;
            }
            opts->lse_detail_level_set = true;
            continue;
        }
        if( strcmp( arg, "--bird-watching-mode" ) == 0 && i + 1 < argc )
        {
            if( !parse_u8( argv[++i], &opts->bird_watching_mode ) )
            {
                fprintf( stderr, "error: invalid --bird-watching-mode: %s\n", argv[i] );
                return false;
            }
            opts->bird_watching_mode_set = true;
            continue;
        }
        if( strcmp( arg, "--highlight-mode" ) == 0 && i + 1 < argc )
        {
            if( !parse_u8( argv[++i], &opts->highlight_mode ) )
            {
                fprintf( stderr, "error: invalid --highlight-mode: %s\n", argv[i] );
                return false;
            }
            opts->highlight_mode_set = true;
            continue;
        }
        if( strcmp( arg, "--highlight-level" ) == 0 && i + 1 < argc )
        {
            if( !parse_u8( argv[++i], &opts->highlight_level ) )
            {
                fprintf( stderr, "error: invalid --highlight-level: %s\n", argv[i] );
                return false;
            }
            opts->highlight_level_set = true;
            continue;
        }
        if( strcmp( arg, "--hook-edge-mode" ) == 0 && i + 1 < argc )
        {
            if( !parse_u8( argv[++i], &opts->hook_edge_mode ) )
            {
                fprintf( stderr, "error: invalid --hook-edge-mode: %s\n", argv[i] );
                return false;
            }
            opts->hook_edge_mode_set = true;
            continue;
        }
        if( strcmp( arg, "--hook-edge-level" ) == 0 && i + 1 < argc )
        {
            if( !parse_u8( argv[++i], &opts->hook_edge_level ) )
            {
                fprintf( stderr, "error: invalid --hook-edge-level: %s\n", argv[i] );
                return false;
            }
            opts->hook_edge_level_set = true;
            continue;
        }
        if( strcmp( arg, "--agc-mode" ) == 0 && i + 1 < argc )
        {
            if( !parse_u8( argv[++i], &opts->agc_mode ) )
            {
                fprintf( stderr, "error: invalid --agc-mode: %s\n", argv[i] );
                return false;
            }
            opts->agc_mode_set = true;
            continue;
        }
        if( strcmp( arg, "--vid" ) == 0 && i + 1 < argc )
        {
            if( !parse_u16( argv[++i], &opts->vid ) )
            {
                fprintf( stderr, "error: invalid --vid value: %s\n", argv[i] );
                return false;
            }
            continue;
        }
        if( strcmp( arg, "--pid" ) == 0 && i + 1 < argc )
        {
            if( !parse_u16( argv[++i], &opts->pid ) )
            {
                fprintf( stderr, "error: invalid --pid value: %s\n", argv[i] );
                return false;
            }
            continue;
        }
        if( strcmp( arg, "--interface" ) == 0 && i + 1 < argc )
        {
            if( !parse_u8( argv[++i], &opts->interface_number ) )
            {
                fprintf( stderr, "error: invalid --interface value: %s\n", argv[i] );
                return false;
            }
            continue;
        }
        if( strcmp( arg, "--unit" ) == 0 && i + 1 < argc )
        {
            if( !parse_u8( argv[++i], &opts->unit_id ) )
            {
                fprintf( stderr, "error: invalid --unit value: %s\n", argv[i] );
                return false;
            }
            continue;
        }
        if( strcmp( arg, "--selector" ) == 0 && i + 1 < argc )
        {
            if( !parse_u8( argv[++i], &opts->selector ) )
            {
                fprintf( stderr, "error: invalid --selector value: %s\n", argv[i] );
                return false;
            }
            opts->selector_set = true;
            continue;
        }
        if( strcmp( arg, "--timeout-ms" ) == 0 && i + 1 < argc )
        {
            if( !parse_uint( argv[++i], &opts->timeout_ms ) )
            {
                fprintf( stderr, "error: invalid --timeout-ms value: %s\n", argv[i] );
                return false;
            }
            continue;
        }
        if( strcmp( arg, "--raw-out" ) == 0 && i + 1 < argc )
        {
            opts->raw_out_path = argv[++i];
            continue;
        }
        if( strcmp( arg, "--force-len" ) == 0 && i + 1 < argc )
        {
            if( !parse_u16( argv[++i], &opts->force_len ) || opts->force_len == 0 )
            {
                fprintf( stderr, "error: invalid --force-len value: %s\n", argv[i] );
                return false;
            }
            opts->force_len_set = true;
            continue;
        }
        if( strcmp( arg, "--scan-prewrite-selector" ) == 0 && i + 1 < argc )
        {
            if( !parse_u8( argv[++i], &opts->scan_prewrite_selector ) )
            {
                fprintf( stderr, "error: invalid --scan-prewrite-selector value: %s\n", argv[i] );
                return false;
            }
            opts->scan_prewrite_selector_set = true;
            continue;
        }
        if( strcmp( arg, "--scan-target-selector" ) == 0 && i + 1 < argc )
        {
            if( !parse_u8( argv[++i], &opts->scan_target_selector ) )
            {
                fprintf( stderr, "error: invalid --scan-target-selector value: %s\n", argv[i] );
                return false;
            }
            opts->scan_target_selector_set = true;
            continue;
        }
        if( strcmp( arg, "--scan-prefix-hex" ) == 0 && i + 1 < argc )
        {
            if( !parse_hex_blob( argv[++i],
                                 opts->scan_prefix,
                                 sizeof( opts->scan_prefix ),
                                 &opts->scan_prefix_len ) )
            {
                fprintf( stderr, "error: invalid --scan-prefix-hex value: %s\n", argv[i] );
                return false;
            }
            continue;
        }
        if( strcmp( arg, "--scan-range" ) == 0 && i + 1 < argc )
        {
            if( !parse_u8_range( argv[++i], &opts->scan_range_start, &opts->scan_range_end ) )
            {
                fprintf( stderr, "error: invalid --scan-range value: %s\n", argv[i] );
                return false;
            }
            opts->scan_range_set = true;
            continue;
        }
        if( strcmp( arg, "--scan-read-cur" ) == 0 )
        {
            opts->scan_read_cur = true;
            continue;
        }
        if( strcmp( arg, "--no-auto-detach" ) == 0 )
        {
            opts->auto_detach = false;
            continue;
        }
        if( strcmp( arg, "--no-paired-init" ) == 0 )
        {
            opts->paired_init = false;
            continue;
        }
        if( strcmp( arg, "--verbose" ) == 0 )
        {
            opts->verbose = true;
            continue;
        }

        fprintf( stderr, "error: unknown or incomplete argument: %s\n", arg );
        return false;
    }

    return true;
}

int hik_validate_options( const HikOptions *opts,
                          const HikProfile *profile,
                          HikTransportKind  transport )
{
    if( transport == HIK_TRANSPORT_V4L2 && opts->video_device == NULL )
    {
        fprintf( stderr, "error: --video-device is required for v4l2 transport\n" );
        return 2;
    }

    if( profile->kind != HIK_PROFILE_SCAN && ( opts->scan_prewrite_selector_set || opts->scan_target_selector_set || opts->scan_prefix_len != 0 || opts->scan_range_set || opts->scan_read_cur ) )
    {
        fprintf( stderr, "error: --scan-* options require --profile selector-state-scan\n" );
        return 2;
    }

    if( profile->kind == HIK_PROFILE_SCAN && !opts->scan_range_set )
    {
        fprintf( stderr, "error: selector-state-scan requires --scan-range\n" );
        return 2;
    }

    if( profile->write_shape == HIK_WRITE_SHAPE_ENHANCEMENT_PATCH && !hik_enhancement_overrides_requested( opts ) )
    {
        fprintf( stderr,
                 "error: %s requires at least one enhancement option such as --palette or --noise-reduce-mode\n",
                 profile->name );
        return 2;
    }

    return 0;
}
