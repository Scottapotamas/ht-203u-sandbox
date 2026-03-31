#include "hti_usb_view.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    HTI_TOP_HALF_ROWS      = 196,
    HTI_LOWER_BLOCK_START  = HTI_TOP_HALF_ROWS,
    HTI_LOWER_FOOTER_START = HTI_LOWER_BLOCK_START + HTI_FRAME_HEIGHT,
};

static const uint32_t HTI_VENDOR_MARKER = 0xaabbccddU;

static const char *base_name( const char *path )
{
    const char *slash = strrchr( path, '/' );
    return slash ? slash + 1 : path;
}

static int file_exists( const char *path )
{
    FILE *f = fopen( path, "rb" );

    if( f == NULL )
    {
        return 0;
    }

    fclose( f );
    return 1;
}

static int read_file_exact( const char *path, uint8_t *dst, size_t expected_len )
{
    FILE *f = fopen( path, "rb" );

    if( f == NULL )
    {
        return -1;
    }

    if( fread( dst, 1, expected_len, f ) != expected_len )
    {
        fclose( f );
        errno = EIO;
        return -1;
    }

    if( fgetc( f ) != EOF )
    {
        fclose( f );
        errno = EOVERFLOW;
        return -1;
    }

    fclose( f );
    return 0;
}

static void decode_footer( struct hti_footer_fields *footer, const uint8_t *meta_raw )
{
    memset( footer, 0, sizeof( *footer ) );
    footer->r0_w00    = hti_read_le16( meta_raw + 0x00 );
    footer->r0_w01    = hti_read_le16( meta_raw + 0x02 );
    footer->r0_w03    = hti_read_le16( meta_raw + 0x06 );
    footer->r0_w20    = hti_read_le16( meta_raw + 0x28 );
    footer->r0_w25    = hti_read_le16( meta_raw + 0x32 );
    footer->r1_u32_10 = hti_read_le32( meta_raw + 0x210 );
    footer->r1_u32_14 = hti_read_le32( meta_raw + 0x214 );
    footer->valid     = true;
}

static void decode_display_footer( struct hti_display_footer_fields *footer, const uint8_t *row0_raw )
{
    memset( footer, 0, sizeof( *footer ) );
    footer->marker0 = hti_read_le16( row0_raw + 0x00 );
    footer->marker1 = hti_read_le16( row0_raw + 0x02 );
    footer->w2      = hti_read_le16( row0_raw + 0x04 );
    footer->w5      = hti_read_le16( row0_raw + 0x0a );
    footer->w8      = hti_read_le16( row0_raw + 0x10 );
    footer->w9      = hti_read_le16( row0_raw + 0x12 );
    footer->valid   = footer->marker0 == 0xffffu && footer->marker1 == 0xeeeeu && ( footer->w2 != 0 || footer->w5 != 0 || footer->w8 != 0 || footer->w9 != 0 );
}

static void decode_active_u16( const uint8_t *src, uint16_t *dst )
{
    for( int i = 0; i < HTI_IMAGE_PIXELS; i++ )
    {
        dst[i] = hti_read_le16( src + (size_t)i * 2 );
    }
}

static int decode_stacked_raw( const uint8_t *src, size_t length, struct hti_frame *dst )
{
    uint8_t display_footer_row0[HTI_BYTES_PER_LINE];

    if( length != HTI_FULL_FRAME_BYTES )
    {
        errno = EINVAL;
        return -1;
    }

    memset( dst, 0, sizeof( *dst ) );
    dst->layout = HTI_LAYOUT_STACKED_RAW;
    decode_active_u16( src, dst->radiometric );

    memcpy( dst->meta_raw, src + (size_t)HTI_FRAME_HEIGHT * HTI_BYTES_PER_LINE, HTI_META_RAW_BYTES );
    dst->has_meta_raw = true;
    decode_footer( &dst->footer, dst->meta_raw );

    memcpy( dst->display_raw,
            src + (size_t)HTI_LOWER_BLOCK_START * HTI_BYTES_PER_LINE,
            HTI_ACTIVE_BYTES );
    dst->has_display_raw = true;
    memcpy( display_footer_row0,
            src + (size_t)HTI_LOWER_FOOTER_START * HTI_BYTES_PER_LINE,
            sizeof( display_footer_row0 ) );
    decode_display_footer( &dst->display_footer, display_footer_row0 );

    return 0;
}

static int decode_dual_active( const uint8_t *src, size_t length, struct hti_frame *dst )
{
    if( length != (size_t)HTI_ACTIVE_BYTES * 2 )
    {
        errno = EINVAL;
        return -1;
    }

    memset( dst, 0, sizeof( *dst ) );
    dst->layout = HTI_LAYOUT_DUAL_ACTIVE;
    decode_active_u16( src, dst->radiometric );
    memcpy( dst->display_raw, src + HTI_ACTIVE_BYTES, HTI_ACTIVE_BYTES );
    dst->has_display_raw = true;

    return 0;
}

static int decode_vendor_framed( const uint8_t *src, size_t length, struct hti_frame *dst )
{
    uint32_t offset = 0;

    if( length < 8 )
    {
        errno = EINVAL;
        return -1;
    }

    offset = hti_read_le32( src + 4 );
    if( (size_t)offset + 4u + (size_t)HTI_ACTIVE_BYTES * 2u > length )
    {
        errno = EINVAL;
        return -1;
    }
    if( hti_read_le32( src + offset ) != HTI_VENDOR_MARKER )
    {
        errno = EINVAL;
        return -1;
    }

    memset( dst, 0, sizeof( *dst ) );
    dst->layout = HTI_LAYOUT_VENDOR_FRAMED;
    decode_active_u16( src + offset + 4, dst->radiometric );
    memcpy( dst->display_raw, src + offset + 4 + HTI_ACTIVE_BYTES, HTI_ACTIVE_BYTES );
    dst->has_display_raw = true;

    return 0;
}

const char *hti_frame_layout_name( enum hti_frame_layout layout )
{
    switch( layout )
    {
        case HTI_LAYOUT_UNKNOWN:
            return "unknown";
        case HTI_LAYOUT_STACKED_RAW:
            return "stacked";
        case HTI_LAYOUT_DUAL_ACTIVE:
            return "dual-active";
        case HTI_LAYOUT_VENDOR_FRAMED:
            return "vendor-framed";
    }
    return "unknown";
}

int hti_decode_transport_frame( const struct hti_transport_frame *src, struct hti_frame *dst )
{
    int rc = -1;

    if( src->length == HTI_FULL_FRAME_BYTES )
    {
        rc = decode_stacked_raw( src->bytes, src->length, dst );
    }
    else if( src->length == (size_t)HTI_ACTIVE_BYTES * 2u )
    {
        rc = decode_dual_active( src->bytes, src->length, dst );
    }
    else
    {
        rc = decode_vendor_framed( src->bytes, src->length, dst );
    }

    if( rc == 0 )
    {
        snprintf( dst->name, sizeof( dst->name ), "%s", src->name[0] ? src->name : "live" );
    }

    return rc;
}

int hti_load_saved_prefix( const char                 *prefix,
                           struct hti_transport_frame *transport,
                           struct hti_frame           *frame )
{
    char        full_path[1024];
    char        region_path[1024];
    char        meta_path[1024];
    uint8_t     region[HTI_TOP_HALF_ROWS * HTI_BYTES_PER_LINE];
    const char *name = base_name( prefix );

    memset( transport, 0, sizeof( *transport ) );
    snprintf( transport->name, sizeof( transport->name ), "%s", name );
    snprintf( full_path, sizeof( full_path ), "%s_full_yuyv.raw", prefix );
    snprintf( region_path, sizeof( region_path ), "%s_radiometric_region_yuyv.raw", prefix );
    snprintf( meta_path, sizeof( meta_path ), "%s_meta_yuyv.raw", prefix );

    if( file_exists( full_path ) )
    {
        transport->length = HTI_FULL_FRAME_BYTES;
        if( read_file_exact( full_path, transport->bytes, transport->length ) != 0 )
        {
            perror( full_path );
            return -1;
        }
        return hti_decode_transport_frame( transport, frame );
    }

    if( read_file_exact( region_path, region, sizeof( region ) ) != 0 )
    {
        perror( region_path );
        return -1;
    }

    if( read_file_exact( meta_path, frame->meta_raw, HTI_META_RAW_BYTES ) != 0 )
    {
        perror( meta_path );
        return -1;
    }

    memset( frame, 0, sizeof( *frame ) );
    frame->layout       = HTI_LAYOUT_STACKED_RAW;
    frame->has_meta_raw = true;
    decode_active_u16( region, frame->radiometric );
    decode_footer( &frame->footer, frame->meta_raw );
    snprintf( frame->name, sizeof( frame->name ), "%s", name );

    return 0;
}
