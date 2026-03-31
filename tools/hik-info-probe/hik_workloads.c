#include "hik_probe.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void print_hexdump( const uint8_t *data, size_t len )
{
    for( size_t offset = 0; offset < len; offset += 16 )
    {
        size_t row_len = len - offset;

        if( row_len > 16 )
        {
            row_len = 16;
        }

        printf( "%04zx  ", offset );
        for( size_t i = 0; i < 16; ++i )
        {
            if( i < row_len )
            {
                printf( "%02x ", data[offset + i] );
            }
            else
            {
                printf( "   " );
            }
        }

        printf( " " );
        for( size_t i = 0; i < row_len; ++i )
        {
            unsigned char c = data[offset + i];

            putchar( isprint( c ) ? (int)c : '.' );
        }
        putchar( '\n' );
    }
}

static void print_ascii_fragments( const uint8_t *data, size_t len )
{
    size_t i     = 0;
    int    index = 0;

    puts( "Printable ASCII fragments:" );
    while( i < len )
    {
        size_t start = 0;

        while( i < len && !isprint( data[i] ) )
        {
            ++i;
        }

        start = i;
        while( i < len && isprint( data[i] ) )
        {
            ++i;
        }

        if( i > start && ( i - start ) >= 3 )
        {
            printf( "  [%d] %.*s\n", ++index, (int)( i - start ), (const char *)( data + start ) );
        }
    }

    if( index == 0 )
    {
        puts( "  (none found)" );
    }
}

static void print_ascii_string_value( const uint8_t *data, size_t len )
{
    char   buf[256];
    size_t out = 0;

    for( size_t i = 0; i < len && out + 1 < sizeof( buf ); ++i )
    {
        if( data[i] == '\0' )
        {
            break;
        }
        buf[out++] = isprint( data[i] ) ? (char)data[i] : '.';
    }
    buf[out] = '\0';

    printf( "ASCII value: %s\n", out == 0 ? "<empty>" : buf );
}

static void print_hex_bytes_inline( const uint8_t *data, size_t len )
{
    for( size_t i = 0; i < len; ++i )
    {
        printf( "%s%02x", ( i == 0 ) ? "" : " ", data[i] );
    }
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

static long diff_milliseconds( const struct timespec *start, const struct timespec *end )
{
    long seconds = (long)( end->tv_sec - start->tv_sec );
    long nanos   = end->tv_nsec - start->tv_nsec;

    return ( seconds * 1000L ) + ( nanos / 1000000L );
}

static void verify_thermometry_after_write( const HikSession *session,
                                            const HikOptions *opts,
                                            const HikProfile *profile,
                                            const uint8_t    *expected,
                                            size_t            expected_len );

static int write_blob( const char *path, const uint8_t *buf, size_t len )
{
    FILE  *fp      = fopen( path, "wb" );
    size_t written = 0;

    if( fp == NULL )
    {
        fprintf( stderr, "error: could not open %s for writing: %s\n", path, strerror( errno ) );
        return -1;
    }

    written = fwrite( buf, 1, len, fp );
    if( written != len )
    {
        fprintf( stderr, "error: short write to %s\n", path );
        fclose( fp );
        return -1;
    }

    if( fclose( fp ) != 0 )
    {
        fprintf( stderr, "error: fclose(%s) failed: %s\n", path, strerror( errno ) );
        return -1;
    }

    return 0;
}

static uint32_t read_le32_local( const uint8_t *buf )
{
    return (uint32_t)buf[0] | ( (uint32_t)buf[1] << 8 ) | ( (uint32_t)buf[2] << 16 ) |
           ( (uint32_t)buf[3] << 24 );
}

static bool ensure_capacity( uint8_t **buf_out, size_t *capacity_out, size_t needed )
{
    uint8_t *grown        = NULL;
    size_t   new_capacity = *capacity_out;

    if( needed <= *capacity_out )
    {
        return true;
    }

    if( new_capacity == 0 )
    {
        new_capacity = 4096;
    }
    while( new_capacity < needed )
    {
        new_capacity *= 2;
    }

    grown = realloc( *buf_out, new_capacity );
    if( grown == NULL )
    {
        return false;
    }

    *buf_out      = grown;
    *capacity_out = new_capacity;
    return true;
}

static size_t find_subsequence( const uint8_t *haystack,
                                size_t         haystack_len,
                                const uint8_t *needle,
                                size_t         needle_len )
{
    if( needle_len == 0 || haystack_len < needle_len )
    {
        return SIZE_MAX;
    }

    for( size_t i = 0; i + needle_len <= haystack_len; ++i )
    {
        if( memcmp( haystack + i, needle, needle_len ) == 0 )
        {
            return i;
        }
    }

    return SIZE_MAX;
}

static void print_calibration_preview( const char *label, const uint8_t *buf, size_t len )
{
    size_t preview_len = len;

    if( preview_len > 128 )
    {
        preview_len = 128;
    }

    printf( "%s first %zu byte(s):\n", label, preview_len );
    print_hexdump( buf, preview_len );
}

static int run_paired_init( const HikSession *session, const HikOptions *opts )
{
    HikLengthInfo length_info;
    uint8_t       protocol_buf[16];
    uint8_t       prelude_payload[2] = { 0x01, 0x01 };
    int           rc                 = 0;

    puts( "Running paired-capture preamble:" );

    rc = hik_fetch_control_length( session, opts, HIK_PROTOCOL_SELECTOR, &length_info );
    if( rc != 0 )
    {
        fprintf( stderr,
                 "error: selector 0x%02x GET_LEN failed: %s\n",
                 HIK_PROTOCOL_SELECTOR,
                 hik_transport_error( session->transport, rc ) );
        return rc;
    }
    hik_print_length_report( "  selector 0x04 GET_LEN", &length_info );

    if( length_info.decoded_length > 0 && length_info.decoded_length <= sizeof( protocol_buf ) )
    {
        rc = hik_fetch_current_value( session, opts, HIK_PROTOCOL_SELECTOR, protocol_buf, length_info.decoded_length );
        if( rc < 0 )
        {
            fprintf( stderr,
                     "error: selector 0x%02x GET_CUR failed: %s\n",
                     HIK_PROTOCOL_SELECTOR,
                     hik_transport_error( session->transport, rc ) );
            return rc;
        }
        printf( "  selector 0x04 GET_CUR returned %d byte(s): ", rc );
        for( int i = 0; i < rc; ++i )
        {
            putchar( isprint( protocol_buf[i] ) ? protocol_buf[i] : '.' );
        }
        putchar( '\n' );
    }

    rc = hik_fetch_control_length( session, opts, HIK_PRELUDE_SELECTOR, &length_info );
    if( rc != 0 )
    {
        fprintf( stderr,
                 "error: selector 0x%02x GET_LEN failed: %s\n",
                 HIK_PRELUDE_SELECTOR,
                 hik_transport_error( session->transport, rc ) );
        return rc;
    }
    hik_print_length_report( "  selector 0x05 GET_LEN", &length_info );

    rc = hik_set_current_value( session, opts, HIK_PRELUDE_SELECTOR, prelude_payload, sizeof( prelude_payload ) );
    if( rc < 0 )
    {
        fprintf( stderr,
                 "error: selector 0x%02x SET_CUR failed: %s\n",
                 HIK_PRELUDE_SELECTOR,
                 hik_transport_error( session->transport, rc ) );
        return rc;
    }
    printf( "  selector 0x05 SET_CUR wrote %d byte(s): 01 01\n", rc );

    return 0;
}

static int run_write_stage( const HikSession *session,
                            const HikOptions *opts,
                            uint8_t           selector,
                            const uint8_t    *payload,
                            size_t            payload_len,
                            uint16_t          expected_len,
                            const char       *label )
{
    HikLengthInfo length_info;
    uint16_t      effective_len = 0;
    struct timespec write_start;
    struct timespec write_end;
    bool          have_timing   = false;
    int           rc            = 0;

    rc = hik_fetch_control_length( session, opts, selector, &length_info );
    if( rc != 0 )
    {
        fprintf( stderr,
                 "error: %s GET_LEN failed: %s\n",
                 label,
                 hik_transport_error( session->transport, rc ) );
        return rc;
    }

    hik_print_length_report( label, &length_info );
    effective_len = opts->force_len_set ? opts->force_len
                                        : hik_select_reported_length( length_info.decoded_length, expected_len );
    if( expected_len != 0 && effective_len != expected_len )
    {
        fprintf( stderr,
                 "warning: %s expected length %u but device reported %u\n",
                 label,
                 expected_len,
                 length_info.decoded_length );
    }
    if( opts->force_len_set )
    {
        printf( "%s using forced length override: %u byte(s)\n", label, opts->force_len );
    }

    if( clock_gettime( CLOCK_MONOTONIC, &write_start ) == 0 )
    {
        have_timing = true;
    }

    rc = hik_set_current_value( session, opts, selector, payload, (uint16_t)payload_len );
    if( rc < 0 )
    {
        fprintf( stderr,
                 "error: %s SET_CUR failed: %s\n",
                 label,
                 hik_transport_error( session->transport, rc ) );
        return rc;
    }

    printf( "%s SET_CUR wrote %d byte(s): ", label, rc );
    print_hex_bytes_inline( payload, payload_len );
    printf( "\n" );
    if( have_timing && clock_gettime( CLOCK_MONOTONIC, &write_end ) == 0 )
    {
        printf( "%s SET_CUR completion time: %ld ms\n", label, diff_milliseconds( &write_start, &write_end ) );
    }
    return 0;
}

static int maybe_run_profile_prewrite( const HikSession *session,
                                       const HikOptions *opts,
                                       const HikProfile *profile )
{
    int rc = 0;

    if( profile->prewrite_payload_len == 0 )
    {
        return 0;
    }

    rc = run_write_stage( session,
                          opts,
                          profile->prewrite_selector,
                          profile->prewrite_payload,
                          profile->prewrite_payload_len,
                          (uint16_t)profile->prewrite_payload_len,
                          "Profile prewrite" );
    if( rc == 0 )
    {
        puts( "" );
    }
    return rc;
}

static int run_selector_state_scan( const HikSession *session, const HikOptions *opts )
{
    uint8_t payload[HIK_MAX_SCAN_PAYLOAD_BYTES];
    uint8_t cur_buf[HIK_MAX_REASONABLE_BLOB];
    uint8_t prewrite_selector = opts->scan_prewrite_selector_set
                                    ? opts->scan_prewrite_selector
                                    : HIK_PRELUDE_SELECTOR;
    uint8_t target_selector   = opts->scan_target_selector_set
                                    ? opts->scan_target_selector
                                    : HIK_ENHANCEMENT_SELECTOR;
    int     rc                = 0;

    if( opts->scan_prefix_len + 1 > sizeof( payload ) )
    {
        fprintf( stderr, "error: scan prefix too long\n" );
        return 2;
    }

    printf( "Selector-state scan: prewrite selector 0x%02x, target selector 0x%02x, range 0x%02x..0x%02x\n",
            prewrite_selector,
            target_selector,
            opts->scan_range_start,
            opts->scan_range_end );
    printf( "Fixed prefix bytes:" );
    if( opts->scan_prefix_len == 0 )
    {
        printf( " <none>" );
    }
    else
    {
        printf( " " );
        print_hex_bytes_inline( opts->scan_prefix, opts->scan_prefix_len );
    }
    printf( "\n" );
    if( opts->scan_read_cur )
    {
        puts( "GET_CUR sampling is enabled for each candidate." );
    }
    puts( "" );

    memcpy( payload, opts->scan_prefix, opts->scan_prefix_len );
    for( unsigned int candidate = opts->scan_range_start; candidate <= opts->scan_range_end; ++candidate )
    {
        HikLengthInfo length_info;
        uint16_t      effective_len = 0;
        int           cur_rc        = 0;
        size_t        payload_len   = opts->scan_prefix_len + 1;

        payload[payload_len - 1] = (uint8_t)candidate;
        rc                       = hik_set_current_value( session, opts, prewrite_selector, payload, (uint16_t)payload_len );
        if( rc < 0 )
        {
            fprintf( stderr,
                     "error: scan prewrite 0x%02x failed for candidate 0x%02x: %s\n",
                     prewrite_selector,
                     candidate,
                     hik_transport_error( session->transport, rc ) );
            return rc;
        }

        rc = hik_fetch_control_length( session, opts, target_selector, &length_info );
        if( rc != 0 )
        {
            fprintf( stderr,
                     "error: target selector 0x%02x GET_LEN failed after candidate 0x%02x: %s\n",
                     target_selector,
                     candidate,
                     hik_transport_error( session->transport, rc ) );
            return rc;
        }

        printf( "prewrite " );
        print_hex_bytes_inline( payload, payload_len );
        printf( " -> len raw=" );
        print_hex_bytes_inline( length_info.raw, (size_t)length_info.raw_len );
        printf( " len=%u", (unsigned int)length_info.decoded_length );

        if( opts->scan_read_cur )
        {
            effective_len = opts->force_len_set ? opts->force_len
                                                : hik_select_reported_length( length_info.decoded_length, 0 );
            if( effective_len == 0 || effective_len > sizeof( cur_buf ) )
            {
                printf( " cur=skipped" );
            }
            else
            {
                memset( cur_buf, 0, effective_len );
                cur_rc = hik_fetch_current_value( session, opts, target_selector, cur_buf, effective_len );
                if( cur_rc < 0 )
                {
                    printf( " cur=error(%s)", hik_transport_error( session->transport, cur_rc ) );
                }
                else
                {
                    printf( " cur[%d]=", cur_rc );
                    print_hex_bytes_inline( cur_buf, (size_t)cur_rc );
                }
            }
        }

        putchar( '\n' );
    }

    return 0;
}

static int run_read_selector( const HikSession *session,
                              const HikOptions *opts,
                              const HikProfile *profile,
                              const char       *label,
                              const char       *raw_out_path )
{
    HikLengthInfo length_info;
    uint8_t      *blob               = NULL;
    uint16_t      effective_blob_len = 0;
    int           rc                 = 0;

    if( profile->auto_paired_init && opts->paired_init )
    {
        rc = run_paired_init( session, opts );
        if( rc != 0 )
        {
            return rc;
        }
    }

    rc = hik_fetch_control_length( session, opts, profile->selector, &length_info );
    if( rc != 0 )
    {
        fprintf( stderr,
                 "error: %s GET_LEN failed: %s (raw rc=%d)\n",
                 label,
                 hik_transport_error( session->transport, rc ),
                 length_info.raw_len );
        return rc;
    }

    hik_print_length_report( label, &length_info );
    effective_blob_len = opts->force_len_set ? opts->force_len
                                             : hik_select_reported_length( length_info.decoded_length,
                                                                           profile->expected_len );
    if( opts->force_len_set )
    {
        printf( "%s using forced length override: %u byte(s)\n", label, opts->force_len );
    }

    if( profile->expected_len != 0 && effective_blob_len != profile->expected_len )
    {
        fprintf( stderr,
                 "warning: %s expected length %u but device reported %u\n",
                 label,
                 profile->expected_len,
                 length_info.decoded_length );
    }

    if( effective_blob_len == 0 || effective_blob_len > HIK_MAX_REASONABLE_BLOB )
    {
        fprintf( stderr,
                 "error: %s refusing suspicious blob length %" PRIu16 "\n",
                 label,
                 length_info.decoded_length );
        return 1;
    }

    blob = calloc( effective_blob_len, 1 );
    if( blob == NULL )
    {
        fprintf( stderr, "error: out of memory allocating %" PRIu16 " bytes\n", effective_blob_len );
        return 1;
    }

    rc = hik_fetch_current_value( session, opts, profile->selector, blob, effective_blob_len );
    if( rc < 0 )
    {
        fprintf( stderr,
                 "error: %s GET_CUR failed: %s\n",
                 label,
                 hik_transport_error( session->transport, rc ) );
        free( blob );
        return rc;
    }

    printf( "%s GET_CUR returned %d byte(s)\n", label, rc );
    if( (uint16_t)rc != effective_blob_len )
    {
        fprintf( stderr,
                 "warning: %s GET_CUR returned %d bytes but GET_LEN advertised %" PRIu16 "\n",
                 label,
                 rc,
                 effective_blob_len );
    }

    if( raw_out_path != NULL && write_blob( raw_out_path, blob, (size_t)rc ) != 0 )
    {
        free( blob );
        return 1;
    }

    puts( "" );
    if( profile->read_output_kind == HIK_READ_OUTPUT_ASCII_STRING )
    {
        print_ascii_string_value( blob, (size_t)rc );
    }
    else if( profile->read_output_kind == HIK_READ_OUTPUT_IMAGE_ENHANCEMENT )
    {
        hik_print_enhancement_summary( blob, (size_t)rc );
    }
    else if( profile->read_output_kind == HIK_READ_OUTPUT_THERMOMETRY_BASIC )
    {
        hik_print_thermometry_summary( blob, (size_t)rc );
    }
    else
    {
        print_ascii_fragments( blob, (size_t)rc );
    }

    puts( "" );
    puts( "Hexdump:" );
    print_hexdump( blob, (size_t)rc );

    free( blob );
    return 0;
}

static int run_calibration_file_read( const HikSession *session,
                                      const HikOptions *opts,
                                      const HikProfile *profile,
                                      const char       *label )
{
    static const uint8_t kCalibrationSignature[] = {
        0x04, 0x04, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x53, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    };

    HikLengthInfo length_info;
    uint8_t       session_header[32];
    uint8_t      *wrapped            = NULL;
    uint8_t      *dechunked          = NULL;
    size_t        wrapped_capacity   = 0;
    size_t        wrapped_len        = 0;
    size_t        dechunked_capacity = 0;
    size_t        dechunked_len      = 0;
    size_t        extracted_offset   = SIZE_MAX;
    size_t        chunk_count        = 0;
    uint32_t      declared_stream_len = 0;
    uint16_t      header_len         = 0;
    uint16_t      chunk_len          = 0;
    bool          extracted          = false;
    int           rc                 = 0;

    rc = hik_fetch_control_length( session, opts, profile->selector, &length_info );
    if( rc != 0 )
    {
        fprintf( stderr,
                 "error: %s session-header GET_LEN failed: %s\n",
                 label,
                 hik_transport_error( session->transport, rc ) );
        return rc;
    }

    hik_print_length_report( "Calibration session header GET_LEN", &length_info );
    header_len = length_info.decoded_length;
    if( header_len == 0 || header_len > sizeof( session_header ) )
    {
        bool looks_ascii = false;

        if( length_info.raw_len >= 4 &&
            isprint( length_info.raw[0] ) &&
            isprint( length_info.raw[1] ) &&
            isprint( length_info.raw[2] ) &&
            isprint( length_info.raw[3] ) )
        {
            looks_ascii = true;
        }

        fprintf( stderr,
                 "error: %s reported an unexpected session-header length %u\n",
                 label,
                 (unsigned int)header_len );
        if( looks_ascii )
        {
            fprintf( stderr,
                     "hint: selector 0x%02x appears to still be in its default printable state (%c%c%c%c), not the calibration-file state.\n"
                     "hint: the March 28 capture likely reflects a prior SDK/device-config staging step that is not yet reproduced here.\n"
                     "hint: try probing selector-state-scan with selector 0x05 writes against selector 0x03, for example:\n"
                     "hint:   ./build/hik-info-probe --profile selector-state-scan --scan-prewrite-selector 0x05 --scan-target-selector 0x03 --scan-prefix-hex 03 --scan-range 0:31\n",
                     profile->selector,
                     length_info.raw[0],
                     length_info.raw[1],
                     length_info.raw[2],
                     length_info.raw[3] );
        }
        return 1;
    }
    if( header_len != HIK_CALIBRATION_HEADER_LEN )
    {
        fprintf( stderr,
                 "warning: %s expected a %u-byte session header but the device reported %u\n",
                 label,
                 HIK_CALIBRATION_HEADER_LEN,
                 (unsigned int)header_len );
    }

    memset( session_header, 0, sizeof( session_header ) );
    rc = hik_fetch_current_value( session, opts, profile->selector, session_header, header_len );
    if( rc < 0 )
    {
        fprintf( stderr,
                 "error: %s session-header GET_CUR failed: %s\n",
                 label,
                 hik_transport_error( session->transport, rc ) );
        return rc;
    }

    printf( "Calibration session header (%d byte(s)): ", rc );
    print_hex_bytes_inline( session_header, (size_t)rc );
    printf( "\n" );
    if( rc >= HIK_CALIBRATION_HEADER_LEN )
    {
        declared_stream_len = read_le32_local( session_header + 1 );
        printf( "  status byte: 0x%02x\n", session_header[0] );
        printf( "  declared stream length: %" PRIu32 " byte(s)\n", declared_stream_len );
    }
    puts( "" );

    for( ;; )
    {
        rc = hik_fetch_control_length( session, opts, profile->selector, &length_info );
        if( rc != 0 )
        {
            fprintf( stderr,
                     "error: %s chunk %zu GET_LEN failed: %s\n",
                     label,
                     chunk_count + 1,
                     hik_transport_error( session->transport, rc ) );
            free( wrapped );
            free( dechunked );
            return rc;
        }

        chunk_len = length_info.decoded_length;
        printf( "Chunk %zu length probe: ", chunk_count + 1 );
        print_hex_bytes_inline( length_info.raw, (size_t)length_info.raw_len );
        printf( " -> %u byte(s)\n", (unsigned int)chunk_len );

        if( chunk_len == 0 || chunk_len > HIK_MAX_REASONABLE_BLOB )
        {
            fprintf( stderr,
                     "error: %s refusing suspicious chunk length %u\n",
                     label,
                     (unsigned int)chunk_len );
            free( wrapped );
            free( dechunked );
            return 1;
        }

        if( !ensure_capacity( &wrapped, &wrapped_capacity, wrapped_len + chunk_len ) )
        {
            fprintf( stderr, "error: out of memory growing calibration buffer to %zu bytes\n", wrapped_len + chunk_len );
            free( wrapped );
            free( dechunked );
            return 1;
        }

        rc = hik_fetch_current_value( session, opts, profile->selector, wrapped + wrapped_len, chunk_len );
        if( rc < 0 )
        {
            fprintf( stderr,
                     "error: %s chunk %zu GET_CUR failed: %s\n",
                     label,
                     chunk_count + 1,
                     hik_transport_error( session->transport, rc ) );
            free( wrapped );
            free( dechunked );
            return rc;
        }
        if( (uint16_t)rc != chunk_len )
        {
            fprintf( stderr,
                     "warning: %s chunk %zu returned %d byte(s) after advertising %u\n",
                     label,
                     chunk_count + 1,
                     rc,
                     (unsigned int)chunk_len );
        }

        wrapped_len += (size_t)rc;
        ++chunk_count;

        if( (size_t)rc < HIK_CALIBRATION_HEADER_LEN )
        {
            fprintf( stderr,
                     "error: %s chunk %zu is too short to contain the expected 5-byte chunk header\n",
                     label,
                     chunk_count );
            free( wrapped );
            free( dechunked );
            return 1;
        }

        if( wrapped[wrapped_len - (size_t)rc] != 0x02 )
        {
            fprintf( stderr,
                     "warning: %s chunk %zu starts with 0x%02x instead of the expected 0x02 chunk marker\n",
                     label,
                     chunk_count,
                     wrapped[wrapped_len - (size_t)rc] );
        }
        if( wrapped[wrapped_len - (size_t)rc + 1] != (uint8_t)chunk_count )
        {
            fprintf( stderr,
                     "warning: %s chunk %zu reports sequence byte 0x%02x\n",
                     label,
                     chunk_count,
                     wrapped[wrapped_len - (size_t)rc + 1] );
        }

        if( !ensure_capacity( &dechunked,
                              &dechunked_capacity,
                              dechunked_len + ( (size_t)rc - HIK_CALIBRATION_HEADER_LEN ) ) )
        {
            fprintf( stderr,
                     "error: out of memory growing dechunked calibration buffer to %zu bytes\n",
                     dechunked_len + ( (size_t)rc - HIK_CALIBRATION_HEADER_LEN ) );
            free( wrapped );
            free( dechunked );
            return 1;
        }
        memcpy( dechunked + dechunked_len,
                wrapped + ( wrapped_len - (size_t)rc ) + HIK_CALIBRATION_HEADER_LEN,
                (size_t)rc - HIK_CALIBRATION_HEADER_LEN );
        dechunked_len += (size_t)rc - HIK_CALIBRATION_HEADER_LEN;

        if( chunk_count == 1 || chunk_len != HIK_CALIBRATION_CHUNK_LEN )
        {
            print_calibration_preview( "Chunk preview", wrapped + ( wrapped_len - (size_t)rc ), (size_t)rc );
            puts( "" );
        }

        if( (uint16_t)rc < HIK_CALIBRATION_CHUNK_LEN )
        {
            break;
        }
        if( chunk_count >= 128 )
        {
            fprintf( stderr, "error: %s exceeded the safety limit of 128 chunks\n", label );
            free( wrapped );
            free( dechunked );
            return 1;
        }
    }

    printf( "Wrapped calibration stream: %zu chunk(s), %zu byte(s)\n", chunk_count, wrapped_len );
    printf( "Dechunked calibration payload: %zu byte(s)\n", dechunked_len );
    if( declared_stream_len != 0 )
    {
        printf( "Declared session payload length: %" PRIu32 " byte(s)\n", declared_stream_len );
        if( declared_stream_len != dechunked_len )
        {
            fprintf( stderr,
                     "warning: dechunked payload length %zu does not match declared session payload length %" PRIu32 "\n",
                     dechunked_len,
                     declared_stream_len );
        }
    }

    extracted_offset = find_subsequence( dechunked,
                                         dechunked_len,
                                         kCalibrationSignature,
                                         sizeof( kCalibrationSignature ) );
    if( extracted_offset != SIZE_MAX &&
        dechunked_len >= extracted_offset + HIK_EXPECTED_CALIBRATION_LEN )
    {
        extracted = true;
        printf( "Extracted calibration-file candidate at dechunked offset %zu (%u byte(s)).\n",
                extracted_offset,
                HIK_EXPECTED_CALIBRATION_LEN );
        print_calibration_preview( "Extracted calibration-file preview",
                                   dechunked + extracted_offset,
                                   HIK_EXPECTED_CALIBRATION_LEN );
    }
    else
    {
        puts( "Could not carve a clean calibration-file candidate from the dechunked payload with the current header heuristic." );
        print_calibration_preview( "Dechunked payload preview", dechunked, dechunked_len );
    }

    puts( "" );
    print_ascii_fragments( dechunked, dechunked_len );

    if( opts->raw_out_path != NULL )
    {
        const uint8_t *to_write = dechunked;
        size_t         write_len = dechunked_len;

        if( extracted )
        {
            to_write  = dechunked + extracted_offset;
            write_len = HIK_EXPECTED_CALIBRATION_LEN;
        }

        if( write_blob( opts->raw_out_path, to_write, write_len ) != 0 )
        {
            free( wrapped );
            free( dechunked );
            return 1;
        }

        printf( "\nSaved %s to %s (%zu byte(s)).\n",
                extracted ? "extracted calibration-file candidate" : "dechunked calibration payload",
                opts->raw_out_path,
                write_len );
    }

    free( wrapped );
    free( dechunked );
    return 0;
}

static int run_enhancement_patch( const HikSession *session,
                                  const HikOptions *opts,
                                  const HikProfile *profile,
                                  const char       *label )
{
    HikLengthInfo length_info;
    uint8_t      *current            = NULL;
    uint8_t      *updated            = NULL;
    uint16_t      effective_blob_len = 0;
    int           rc                 = 0;
    size_t        changed            = 0;

    if( !hik_enhancement_overrides_requested( opts ) )
    {
        fprintf( stderr,
                 "error: %s requires at least one enhancement option such as --palette or --noise-reduce-mode\n",
                 profile->name );
        return 1;
    }

    rc = hik_fetch_control_length( session, opts, profile->selector, &length_info );
    if( rc != 0 )
    {
        fprintf( stderr,
                 "error: %s GET_LEN failed: %s\n",
                 label,
                 hik_transport_error( session->transport, rc ) );
        return rc;
    }

    hik_print_length_report( label, &length_info );
    effective_blob_len = opts->force_len_set ? opts->force_len
                                             : hik_select_reported_length( length_info.decoded_length,
                                                                           profile->expected_len );
    if( opts->force_len_set )
    {
        printf( "%s using forced length override: %u byte(s)\n", label, opts->force_len );
    }
    if( effective_blob_len != HIK_EXPECTED_ENHANCEMENT_LEN )
    {
        fprintf( stderr,
                 "error: %s expected a %u-byte enhancement payload but the device reported %u\n",
                 label,
                 HIK_EXPECTED_ENHANCEMENT_LEN,
                 length_info.decoded_length );
        if( !opts->force_len_set && length_info.decoded_length == 2 )
        {
            fprintf( stderr,
                     "hint: selector 0x02 is currently exposing the 2-byte scalar form on this device state.\n"
                     "hint: try reproducing the Android/vendor initialization first, or experiment with\n"
                     "hint: --force-len 24 or the selector-state-scan profile.\n" );
        }
        return 1;
    }

    current = calloc( effective_blob_len, 1 );
    updated = calloc( effective_blob_len, 1 );
    if( current == NULL || updated == NULL )
    {
        fprintf( stderr, "error: out of memory allocating %" PRIu16 " bytes\n", effective_blob_len );
        free( current );
        free( updated );
        return 1;
    }

    rc = hik_fetch_current_value( session, opts, profile->selector, current, effective_blob_len );
    if( rc < 0 )
    {
        fprintf( stderr,
                 "error: %s GET_CUR failed: %s\n",
                 label,
                 hik_transport_error( session->transport, rc ) );
        free( current );
        free( updated );
        return rc;
    }

    if( (uint16_t)rc != effective_blob_len )
    {
        fprintf( stderr,
                 "warning: %s GET_CUR returned %d bytes but GET_LEN advertised %" PRIu16 "\n",
                 label,
                 rc,
                 effective_blob_len );
    }

    memcpy( updated, current, effective_blob_len );
    changed = hik_apply_enhancement_patch( updated, effective_blob_len, opts );

    puts( "Current image enhancement state:" );
    hik_print_enhancement_summary( current, effective_blob_len );
    printf( "Current payload bytes: " );
    print_hex_bytes_inline( current, effective_blob_len );
    printf( "\n" );

    if( changed == 0 )
    {
        puts( "Requested enhancement values already match the current payload." );
    }
    else
    {
        printf( "Patched %zu byte(s) in the enhancement payload.\n", changed );
    }

    puts( "Updated image enhancement state:" );
    hik_print_enhancement_summary( updated, effective_blob_len );
    printf( "Updated payload bytes: " );
    print_hex_bytes_inline( updated, effective_blob_len );
    printf( "\n" );

    rc = hik_set_current_value( session, opts, profile->selector, updated, effective_blob_len );
    if( rc < 0 )
    {
        fprintf( stderr,
                 "error: %s SET_CUR failed: %s\n",
                 label,
                 hik_transport_error( session->transport, rc ) );
        free( current );
        free( updated );
        return rc;
    }

    printf( "%s SET_CUR wrote %d byte(s): ", label, rc );
    print_hex_bytes_inline( updated, effective_blob_len );
    printf( "\n" );

    if( opts->raw_out_path != NULL && write_blob( opts->raw_out_path, updated, effective_blob_len ) != 0 )
    {
        free( current );
        free( updated );
        return 1;
    }

    free( current );
    free( updated );
    return 0;
}

static int run_thermometry_patch( const HikSession *session,
                                  const HikOptions *opts,
                                  const HikProfile *profile,
                                  const char       *label )
{
    HikLengthInfo length_info;
    uint8_t      *current            = NULL;
    uint8_t      *updated            = NULL;
    uint16_t      effective_blob_len = 0;
    int           rc                 = 0;
    size_t        changed            = 0;

    rc = hik_fetch_control_length( session, opts, profile->selector, &length_info );
    if( rc != 0 )
    {
        fprintf( stderr,
                 "error: %s GET_LEN failed: %s\n",
                 label,
                 hik_transport_error( session->transport, rc ) );
        return rc;
    }

    hik_print_length_report( label, &length_info );
    effective_blob_len = opts->force_len_set ? opts->force_len
                                             : hik_select_reported_length( length_info.decoded_length,
                                                                           profile->expected_len );
    if( opts->force_len_set )
    {
        printf( "%s using forced length override: %u byte(s)\n", label, opts->force_len );
    }
    if( effective_blob_len != HIK_THERMOMETRY_TYPED_LEN )
    {
        fprintf( stderr,
                 "error: %s expected a %u-byte thermometry payload but the device reported %u\n",
                 label,
                 HIK_THERMOMETRY_TYPED_LEN,
                 length_info.decoded_length );
        return 1;
    }

    current = calloc( effective_blob_len, 1 );
    updated = calloc( effective_blob_len, 1 );
    if( current == NULL || updated == NULL )
    {
        fprintf( stderr, "error: out of memory allocating %" PRIu16 " bytes\n", effective_blob_len );
        free( current );
        free( updated );
        return 1;
    }

    rc = hik_fetch_current_value( session, opts, profile->selector, current, effective_blob_len );
    if( rc < 0 )
    {
        fprintf( stderr,
                 "error: %s GET_CUR failed: %s\n",
                 label,
                 hik_transport_error( session->transport, rc ) );
        free( current );
        free( updated );
        return rc;
    }

    if( (uint16_t)rc != effective_blob_len )
    {
        fprintf( stderr,
                 "warning: %s GET_CUR returned %d bytes but GET_LEN advertised %" PRIu16 "\n",
                 label,
                 rc,
                 effective_blob_len );
    }

    memcpy( updated, current, effective_blob_len );
    changed = hik_apply_thermometry_patch( updated, effective_blob_len, opts );

    puts( "Current thermometry basic state:" );
    hik_print_thermometry_summary( current, effective_blob_len );
    printf( "Current payload bytes: " );
    print_hex_bytes_inline( current, effective_blob_len );
    printf( "\n" );

    if( changed == 0 )
    {
        puts( "Requested thermometry values already match the current payload." );
    }
    else
    {
        printf( "Patched %zu byte(s) in the thermometry payload.\n", changed );
    }

    puts( "Updated thermometry basic state:" );
    hik_print_thermometry_summary( updated, effective_blob_len );
    printf( "Updated payload bytes: " );
    print_hex_bytes_inline( updated, effective_blob_len );
    printf( "\n" );

    rc = hik_set_current_value( session, opts, profile->selector, updated, effective_blob_len );
    if( rc < 0 )
    {
        fprintf( stderr,
                 "error: %s SET_CUR failed: %s\n",
                 label,
                 hik_transport_error( session->transport, rc ) );
        free( current );
        free( updated );
        return rc;
    }

    printf( "%s SET_CUR wrote %d byte(s): ", label, rc );
    print_hex_bytes_inline( updated, effective_blob_len );
    printf( "\n" );

    if( opts->raw_out_path != NULL && write_blob( opts->raw_out_path, updated, effective_blob_len ) != 0 )
    {
        free( current );
        free( updated );
        return 1;
    }

    verify_thermometry_after_write( session, opts, profile, updated, effective_blob_len );

    free( current );
    free( updated );
    return 0;
}

static int run_expert_regions_zero_write( const HikSession *session,
                                          const HikOptions *opts,
                                          const HikProfile *profile )
{
    static const uint8_t kHeaderChunk[] = { 0x01, 0x6a, 0x0b, 0x00, 0x00 };
    uint8_t              chunk[512];
    int                  rc = 0;

    rc = run_write_stage( session,
                          opts,
                          profile->selector,
                          kHeaderChunk,
                          sizeof( kHeaderChunk ),
                          (uint16_t)sizeof( kHeaderChunk ),
                          "Expert regions header" );
    if( rc != 0 )
    {
        return rc;
    }

    for( uint8_t part = 1; part <= 5; ++part )
    {
        char label[64];

        memset( chunk, 0, sizeof( chunk ) );
        chunk[0] = 0x02;
        chunk[1] = part;
        snprintf( label, sizeof( label ), "Expert regions chunk %u/6", (unsigned int)part );
        rc = run_write_stage( session,
                              opts,
                              profile->selector,
                              chunk,
                              sizeof( chunk ),
                              (uint16_t)sizeof( chunk ),
                              label );
        if( rc != 0 )
        {
            return rc;
        }
    }

    memset( chunk, 0, sizeof( chunk ) );
    chunk[0] = 0x02;
    chunk[1] = 0x06;
    return run_write_stage( session, opts, profile->selector, chunk, 392, 392, "Expert regions chunk 6/6" );
}

static void verify_thermometry_after_write( const HikSession *session,
                                            const HikOptions *opts,
                                            const HikProfile *profile,
                                            const uint8_t    *expected,
                                            size_t            expected_len )
{
    enum
    {
        HIK_VERIFY_ATTEMPTS = 5,
        HIK_VERIFY_DELAY_MS = 200
    };

    uint8_t verify[HIK_THERMOMETRY_TYPED_LEN];
    int     verify_rc = -1;

    for( int attempt = 1; attempt <= HIK_VERIFY_ATTEMPTS; ++attempt )
    {
        if( profile->prewrite_payload_len != 0 )
        {
            int prewrite_rc = hik_set_current_value( session,
                                                     opts,
                                                     profile->prewrite_selector,
                                                     profile->prewrite_payload,
                                                     (uint16_t)profile->prewrite_payload_len );

            if( prewrite_rc < 0 )
            {
                fprintf( stderr,
                         "warning: thermometry verification prewrite failed on attempt %d: %s\n",
                         attempt,
                         hik_transport_error( session->transport, prewrite_rc ) );
                sleep_ms( HIK_VERIFY_DELAY_MS );
                continue;
            }
        }

        verify_rc = hik_fetch_current_value( session, opts, profile->selector, verify, sizeof( verify ) );
        if( verify_rc < 0 )
        {
            fprintf( stderr,
                     "warning: verification GET_CUR failed on attempt %d: %s\n",
                     attempt,
                     hik_transport_error( session->transport, verify_rc ) );
            sleep_ms( HIK_VERIFY_DELAY_MS );
            continue;
        }

        if( (size_t)verify_rc == expected_len && memcmp( verify, expected, expected_len ) == 0 )
        {
            printf( "Verified thermometry basic state on attempt %d:\n", attempt );
            hik_print_thermometry_summary( verify, (size_t)verify_rc );
            printf( "Verified payload bytes: " );
            print_hex_bytes_inline( verify, (size_t)verify_rc );
            printf( "\n" );
            return;
        }

        if( attempt < HIK_VERIFY_ATTEMPTS )
        {
            sleep_ms( HIK_VERIFY_DELAY_MS );
        }
    }

    if( verify_rc >= 0 )
    {
        printf( "Post-write thermometry state did not match after %d verification attempt(s):\n",
                HIK_VERIFY_ATTEMPTS );
        hik_print_thermometry_summary( verify, (size_t)verify_rc );
        printf( "Observed payload bytes: " );
        print_hex_bytes_inline( verify, (size_t)verify_rc );
        printf( "\n" );
    }
}

int hik_execute_profile( const HikSession *session, const HikOptions *opts, const HikProfile *profile )
{
    uint8_t payload[HIK_MAX_PROFILE_PAYLOAD_BYTES];
    size_t  payload_len = 0;
    int     rc          = 0;

    if( profile->kind == HIK_PROFILE_SCAN )
    {
        return run_selector_state_scan( session, opts );
    }

    if( profile->kind == HIK_PROFILE_READ )
    {
        rc = maybe_run_profile_prewrite( session, opts, profile );
        if( rc != 0 )
        {
            return rc;
        }
        if( profile->read_output_kind == HIK_READ_OUTPUT_THERMOMETRY_CALIBRATION )
        {
            return run_calibration_file_read( session, opts, profile, "Thermometry calibration file" );
        }
        return run_read_selector( session, opts, profile, "Selector read", opts->raw_out_path );
    }

    if( profile->write_shape == HIK_WRITE_SHAPE_ENHANCEMENT_PATCH )
    {
        rc = maybe_run_profile_prewrite( session, opts, profile );
        if( rc != 0 )
        {
            return rc;
        }
        return run_enhancement_patch( session, opts, profile, "Image enhancement" );
    }
    if( profile->write_shape == HIK_WRITE_SHAPE_THERMOMETRY_PATCH )
    {
        rc = maybe_run_profile_prewrite( session, opts, profile );
        if( rc != 0 )
        {
            return rc;
        }
        return run_thermometry_patch( session, opts, profile, "Thermometry basic" );
    }
    if( profile->write_shape == HIK_WRITE_SHAPE_EXPERT_REGIONS_ZERO )
    {
        rc = maybe_run_profile_prewrite( session, opts, profile );
        if( rc != 0 )
        {
            return rc;
        }
        return run_expert_regions_zero_write( session, opts, profile );
    }

    if( !hik_build_profile_payload( profile, opts, payload, &payload_len ) )
    {
        fprintf( stderr, "error: could not build payload for profile %s\n", profile->name );
        return 1;
    }

    rc = maybe_run_profile_prewrite( session, opts, profile );
    if( rc != 0 )
    {
        return rc;
    }

    return run_write_stage( session,
                            opts,
                            profile->selector,
                            payload,
                            payload_len,
                            profile->expected_len,
                            "Main stage" );
}
