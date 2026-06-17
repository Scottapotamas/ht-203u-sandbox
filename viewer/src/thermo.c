#include "hti_usb_view.h"

#include <math.h>
#include <string.h>

enum
{
    /* Lower-footer affine fit degrades above ~120°C; scenes hotter than
     * this fall through to the min/mean fallback path instead of min/max. */
    HTI_LOWER_AFFINE_MAX_TRUST_C = 120,
};

static const double HTI_W5_TO_MIN_SLOPE    = 0.012144;
static const double HTI_W5_TO_MIN_BIAS     = -29.264595;
static const double HTI_W2_TO_MAX_SLOPE    = 0.018196;
static const double HTI_W2_TO_MAX_BIAS     = -59.332719;
static const double HTI_W8_TO_MEAN_SLOPE   = 0.017148;
static const double HTI_W8_TO_MEAN_BIAS    = -53.198287;
static const double HTI_W9_TO_CENTER_SLOPE = 0.017119;
static const double HTI_W9_TO_CENTER_BIAS  = -52.032800;

/*
 * Provisional upper-footer-only additive correction.
 * Coefficients are a count-weighted blend of the two mean-stat
 * w20+r1 fits from the cold-start and room-temperature warmup runs.
 */
static const double HTI_W20_PLUS_R1_INTERCEPT_C = -36.205448;
static const double HTI_W20_PLUS_R1_W20_COEF_C  = 2.482923;
static const double HTI_W20_PLUS_R1_R1_COEF_C   = -0.356713;

struct hti_base_stats
{
    double min_c;
    double max_c;
    double center_c;
    double mean_c;
};

const char *hti_correction_name( enum hti_correction_mode correction )
{
    switch( correction )
    {
        case HTI_CORRECTION_RAW:
            return "raw";
        case HTI_CORRECTION_W20_PLUS_R1:
            return "w20+r1";
        case HTI_CORRECTION_LOWER_AFFINE:
            return "lower-affine";
        case HTI_CORRECTION_COUNT:
            break;
    }
    return "unknown";
}

enum hti_correction_mode hti_next_correction( enum hti_correction_mode correction )
{
    switch( correction )
    {
        case HTI_CORRECTION_RAW:
            return HTI_CORRECTION_W20_PLUS_R1;
        case HTI_CORRECTION_W20_PLUS_R1:
            return HTI_CORRECTION_LOWER_AFFINE;
        case HTI_CORRECTION_LOWER_AFFINE:
            return HTI_CORRECTION_RAW;
        case HTI_CORRECTION_COUNT:
            break;
    }
    return HTI_CORRECTION_RAW;
}

const char *hti_ffc_state_name( enum hti_ffc_state state )
{
    switch( state )
    {
        case HTI_FFC_NONE:
            return "none";
        case HTI_FFC_TRIGGERED:
            return "triggered";
        case HTI_FFC_REPLAY:
            return "replay";
        case HTI_FFC_RECOVERED:
            return "recovered";
    }
    return "unknown";
}

const char *hti_lower_affine_kind_name( enum hti_lower_affine_kind kind )
{
    switch( kind )
    {
        case HTI_LOWER_AFFINE_NONE:
            return "none";
        case HTI_LOWER_AFFINE_MINMAX:
            return "minmax";
        case HTI_LOWER_AFFINE_MINMEAN:
            return "minmean";
    }
    return "unknown";
}

/*
 * Convert a raw u16 pixel code to a temperature-independent code value.
 *
 * For LOWER_AFFINE mode this also applies a shutter/FFC pre-correction.
 * When the camera re-baselines at an FFC event, the pixel codes shift by an
 * offset proportional to the w20/w25 delta.

 * Subtracting ~0.8125 * delta compensates for this before the lower-footer
 * affine remap is applied later in hti_temperature_at(). T

 * This is a two-stage correction strategy:
 *   - undo the FFC baseline jump here,
 *   - affine-map to match the lower processed footer reference.
 */
static double corrected_code_raw( const struct hti_frame  *frame,
                                  enum hti_correction_mode correction,
                                  int                      index )
{
    (void)index;

    double code = (double)frame->radiometric[index];

    if( correction == HTI_CORRECTION_LOWER_AFFINE )
    {
        double w20   = (double)frame->footer.r0_w20;
        double w25   = (double)frame->footer.r0_w25;
        double delta = w20 - w25;

        if( w25 > 0.0 && delta >= -64.0 && delta <= 64.0 )
        {
            code -= delta * 0.8125;
        }
    }

    return code;
}

static double code_to_temp_c( double code )
{
    return ( code - 4096.0 ) / 29.5;
}

static double upper_footer_offset_c( const struct hti_frame  *frame,
                                     enum hti_correction_mode correction )
{
    if( correction != HTI_CORRECTION_W20_PLUS_R1 )
    {
        return 0.0;
    }

    return HTI_W20_PLUS_R1_INTERCEPT_C + HTI_W20_PLUS_R1_W20_COEF_C * ( (double)frame->footer.r0_w20 / 100.0 ) + HTI_W20_PLUS_R1_R1_COEF_C * ( (double)frame->footer.r1_u32_10 / 1000.0 );
}

static double base_temperature_at( const struct hti_frame *frame, int index )
{
    return code_to_temp_c( corrected_code_raw( frame, HTI_CORRECTION_W20_PLUS_R1, index ) );
}

static void compute_base_stats( const struct hti_frame *frame, struct hti_base_stats *stats )
{
    double sum          = 0.0;
    int    center_index = ( HTI_FRAME_HEIGHT / 2 ) * HTI_FRAME_WIDTH + ( HTI_FRAME_WIDTH / 2 );

    stats->min_c = 1e30;
    stats->max_c = -1e30;

    for( int i = 0; i < HTI_IMAGE_PIXELS; i++ )
    {
        double temp_c = base_temperature_at( frame, i );

        sum += temp_c;
        if( temp_c < stats->min_c )
            stats->min_c = temp_c;
        if( temp_c > stats->max_c )
            stats->max_c = temp_c;
    }

    stats->center_c = base_temperature_at( frame, center_index );
    stats->mean_c   = sum / (double)HTI_IMAGE_PIXELS;
}

double hti_lower_footer_min_c( const struct hti_frame *frame )
{
    return HTI_W5_TO_MIN_SLOPE * (double)frame->display_footer.w5 + HTI_W5_TO_MIN_BIAS;
}

double hti_lower_footer_max_c( const struct hti_frame *frame )
{
    return HTI_W2_TO_MAX_SLOPE * (double)frame->display_footer.w2 + HTI_W2_TO_MAX_BIAS;
}

double hti_lower_footer_mean_c( const struct hti_frame *frame )
{
    return HTI_W8_TO_MEAN_SLOPE * (double)frame->display_footer.w8 + HTI_W8_TO_MEAN_BIAS;
}

double hti_lower_footer_center_c( const struct hti_frame *frame )
{
    return HTI_W9_TO_CENTER_SLOPE * (double)frame->display_footer.w9 + HTI_W9_TO_CENTER_BIAS;
}

static bool compute_affine( double  src_a,
                            double  src_b,
                            double  dst_a,
                            double  dst_b,
                            double *scale_out,
                            double *bias_out )
{
    double denom = src_b - src_a;
    double scale = 0.0;
    double bias  = 0.0;

    if( !isfinite( src_a ) || !isfinite( src_b ) || !isfinite( dst_a ) || !isfinite( dst_b ) )
    {
        return false;
    }

    if( fabs( denom ) < 0.1 )
    {
        return false;
    }

    scale = ( dst_b - dst_a ) / denom;
    bias  = dst_a - scale * src_a;

    if( !isfinite( scale ) || !isfinite( bias ) )
    {
        return false;
    }

    /* Empirically-derived. Anything outside these bounds indicates something odd in the 
     * scene (all-one-temperature, saturated, or misaligned footer vs image). */
    if( scale < 0.5 || scale > 2.0 )
    {
        return false;
    }

    if( bias < -80.0 || bias > 80.0 )
    {
        return false;
    }

    *scale_out = scale;
    *bias_out  = bias;
    return true;
}

void hti_prepare_frame( struct hti_frame *frame )
{
    struct hti_base_stats base;
    double                target_min    = 0.0;
    double                target_max    = 0.0;
    double                target_mean   = 0.0;
    double                target_center = 0.0;

    frame->lower_affine_valid           = false;
    frame->lower_affine_kind            = HTI_LOWER_AFFINE_NONE;
    frame->lower_affine_scale           = 1.0;
    frame->lower_affine_bias            = 0.0;
    frame->lower_affine_target_min_c    = 0.0;
    frame->lower_affine_target_max_c    = 0.0;
    frame->lower_affine_target_mean_c   = 0.0;
    frame->lower_affine_target_center_c = 0.0;

    if( !frame->display_footer.valid )
    {
        return;
    }

    if( frame->ffc_state == HTI_FFC_TRIGGERED || frame->ffc_state == HTI_FFC_REPLAY )
    {
        return;
    }

    compute_base_stats( frame, &base );
    target_min    = hti_lower_footer_min_c( frame );
    target_max    = hti_lower_footer_max_c( frame );
    target_mean   = hti_lower_footer_mean_c( frame );
    target_center = hti_lower_footer_center_c( frame );

    frame->lower_affine_target_min_c    = target_min;
    frame->lower_affine_target_max_c    = target_max;
    frame->lower_affine_target_mean_c   = target_mean;
    frame->lower_affine_target_center_c = target_center;

    if( target_max > target_min + 1.0 && target_max <= HTI_LOWER_AFFINE_MAX_TRUST_C && compute_affine( base.min_c, base.max_c, target_min, target_max, &frame->lower_affine_scale, &frame->lower_affine_bias ) )
    {
        frame->lower_affine_valid = true;
        frame->lower_affine_kind  = HTI_LOWER_AFFINE_MINMAX;
        return;
    }

    if( target_mean > target_min + 0.5 && compute_affine( base.min_c, base.mean_c, target_min, target_mean, &frame->lower_affine_scale, &frame->lower_affine_bias ) )
    {
        frame->lower_affine_valid = true;
        frame->lower_affine_kind  = HTI_LOWER_AFFINE_MINMEAN;
    }
}

double hti_temperature_at( const struct hti_frame  *frame,
                           enum hti_correction_mode correction,
                           int                      index )
{
    double temp_c = code_to_temp_c( corrected_code_raw( frame, correction, index ) );

    temp_c += upper_footer_offset_c( frame, correction );

    if( correction == HTI_CORRECTION_LOWER_AFFINE && frame->lower_affine_valid )
    {
        temp_c = frame->lower_affine_scale * temp_c + frame->lower_affine_bias;
    }

    return temp_c;
}

void hti_compute_stats( const struct hti_frame       *frame,
                        enum hti_correction_mode      correction,
                        struct hti_temperature_stats *stats )
{
    double sum          = 0.0;
    int    center_index = ( HTI_FRAME_HEIGHT / 2 ) * HTI_FRAME_WIDTH + ( HTI_FRAME_WIDTH / 2 );

    stats->min_c              = 1e30;
    stats->max_c              = -1e30;
    stats->scene_range_c      = 0.0;
    stats->scene_dynamic_bits = 0.0;
    stats->raw_min_code       = UINT16_MAX;
    stats->raw_max_code       = 0;
    stats->min_x              = 0;
    stats->min_y              = 0;
    stats->max_x              = 0;
    stats->max_y              = 0;

    for( int y = 0; y < HTI_FRAME_HEIGHT; y++ )
    {
        for( int x = 0; x < HTI_FRAME_WIDTH; x++ )
        {
            int      idx      = y * HTI_FRAME_WIDTH + x;
            uint16_t raw_code = frame->radiometric[idx];
            double   temp_c   = hti_temperature_at( frame, correction, idx );

            sum += temp_c;
            if( raw_code < stats->raw_min_code )
            {
                stats->raw_min_code = raw_code;
            }

            if( raw_code > stats->raw_max_code )
            {
                stats->raw_max_code = raw_code;
            }

            if( temp_c < stats->min_c )
            {
                stats->min_c = temp_c;
                stats->min_x = x;
                stats->min_y = y;
            }

            if( temp_c > stats->max_c )
            {
                stats->max_c = temp_c;
                stats->max_x = x;
                stats->max_y = y;
            }
        }
    }

    stats->center_c = hti_temperature_at( frame, correction, center_index );
    stats->mean_c   = sum / (double)HTI_IMAGE_PIXELS;
    stats->scene_range_c = stats->max_c - stats->min_c;

    if( !isfinite( stats->scene_range_c ) || stats->scene_range_c < 0.0 )
    {
        stats->scene_range_c = 0.0;
    }

    if( stats->raw_max_code >= stats->raw_min_code )
    {
        double raw_span = (double)( stats->raw_max_code - stats->raw_min_code );

        stats->scene_dynamic_bits = raw_span > 0.0 ? log2( raw_span + 1.0 ) : 0.0;
    }

    if( !isfinite( stats->scene_dynamic_bits ) || stats->scene_dynamic_bits < 0.0 )
    {
        stats->scene_dynamic_bits = 0.0;
    }
}

void hti_compute_display_range( const struct hti_temperature_stats *stats,
                                bool                                manual_scale,
                                double                              manual_min_c,
                                double                              manual_max_c,
                                struct hti_display_range           *range )
{
    if( manual_scale )
    {
        range->min_c = manual_min_c;
        range->max_c = manual_max_c;
    }
    else
    {
        range->min_c = stats->min_c - 1.0;
        range->max_c = stats->max_c + 1.0;
    }

    if( !isfinite( range->min_c ) || !isfinite( range->max_c ) || range->max_c <= range->min_c )
    {
        range->min_c = stats->mean_c - 5.0;
        range->max_c = stats->mean_c + 5.0;
    }

    if( range->max_c <= range->min_c )
    {
        range->max_c = range->min_c + 1.0;
    }
}

/* Seconds of smoothing for auto-range EMA. Larger = more stable and slower to react. */
static const double HTI_RANGE_SMOOTHING_SEC = 0.5;
static const uint64_t HTI_DEFAULT_FRAME_DELTA_NS = 40000000ull;

static double range_alpha( uint64_t frame_delta_ns )
{
    double delta_s = frame_delta_ns > 0 ? (double)frame_delta_ns / 1000000000.0
                                        : (double)HTI_DEFAULT_FRAME_DELTA_NS / 1000000000.0;

    if( delta_s <= 0.0 || !isfinite( delta_s ) )
    {
        delta_s = (double)HTI_DEFAULT_FRAME_DELTA_NS / 1000000000.0;
    }

    if( HTI_RANGE_SMOOTHING_SEC <= 0.0 )
    {
        return 1.0;
    }

    if( delta_s >= HTI_RANGE_SMOOTHING_SEC * 4.0 )
    {
        return 1.0;
    }

    return 1.0 - exp( -delta_s / HTI_RANGE_SMOOTHING_SEC );
}

void hti_range_reset( struct hti_auto_range_state *state )
{
    memset( state, 0, sizeof( *state ) );
}

void hti_range_update( const struct hti_frame             *frame,
                        const struct hti_temperature_stats *stats,
                        bool                                manual_scale,
                        double                              manual_min_c,
                        double                              manual_max_c,
                        struct hti_auto_range_state        *auto_range,
                        struct hti_display_range           *range_out )
{
    struct hti_display_range instant;

    hti_compute_display_range( stats, manual_scale, manual_min_c, manual_max_c, &instant );

    if( manual_scale )
    {
        *range_out = instant;
        return;
    }

    if( !auto_range->initialized
        || !isfinite( auto_range->min_c )
        || !isfinite( auto_range->max_c )
        || auto_range->max_c <= auto_range->min_c )
    {
        auto_range->initialized = true;
        auto_range->min_c       = instant.min_c;
        auto_range->max_c       = instant.max_c;
    }
    else if( frame->quantitative_valid )
    {
        double alpha = range_alpha( frame->frame_delta_ns );

        auto_range->min_c += ( instant.min_c - auto_range->min_c ) * alpha;
        auto_range->max_c += ( instant.max_c - auto_range->max_c ) * alpha;
    }

    range_out->min_c = auto_range->min_c;
    range_out->max_c = auto_range->max_c;

    if( !isfinite( range_out->min_c ) || !isfinite( range_out->max_c ) || range_out->max_c <= range_out->min_c )
    {
        *range_out = instant;
        auto_range->initialized = true;
        auto_range->min_c       = range_out->min_c;
        auto_range->max_c       = range_out->max_c;
    }
}
