#include "hti_usb_view.h"

#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL2/SDL.h>

enum
{
    HTI_STATUS_INTERVAL_MS = 1000,
};

struct hti_live_state
{
    bool     have_previous_frame;
    bool     manual_trigger_pending;
    bool     ffc_window_active;
    bool     current_ffc_manual;
    bool     replay_seen;
    uint64_t last_frame_monotonic_ns;
    uint64_t ffc_window_start_ns;
    uint64_t stable_run_start_ns;
    uint64_t last_completed_stable_run_ns;
    uint16_t previous_r0_w20;
    uint16_t previous_r0_w25;
    uint16_t previous_r0_w03;
    uint32_t previous_r1_u32_10;
    uint16_t previous_w2;
    uint16_t previous_w5;
    uint16_t previous_w8;
    uint16_t previous_w9;
    uint16_t previous_radiometric[HTI_IMAGE_PIXELS];
};

static volatile sig_atomic_t g_interrupted = 0;

static void on_signal( int signo )
{
    (void)signo;
    g_interrupted = 1;
}

static void die_msg( const char *msg )
{
    fprintf( stderr, "%s\n", msg );
    exit( EXIT_FAILURE );
}

static int parse_int( const char *s, const char *what )
{
    char *end   = NULL;
    long  value = strtol( s, &end, 10 );

    if( s == NULL || s[0] == '\0' || ( end != NULL && *end != '\0' ) )
    {
        fprintf( stderr, "invalid %s: %s\n", what, s );
        exit( EXIT_FAILURE );
    }
    if( value < -2147483647L || value > 2147483647L )
    {
        fprintf( stderr, "out of range %s: %s\n", what, s );
        exit( EXIT_FAILURE );
    }
    return (int)value;
}

static double parse_double( const char *s, const char *what )
{
    char  *end   = NULL;
    double value = strtod( s, &end );

    if( s == NULL || s[0] == '\0' || ( end != NULL && *end != '\0' ) )
    {
        fprintf( stderr, "invalid %s: %s\n", what, s );
        exit( EXIT_FAILURE );
    }
    return value;
}

static double snap_half_degree( double value )
{
    return round( value * 2.0 ) / 2.0;
}

static void snap_manual_scale_pair( struct hti_options *opt )
{
    opt->manual_min_c = snap_half_degree( opt->manual_min_c );
    opt->manual_max_c = snap_half_degree( opt->manual_max_c );
    if( opt->manual_max_c <= opt->manual_min_c )
    {
        opt->manual_max_c = opt->manual_min_c + 0.5;
    }
}

static void clamp_manual_min( struct hti_options *opt )
{
    opt->manual_min_c = snap_half_degree( opt->manual_min_c );
    if( opt->manual_min_c >= opt->manual_max_c )
    {
        opt->manual_min_c = opt->manual_max_c - 0.5;
    }
}

static void clamp_manual_max( struct hti_options *opt )
{
    opt->manual_max_c = snap_half_degree( opt->manual_max_c );
    if( opt->manual_max_c <= opt->manual_min_c )
    {
        opt->manual_max_c = opt->manual_min_c + 0.5;
    }
}

static enum hti_correction_mode parse_correction( const char *s )
{
    if( strcmp( s, "raw" ) == 0 )
    {
        return HTI_CORRECTION_RAW;
    }
    if( strcmp( s, "w20delta" ) == 0 )
    {
        return HTI_CORRECTION_W20DELTA;
    }
    if( strcmp( s, "w20+r1" ) == 0 || strcmp( s, "w20plusr1" ) == 0 || strcmp( s, "w20_plus_r1" ) == 0 )
    {
        return HTI_CORRECTION_W20_PLUS_R1;
    }
    if( strcmp( s, "lower-affine" ) == 0 || strcmp( s, "loweraffine" ) == 0 )
    {
        return HTI_CORRECTION_LOWER_AFFINE;
    }
    die_msg( "unknown correction: use raw, w20delta, w20+r1, or lower-affine" );

    return HTI_CORRECTION_RAW;
}

static enum hti_palette_mode parse_palette( const char *s )
{
    if( strcmp( s, "iron" ) == 0 )
    {
        return HTI_PALETTE_IRON;
    }
    if( strcmp( s, "gray" ) == 0 )
    {
        return HTI_PALETTE_GRAY;
    }
    die_msg( "unknown palette: use iron or gray" );

    return HTI_PALETTE_IRON;
}

static enum hti_overlay_mode parse_overlay( const char *s )
{
    if( strcmp( s, "none" ) == 0 )
    {
        return HTI_OVERLAY_NONE;
    }
    if( strcmp( s, "minimal" ) == 0 || strcmp( s, "min" ) == 0 )
    {
        return HTI_OVERLAY_MINIMAL;
    }
    if( strcmp( s, "debug" ) == 0 )
    {
        return HTI_OVERLAY_DEBUG;
    }
    die_msg( "unknown overlay: use none, minimal, or debug" );

    return HTI_OVERLAY_MINIMAL;
}

static enum hti_overlay_mode next_overlay( enum hti_overlay_mode overlay )
{
    switch( overlay )
    {
        case HTI_OVERLAY_NONE:
            return HTI_OVERLAY_MINIMAL;
        case HTI_OVERLAY_MINIMAL:
            return HTI_OVERLAY_DEBUG;
        case HTI_OVERLAY_DEBUG:
            return HTI_OVERLAY_NONE;
        case HTI_OVERLAY_COUNT:
            break;
    }
    return HTI_OVERLAY_MINIMAL;
}

static void print_usage( FILE *out )
{
    fprintf( out,
             "Usage: hti-usb-view [options]\n"
             "\n"
             "Source:\n"
             "  --prefix PATH            Open one saved capture prefix instead of live USB\n"
             "\n"
             "Display and thermometry:\n"
             "  --correction NAME        raw, w20delta, w20+r1, or lower-affine (default: w20delta)\n"
             "  --palette NAME           iron or gray (default: iron)\n"
             "  --overlay MODE           none, minimal, or debug (default: minimal)\n"
             "  --min C                  Manual minimum display temperature\n"
             "  --max C                  Manual maximum display temperature\n"
             "  --window-scale N         Initial SDL window scale (default: 4)\n"
             "  --headless               Skip SDL and print status in the terminal only\n"
             "  --frames N               Stop after N decoded frames (default: unlimited)\n"
             "  --save-dir DIR           Save frames into DIR when requested, or automatically in headless mode\n"
             "\n"
             "Live USB stream:\n"
             "  --format-index N         UVC format index (default: 1)\n"
             "  --frame-index N          UVC frame index (default: 1)\n"
             "  --frame-interval N       Frame interval in 100ns units (default: 400000)\n"
             "  --trigger-before-start   Trigger shutter once before live acquisition starts\n"
             "  --trigger-after N        Trigger shutter after N decoded live frames\n"
             "  --timeout-ms N           Per-transfer timeout in milliseconds (default: 5000)\n"
             "  --verbose                Enable extra USB logs and detailed status output\n"
             "\n"
             "Runtime keys:\n"
             "  q / Esc                  Quit\n"
             "  space                    Pause or resume live capture\n"
             "  s                        Trigger shutter / FFC / NUC in live mode\n"
             "  c                        Cycle correction mode\n"
             "  o                        Cycle overlay mode\n"
             "  p                        Cycle palette\n"
             "  a                        Toggle auto and manual scale\n"
             "  [ / ]                    Lower or raise manual minimum by 0.5C\n"
             "  - / =                    Lower or raise manual maximum by 0.5C\n"
             "  w                        Save the current frame\n" );
}

static void default_options( struct hti_options *opt )
{
    memset( opt, 0, sizeof( *opt ) );
    opt->correction                          = HTI_CORRECTION_W20DELTA;
    opt->palette                             = HTI_PALETTE_IRON;
    opt->overlay                             = HTI_OVERLAY_MINIMAL;
    opt->window_scale                        = 4;
    opt->frames                              = 0;
    opt->trigger_after_frames                = 0;
    opt->stream_request.format_index         = 1;
    opt->stream_request.frame_index          = 1;
    opt->stream_request.frame_interval_100ns = 400000u;
    opt->stream_request.timeout_ms           = HTI_DEFAULT_TIMEOUT_MS;
}

static void parse_args( int argc, char **argv, struct hti_options *opt )
{
    int have_min = 0;
    int have_max = 0;

    default_options( opt );

    for( int i = 1; i < argc; i++ )
    {
        if( strcmp( argv[i], "--help" ) == 0 || strcmp( argv[i], "-h" ) == 0 )
        {
            print_usage( stdout );
            exit( EXIT_SUCCESS );
        }
        else if( strcmp( argv[i], "--prefix" ) == 0 )
        {
            if( ++i >= argc )
            {
                die_msg( "--prefix needs a path" );
            }
            opt->prefix = argv[i];
        }
        else if( strcmp( argv[i], "--correction" ) == 0 )
        {
            if( ++i >= argc )
            {
                die_msg( "--correction needs a value" );
            }
            opt->correction = parse_correction( argv[i] );
        }
        else if( strcmp( argv[i], "--palette" ) == 0 )
        {
            if( ++i >= argc )
            {
                die_msg( "--palette needs a value" );
            }
            opt->palette = parse_palette( argv[i] );
        }
        else if( strcmp( argv[i], "--overlay" ) == 0 )
        {
            if( ++i >= argc )
            {
                die_msg( "--overlay needs a value" );
            }
            opt->overlay = parse_overlay( argv[i] );
        }
        else if( strcmp( argv[i], "--min" ) == 0 )
        {
            if( ++i >= argc )
            {
                die_msg( "--min needs a value" );
            }
            opt->manual_min_c = parse_double( argv[i], "minimum temperature" );
            have_min          = 1;
        }
        else if( strcmp( argv[i], "--max" ) == 0 )
        {
            if( ++i >= argc )
            {
                die_msg( "--max needs a value" );
            }
            opt->manual_max_c = parse_double( argv[i], "maximum temperature" );
            have_max          = 1;
        }
        else if( strcmp( argv[i], "--window-scale" ) == 0 )
        {
            if( ++i >= argc )
            {
                die_msg( "--window-scale needs a value" );
            }
            opt->window_scale = parse_int( argv[i], "window scale" );
            if( opt->window_scale <= 0 )
            {
                die_msg( "--window-scale must be > 0" );
            }
        }
        else if( strcmp( argv[i], "--headless" ) == 0 )
        {
            opt->headless = true;
        }
        else if( strcmp( argv[i], "--frames" ) == 0 )
        {
            if( ++i >= argc )
            {
                die_msg( "--frames needs a value" );
            }
            opt->frames = parse_int( argv[i], "frame count" );
            if( opt->frames < 0 )
            {
                die_msg( "--frames must be >= 0" );
            }
        }
        else if( strcmp( argv[i], "--save-dir" ) == 0 )
        {
            if( ++i >= argc )
            {
                die_msg( "--save-dir needs a path" );
            }
            opt->save_dir = argv[i];
        }
        else if( strcmp( argv[i], "--format-index" ) == 0 )
        {
            if( ++i >= argc )
            {
                die_msg( "--format-index needs a value" );
            }
            opt->stream_request.format_index = (uint8_t)parse_int( argv[i], "format index" );
        }
        else if( strcmp( argv[i], "--frame-index" ) == 0 )
        {
            if( ++i >= argc )
            {
                die_msg( "--frame-index needs a value" );
            }
            opt->stream_request.frame_index = (uint8_t)parse_int( argv[i], "frame index" );
        }
        else if( strcmp( argv[i], "--frame-interval" ) == 0 )
        {
            if( ++i >= argc )
            {
                die_msg( "--frame-interval needs a value" );
            }
            opt->stream_request.frame_interval_100ns = (uint32_t)parse_int( argv[i], "frame interval" );
        }
        else if( strcmp( argv[i], "--trigger-after" ) == 0 )
        {
            if( ++i >= argc )
            {
                die_msg( "--trigger-after needs a value" );
            }
            opt->trigger_after_frames = parse_int( argv[i], "trigger frame count" );
            if( opt->trigger_after_frames < 0 )
            {
                die_msg( "--trigger-after must be >= 0" );
            }
        }
        else if( strcmp( argv[i], "--trigger-before-start" ) == 0 )
        {
            opt->trigger_before_start = true;
        }
        else if( strcmp( argv[i], "--timeout-ms" ) == 0 )
        {
            if( ++i >= argc )
            {
                die_msg( "--timeout-ms needs a value" );
            }
            opt->stream_request.timeout_ms = parse_int( argv[i], "timeout" );
            if( opt->stream_request.timeout_ms <= 0 )
            {
                die_msg( "--timeout-ms must be > 0" );
            }
        }
        else if( strcmp( argv[i], "--verbose" ) == 0 )
        {
            opt->verbose = true;
        }
        else
        {
            fprintf( stderr, "unknown argument: %s\n", argv[i] );
            print_usage( stderr );
            exit( EXIT_FAILURE );
        }
    }

    if( have_min != have_max )
    {
        die_msg( "set both --min and --max together" );
    }

    opt->manual_scale = have_min && have_max;
    if( opt->manual_scale && opt->manual_max_c <= opt->manual_min_c )
    {
        die_msg( "--max must be greater than --min" );
    }

    if( opt->manual_scale )
    {
        snap_manual_scale_pair( opt );
    }
}

static uint64_t monotonic_ms( void )
{
    struct timespec ts;

    if( clock_gettime( CLOCK_MONOTONIC, &ts ) != 0 )
    {
        return 0;
    }

    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)( ts.tv_nsec / 1000000u );
}

static void sleep_ms( long milliseconds )
{
    struct timespec delay;

    if( milliseconds <= 0 )
    {
        return;
    }

    delay.tv_sec  = milliseconds / 1000;
    delay.tv_nsec = ( milliseconds % 1000 ) * 1000000L;
    nanosleep( &delay, NULL );
}

static void print_runtime_help( bool live_mode )
{
    fprintf( stderr,
             "viewer controls: q/Esc quit, c cycle correction, o cycle overlay, p cycle palette, a toggle auto/manual scale,\n"
             "                 [ and ] adjust min by 0.5C, - and = adjust max by 0.5C, w save current frame%s\n",
             live_mode ? ", space pause, s shutter" : "" );
}

static void print_compact_status_line( const struct hti_frame             *frame,
                                       const struct hti_temperature_stats *stats,
                                       const struct hti_display_range     *range,
                                       const struct hti_options           *opt,
                                       bool                                paused )
{
    fprintf( stdout, "%s", frame->name );
    if( frame->quantitative_valid )
    {
        fprintf( stdout,
                 " hot=%.1fC cold=%.1fC ctr=%.1fC avg=%.1fC scale=%.1f..%.1fC",
                 stats->max_c,
                 stats->min_c,
                 stats->center_c,
                 stats->mean_c,
                 range->min_c,
                 range->max_c );
    }
    else
    {
        fprintf( stdout, " quantitative=masked scale=%.1f..%.1fC", range->min_c, range->max_c );
    }

    if( opt->correction != HTI_CORRECTION_W20DELTA )
    {
        fprintf( stdout, " corr=%s", hti_correction_name( opt->correction ) );
    }

    if( opt->palette != HTI_PALETTE_IRON )
    {
        fprintf( stdout, " palette=%s", hti_palette_name( opt->palette ) );
    }

    if( opt->manual_scale )
    {
        fprintf( stdout, " manual" );
    }

    if( frame->ffc_state == HTI_FFC_TRIGGERED && frame->ffc_manual )
    {
        fprintf( stdout, " ffc=manual" );
    }
    else if( frame->ffc_state != HTI_FFC_NONE )
    {
        fprintf( stdout, " ffc=%s", hti_ffc_state_name( frame->ffc_state ) );
    }

    if( frame->replay_detected )
    {
        fprintf( stdout, " replay" );
    }

    if( paused )
    {
        fprintf( stdout, " paused" );
    }

    fputc( '\n', stdout );
}

static void print_status_line( const struct hti_frame             *frame,
                               const struct hti_temperature_stats *stats,
                               const struct hti_display_range     *range,
                               const struct hti_options           *opt,
                               bool                                paused )
{
    const char *quant_state    = frame->quantitative_valid ? "valid" : "masked";
    double      stable_ms      = (double)frame->stable_run_ns / 1000000.0;
    double      prev_stable_ms = (double)frame->previous_stable_run_ns / 1000000.0;
    double      ffc_ms         = (double)frame->ffc_window_ns / 1000000.0;

    if( !opt->verbose )
    {
        print_compact_status_line( frame, stats, range, opt, paused );
        fflush( stdout );
        return;
    }

    if( frame->quantitative_valid )
    {
        fprintf( stdout,
                 "%s hot=%.2fC@%d,%d cold=%.2fC@%d,%d center=%.2fC mean=%.2fC scale=%.2f..%.2fC %s corr=%s palette=%s layout=%s quant=%s ffc=%s manual=%s replay=%s stable=%.1fms prev_stable=%.1fms ffc_age=%.1fms current_baseline/w20=%u previous_baseline/w25=%u internal_state/r1_10=%u%s\n",
                 frame->name,
                 stats->max_c,
                 stats->max_x,
                 stats->max_y,
                 stats->min_c,
                 stats->min_x,
                 stats->min_y,
                 stats->center_c,
                 stats->mean_c,
                 range->min_c,
                 range->max_c,
                 opt->manual_scale ? "manual" : "auto",
                 hti_correction_name( opt->correction ),
                 hti_palette_name( opt->palette ),
                 hti_frame_layout_name( frame->layout ),
                 quant_state,
                 hti_ffc_state_name( frame->ffc_state ),
                 frame->ffc_manual ? "yes" : "no",
                 frame->replay_detected ? "yes" : "no",
                 stable_ms,
                 prev_stable_ms,
                 ffc_ms,
                 frame->footer.r0_w20,
                 frame->footer.r0_w25,
                 frame->footer.r1_u32_10,
                 paused ? " paused" : "" );
    }
    else
    {
        fprintf( stdout,
                 "%s quantitative=masked scale=%.2f..%.2fC %s corr=%s palette=%s layout=%s ffc=%s manual=%s replay=%s stable=%.1fms prev_stable=%.1fms ffc_age=%.1fms current_baseline/w20=%u previous_baseline/w25=%u internal_state/r1_10=%u%s\n",
                 frame->name,
                 range->min_c,
                 range->max_c,
                 opt->manual_scale ? "manual" : "auto",
                 hti_correction_name( opt->correction ),
                 hti_palette_name( opt->palette ),
                 hti_frame_layout_name( frame->layout ),
                 hti_ffc_state_name( frame->ffc_state ),
                 frame->ffc_manual ? "yes" : "no",
                 frame->replay_detected ? "yes" : "no",
                 stable_ms,
                 prev_stable_ms,
                 ffc_ms,
                 frame->footer.r0_w20,
                 frame->footer.r0_w25,
                 frame->footer.r1_u32_10,
                 paused ? " paused" : "" );
    }

    if( frame->display_footer.valid )
    {
        double lower_min_c    = hti_lower_footer_min_c( frame );
        double lower_max_c    = hti_lower_footer_max_c( frame );
        double lower_mean_c   = hti_lower_footer_mean_c( frame );
        double lower_center_c = hti_lower_footer_center_c( frame );

        fprintf( stdout,
                 "    lower footer: proc_min/w5=%u proc_max/w2=%u proc_mean/w8=%u proc_center/w9=%u\n",
                 frame->display_footer.w5,
                 frame->display_footer.w2,
                 frame->display_footer.w8,
                 frame->display_footer.w9 );
        fprintf( stdout,
                 "    lower refs:   min=%.2fC max=%.2fC mean=%.2fC center=%.2fC lower_affine=%s\n",
                 lower_min_c,
                 lower_max_c,
                 lower_mean_c,
                 lower_center_c,
                 hti_lower_affine_kind_name( frame->lower_affine_kind ) );
    }

    fflush( stdout );
}

static void seed_manual_scale( const struct hti_display_range *range, struct hti_options *opt )
{
    opt->manual_min_c = range->min_c;
    opt->manual_max_c = range->max_c;
    snap_manual_scale_pair( opt );
    opt->manual_scale = true;
}

static const char *save_dir_or_default( const struct hti_options *opt )
{
    return opt->save_dir != NULL ? opt->save_dir : "viewer-captures";
}

static uint64_t monotonic_ns( void )
{
    struct timespec ts;

    if( clock_gettime( CLOCK_MONOTONIC, &ts ) != 0 )
    {
        return 0;
    }

    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static bool frame_footer_changed( const struct hti_live_state *state, const struct hti_frame *frame )
{
    if( !state->have_previous_frame )
    {
        return false;
    }

    return frame->footer.r0_w20 != state->previous_r0_w20 || frame->footer.r0_w25 != state->previous_r0_w25 || frame->footer.r0_w03 != state->previous_r0_w03 || frame->footer.r1_u32_10 != state->previous_r1_u32_10 || frame->display_footer.w2 != state->previous_w2 || frame->display_footer.w5 != state->previous_w5 || frame->display_footer.w8 != state->previous_w8 || frame->display_footer.w9 != state->previous_w9;
}

static bool frame_upper_baseline_changed( const struct hti_live_state *state, const struct hti_frame *frame )
{
    if( !state->have_previous_frame )
    {
        return false;
    }

    return frame->footer.r0_w20 != state->previous_r0_w20 || frame->footer.r0_w25 != state->previous_r0_w25 || frame->footer.r0_w03 != state->previous_r0_w03;
}

static bool frame_radiometric_replay( const struct hti_live_state *state, const struct hti_frame *frame )
{
    if( !state->have_previous_frame )
    {
        return false;
    }

    return memcmp( frame->radiometric, state->previous_radiometric, sizeof( state->previous_radiometric ) ) == 0;
}

static void update_live_frame_state( struct hti_live_state *state, struct hti_frame *frame )
{
    uint64_t now_ns                 = monotonic_ns();
    bool     footer_changed         = frame_footer_changed( state, frame );
    bool     upper_baseline_changed = frame_upper_baseline_changed( state, frame );
    bool     replay_detected        = frame_radiometric_replay( state, frame );
    bool     start_ffc_window       = false;
    bool     manual_ffc             = false;

    frame->capture_monotonic_ns   = now_ns;
    frame->frame_delta_ns         = state->last_frame_monotonic_ns > 0 && now_ns >= state->last_frame_monotonic_ns
                                        ? now_ns - state->last_frame_monotonic_ns
                                        : 0;
    frame->footer_changed         = footer_changed;
    frame->replay_detected        = replay_detected;
    frame->ffc_state              = HTI_FFC_NONE;
    frame->ffc_manual             = false;
    frame->quantitative_valid     = true;
    frame->ffc_window_ns          = 0;
    frame->stable_run_ns          = 0;
    frame->previous_stable_run_ns = state->last_completed_stable_run_ns;

    if( !state->ffc_window_active && state->manual_trigger_pending )
    {
        start_ffc_window = true;
        manual_ffc       = true;
    }
    else if( !state->ffc_window_active && upper_baseline_changed )
    {
        start_ffc_window = true;
        manual_ffc       = false;
    }

    if( start_ffc_window )
    {
        if( state->stable_run_start_ns > 0 && now_ns >= state->stable_run_start_ns )
        {
            state->last_completed_stable_run_ns = now_ns - state->stable_run_start_ns;
        }
        state->stable_run_start_ns    = 0;
        state->ffc_window_active      = true;
        state->current_ffc_manual     = manual_ffc;
        state->ffc_window_start_ns    = now_ns;
        state->manual_trigger_pending = false;
        frame->previous_stable_run_ns = state->last_completed_stable_run_ns;
    }

    if( state->ffc_window_active )
    {
        frame->ffc_manual         = state->current_ffc_manual;
        frame->ffc_window_ns      = now_ns >= state->ffc_window_start_ns ? now_ns - state->ffc_window_start_ns : 0;
        frame->quantitative_valid = false;
        if( replay_detected )
        {
            frame->ffc_state   = HTI_FFC_REPLAY;
            state->replay_seen = true;
        }
        else if( state->replay_seen )
        {
            frame->ffc_state           = HTI_FFC_RECOVERED;
            state->ffc_window_active   = false;
            state->replay_seen         = false;
            state->stable_run_start_ns = now_ns;
            frame->quantitative_valid  = true;
        }
        else
        {
            frame->ffc_state = HTI_FFC_TRIGGERED;
        }
    }

    if( !state->ffc_window_active && frame->ffc_state == HTI_FFC_NONE )
    {
        if( state->stable_run_start_ns == 0 )
        {
            state->stable_run_start_ns = now_ns;
        }
        frame->stable_run_ns = now_ns >= state->stable_run_start_ns ? now_ns - state->stable_run_start_ns : 0;
    }
    else if( frame->ffc_state == HTI_FFC_RECOVERED )
    {
        frame->stable_run_ns = 0;
    }

    state->last_frame_monotonic_ns = now_ns;
    state->previous_r0_w20         = frame->footer.r0_w20;
    state->previous_r0_w25         = frame->footer.r0_w25;
    state->previous_r0_w03         = frame->footer.r0_w03;
    state->previous_r1_u32_10      = frame->footer.r1_u32_10;
    state->previous_w2             = frame->display_footer.w2;
    state->previous_w5             = frame->display_footer.w5;
    state->previous_w8             = frame->display_footer.w8;
    state->previous_w9             = frame->display_footer.w9;
    memcpy( state->previous_radiometric, frame->radiometric, sizeof( state->previous_radiometric ) );
    state->have_previous_frame = true;
}

static bool trigger_live_shutter( struct hti_usb_device *usb, const struct hti_options *opt )
{
    int  rc         = 0;
    bool trigger_ok = false;

    fprintf( stderr, "trying shutter trigger on the live USB handle\n" );
    rc = hti_usb_trigger_shutter( usb );
    if( rc == 0 )
    {
        fprintf( stderr, "shutter trigger sent on the live handle\n" );
        return true;
    }

    fprintf( stderr, "live-handle shutter trigger failed: %s\n", libusb_error_name( rc ) );
    fprintf( stderr, "restarting the live USB stream for a control-only shutter trigger\n" );
    hti_usb_close( usb );
    sleep_ms( 250 );

    rc = hti_usb_trigger_shutter_standalone( 0, 0, opt->stream_request.timeout_ms, opt->verbose );
    if( rc != 0 )
    {
        fprintf( stderr, "shutter trigger failed: %s\n", libusb_error_name( rc ) );
    }
    else
    {
        trigger_ok = true;
    }

    sleep_ms( 250 );
    rc = hti_usb_open( usb, 0, 0, &opt->stream_request, opt->verbose );
    if( rc != 0 )
    {
        fprintf( stderr, "failed to reopen live stream after shutter trigger: %s\n", libusb_error_name( rc ) );
        return false;
    }

    if( trigger_ok )
    {
        fprintf( stderr, "shutter trigger sent; live stream resumed\n" );
    }

    return trigger_ok;
}

int main( int argc, char **argv )
{
    struct hti_options           opt;
    struct hti_usb_device        usb;
    struct hti_renderer          renderer;
    struct hti_transport_frame   transport;
    struct hti_frame             frame;
    struct hti_temperature_stats stats;
    struct hti_display_range     range;
    struct hti_live_state        live_state;
    bool                         live_mode        = false;
    bool                         running          = true;
    bool                         paused           = false;
    bool                         have_frame       = false;
    bool                         pending_save     = false;
    uint64_t                     last_status_ms   = 0;
    int                          processed_frames = 0;

    parse_args( argc, argv, &opt );
    if( opt.prefix != NULL && opt.headless && opt.frames == 0 )
    {
        opt.frames = 1;
    }

    memset( &usb, 0, sizeof( usb ) );
    memset( &renderer, 0, sizeof( renderer ) );
    memset( &transport, 0, sizeof( transport ) );
    memset( &frame, 0, sizeof( frame ) );
    memset( &live_state, 0, sizeof( live_state ) );

    signal( SIGINT, on_signal );
    signal( SIGTERM, on_signal );

    live_mode = ( opt.prefix == NULL );
    if( live_mode )
    {
        int rc = hti_usb_open( &usb, 0, 0, &opt.stream_request, opt.verbose );

        if( rc != 0 )
        {
            fprintf( stderr, "failed to open camera: %s\n", libusb_error_name( rc ) );
            return EXIT_FAILURE;
        }
        if( opt.trigger_before_start )
        {
            if( trigger_live_shutter( &usb, &opt ) )
            {
                live_state.manual_trigger_pending = true;
            }
            else if( usb.handle == NULL )
            {
                hti_usb_close( &usb );
                return EXIT_FAILURE;
            }
        }
    }
    else
    {
        if( hti_load_saved_prefix( opt.prefix, &transport, &frame ) != 0 )
        {
            return EXIT_FAILURE;
        }

        frame.quantitative_valid = true;
        frame.ffc_state          = HTI_FFC_NONE;
        hti_prepare_frame( &frame );
        have_frame = true;
    }

    if( !opt.headless )
    {
        if( hti_renderer_init( &renderer, opt.window_scale ) != 0 )
        {
            hti_usb_close( &usb );
            return EXIT_FAILURE;
        }
        print_runtime_help( live_mode );
    }

    while( running && !g_interrupted )
    {
        bool frame_updated = false;
        bool ui_dirty      = false;

        if( !opt.headless )
        {
            SDL_Event ev;

            while( SDL_PollEvent( &ev ) )
            {
                if( ev.type == SDL_QUIT )
                {
                    running = false;
                }
                else if( ev.type == SDL_KEYDOWN )
                {
                    SDL_Keycode key = ev.key.keysym.sym;

                    if( key == SDLK_ESCAPE || key == SDLK_q )
                    {
                        running = false;
                    }
                    else if( live_mode && key == SDLK_SPACE )
                    {
                        paused   = !paused;
                        ui_dirty = true;
                    }
                    else if( live_mode && key == SDLK_s )
                    {
                        if( trigger_live_shutter( &usb, &opt ) )
                        {
                            live_state.manual_trigger_pending = true;
                        }
                        else if( usb.handle == NULL )
                        {
                            running = false;
                        }
                    }
                    else if( key == SDLK_c )
                    {
                        opt.correction = hti_next_correction( opt.correction );
                        ui_dirty       = true;
                    }
                    else if( key == SDLK_o )
                    {
                        opt.overlay = next_overlay( opt.overlay );
                        ui_dirty    = true;
                    }
                    else if( key == SDLK_p )
                    {
                        opt.palette = hti_next_palette( opt.palette );
                        ui_dirty    = true;
                    }
                    else if( key == SDLK_a )
                    {
                        if( have_frame && !opt.manual_scale )
                        {
                            hti_compute_stats( &frame, opt.correction, &stats );
                            hti_compute_display_range( &stats, false, 0.0, 0.0, &range );
                            seed_manual_scale( &range, &opt );
                        }
                        else
                        {
                            opt.manual_scale = !opt.manual_scale;
                        }
                        ui_dirty = true;
                    }
                    else if( key == SDLK_LEFTBRACKET )
                    {
                        if( have_frame && !opt.manual_scale )
                        {
                            hti_compute_stats( &frame, opt.correction, &stats );
                            hti_compute_display_range( &stats, false, 0.0, 0.0, &range );
                            seed_manual_scale( &range, &opt );
                        }
                        opt.manual_min_c -= 0.5;
                        clamp_manual_min( &opt );
                        ui_dirty = true;
                    }
                    else if( key == SDLK_RIGHTBRACKET )
                    {
                        if( have_frame && !opt.manual_scale )
                        {
                            hti_compute_stats( &frame, opt.correction, &stats );
                            hti_compute_display_range( &stats, false, 0.0, 0.0, &range );
                            seed_manual_scale( &range, &opt );
                        }
                        opt.manual_min_c += 0.5;
                        clamp_manual_min( &opt );
                        ui_dirty = true;
                    }
                    else if( key == SDLK_MINUS )
                    {
                        if( have_frame && !opt.manual_scale )
                        {
                            hti_compute_stats( &frame, opt.correction, &stats );
                            hti_compute_display_range( &stats, false, 0.0, 0.0, &range );
                            seed_manual_scale( &range, &opt );
                        }
                        opt.manual_max_c -= 0.5;
                        clamp_manual_max( &opt );
                        ui_dirty = true;
                    }
                    else if( key == SDLK_EQUALS )
                    {
                        if( have_frame && !opt.manual_scale )
                        {
                            hti_compute_stats( &frame, opt.correction, &stats );
                            hti_compute_display_range( &stats, false, 0.0, 0.0, &range );
                            seed_manual_scale( &range, &opt );
                        }
                        opt.manual_max_c += 0.5;
                        clamp_manual_max( &opt );
                        ui_dirty = true;
                    }
                    else if( key == SDLK_w )
                    {
                        pending_save = true;
                    }
                }
            }
        }

        if( live_mode && !paused )
        {
            int rc = hti_usb_read_frame( &usb, &transport );

            if( rc != 0 )
            {
                fprintf( stderr, "bulk read failed: %s\n", libusb_error_name( rc ) );
                break;
            }
            if( hti_decode_transport_frame( &transport, &frame ) != 0 )
            {
                fprintf( stderr, "warning: could not decode frame %llu\n",
                         (unsigned long long)transport.sequence );
                continue;
            }
            update_live_frame_state( &live_state, &frame );
            if( frame.ffc_state == HTI_FFC_TRIGGERED && !frame.ffc_manual )
            {
                fprintf( stderr,
                         "spontaneous correction detected after %.1f ms of stable frames\n",
                         (double)frame.previous_stable_run_ns / 1000000.0 );
            }
            hti_prepare_frame( &frame );
            frame_updated = true;
            have_frame    = true;
        }
        else if( !live_mode && have_frame )
        {
            frame_updated = ( processed_frames == 0 );
        }

        if( !have_frame )
        {
            if( !opt.headless )
                SDL_Delay( 10 );
            continue;
        }

        if( frame_updated || ui_dirty || pending_save )
        {
            hti_compute_stats( &frame, opt.correction, &stats );
            hti_compute_display_range( &stats,
                                       opt.manual_scale,
                                       opt.manual_min_c,
                                       opt.manual_max_c,
                                       &range );

            if( opt.headless || frame_updated || ui_dirty || monotonic_ms() - last_status_ms >= HTI_STATUS_INTERVAL_MS )
            {
                print_status_line( &frame, &stats, &range, &opt, paused );
                last_status_ms = monotonic_ms();
            }

            if( !opt.headless )
            {
                hti_renderer_present( &renderer,
                                      &frame,
                                      &stats,
                                      &opt,
                                      &range,
                                      paused );
            }

            if( ( opt.headless && opt.save_dir != NULL && frame_updated ) || pending_save )
            {
                if( hti_save_frame( save_dir_or_default( &opt ),
                                    &transport,
                                    &frame,
                                    &stats,
                                    opt.correction,
                                    opt.palette,
                                    &range ) != 0 )
                {
                    perror( "save frame" );
                }
                else
                {
                    fprintf( stderr, "saved frame to %s\n", save_dir_or_default( &opt ) );
                }
            }
            pending_save = false;
        }

        if( frame_updated )
        {
            if( live_mode && opt.trigger_after_frames > 0 && processed_frames + 1 == opt.trigger_after_frames )
            {
                if( trigger_live_shutter( &usb, &opt ) )
                {
                    live_state.manual_trigger_pending = true;
                }
                else if( usb.handle == NULL )
                {
                    running = false;
                }
            }
            processed_frames++;
            if( opt.frames > 0 && processed_frames >= opt.frames )
            {
                running = false;
            }
        }
        else if( !live_mode && !opt.headless )
        {
            SDL_Delay( 16 );
        }
    }

    if( !opt.headless )
    {
        hti_renderer_shutdown( &renderer );
    }

    if( live_mode )
    {
        hti_usb_close( &usb );
    }

    return g_interrupted ? EXIT_FAILURE : EXIT_SUCCESS;
}
