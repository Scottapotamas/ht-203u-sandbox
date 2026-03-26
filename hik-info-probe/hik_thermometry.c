#include "hik_probe.h"

#include <stdio.h>
#include <string.h>

enum
{
    HIK_THERMOMETRY_BYTE_CHANNEL = 0,
    HIK_THERMOMETRY_BYTE_ENABLED = 1,
    HIK_THERMOMETRY_BYTE_DISPLAY_MAX_ENABLED = 2,
    HIK_THERMOMETRY_BYTE_DISPLAY_MIN_ENABLED = 3,
    HIK_THERMOMETRY_BYTE_DISPLAY_AVG_ENABLED = 4,
    HIK_THERMOMETRY_BYTE_TEMPERATURE_UNIT = 5,
    HIK_THERMOMETRY_BYTE_TEMPERATURE_RANGE = 6,
    HIK_THERMOMETRY_BYTE_CALIBRATION_COEFF_ENABLED = 7,
    HIK_THERMOMETRY_DWORD_CALIBRATION_COEFF = 8,
    HIK_THERMOMETRY_DWORD_EXTERNAL_OPTICS_WINDOW_CORRECTION = 12,
    HIK_THERMOMETRY_DWORD_EMISSIVITY = 16,
    HIK_THERMOMETRY_BYTE_DISTANCE_UNIT = 20,
    HIK_THERMOMETRY_DWORD_DISTANCE = 21,
    HIK_THERMOMETRY_BYTE_REFLECTIVE_ENABLE = 25,
    HIK_THERMOMETRY_DWORD_REFLECTIVE_TEMPERATURE = 26,
    HIK_THERMOMETRY_BYTE_INFO_DISPLAY_POSITION = 30,
    HIK_THERMOMETRY_BYTE_STREAM_OVERLAY = 31,
    HIK_THERMOMETRY_DWORD_ALERT = 32,
    HIK_THERMOMETRY_DWORD_ALARM = 36,
    HIK_THERMOMETRY_DWORD_EXTERNAL_OPTICS_TRANSMIT = 40,
    HIK_THERMOMETRY_BYTE_DISPLAY_CENTER_ENABLED = 44,
    HIK_THERMOMETRY_BYTE_BACKCOLOR_ENABLED = 45,
    HIK_THERMOMETRY_BYTE_SHOW_ALARM_COLOR_ENABLED = 46,

    HIK_THERMOMETRY_PUBLIC_OFFSET_SIZE = 0,
    HIK_THERMOMETRY_PUBLIC_OFFSET_ENABLED = 5,
    HIK_THERMOMETRY_PUBLIC_OFFSET_DISPLAY_MAX = 6,
    HIK_THERMOMETRY_PUBLIC_OFFSET_DISPLAY_MIN = 7,
    HIK_THERMOMETRY_PUBLIC_OFFSET_DISPLAY_AVG = 8,
    HIK_THERMOMETRY_PUBLIC_OFFSET_TEMPERATURE_UNIT = 9,
    HIK_THERMOMETRY_PUBLIC_OFFSET_TEMPERATURE_RANGE = 10,
    HIK_THERMOMETRY_PUBLIC_OFFSET_CALIBRATION_COEFF_ENABLED = 11,
    HIK_THERMOMETRY_PUBLIC_OFFSET_CALIBRATION_COEFF = 12,
    HIK_THERMOMETRY_PUBLIC_OFFSET_EXTERNAL_OPTICS_WINDOW_CORRECTION = 16,
    HIK_THERMOMETRY_PUBLIC_OFFSET_EMISSIVITY = 20,
    HIK_THERMOMETRY_PUBLIC_OFFSET_DISTANCE_UNIT = 24,
    HIK_THERMOMETRY_PUBLIC_OFFSET_SHOW_ALARM_COLOR = 25,
    HIK_THERMOMETRY_PUBLIC_OFFSET_DISTANCE = 28,
    HIK_THERMOMETRY_PUBLIC_OFFSET_REFLECTIVE_ENABLE = 32,
    HIK_THERMOMETRY_PUBLIC_OFFSET_REFLECTIVE_TEMPERATURE = 36,
    HIK_THERMOMETRY_PUBLIC_OFFSET_INFO_DISPLAY_POSITION = 40,
    HIK_THERMOMETRY_PUBLIC_OFFSET_STREAM_OVERLAY = 41,
    HIK_THERMOMETRY_PUBLIC_OFFSET_DISPLAY_CENTER = 42,
    HIK_THERMOMETRY_PUBLIC_OFFSET_BACKCOLOR = 43,
    HIK_THERMOMETRY_PUBLIC_OFFSET_ALERT = 44,
    HIK_THERMOMETRY_PUBLIC_OFFSET_ALARM = 48,
    HIK_THERMOMETRY_PUBLIC_OFFSET_EXTERNAL_OPTICS_TRANSMIT = 52
};

static uint32_t read_le32( const uint8_t *buf, size_t offset )
{
    return (uint32_t)buf[offset] | ( (uint32_t)buf[offset + 1] << 8 ) | ( (uint32_t)buf[offset + 2] << 16 ) | ( (uint32_t)buf[offset + 3] << 24 );
}

static void write_le32( uint8_t *buf, size_t offset, uint32_t value )
{
    buf[offset] = (uint8_t)( value & 0xffu );
    buf[offset + 1] = (uint8_t)( ( value >> 8 ) & 0xffu );
    buf[offset + 2] = (uint8_t)( ( value >> 16 ) & 0xffu );
    buf[offset + 3] = (uint8_t)( ( value >> 24 ) & 0xffu );
}

static void maybe_patch_u8( uint8_t *buf, size_t index, uint8_t value, size_t *changed_count )
{
    if( buf[index] != value )
    {
        buf[index] = value;
        ++( *changed_count );
    }
}

static void maybe_patch_u32( uint8_t *buf, size_t index, uint32_t value, size_t *changed_count )
{
    if( read_le32( buf, index ) != value )
    {
        write_le32( buf, index, value );
        ++( *changed_count );
    }
}

static bool typed_payload_plausible( const uint8_t *typed )
{
    uint32_t calibration_coeff = read_le32( typed, HIK_THERMOMETRY_DWORD_CALIBRATION_COEFF );
    uint32_t window_correction = read_le32( typed, HIK_THERMOMETRY_DWORD_EXTERNAL_OPTICS_WINDOW_CORRECTION );
    uint32_t emissivity        = read_le32( typed, HIK_THERMOMETRY_DWORD_EMISSIVITY );
    uint32_t distance          = read_le32( typed, HIK_THERMOMETRY_DWORD_DISTANCE );
    uint32_t reflective_temp   = read_le32( typed, HIK_THERMOMETRY_DWORD_REFLECTIVE_TEMPERATURE );
    uint32_t alert             = read_le32( typed, HIK_THERMOMETRY_DWORD_ALERT );
    uint32_t alarm             = read_le32( typed, HIK_THERMOMETRY_DWORD_ALARM );
    uint32_t external_tx       = read_le32( typed, HIK_THERMOMETRY_DWORD_EXTERNAL_OPTICS_TRANSMIT );

    if( typed[HIK_THERMOMETRY_BYTE_CHANNEL] != 0x01 )
    {
        return false;
    }

    if( typed[HIK_THERMOMETRY_BYTE_ENABLED] > 1 || typed[HIK_THERMOMETRY_BYTE_DISPLAY_MAX_ENABLED] > 1 || typed[HIK_THERMOMETRY_BYTE_DISPLAY_MIN_ENABLED] > 1 || typed[HIK_THERMOMETRY_BYTE_DISPLAY_AVG_ENABLED] > 1 || typed[HIK_THERMOMETRY_BYTE_CALIBRATION_COEFF_ENABLED] > 1 || typed[HIK_THERMOMETRY_BYTE_REFLECTIVE_ENABLE] > 1 || typed[HIK_THERMOMETRY_BYTE_STREAM_OVERLAY] > 1 || typed[HIK_THERMOMETRY_BYTE_DISPLAY_CENTER_ENABLED] > 1 || typed[HIK_THERMOMETRY_BYTE_BACKCOLOR_ENABLED] > 1 || typed[HIK_THERMOMETRY_BYTE_SHOW_ALARM_COLOR_ENABLED] > 1 )
    {
        return false;
    }

    if( typed[HIK_THERMOMETRY_BYTE_TEMPERATURE_UNIT] > 3 || typed[HIK_THERMOMETRY_BYTE_TEMPERATURE_RANGE] > 8 || typed[HIK_THERMOMETRY_BYTE_DISTANCE_UNIT] > 8 || typed[HIK_THERMOMETRY_BYTE_INFO_DISPLAY_POSITION] > 8 )
    {
        return false;
    }

    if( calibration_coeff > 1000000u || window_correction > 100000u || emissivity > 100000u || distance > 1000000u || reflective_temp > 1000000u || alert > 1000000u || alarm > 1000000u || external_tx > 100000u )
    {
        return false;
    }

    if( calibration_coeff == 0 && window_correction == 0 && emissivity == 0 && distance == 0 && reflective_temp == 0 && alert == 0 && alarm == 0 && external_tx == 0 )
    {
        return false;
    }

    return true;
}

static bool public_struct_plausible( const uint8_t *public_buf )
{
    uint32_t dw_size = read_le32( public_buf, HIK_THERMOMETRY_PUBLIC_OFFSET_SIZE );

    if( dw_size != HIK_THERMOMETRY_STRUCT_LEN )
    {
        return false;
    }

    if( public_buf[HIK_THERMOMETRY_PUBLIC_OFFSET_ENABLED] > 1 || public_buf[HIK_THERMOMETRY_PUBLIC_OFFSET_DISPLAY_MAX] > 1 || public_buf[HIK_THERMOMETRY_PUBLIC_OFFSET_DISPLAY_MIN] > 1 || public_buf[HIK_THERMOMETRY_PUBLIC_OFFSET_DISPLAY_AVG] > 1 || public_buf[HIK_THERMOMETRY_PUBLIC_OFFSET_CALIBRATION_COEFF_ENABLED] > 1 || public_buf[HIK_THERMOMETRY_PUBLIC_OFFSET_SHOW_ALARM_COLOR] > 1 )
    {
        return false;
    }

    if( public_buf[HIK_THERMOMETRY_PUBLIC_OFFSET_TEMPERATURE_UNIT] > 3 || public_buf[HIK_THERMOMETRY_PUBLIC_OFFSET_TEMPERATURE_RANGE] > 8 || public_buf[HIK_THERMOMETRY_PUBLIC_OFFSET_DISTANCE_UNIT] > 8 || public_buf[HIK_THERMOMETRY_PUBLIC_OFFSET_INFO_DISPLAY_POSITION] > 8 )
    {
        return false;
    }

    return true;
}

void hik_thermometry_default_seed( uint8_t *typed_out )
{
    static const uint8_t kDefaultSeed[HIK_THERMOMETRY_TYPED_LEN] = {
        0x01,
        0x00,
        0x00,
        0x00,
        0x00,
        0x01,
        0x02,
        0x00,
        0x00, 0x00, 0x00, 0x00,
        0x06, 0x04, 0x00, 0x00,
        0x5f, 0x00, 0x00, 0x00,
        0x02,
        0x1e, 0x00, 0x00, 0x00,
        0x00,
        0x1a, 0x04, 0x00, 0x00,
        0x02,
        0x01,
        0x40, 0x06, 0x00, 0x00,
        0x08, 0x07, 0x00, 0x00,
        0x64, 0x00, 0x00, 0x00,
        0x00,
        0x00,
        0x01
    };

    memcpy( typed_out, kDefaultSeed, sizeof( kDefaultSeed ) );
}

static void public_to_typed( const uint8_t *public_buf, uint8_t *typed_out )
{
    memset( typed_out, 0, HIK_THERMOMETRY_TYPED_LEN );
    typed_out[HIK_THERMOMETRY_BYTE_CHANNEL] = 0x01;
    typed_out[HIK_THERMOMETRY_BYTE_ENABLED] = public_buf[HIK_THERMOMETRY_PUBLIC_OFFSET_ENABLED];
    typed_out[HIK_THERMOMETRY_BYTE_DISPLAY_MAX_ENABLED] = public_buf[HIK_THERMOMETRY_PUBLIC_OFFSET_DISPLAY_MAX];
    typed_out[HIK_THERMOMETRY_BYTE_DISPLAY_MIN_ENABLED] = public_buf[HIK_THERMOMETRY_PUBLIC_OFFSET_DISPLAY_MIN];
    typed_out[HIK_THERMOMETRY_BYTE_DISPLAY_AVG_ENABLED] = public_buf[HIK_THERMOMETRY_PUBLIC_OFFSET_DISPLAY_AVG];
    typed_out[HIK_THERMOMETRY_BYTE_TEMPERATURE_UNIT] = public_buf[HIK_THERMOMETRY_PUBLIC_OFFSET_TEMPERATURE_UNIT];
    typed_out[HIK_THERMOMETRY_BYTE_TEMPERATURE_RANGE] = public_buf[HIK_THERMOMETRY_PUBLIC_OFFSET_TEMPERATURE_RANGE];
    typed_out[HIK_THERMOMETRY_BYTE_CALIBRATION_COEFF_ENABLED] = public_buf[HIK_THERMOMETRY_PUBLIC_OFFSET_CALIBRATION_COEFF_ENABLED];
    write_le32( typed_out,
                HIK_THERMOMETRY_DWORD_CALIBRATION_COEFF,
                read_le32( public_buf, HIK_THERMOMETRY_PUBLIC_OFFSET_CALIBRATION_COEFF ) );
    write_le32( typed_out,
                HIK_THERMOMETRY_DWORD_EXTERNAL_OPTICS_WINDOW_CORRECTION,
                read_le32( public_buf, HIK_THERMOMETRY_PUBLIC_OFFSET_EXTERNAL_OPTICS_WINDOW_CORRECTION ) );
    write_le32( typed_out,
                HIK_THERMOMETRY_DWORD_EMISSIVITY,
                read_le32( public_buf, HIK_THERMOMETRY_PUBLIC_OFFSET_EMISSIVITY ) );
    typed_out[HIK_THERMOMETRY_BYTE_DISTANCE_UNIT] = public_buf[HIK_THERMOMETRY_PUBLIC_OFFSET_DISTANCE_UNIT];
    write_le32( typed_out,
                HIK_THERMOMETRY_DWORD_DISTANCE,
                read_le32( public_buf, HIK_THERMOMETRY_PUBLIC_OFFSET_DISTANCE ) );
    typed_out[HIK_THERMOMETRY_BYTE_REFLECTIVE_ENABLE] = public_buf[HIK_THERMOMETRY_PUBLIC_OFFSET_REFLECTIVE_ENABLE];
    write_le32( typed_out,
                HIK_THERMOMETRY_DWORD_REFLECTIVE_TEMPERATURE,
                read_le32( public_buf, HIK_THERMOMETRY_PUBLIC_OFFSET_REFLECTIVE_TEMPERATURE ) );
    typed_out[HIK_THERMOMETRY_BYTE_INFO_DISPLAY_POSITION] = public_buf[HIK_THERMOMETRY_PUBLIC_OFFSET_INFO_DISPLAY_POSITION];
    typed_out[HIK_THERMOMETRY_BYTE_STREAM_OVERLAY] = public_buf[HIK_THERMOMETRY_PUBLIC_OFFSET_STREAM_OVERLAY];
    write_le32( typed_out, HIK_THERMOMETRY_DWORD_ALERT, read_le32( public_buf, HIK_THERMOMETRY_PUBLIC_OFFSET_ALERT ) );
    write_le32( typed_out, HIK_THERMOMETRY_DWORD_ALARM, read_le32( public_buf, HIK_THERMOMETRY_PUBLIC_OFFSET_ALARM ) );
    write_le32( typed_out,
                HIK_THERMOMETRY_DWORD_EXTERNAL_OPTICS_TRANSMIT,
                read_le32( public_buf, HIK_THERMOMETRY_PUBLIC_OFFSET_EXTERNAL_OPTICS_TRANSMIT ) );
    typed_out[HIK_THERMOMETRY_BYTE_DISPLAY_CENTER_ENABLED] = public_buf[HIK_THERMOMETRY_PUBLIC_OFFSET_DISPLAY_CENTER];
    typed_out[HIK_THERMOMETRY_BYTE_BACKCOLOR_ENABLED] = public_buf[HIK_THERMOMETRY_PUBLIC_OFFSET_BACKCOLOR];
    typed_out[HIK_THERMOMETRY_BYTE_SHOW_ALARM_COLOR_ENABLED] = public_buf[HIK_THERMOMETRY_PUBLIC_OFFSET_SHOW_ALARM_COLOR];
}

bool hik_normalize_thermometry_payload( const uint8_t *data, size_t len, uint8_t *typed_out )
{
    if( len >= HIK_THERMOMETRY_STRUCT_LEN && public_struct_plausible( data ) )
    {
        public_to_typed( data, typed_out );
        return true;
    }

    if( len >= HIK_THERMOMETRY_TYPED_LEN && typed_payload_plausible( data ) )
    {
        memcpy( typed_out, data, HIK_THERMOMETRY_TYPED_LEN );
        return true;
    }

    if( len > HIK_THERMOMETRY_TYPED_LEN )
    {
        for( size_t offset = 1; offset + HIK_THERMOMETRY_TYPED_LEN <= len; ++offset )
        {
            if( typed_payload_plausible( data + offset ) )
            {
                memcpy( typed_out, data + offset, HIK_THERMOMETRY_TYPED_LEN );
                return true;
            }
        }
    }

    return false;
}

bool hik_thermometry_overrides_requested( const HikOptions *opts )
{
    return opts->thermometry_enabled_set || opts->thermometry_display_max_set || opts->thermometry_display_min_set || opts->thermometry_display_avg_set || opts->thermometry_temperature_unit_set || opts->thermometry_temperature_range_set || opts->thermometry_calibration_coeff_enabled_set || opts->thermometry_calibration_coeff_set || opts->thermometry_external_optics_window_correction_set || opts->thermometry_emissivity_set || opts->thermometry_distance_unit_set || opts->thermometry_distance_set || opts->thermometry_reflective_enable_set || opts->thermometry_reflective_temperature_set || opts->thermometry_info_display_position_set || opts->thermometry_stream_overlay_set || opts->thermometry_alert_set || opts->thermometry_alarm_set || opts->thermometry_external_optics_transmit_set || opts->thermometry_display_center_set || opts->thermometry_backcolor_set || opts->thermometry_show_alarm_color_set;
}

void hik_print_thermometry_summary( const uint8_t *data, size_t len )
{
    uint8_t typed[HIK_THERMOMETRY_TYPED_LEN];

    if( !hik_normalize_thermometry_payload( data, len, typed ) )
    {
        printf( "Thermometry-basic payload too short to decode (%zu byte(s), expected %u or %u)\n",
                len,
                HIK_THERMOMETRY_TYPED_LEN,
                HIK_THERMOMETRY_STRUCT_LEN );
        return;
    }

    puts( "Thermometry basic fields:" );
    printf( "  channel/sub-id: %u\n", typed[HIK_THERMOMETRY_BYTE_CHANNEL] );
    printf( "  enabled: %u\n", typed[HIK_THERMOMETRY_BYTE_ENABLED] );
    printf( "  display-max-enabled: %u\n", typed[HIK_THERMOMETRY_BYTE_DISPLAY_MAX_ENABLED] );
    printf( "  display-min-enabled: %u\n", typed[HIK_THERMOMETRY_BYTE_DISPLAY_MIN_ENABLED] );
    printf( "  display-average-enabled: %u\n", typed[HIK_THERMOMETRY_BYTE_DISPLAY_AVG_ENABLED] );
    printf( "  temperature-unit: %u\n", typed[HIK_THERMOMETRY_BYTE_TEMPERATURE_UNIT] );
    printf( "  temperature-range: %u\n", typed[HIK_THERMOMETRY_BYTE_TEMPERATURE_RANGE] );
    printf( "  calibration-coefficient-enabled: %u\n", typed[HIK_THERMOMETRY_BYTE_CALIBRATION_COEFF_ENABLED] );
    printf( "  calibration-coefficient: %u\n", read_le32( typed, HIK_THERMOMETRY_DWORD_CALIBRATION_COEFF ) );
    printf( "  external-optics-window-correction: %u\n",
            read_le32( typed, HIK_THERMOMETRY_DWORD_EXTERNAL_OPTICS_WINDOW_CORRECTION ) );
    printf( "  emissivity: %u\n", read_le32( typed, HIK_THERMOMETRY_DWORD_EMISSIVITY ) );
    printf( "  distance-unit: %u\n", typed[HIK_THERMOMETRY_BYTE_DISTANCE_UNIT] );
    printf( "  distance: %u\n", read_le32( typed, HIK_THERMOMETRY_DWORD_DISTANCE ) );
    printf( "  reflective-enable: %u\n", typed[HIK_THERMOMETRY_BYTE_REFLECTIVE_ENABLE] );
    printf( "  reflective-temperature: %u\n",
            read_le32( typed, HIK_THERMOMETRY_DWORD_REFLECTIVE_TEMPERATURE ) );
    printf( "  info-display-position: %u\n", typed[HIK_THERMOMETRY_BYTE_INFO_DISPLAY_POSITION] );
    printf( "  stream-overlay: %u\n", typed[HIK_THERMOMETRY_BYTE_STREAM_OVERLAY] );
    printf( "  alert: %u\n", read_le32( typed, HIK_THERMOMETRY_DWORD_ALERT ) );
    printf( "  alarm: %u\n", read_le32( typed, HIK_THERMOMETRY_DWORD_ALARM ) );
    printf( "  external-optics-transmit: %u\n",
            read_le32( typed, HIK_THERMOMETRY_DWORD_EXTERNAL_OPTICS_TRANSMIT ) );
    printf( "  display-center-enabled: %u\n", typed[HIK_THERMOMETRY_BYTE_DISPLAY_CENTER_ENABLED] );
    printf( "  backcolor-enabled: %u\n", typed[HIK_THERMOMETRY_BYTE_BACKCOLOR_ENABLED] );
    printf( "  show-alarm-color-enabled: %u\n", typed[HIK_THERMOMETRY_BYTE_SHOW_ALARM_COLOR_ENABLED] );
}

size_t hik_apply_thermometry_patch( uint8_t *buf, size_t len, const HikOptions *opts )
{
    size_t changed = 0;

    if( len < HIK_THERMOMETRY_TYPED_LEN )
    {
        return 0;
    }

    if( opts->thermometry_enabled_set )
    {
        maybe_patch_u8( buf, HIK_THERMOMETRY_BYTE_ENABLED, opts->thermometry_enabled, &changed );
    }
    if( opts->thermometry_display_max_set )
    {
        maybe_patch_u8( buf,
                        HIK_THERMOMETRY_BYTE_DISPLAY_MAX_ENABLED,
                        opts->thermometry_display_max,
                        &changed );
    }
    if( opts->thermometry_display_min_set )
    {
        maybe_patch_u8( buf,
                        HIK_THERMOMETRY_BYTE_DISPLAY_MIN_ENABLED,
                        opts->thermometry_display_min,
                        &changed );
    }
    if( opts->thermometry_display_avg_set )
    {
        maybe_patch_u8( buf,
                        HIK_THERMOMETRY_BYTE_DISPLAY_AVG_ENABLED,
                        opts->thermometry_display_avg,
                        &changed );
    }
    if( opts->thermometry_temperature_unit_set )
    {
        maybe_patch_u8( buf,
                        HIK_THERMOMETRY_BYTE_TEMPERATURE_UNIT,
                        opts->thermometry_temperature_unit,
                        &changed );
    }
    if( opts->thermometry_temperature_range_set )
    {
        maybe_patch_u8( buf,
                        HIK_THERMOMETRY_BYTE_TEMPERATURE_RANGE,
                        opts->thermometry_temperature_range,
                        &changed );
    }
    if( opts->thermometry_calibration_coeff_enabled_set )
    {
        maybe_patch_u8( buf,
                        HIK_THERMOMETRY_BYTE_CALIBRATION_COEFF_ENABLED,
                        opts->thermometry_calibration_coeff_enabled,
                        &changed );
    }
    if( opts->thermometry_calibration_coeff_set )
    {
        maybe_patch_u32( buf,
                         HIK_THERMOMETRY_DWORD_CALIBRATION_COEFF,
                         opts->thermometry_calibration_coeff,
                         &changed );
    }
    if( opts->thermometry_external_optics_window_correction_set )
    {
        maybe_patch_u32( buf,
                         HIK_THERMOMETRY_DWORD_EXTERNAL_OPTICS_WINDOW_CORRECTION,
                         opts->thermometry_external_optics_window_correction,
                         &changed );
    }
    if( opts->thermometry_emissivity_set )
    {
        maybe_patch_u32( buf,
                         HIK_THERMOMETRY_DWORD_EMISSIVITY,
                         opts->thermometry_emissivity,
                         &changed );
    }
    if( opts->thermometry_distance_unit_set )
    {
        maybe_patch_u8( buf,
                        HIK_THERMOMETRY_BYTE_DISTANCE_UNIT,
                        opts->thermometry_distance_unit,
                        &changed );
    }
    if( opts->thermometry_distance_set )
    {
        maybe_patch_u32( buf, HIK_THERMOMETRY_DWORD_DISTANCE, opts->thermometry_distance, &changed );
    }
    if( opts->thermometry_reflective_enable_set )
    {
        maybe_patch_u8( buf,
                        HIK_THERMOMETRY_BYTE_REFLECTIVE_ENABLE,
                        opts->thermometry_reflective_enable,
                        &changed );
    }
    if( opts->thermometry_reflective_temperature_set )
    {
        maybe_patch_u32( buf,
                         HIK_THERMOMETRY_DWORD_REFLECTIVE_TEMPERATURE,
                         opts->thermometry_reflective_temperature,
                         &changed );
    }
    if( opts->thermometry_info_display_position_set )
    {
        maybe_patch_u8( buf,
                        HIK_THERMOMETRY_BYTE_INFO_DISPLAY_POSITION,
                        opts->thermometry_info_display_position,
                        &changed );
    }
    if( opts->thermometry_stream_overlay_set )
    {
        maybe_patch_u8( buf,
                        HIK_THERMOMETRY_BYTE_STREAM_OVERLAY,
                        opts->thermometry_stream_overlay,
                        &changed );
    }
    if( opts->thermometry_alert_set )
    {
        maybe_patch_u32( buf, HIK_THERMOMETRY_DWORD_ALERT, opts->thermometry_alert, &changed );
    }
    if( opts->thermometry_alarm_set )
    {
        maybe_patch_u32( buf, HIK_THERMOMETRY_DWORD_ALARM, opts->thermometry_alarm, &changed );
    }
    if( opts->thermometry_external_optics_transmit_set )
    {
        maybe_patch_u32( buf,
                         HIK_THERMOMETRY_DWORD_EXTERNAL_OPTICS_TRANSMIT,
                         opts->thermometry_external_optics_transmit,
                         &changed );
    }
    if( opts->thermometry_display_center_set )
    {
        maybe_patch_u8( buf,
                        HIK_THERMOMETRY_BYTE_DISPLAY_CENTER_ENABLED,
                        opts->thermometry_display_center,
                        &changed );
    }
    if( opts->thermometry_backcolor_set )
    {
        maybe_patch_u8( buf, HIK_THERMOMETRY_BYTE_BACKCOLOR_ENABLED, opts->thermometry_backcolor, &changed );
    }
    if( opts->thermometry_show_alarm_color_set )
    {
        maybe_patch_u8( buf,
                        HIK_THERMOMETRY_BYTE_SHOW_ALARM_COLOR_ENABLED,
                        opts->thermometry_show_alarm_color,
                        &changed );
    }

    return changed;
}
