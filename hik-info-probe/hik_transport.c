#include "hik_probe.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/uvcvideo.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static int xioctl( int fd, unsigned long request, void *arg )
{
    int rc = 0;

    do
    {
        rc = ioctl( fd, request, arg );
    } while( rc == -1 && errno == EINTR );

    return rc;
}

static uint16_t make_w_value( uint8_t selector )
{
    return (uint16_t)selector << 8;
}

static uint16_t make_w_index( const HikOptions *opts )
{
    return ( (uint16_t)opts->unit_id << 8 ) | opts->interface_number;
}

static int xu_query_v4l2( const HikOptions *opts,
                          uint8_t           selector,
                          uint8_t           query,
                          uint8_t          *buf,
                          uint16_t          size )
{
    struct uvc_xu_control_query query_args;
    const struct timespec       retry_sleep = { 0, 20 * 1000 * 1000 };
    int                         fd          = -1;
    int                         rc          = 0;

    if( opts->video_device == NULL )
    {
        errno = EINVAL;
        return -1;
    }

    fd = open( opts->video_device, O_RDWR );
    if( fd < 0 )
    {
        return -1;
    }

    memset( &query_args, 0, sizeof( query_args ) );
    query_args.unit     = opts->unit_id;
    query_args.selector = selector;
    query_args.query    = query;
    query_args.size     = size;
    query_args.data     = buf;

    for( int attempt = 0; attempt < 4; ++attempt )
    {
        rc = xioctl( fd, UVCIOC_CTRL_QUERY, &query_args );
        if( rc == 0 )
        {
            close( fd );
            return (int)size;
        }
        if( errno != ENOBUFS && errno != EAGAIN )
        {
            int saved_errno = errno;

            close( fd );
            errno = saved_errno;
            return -1;
        }
        nanosleep( &retry_sleep, NULL );
    }

    {
        int saved_errno = errno;

        close( fd );
        errno = saved_errno;
    }
    return -1;
}

static const char *usb_string_or_unknown( libusb_device_handle *handle,
                                          uint8_t               index,
                                          char                 *buf,
                                          size_t                buf_len )
{
    int rc = 0;

    if( index == 0 )
    {
        snprintf( buf, buf_len, "<none>" );
        return buf;
    }

    rc = libusb_get_string_descriptor_ascii( handle, index, (unsigned char *)buf, (int)buf_len - 1 );
    if( rc < 0 )
    {
        snprintf( buf, buf_len, "<unavailable: %s>", libusb_error_name( rc ) );
        return buf;
    }

    buf[rc] = '\0';
    return buf;
}

const char *hik_transport_name( HikTransportKind transport )
{
    switch( transport )
    {
        case HIK_TRANSPORT_AUTO:
            return "auto";
        case HIK_TRANSPORT_LIBUSB:
            return "libusb";
        case HIK_TRANSPORT_V4L2:
            return "v4l2";
        default:
            return "unknown";
    }
}

const char *hik_transport_error( HikTransportKind transport, int rc )
{
    if( transport == HIK_TRANSPORT_V4L2 )
    {
        return strerror( errno );
    }
    return libusb_error_name( rc );
}

HikTransportKind hik_resolve_transport( const HikOptions *opts, const HikProfile *profile )
{
    if( opts->transport != HIK_TRANSPORT_AUTO )
    {
        return opts->transport;
    }
    if( profile->kind == HIK_PROFILE_WRITE && opts->video_device != NULL )
    {
        return HIK_TRANSPORT_V4L2;
    }
    return HIK_TRANSPORT_LIBUSB;
}

int hik_open_session( HikSession *session, const HikOptions *opts, HikTransportKind transport )
{
    int rc = 0;

    memset( session, 0, sizeof( *session ) );
    session->transport = transport;

    if( transport != HIK_TRANSPORT_LIBUSB )
    {
        return 0;
    }

    rc = libusb_init( &session->usb_context );
    if( rc != 0 )
    {
        fprintf( stderr, "error: libusb_init failed: %s\n", libusb_error_name( rc ) );
        return rc;
    }

    if( opts->verbose )
    {
        libusb_set_option( session->usb_context, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_INFO );
    }

    session->usb_handle = libusb_open_device_with_vid_pid( session->usb_context, opts->vid, opts->pid );
    if( session->usb_handle == NULL )
    {
        fprintf( stderr,
                 "error: could not open device %04x:%04x\n"
                 "hint: make sure the camera is connected and you have permission to access it.\n",
                 opts->vid,
                 opts->pid );
        libusb_exit( session->usb_context );
        session->usb_context = NULL;
        return LIBUSB_ERROR_NO_DEVICE;
    }

    rc = libusb_get_device_descriptor( libusb_get_device( session->usb_handle ), &session->usb_descriptor );
    if( rc != 0 )
    {
        fprintf( stderr, "error: libusb_get_device_descriptor failed: %s\n", libusb_error_name( rc ) );
        hik_close_session( session, opts );
        return rc;
    }
    session->usb_descriptor_valid = true;

    if( opts->auto_detach )
    {
        rc = libusb_set_auto_detach_kernel_driver( session->usb_handle, 1 );
        if( rc == LIBUSB_ERROR_NOT_SUPPORTED )
        {
            fprintf( stderr, "warning: auto-detach not supported on this platform; continuing\n" );
        }
        else if( rc != 0 )
        {
            fprintf( stderr, "error: could not enable auto-detach: %s\n", libusb_error_name( rc ) );
            hik_close_session( session, opts );
            return rc;
        }
    }

    rc = libusb_claim_interface( session->usb_handle, opts->interface_number );
    if( rc != 0 )
    {
        fprintf( stderr,
                 "error: libusb_claim_interface(%u) failed: %s\n"
                 "hint: if uvcvideo owns the interface, try running without another camera client, or use sudo.\n",
                 opts->interface_number,
                 libusb_error_name( rc ) );
        hik_close_session( session, opts );
        return rc;
    }

    return 0;
}

void hik_close_session( HikSession *session, const HikOptions *opts )
{
    if( session->transport == HIK_TRANSPORT_LIBUSB && session->usb_handle != NULL )
    {
        libusb_release_interface( session->usb_handle, opts->interface_number );
        libusb_close( session->usb_handle );
        session->usb_handle = NULL;
    }

    if( session->usb_context != NULL )
    {
        libusb_exit( session->usb_context );
        session->usb_context = NULL;
    }
}

void hik_print_connection_summary( const HikSession *session, const HikOptions *opts )
{
    if( session->transport == HIK_TRANSPORT_V4L2 )
    {
        printf( "Using V4L2 control device %s\n", opts->video_device );
        return;
    }

    if( session->usb_descriptor_valid )
    {
        char manufacturer[256];
        char product[256];
        char serial[256];

        printf( "Opened %04x:%04x\n", session->usb_descriptor.idVendor, session->usb_descriptor.idProduct );
        printf( "  manufacturer: %s\n",
                usb_string_or_unknown( session->usb_handle,
                                       session->usb_descriptor.iManufacturer,
                                       manufacturer,
                                       sizeof( manufacturer ) ) );
        printf( "  product:      %s\n",
                usb_string_or_unknown( session->usb_handle,
                                       session->usb_descriptor.iProduct,
                                       product,
                                       sizeof( product ) ) );
        printf( "  serial:       %s\n",
                usb_string_or_unknown( session->usb_handle,
                                       session->usb_descriptor.iSerialNumber,
                                       serial,
                                       sizeof( serial ) ) );
    }
}

static int query_get_len( const HikSession *session,
                          const HikOptions *opts,
                          uint8_t           selector,
                          HikLengthInfo    *length_out )
{
    memset( length_out->raw, 0, sizeof( length_out->raw ) );

    if( session->transport == HIK_TRANSPORT_V4L2 )
    {
        length_out->raw_len = xu_query_v4l2( opts, selector, HIK_GET_LEN, length_out->raw, 2 );
        return length_out->raw_len;
    }

    length_out->raw_len = libusb_control_transfer( session->usb_handle,
                                                   HIK_XU_GET_BMREQUESTTYPE,
                                                   HIK_GET_LEN,
                                                   make_w_value( selector ),
                                                   make_w_index( opts ),
                                                   length_out->raw,
                                                   4,
                                                   (unsigned int)opts->timeout_ms );
    return length_out->raw_len;
}

int hik_fetch_control_length( const HikSession *session,
                              const HikOptions *opts,
                              uint8_t           selector,
                              HikLengthInfo    *length_out )
{
    int rc = query_get_len( session, opts, selector, length_out );

    if( rc < 0 )
    {
        return rc;
    }
    if( length_out->raw_len < 2 )
    {
        return LIBUSB_ERROR_IO;
    }

    length_out->decoded_length = (uint16_t)( length_out->raw[0] | ( length_out->raw[1] << 8 ) );
    return 0;
}

int hik_fetch_current_value( const HikSession *session,
                             const HikOptions *opts,
                             uint8_t           selector,
                             uint8_t          *buf,
                             uint16_t          len )
{
    if( session->transport == HIK_TRANSPORT_V4L2 )
    {
        if( selector == HIK_ENHANCEMENT_SELECTOR && len == HIK_EXPECTED_ENHANCEMENT_LEN )
        {
            uint8_t padded_buf[HIK_V4L2_SELECTOR2_BUFFER_LEN];
            int     rc = 0;

            memset( padded_buf, 0, sizeof( padded_buf ) );
            rc = xu_query_v4l2( opts,
                                selector,
                                HIK_GET_CUR,
                                padded_buf,
                                HIK_V4L2_SELECTOR2_BUFFER_LEN );
            if( rc < 0 )
            {
                return -1;
            }
            memcpy( buf, padded_buf, len );
            return (int)len;
        }

        {
            int rc = xu_query_v4l2( opts, selector, HIK_GET_CUR, buf, len );

            if( rc >= 0 )
            {
                return rc;
            }

            if( selector == HIK_ENHANCEMENT_SELECTOR && len < HIK_V4L2_SELECTOR2_BUFFER_LEN )
            {
                uint8_t padded_buf[HIK_V4L2_SELECTOR2_BUFFER_LEN];

                memset( padded_buf, 0, sizeof( padded_buf ) );
                rc = xu_query_v4l2( opts,
                                    selector,
                                    HIK_GET_CUR,
                                    padded_buf,
                                    HIK_V4L2_SELECTOR2_BUFFER_LEN );
                if( rc >= 0 )
                {
                    memcpy( buf, padded_buf, len );
                    return (int)len;
                }
            }

            return -1;
        }
    }

    return libusb_control_transfer( session->usb_handle,
                                    HIK_XU_GET_BMREQUESTTYPE,
                                    HIK_GET_CUR,
                                    make_w_value( selector ),
                                    make_w_index( opts ),
                                    buf,
                                    len,
                                    (unsigned int)opts->timeout_ms );
}

int hik_set_current_value( const HikSession *session,
                           const HikOptions *opts,
                           uint8_t           selector,
                           const uint8_t    *buf,
                           uint16_t          len )
{
    if( session->transport == HIK_TRANSPORT_V4L2 )
    {
        if( selector == HIK_ENHANCEMENT_SELECTOR && len == HIK_EXPECTED_ENHANCEMENT_LEN )
        {
            uint8_t padded_buf[HIK_V4L2_SELECTOR2_BUFFER_LEN];
            int     rc = 0;

            memset( padded_buf, 0, sizeof( padded_buf ) );
            memcpy( padded_buf, buf, len );
            rc = xu_query_v4l2( opts,
                                selector,
                                HIK_SET_CUR,
                                padded_buf,
                                HIK_V4L2_SELECTOR2_BUFFER_LEN );
            if( rc < 0 )
            {
                return -1;
            }
            return (int)len;
        }

        {
            int rc = xu_query_v4l2( opts, selector, HIK_SET_CUR, (uint8_t *)buf, len );

            if( rc >= 0 )
            {
                return rc;
            }

            if( selector == HIK_ENHANCEMENT_SELECTOR && len == HIK_EXPECTED_ENHANCEMENT_LEN )
            {
                uint8_t padded_buf[HIK_V4L2_SELECTOR2_BUFFER_LEN];

                memset( padded_buf, 0, sizeof( padded_buf ) );
                memcpy( padded_buf, buf, len );
                rc = xu_query_v4l2( opts,
                                    selector,
                                    HIK_SET_CUR,
                                    padded_buf,
                                    HIK_V4L2_SELECTOR2_BUFFER_LEN );
                if( rc >= 0 )
                {
                    return (int)len;
                }
            }

            return -1;
        }
    }

    return libusb_control_transfer( session->usb_handle,
                                    HIK_XU_SET_BMREQUESTTYPE,
                                    HIK_SET_CUR,
                                    make_w_value( selector ),
                                    make_w_index( opts ),
                                    (unsigned char *)buf,
                                    len,
                                    (unsigned int)opts->timeout_ms );
}

uint16_t hik_select_reported_length( uint16_t reported_len, uint16_t expected_len )
{
    if( expected_len != 0 && reported_len == expected_len )
    {
        return reported_len;
    }
    return reported_len;
}

void hik_print_length_report( const char *label, const HikLengthInfo *length_info )
{
    printf( "%s returned %d byte(s):", label, length_info->raw_len );
    for( int i = 0; i < length_info->raw_len; ++i )
    {
        printf( " %02x", length_info->raw[i] );
    }
    printf( "\n" );
    printf( "Decoded length: %" PRIu16 "\n", length_info->decoded_length );
}
