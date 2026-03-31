#include "hti_usb_view.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static int mkdir_p( const char *path )
{
    char   buf[1024];
    size_t len = 0;

    if( path == NULL || path[0] == '\0' )
    {
        errno = EINVAL;
        return -1;
    }

    len = strlen( path );
    if( len >= sizeof( buf ) )
    {
        errno = ENAMETOOLONG;
        return -1;
    }

    memcpy( buf, path, len + 1 );
    for( size_t i = 1; i < len; i++ )
    {
        if( buf[i] != '/' )
            continue;
        buf[i] = '\0';
        if( mkdir( buf, 0777 ) != 0 && errno != EEXIST )
        {
            return -1;
        }
        buf[i] = '/';
    }

    if( mkdir( buf, 0777 ) != 0 && errno != EEXIST )
    {
        return -1;
    }
    return 0;
}

static int write_binary( const char *path, const void *data, size_t len )
{
    FILE *f = fopen( path, "wb" );

    if( f == NULL )
    {
        return -1;
    }

    if( len > 0 && fwrite( data, 1, len, f ) != len )
    {
        int saved_errno = errno ? errno : EIO;

        fclose( f );
        errno = saved_errno;
        return -1;
    }

    if( fclose( f ) != 0 )
    {
        return -1;
    }
    return 0;
}

static int write_pgm_u16( const char *path, const uint16_t *pixels )
{
    FILE *f = fopen( path, "wb" );

    if( f == NULL )
    {
        return -1;
    }

    if( fprintf( f, "P5\n%d %d\n65535\n", HTI_FRAME_WIDTH, HTI_FRAME_HEIGHT ) < 0 )
    {
        fclose( f );
        return -1;
    }

    for( int i = 0; i < HTI_IMAGE_PIXELS; i++ )
    {
        uint8_t be[2];

        be[0] = (uint8_t)( pixels[i] >> 8 );
        be[1] = (uint8_t)( pixels[i] & 0xffu );
        if( fwrite( be, 1, sizeof( be ), f ) != sizeof( be ) )
        {
            int saved_errno = errno ? errno : EIO;

            fclose( f );
            errno = saved_errno;
            return -1;
        }
    }

    if( fclose( f ) != 0 )
    {
        return -1;
    }

    return 0;
}

static int write_preview_ppm( const char                         *path,
                              const struct hti_frame             *frame,
                              const struct hti_temperature_stats *stats,
                              enum hti_correction_mode            correction,
                              enum hti_palette_mode               palette,
                              const struct hti_display_range     *range )
{
    FILE  *f     = fopen( path, "wb" );
    double denom = range->max_c - range->min_c;

    if( f == NULL )
    {
        return -1;
    }

    if( fprintf( f, "P6\n%d %d\n255\n", HTI_FRAME_WIDTH, HTI_FRAME_HEIGHT ) < 0 )
    {
        fclose( f );
        return -1;
    }

    if( denom <= 0.0 )
    {
        denom = 1.0;
    }

    for( int i = 0; i < HTI_IMAGE_PIXELS; i++ )
    {
        uint8_t rgb[3];
        double  temp_c     = hti_temperature_at( frame, correction, i ) + stats->offset_c;
        double  normalized = ( temp_c - range->min_c ) / denom;

        hti_palette_map( palette, normalized, &rgb[0], &rgb[1], &rgb[2] );
        if( fwrite( rgb, 1, sizeof( rgb ), f ) != sizeof( rgb ) )
        {
            int saved_errno = errno ? errno : EIO;

            fclose( f );
            errno = saved_errno;
            return -1;
        }
    }

    if( fclose( f ) != 0 )
    {
        return -1;
    }

    return 0;
}

static int write_metadata_json( const char                         *path,
                                const struct hti_transport_frame   *transport,
                                const struct hti_frame             *frame,
                                const struct hti_temperature_stats *stats,
                                enum hti_correction_mode            correction,
                                enum hti_palette_mode               palette,
                                const struct hti_display_range     *range )
{
    FILE *f = fopen( path, "w" );

    if( f == NULL )
    {
        return -1;
    }

    fprintf( f, "{\n" );
    fprintf( f, "  \"name\": \"%s\",\n", frame->name );
    fprintf( f, "  \"layout\": \"%s\",\n", hti_frame_layout_name( frame->layout ) );
    fprintf( f, "  \"transport_bytes\": %zu,\n", transport->length );
    fprintf( f, "  \"correction\": \"%s\",\n", hti_correction_name( correction ) );
    fprintf( f, "  \"palette\": \"%s\",\n", hti_palette_name( palette ) );
    fprintf( f,
             "  \"live_state\": {\"capture_monotonic_ns\": %llu, \"frame_delta_ns\": %llu, \"replay_detected\": %s, \"footer_changed\": %s, \"ffc_state\": \"%s\", \"ffc_manual\": %s, \"ffc_window_ns\": %llu, \"stable_run_ns\": %llu, \"previous_stable_run_ns\": %llu},\n",
             (unsigned long long)frame->capture_monotonic_ns,
             (unsigned long long)frame->frame_delta_ns,
             frame->replay_detected ? "true" : "false",
             frame->footer_changed ? "true" : "false",
             hti_ffc_state_name( frame->ffc_state ),
             frame->ffc_manual ? "true" : "false",
             (unsigned long long)frame->ffc_window_ns,
             (unsigned long long)frame->stable_run_ns,
             (unsigned long long)frame->previous_stable_run_ns );
    fprintf( f,
             "  \"quantitative\": {\"valid\": %s, \"reason\": \"%s\"},\n",
             frame->quantitative_valid ? "true" : "false",
             frame->quantitative_valid ? "fresh" : hti_ffc_state_name( frame->ffc_state ) );
    fprintf( f, "  \"display_range_c\": {\"min\": %.6f, \"max\": %.6f},\n", range->min_c, range->max_c );

    if( frame->quantitative_valid )
    {
        fprintf( f,
                 "  \"stats_c\": {\"hot\": %.6f, \"cold\": %.6f, \"center\": %.6f, \"mean\": %.6f, \"offset\": %.6f},\n",
                 stats->max_c,
                 stats->min_c,
                 stats->center_c,
                 stats->mean_c,
                 stats->offset_c );
        fprintf( f,
                 "  \"hot_xy\": [%d, %d],\n"
                 "  \"cold_xy\": [%d, %d],\n",
                 stats->max_x,
                 stats->max_y,
                 stats->min_x,
                 stats->min_y );
    }
    else
    {
        fprintf( f, "  \"stats_c\": null,\n" );
        fprintf( f, "  \"hot_xy\": null,\n" );
        fprintf( f, "  \"cold_xy\": null,\n" );
    }

    fprintf( f,
             "  \"upper_footer\": {\"valid\": %s, \"r0_w00\": %u, \"r0_w01\": %u, \"r0_w03\": %u, \"r0_w20\": %u, \"r0_w25\": %u, \"r1_u32_10\": %u, \"r1_u32_14\": %u},\n",
             frame->footer.valid ? "true" : "false",
             frame->footer.r0_w00,
             frame->footer.r0_w01,
             frame->footer.r0_w03,
             frame->footer.r0_w20,
             frame->footer.r0_w25,
             frame->footer.r1_u32_10,
             frame->footer.r1_u32_14 );
    fprintf( f,
             "  \"lower_footer\": {\"valid\": %s, \"w2\": %u, \"w5\": %u, \"w8\": %u, \"w9\": %u}\n",
             frame->display_footer.valid ? "true" : "false",
             frame->display_footer.w2,
             frame->display_footer.w5,
             frame->display_footer.w8,
             frame->display_footer.w9 );
    fprintf( f,
             ",\n  \"lower_affine\": {\"valid\": %s, \"kind\": \"%s\", \"scale\": %.9f, \"bias\": %.9f, \"target_min_c\": %.6f, \"target_max_c\": %.6f, \"target_mean_c\": %.6f, \"target_center_c\": %.6f}\n",
             frame->lower_affine_valid ? "true" : "false",
             hti_lower_affine_kind_name( frame->lower_affine_kind ),
             frame->lower_affine_scale,
             frame->lower_affine_bias,
             frame->lower_affine_target_min_c,
             frame->lower_affine_target_max_c,
             frame->lower_affine_target_mean_c,
             frame->lower_affine_target_center_c );
    fprintf( f, "}\n" );

    if( fclose( f ) != 0 )
    {
        return -1;
    }

    return 0;
}

int hti_save_frame( const char                         *directory,
                    const struct hti_transport_frame   *transport,
                    const struct hti_frame             *frame,
                    const struct hti_temperature_stats *stats,
                    enum hti_correction_mode            correction,
                    enum hti_palette_mode               palette,
                    const struct hti_display_range     *range )
{
    struct timespec ts;
    struct tm       tm;
    char            stamp[64];
    char            stem[1152];
    char            raw_path[1200];
    char            pgm_path[1200];
    char            preview_path[1200];
    char            json_path[1200];

    if( mkdir_p( directory ) != 0 )
    {
        return -1;
    }

    if( clock_gettime( CLOCK_REALTIME, &ts ) != 0 )
    {
        return -1;
    }
    
    if( localtime_r( &ts.tv_sec, &tm ) == NULL )
    {
        return -1;
    }

    snprintf( stamp,
              sizeof( stamp ),
              "%04d%02d%02dT%02d%02d%02d_%03ld",
              tm.tm_year + 1900,
              tm.tm_mon + 1,
              tm.tm_mday,
              tm.tm_hour,
              tm.tm_min,
              tm.tm_sec,
              ts.tv_nsec / 1000000L );

    snprintf( stem, sizeof( stem ), "%s/%s_%s", directory, stamp, frame->name[0] ? frame->name : "frame" );
    snprintf( raw_path, sizeof( raw_path ), "%s.bin", stem );
    snprintf( pgm_path, sizeof( pgm_path ), "%s_radiometric_u16.pgm", stem );
    snprintf( preview_path, sizeof( preview_path ), "%s_preview.ppm", stem );
    snprintf( json_path, sizeof( json_path ), "%s.json", stem );

    if( transport->length > 0 && write_binary( raw_path, transport->bytes, transport->length ) != 0 )
    {
        return -1;
    }
    
    if( write_pgm_u16( pgm_path, frame->radiometric ) != 0 )
    {
        return -1;
    }

    if( write_preview_ppm( preview_path, frame, stats, correction, palette, range ) != 0 )
    {
        return -1;
    }

    if( write_metadata_json( json_path, transport, frame, stats, correction, palette, range ) != 0 )
    {
        return -1;
    }

    return 0;
}
