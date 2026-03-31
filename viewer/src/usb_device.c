#include "hti_usb_view.h"

#include <linux/usb/video.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

enum
{
    HTI_DEFAULT_VID                 = 0x2bdf,
    HTI_DEFAULT_PID                 = 0x0102,
    HTI_DEFAULT_CONTROL_INTERFACE   = 0,
    HTI_DEFAULT_STREAMING_INTERFACE = 1,
    HTI_DEFAULT_EXTENSION_UNIT      = 0x0a,
    HTI_DEFAULT_BULK_ENDPOINT       = 0x81,
    /* The camera reports UVC 1.10, so the packed Linux header size is 34 bytes. */
    HTI_UVC_PROBE_LEN           = (int)sizeof( struct uvc_streaming_control ),
    HTI_UVC_PROBE_REQUIRED_LEN  = 26,
    HTI_MIN_PAYLOAD_FALLBACK    = 8192,
    HTI_XU_GET_BMREQUESTTYPE    = 0xa1,
    HTI_XU_SET_BMREQUESTTYPE    = 0x21,
    HTI_XU_SET_CUR              = 0x01,
    HTI_XU_GET_LEN              = 0x85,
    HTI_XU_PRELUDE_SELECTOR     = 0x05,
    HTI_XU_TRIGGER_SELECTOR     = 0x02,
    HTI_UVC_SET_BMREQUESTTYPE   = LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
    HTI_UVC_GET_BMREQUESTTYPE   = LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
    HTI_UVC_SET_CUR             = 0x01,
    HTI_UVC_GET_CUR             = 0x81,
    HTI_UVC_VS_PROBE_CONTROL    = 0x01,
    HTI_UVC_VS_COMMIT_CONTROL   = 0x02,
    HTI_UVC_STREAM_EOF          = 1 << 1,
    HTI_EXPECTED_SHUTTER_MIN_MS = 100,
    HTI_STREAM_START_SETTLE_MS  = 100,
    HTI_BULK_RETRY_COUNT        = 8,
    HTI_BULK_RETRY_DELAY_MS     = 20,
};

static unsigned int transfer_timeout_ms( const struct hti_usb_device *dev )
{
    if( dev != NULL && dev->timeout_ms > 0 )
    {
        return (unsigned int)dev->timeout_ms;
    }
    return HTI_DEFAULT_TIMEOUT_MS;
}

static uint32_t fallback_frame_bytes( uint8_t frame_index )
{
    switch( frame_index )
    {
        case 1:
            return 200704u;
        case 2:
            return 98304u;
        case 3:
            return 100352u;
        case 4:
            return 201248u;
        case 5:
            return 98440u;
        case 6:
            return 100968u;
        case 7:
            return 199680u;
        case 8:
            return 203320u;
        default:
            return 0u;
    }
}

static void sleep_ms_local( long milliseconds )
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

static uint16_t make_xu_w_value( uint8_t selector )
{
    return (uint16_t)selector << 8;
}

static uint16_t make_xu_w_index( const struct hti_usb_device *dev )
{
    return ( (uint16_t)dev->extension_unit << 8 ) | dev->control_interface;
}

static int open_matching_camera( libusb_context *ctx, uint16_t vid, uint16_t pid,
                                 libusb_device_handle **handle_out )
{
    libusb_device **devices      = NULL;
    ssize_t         device_count = libusb_get_device_list( ctx, &devices );
    int             rc           = LIBUSB_ERROR_NO_DEVICE;
    int             matched      = 0;

    if( device_count < 0 )
    {
        return (int)device_count;
    }

    for( ssize_t i = 0; i < device_count; i++ )
    {
        libusb_device                  *device = devices[i];
        struct libusb_device_descriptor descriptor;

        if( libusb_get_device_descriptor( device, &descriptor ) != 0 )
        {
            continue;
        }

        if( descriptor.idVendor != vid || descriptor.idProduct != pid )
        {
            continue;
        }

        matched = 1;
        rc      = libusb_open( device, handle_out );
        if( rc == 0 )
        {
            libusb_free_device_list( devices, 1 );
            return 0;
        }
    }

    libusb_free_device_list( devices, 1 );
    if( !matched )
    {
        return LIBUSB_ERROR_NO_DEVICE;
    }

    return rc;
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

static void dump_device_info_strings( libusb_device_handle *handle )
{
    struct libusb_device_descriptor descriptor;
    char                            manufacturer[256];
    char                            product[256];
    char                            serial[256];
    int                             rc = libusb_get_device_descriptor( libusb_get_device( handle ), &descriptor );

    if( rc != 0 )
    {
        fprintf( stderr, "warning: libusb_get_device_descriptor failed: %s\n", libusb_error_name( rc ) );
        return;
    }

    fprintf( stderr, "opened %04x:%04x\n", descriptor.idVendor, descriptor.idProduct );
    fprintf( stderr, "  manufacturer: %s\n",
             usb_string_or_unknown( handle,
                                    descriptor.iManufacturer,
                                    manufacturer,
                                    sizeof( manufacturer ) ) );
    fprintf( stderr, "  product:      %s\n",
             usb_string_or_unknown( handle,
                                    descriptor.iProduct,
                                    product,
                                    sizeof( product ) ) );
    fprintf( stderr, "  serial:       %s\n",
             usb_string_or_unknown( handle,
                                    descriptor.iSerialNumber,
                                    serial,
                                    sizeof( serial ) ) );
}

static int uvc_control_get( struct hti_usb_device *dev, uint8_t selector,
                            uint8_t *buf, uint16_t len, int *actual_len_out )
{
    int rc = libusb_control_transfer( dev->handle,
                                      HTI_UVC_GET_BMREQUESTTYPE,
                                      HTI_UVC_GET_CUR,
                                      (uint16_t)selector << 8,
                                      dev->streaming_interface,
                                      buf,
                                      len,
                                      transfer_timeout_ms( dev ) );
    if( rc < 0 )
    {
        return rc;
    }

    if( actual_len_out != NULL )
    {
        *actual_len_out = rc;
    }

    if( rc < HTI_UVC_PROBE_REQUIRED_LEN || rc > len )
    {
        return LIBUSB_ERROR_IO;
    }

    return 0;
}

static int uvc_control_set( struct hti_usb_device *dev, uint8_t selector,
                            const uint8_t *buf, uint16_t len )
{
    int rc = libusb_control_transfer( dev->handle,
                                      HTI_UVC_SET_BMREQUESTTYPE,
                                      HTI_UVC_SET_CUR,
                                      (uint16_t)selector << 8,
                                      dev->streaming_interface,
                                      (unsigned char *)buf,
                                      len,
                                      transfer_timeout_ms( dev ) );
    if( rc < 0 )
    {
        return rc;
    }

    if( (uint16_t)rc != len )
    {
        return LIBUSB_ERROR_IO;
    }

    return 0;
}

static int negotiate_stream( struct hti_usb_device           *dev,
                             const struct hti_stream_request *request,
                             bool                             verbose )
{
    uint8_t probe[HTI_UVC_PROBE_LEN];
    int     actual_len = 0;
    int     rc         = 0;

    memset( probe, 0, sizeof( probe ) );
    hti_write_le16( probe + 0, 1u );
    probe[2] = request->format_index;
    probe[3] = request->frame_index;
    hti_write_le32( probe + 4, request->frame_interval_100ns );
    hti_write_le32( probe + 18, fallback_frame_bytes( request->frame_index ) );

    rc = uvc_control_set( dev, HTI_UVC_VS_PROBE_CONTROL, probe, sizeof( probe ) );
    if( rc != 0 )
    {
        return rc;
    }

    rc = uvc_control_get( dev, HTI_UVC_VS_PROBE_CONTROL, probe, sizeof( probe ), &actual_len );
    if( rc != 0 )
    {
        return rc;
    }

    dev->negotiated_frame_bytes   = hti_read_le32( probe + 18 );
    dev->negotiated_payload_bytes = hti_read_le32( probe + 22 );

    if( dev->negotiated_frame_bytes == 0 )
    {
        dev->negotiated_frame_bytes = fallback_frame_bytes( request->frame_index );
    }

    if( dev->negotiated_payload_bytes == 0 )
    {
        dev->negotiated_payload_bytes = HTI_MIN_PAYLOAD_FALLBACK;
    }

    if( dev->negotiated_payload_bytes > sizeof( dev->packet_buf ) )
    {
        dev->negotiated_payload_bytes = sizeof( dev->packet_buf );
    }

    rc = uvc_control_set( dev, HTI_UVC_VS_COMMIT_CONTROL, probe, sizeof( probe ) );
    if( rc != 0 )
    {
        return rc;
    }

    if( verbose )
    {
        fprintf( stderr,
                 "uvc stream negotiated: format=%u frame=%u interval=%u frame_bytes=%u payload_bytes=%u probe_len=%d returned=%d\n",
                 probe[2],
                 probe[3],
                 hti_read_le32( probe + 4 ),
                 dev->negotiated_frame_bytes,
                 dev->negotiated_payload_bytes,
                 (int)sizeof( probe ),
                 actual_len );
    }
    return 0;
}

static int xu_get_len( struct hti_usb_device *dev, uint8_t selector, uint16_t *decoded_len )
{
    uint8_t len_buf[4] = { 0 };
    int     rc         = libusb_control_transfer( dev->handle,
                                                  HTI_XU_GET_BMREQUESTTYPE,
                                                  HTI_XU_GET_LEN,
                                                  make_xu_w_value( selector ),
                                                  make_xu_w_index( dev ),
                                                  len_buf,
                                                  sizeof( len_buf ),
                                                  transfer_timeout_ms( dev ) );
    if( rc < 0 )
    {
        return rc;
    }

    if( rc < 2 )
    {
        return LIBUSB_ERROR_IO;
    }

    *decoded_len = hti_read_le16( len_buf );
    return 0;
}

static int xu_set_cur( struct hti_usb_device *dev, uint8_t selector,
                       const uint8_t *payload, uint16_t payload_len )
{
    int rc = libusb_control_transfer( dev->handle,
                                      HTI_XU_SET_BMREQUESTTYPE,
                                      HTI_XU_SET_CUR,
                                      make_xu_w_value( selector ),
                                      make_xu_w_index( dev ),
                                      (unsigned char *)payload,
                                      payload_len,
                                      transfer_timeout_ms( dev ) );
    if( rc < 0 )
    {
        return rc;
    }

    if( rc != (int)payload_len )
    {
        return LIBUSB_ERROR_IO;
    }

    return 0;
}

static int claim_interface_with_detach( struct hti_usb_device *dev,
                                        uint8_t                interface_number,
                                        bool                  *detached_out )
{
    int rc     = 0;
    int active = libusb_kernel_driver_active( dev->handle, interface_number );

    if( detached_out != NULL )
    {
        *detached_out = false;
    }

    if( active > 0 )
    {
        rc = libusb_detach_kernel_driver( dev->handle, interface_number );
        if( rc != 0 && rc != LIBUSB_ERROR_NOT_FOUND && rc != LIBUSB_ERROR_NOT_SUPPORTED )
        {
            return rc;
        }
        if( rc == 0 && detached_out != NULL )
        {
            *detached_out = true;
        }
    }

    rc = libusb_claim_interface( dev->handle, interface_number );
    if( rc == LIBUSB_ERROR_BUSY )
    {
        int detach_rc = libusb_detach_kernel_driver( dev->handle, interface_number );

        if( detach_rc == 0 || detach_rc == LIBUSB_ERROR_NOT_FOUND || detach_rc == LIBUSB_ERROR_NOT_SUPPORTED )
        {
            rc = libusb_claim_interface( dev->handle, interface_number );
            if( rc == 0 && detach_rc == 0 && detached_out != NULL )
            {
                *detached_out = true;
            }
        }
    }

    return rc;
}

int hti_usb_open( struct hti_usb_device *dev, uint16_t vid, uint16_t pid,
                  const struct hti_stream_request *request, bool verbose )
{
    int rc = 0;

    memset( dev, 0, sizeof( *dev ) );
    dev->vid                 = vid ? vid : HTI_DEFAULT_VID;
    dev->pid                 = pid ? pid : HTI_DEFAULT_PID;
    dev->control_interface   = HTI_DEFAULT_CONTROL_INTERFACE;
    dev->streaming_interface = HTI_DEFAULT_STREAMING_INTERFACE;
    dev->extension_unit      = HTI_DEFAULT_EXTENSION_UNIT;
    dev->bulk_endpoint       = HTI_DEFAULT_BULK_ENDPOINT;
    dev->timeout_ms          = ( request != NULL && request->timeout_ms > 0 )
                                   ? request->timeout_ms
                                   : HTI_DEFAULT_TIMEOUT_MS;

    rc = libusb_init( &dev->ctx );
    if( rc != 0 )
    {
        return rc;
    }

    if( verbose )
    {
        libusb_set_option( dev->ctx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_INFO );
    }

    rc = open_matching_camera( dev->ctx, dev->vid, dev->pid, &dev->handle );
    if( rc != 0 )
    {
        hti_usb_close( dev );
        return rc;
    }

    dump_device_info_strings( dev->handle );

    rc = libusb_set_auto_detach_kernel_driver( dev->handle, 1 );
    if( rc != 0 && rc != LIBUSB_ERROR_NOT_SUPPORTED )
    {
        hti_usb_close( dev );
        return rc;
    }

    rc = claim_interface_with_detach( dev, dev->control_interface, &dev->control_detached );
    if( rc != 0 )
    {
        hti_usb_close( dev );
        return rc;
    }

    rc = claim_interface_with_detach( dev, dev->streaming_interface, &dev->streaming_detached );
    if( rc != 0 )
    {
        hti_usb_close( dev );
        return rc;
    }

    rc = libusb_set_interface_alt_setting( dev->handle, dev->streaming_interface, 0 );
    if( rc != 0 && rc != LIBUSB_ERROR_NOT_FOUND && rc != LIBUSB_ERROR_PIPE )
    {
        hti_usb_close( dev );
        return rc;
    }

    rc = negotiate_stream( dev, request, verbose );
    if( rc != 0 )
    {
        if( verbose )
        {
            fprintf( stderr, "uvc stream negotiation failed: %s\n", libusb_error_name( rc ) );
        }

        hti_usb_close( dev );
        return rc;
    }

    sleep_ms_local( HTI_STREAM_START_SETTLE_MS );

    return 0;
}

void hti_usb_close( struct hti_usb_device *dev )
{
    if( dev->handle != NULL )
    {
        libusb_release_interface( dev->handle, dev->streaming_interface );
        libusb_release_interface( dev->handle, dev->control_interface );

        if( dev->streaming_detached )
        {
            libusb_attach_kernel_driver( dev->handle, dev->streaming_interface );
        }

        if( dev->control_detached )
        {
            libusb_attach_kernel_driver( dev->handle, dev->control_interface );
        }
        libusb_close( dev->handle );
    }

    if( dev->ctx != NULL )
    {
        libusb_exit( dev->ctx );
    }

    memset( dev, 0, sizeof( *dev ) );
}

int hti_usb_read_frame( struct hti_usb_device *dev, struct hti_transport_frame *frame )
{
    uint32_t packet_request    = dev->negotiated_payload_bytes;
    size_t   total_len         = 0;
    int      transient_retries = 0;

    memset( frame, 0, sizeof( *frame ) );

    if( packet_request < 2u )
    {
        packet_request = HTI_MIN_PAYLOAD_FALLBACK;
    }

    if( packet_request > sizeof( dev->packet_buf ) )
    {
        packet_request = sizeof( dev->packet_buf );
    }

    for( ;; )
    {
        int     actual      = 0;
        int     rc          = libusb_bulk_transfer( dev->handle,
                                                    dev->bulk_endpoint,
                                                    dev->packet_buf,
                                                    (int)packet_request,
                                                    &actual,
                                                    transfer_timeout_ms( dev ) );
        uint8_t header_len  = 0;
        uint8_t flags       = 0;
        size_t  payload_len = 0;

        if( rc != 0 )
        {
            if( ( rc == LIBUSB_ERROR_IO || rc == LIBUSB_ERROR_TIMEOUT ) && transient_retries < HTI_BULK_RETRY_COUNT )
            {
                transient_retries++;
                sleep_ms_local( HTI_BULK_RETRY_DELAY_MS );
                continue;
            }

            if( rc == LIBUSB_ERROR_PIPE && transient_retries < HTI_BULK_RETRY_COUNT )
            {
                transient_retries++;
                libusb_clear_halt( dev->handle, dev->bulk_endpoint );
                sleep_ms_local( HTI_BULK_RETRY_DELAY_MS );
                continue;
            }

            return rc;
        }

        if( actual < 2 )
        {
            if( transient_retries < HTI_BULK_RETRY_COUNT )
            {
                transient_retries++;
                sleep_ms_local( HTI_BULK_RETRY_DELAY_MS );
                continue;
            }
            return LIBUSB_ERROR_IO;
        }

        transient_retries = 0;

        header_len = dev->packet_buf[0];
        flags      = dev->packet_buf[1];
        if( header_len < 2 || header_len > (uint8_t)actual )
        {
            return LIBUSB_ERROR_IO;
        }

        payload_len = (size_t)actual - header_len;
        if( total_len + payload_len > sizeof( frame->bytes ) )
        {
            return LIBUSB_ERROR_OVERFLOW;
        }

        if( payload_len > 0 )
        {
            memcpy( frame->bytes + total_len, dev->packet_buf + header_len, payload_len );
            total_len += payload_len;
        }

        if( flags & HTI_UVC_STREAM_EOF )
        {
            frame->length   = total_len;
            frame->sequence = ++dev->frame_sequence;
            snprintf( frame->name, sizeof( frame->name ), "live_%06llu",
                      (unsigned long long)frame->sequence );
            return 0;
        }
    }
}

int hti_usb_trigger_shutter( struct hti_usb_device *dev )
{
    static const uint8_t prelude_payload[2] = { 0x02, 0x04 };
    static const uint8_t trigger_payload[1] = { 0x01 };
    struct timespec      start;
    struct timespec      end;
    long                 elapsed_ms  = -1;
    uint16_t             prelude_len = 0;
    uint16_t             trigger_len = 0;
    int                  rc          = xu_get_len( dev, HTI_XU_PRELUDE_SELECTOR, &prelude_len );

    if( rc != 0 )
    {
        return rc;
    }

    rc = xu_set_cur( dev, HTI_XU_PRELUDE_SELECTOR, prelude_payload, sizeof( prelude_payload ) );
    if( rc != 0 )
    {
        return rc;
    }

    rc = xu_get_len( dev, HTI_XU_TRIGGER_SELECTOR, &trigger_len );
    if( rc != 0 )
    {
        return rc;
    }

    if( clock_gettime( CLOCK_MONOTONIC, &start ) == 0 )
    {
        elapsed_ms = 0;
    }

    rc = xu_set_cur( dev, HTI_XU_TRIGGER_SELECTOR, trigger_payload, sizeof( trigger_payload ) );
    if( rc != 0 )
    {
        return rc;
    }

    if( elapsed_ms == 0 && clock_gettime( CLOCK_MONOTONIC, &end ) == 0 )
    {
        elapsed_ms = (long)( end.tv_sec - start.tv_sec ) * 1000L + ( end.tv_nsec - start.tv_nsec ) / 1000000L;
    }

    if( prelude_len != 2 || trigger_len != 1 )
    {
        return LIBUSB_ERROR_IO;
    }

    if( elapsed_ms >= 0 && elapsed_ms < HTI_EXPECTED_SHUTTER_MIN_MS )
    {
        return LIBUSB_ERROR_IO;
    }

    return 0;
}

int hti_usb_trigger_shutter_standalone( uint16_t vid, uint16_t pid, int timeout_ms, bool verbose )
{
    struct hti_usb_device dev;
    int                   rc = 0;

    memset( &dev, 0, sizeof( dev ) );
    dev.vid                 = vid ? vid : HTI_DEFAULT_VID;
    dev.pid                 = pid ? pid : HTI_DEFAULT_PID;
    dev.control_interface   = HTI_DEFAULT_CONTROL_INTERFACE;
    dev.streaming_interface = HTI_DEFAULT_STREAMING_INTERFACE;
    dev.extension_unit      = HTI_DEFAULT_EXTENSION_UNIT;
    dev.bulk_endpoint       = HTI_DEFAULT_BULK_ENDPOINT;
    dev.timeout_ms          = timeout_ms > 0 ? timeout_ms : HTI_DEFAULT_TIMEOUT_MS;

    rc = libusb_init( &dev.ctx );
    if( rc != 0 )
    {
        return rc;
    }

    if( verbose )
    {
        libusb_set_option( dev.ctx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_INFO );
    }

    rc = open_matching_camera( dev.ctx, dev.vid, dev.pid, &dev.handle );
    if( rc != 0 )
    {
        hti_usb_close( &dev );
        return rc;
    }

    dump_device_info_strings( dev.handle );

    rc = libusb_set_auto_detach_kernel_driver( dev.handle, 1 );
    if( rc != 0 && rc != LIBUSB_ERROR_NOT_SUPPORTED )
    {
        hti_usb_close( &dev );
        return rc;
    }

    rc = libusb_claim_interface( dev.handle, dev.control_interface );
    if( rc != 0 )
    {
        hti_usb_close( &dev );
        return rc;
    }

    rc = hti_usb_trigger_shutter( &dev );
    hti_usb_close( &dev );
    return rc;
}
