#include "hik_probe.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

const char HIK_DEFAULT_PROFILE_NAME[] = "system-device-info";

static const HikPaletteValue kPaletteValues[] = {
    { "WHITE_HOT", 0x01 },
    { "BLACK_HOT", 0x02 },
    { "BLEND_1", 0x0a },
    { "RAINBOW", 0x0b },
    { "BLEND_2", 0x0c },
    { "IRON_1", 0x0d },
    { "IRON_2", 0x0e },
    { "BROWN", 0x0f },
    { "COLOR_1", 0x10 },
    { "COLOR_2", 0x11 },
    { "ICE_FIRE", 0x12 },
    { "RAIN", 0x13 },
    { "RED_HOT", 0x14 },
    { "GREEN_HOT", 0x15 },
    { "BLUE", 0x16 },
};

static const HikProfile kProfiles[] = {
    { "system-device-info",
      HIK_PROFILE_READ,
      0x01,
      HIK_EXPECTED_INFO_BLOB_LEN,
      true,
      HIK_READ_OUTPUT_INFO_BLOB,
      0,
      { 0 },
      0,
      { 0 },
      0,
      HIK_WRITE_SHAPE_FIXED,
      0,
      "Selector 0x01 read with the paired-capture preamble; returns firmware strings, serial, and device ID." },
    { "protocol-version",
      HIK_PROFILE_READ,
      HIK_PROTOCOL_SELECTOR,
      4,
      false,
      HIK_READ_OUTPUT_ASCII_STRING,
      0,
      { 0 },
      0,
      { 0 },
      0,
      HIK_WRITE_SHAPE_FIXED,
      0,
      "Selector 0x04 GET_LEN/GET_CUR; returns ASCII protocol string such as 2.0." },
    { "image-enhancement",
      HIK_PROFILE_READ,
      HIK_ENHANCEMENT_SELECTOR,
      HIK_EXPECTED_ENHANCEMENT_LEN,
      false,
      HIK_READ_OUTPUT_IMAGE_ENHANCEMENT,
      HIK_PRELUDE_SELECTOR,
      { 0x02, 0x05 },
      2,
      { 0 },
      0,
      HIK_WRITE_SHAPE_FIXED,
      0,
      "Selector 0x02 GET_LEN/GET_CUR; returns the 24-byte image-enhancement block and decodes the known fields." },
    { "image-enhancement-set",
      HIK_PROFILE_WRITE,
      HIK_ENHANCEMENT_SELECTOR,
      HIK_EXPECTED_ENHANCEMENT_LEN,
      false,
      HIK_READ_OUTPUT_INFO_BLOB,
      HIK_PRELUDE_SELECTOR,
      { 0x02, 0x05 },
      2,
      { 0 },
      0,
      HIK_WRITE_SHAPE_ENHANCEMENT_PATCH,
      0,
      "Selector 0x02 read-modify-write for palette and the confirmed image-enhancement fields. Use --palette and related enhancement flags." },
    { "thermometry-basic",
      HIK_PROFILE_READ,
      HIK_THERMOMETRY_SELECTOR,
      HIK_THERMOMETRY_TYPED_LEN,
      false,
      HIK_READ_OUTPUT_THERMOMETRY_BASIC,
      HIK_PRELUDE_SELECTOR,
      { 0x03, 0x01 },
      2,
      { 0 },
      0,
      HIK_WRITE_SHAPE_FIXED,
      0,
      "Selector 0x03 thermometry-basic page, staged by selector 0x05 payload 03 01. Reads and decodes the 69-byte mailbox payload." },
    { "thermometry-basic-set",
      HIK_PROFILE_WRITE,
      HIK_THERMOMETRY_SELECTOR,
      HIK_THERMOMETRY_TYPED_LEN,
      false,
      HIK_READ_OUTPUT_INFO_BLOB,
      HIK_PRELUDE_SELECTOR,
      { 0x03, 0x01 },
      2,
      { 0 },
      0,
      HIK_WRITE_SHAPE_THERMOMETRY_PATCH,
      0,
      "Selector 0x03 thermometry-basic read-modify-write for emissivity, distance, reflection, and related compensation fields." },
    { "expert-correction-set",
      HIK_PROFILE_WRITE,
      HIK_THERMOMETRY_SELECTOR,
      53,
      false,
      HIK_READ_OUTPUT_INFO_BLOB,
      HIK_PRELUDE_SELECTOR,
      { 0x03, 0x10 },
      2,
      { 0 },
      0,
      HIK_WRITE_SHAPE_EXPERT_CORRECTION_REPLAY,
      0,
      "Selector 0x03 replay of the captured 53-byte USB_SetExpertCorrectionParam payload, staged by selector 0x05 payload 03 10." },
    { "temperature-correct-set",
      HIK_PROFILE_WRITE,
      HIK_THERMOMETRY_SELECTOR,
      53,
      false,
      HIK_READ_OUTPUT_INFO_BLOB,
      HIK_PRELUDE_SELECTOR,
      { 0x03, 0x06 },
      2,
      { 0 },
      0,
      HIK_WRITE_SHAPE_TEMPERATURE_CORRECT_REPLAY,
      0,
      "Selector 0x03 replay of the captured 53-byte USB_SetTemperatureCorrect payload, staged by selector 0x05 payload 03 06." },
    { "thermometry-expert-regions-set",
      HIK_PROFILE_WRITE,
      HIK_THERMOMETRY_SELECTOR,
      0,
      false,
      HIK_READ_OUTPUT_INFO_BLOB,
      HIK_PRELUDE_SELECTOR,
      { 0x03, 0x0f },
      2,
      { 0 },
      0,
      HIK_WRITE_SHAPE_EXPERT_REGIONS_ZERO,
      0,
      "Selector 0x03 replay of the captured zero-region USB_SetThermometryExpertRegions chunked transfer, staged by selector 0x05 payload 03 0f." },
    { "selector-state-scan",
      HIK_PROFILE_SCAN,
      HIK_ENHANCEMENT_SELECTOR,
      0,
      false,
      HIK_READ_OUTPUT_INFO_BLOB,
      0,
      { 0 },
      0,
      { 0 },
      0,
      HIK_WRITE_SHAPE_FIXED,
      0,
      "Sweep a staged prewrite byte and report how a target selector responds. Use --scan-prewrite-selector, --scan-prefix-hex, and --scan-range." },
    { "brightness",
      HIK_PROFILE_WRITE,
      HIK_ENHANCEMENT_SELECTOR,
      2,
      false,
      HIK_READ_OUTPUT_INFO_BLOB,
      HIK_PRELUDE_SELECTOR,
      { 0x02, 0x01 },
      2,
      { 0x01, 0x28 },
      2,
      HIK_WRITE_SHAPE_SCALAR,
      40,
      "Selector 0x02 write matching brightness-like scalar control, staged by selector 0x05 payload 02 01. Override with --value." },
    { "contrast",
      HIK_PROFILE_WRITE,
      HIK_ENHANCEMENT_SELECTOR,
      2,
      false,
      HIK_READ_OUTPUT_INFO_BLOB,
      HIK_PRELUDE_SELECTOR,
      { 0x02, 0x02 },
      2,
      { 0x01, 0x23 },
      2,
      HIK_WRITE_SHAPE_SCALAR,
      35,
      "Selector 0x02 write matching contrast-like scalar control, staged by selector 0x05 payload 02 02. Override with --value." },
    { "manual-correct",
      HIK_PROFILE_WRITE,
      HIK_PRELUDE_SELECTOR,
      2,
      false,
      HIK_READ_OUTPUT_INFO_BLOB,
      0,
      { 0 },
      0,
      { 0x02, 0x04 },
      2,
      HIK_WRITE_SHAPE_FIXED,
      0,
      "Selector 0x05 action trigger correlated with USB_SetImageManualCorrect." },
    { "background-correct",
      HIK_PROFILE_WRITE,
      HIK_PRELUDE_SELECTOR,
      2,
      false,
      HIK_READ_OUTPUT_INFO_BLOB,
      0,
      { 0 },
      0,
      { 0x02, 0x03 },
      2,
      HIK_WRITE_SHAPE_FIXED,
      0,
      "Selector 0x05 action trigger correlated with USB_SetImageBackGroundCorrect." },
    { "start-expert-correction",
      HIK_PROFILE_WRITE,
      HIK_PRELUDE_SELECTOR,
      2,
      false,
      HIK_READ_OUTPUT_INFO_BLOB,
      0,
      { 0 },
      0,
      { 0x03, 0x11 },
      2,
      HIK_WRITE_SHAPE_FIXED,
      0,
      "Selector 0x05 action trigger correlated with USB_StartExpertCorrection." },
};

static bool normalized_name_equal( const char *lhs, const char *rhs )
{
    size_t i = 0;
    size_t j = 0;

    for( ;; )
    {
        while( lhs[i] == '-' || lhs[i] == '_' || isspace( (unsigned char)lhs[i] ) )
        {
            ++i;
        }
        while( rhs[j] == '-' || rhs[j] == '_' || isspace( (unsigned char)rhs[j] ) )
        {
            ++j;
        }

        if( lhs[i] == '\0' || rhs[j] == '\0' )
        {
            return lhs[i] == '\0' && rhs[j] == '\0';
        }

        if( tolower( (unsigned char)lhs[i] ) != tolower( (unsigned char)rhs[j] ) )
        {
            return false;
        }

        ++i;
        ++j;
    }
}

static HikReadOutputKind selector_default_output_kind( uint8_t selector )
{
    if( selector == HIK_PROTOCOL_SELECTOR )
    {
        return HIK_READ_OUTPUT_ASCII_STRING;
    }
    if( selector == HIK_ENHANCEMENT_SELECTOR )
    {
        return HIK_READ_OUTPUT_IMAGE_ENHANCEMENT;
    }
    return HIK_READ_OUTPUT_INFO_BLOB;
}

const HikProfile *hik_get_profiles( size_t *count_out )
{
    if( count_out != NULL )
    {
        *count_out = sizeof( kProfiles ) / sizeof( kProfiles[0] );
    }
    return kProfiles;
}

const HikProfile *hik_find_profile( const char *name )
{
    size_t            profile_count = 0;
    const HikProfile *profiles      = hik_get_profiles( &profile_count );

    for( size_t i = 0; i < profile_count; ++i )
    {
        if( strcmp( profiles[i].name, name ) == 0 )
        {
            return &profiles[i];
        }
    }

    return NULL;
}

const HikProfile *hik_resolve_profile( const HikOptions *opts, HikProfile *manual_profile_out )
{
    if( opts->profile_set )
    {
        return hik_find_profile( opts->profile_name );
    }

    if( !opts->selector_set )
    {
        return hik_find_profile( opts->profile_name );
    }

    memset( manual_profile_out, 0, sizeof( *manual_profile_out ) );
    manual_profile_out->name             = "manual-selector-read";
    manual_profile_out->kind             = HIK_PROFILE_READ;
    manual_profile_out->selector         = opts->selector;
    manual_profile_out->expected_len     = 0;
    manual_profile_out->auto_paired_init = ( opts->selector == HIK_DEFAULT_SELECTOR );
    manual_profile_out->read_output_kind = selector_default_output_kind( opts->selector );
    manual_profile_out->write_shape      = HIK_WRITE_SHAPE_FIXED;
    manual_profile_out->description      = "Ad hoc raw selector read.";
    return manual_profile_out;
}

const char *hik_palette_name( uint8_t value )
{
    for( size_t i = 0; i < sizeof( kPaletteValues ) / sizeof( kPaletteValues[0] ); ++i )
    {
        if( kPaletteValues[i].value == value )
        {
            return kPaletteValues[i].name;
        }
    }

    return NULL;
}

bool hik_parse_palette_value( const char *text, uint8_t *out )
{
    char         *end           = NULL;
    unsigned long numeric_value = strtoul( text, &end, 0 );

    if( text[0] != '\0' && end != text && *end == '\0' && numeric_value <= UINT8_MAX )
    {
        *out = (uint8_t)numeric_value;
        return true;
    }

    for( size_t i = 0; i < sizeof( kPaletteValues ) / sizeof( kPaletteValues[0] ); ++i )
    {
        if( normalized_name_equal( text, kPaletteValues[i].name ) )
        {
            *out = kPaletteValues[i].value;
            return true;
        }
    }

    return false;
}

bool hik_build_profile_payload( const HikProfile *profile,
                                const HikOptions *opts,
                                uint8_t          *payload_out,
                                size_t           *payload_len_out )
{
    static const uint8_t kExpertCorrectionReplay[] = {
        0x01,
        0x1e, 0x00, 0x00, 0x00,
        0xac, 0x0d, 0x00, 0x00,
        0x61, 0x00, 0x00, 0x00,
        0x01, 0x40, 0x06, 0x00, 0x00, 0xf4, 0x01, 0x00, 0x00, 0xf4, 0x01, 0x00, 0x00,
        0x02, 0x40, 0x06, 0x00, 0x00, 0xf4, 0x01, 0x00, 0x00, 0xf4, 0x01, 0x00, 0x00,
        0x03, 0x40, 0x06, 0x00, 0x00, 0xf4, 0x01, 0x00, 0x00, 0xf4, 0x01, 0x00, 0x00,
    };
    static const uint8_t kTemperatureCorrectReplay[] = {
        0x01,
        0x1e, 0x00, 0x00, 0x00,
        0xac, 0x0d, 0x00, 0x00,
        0x61, 0x00, 0x00, 0x00,
        0x01, 0x40, 0x06, 0x00, 0x00, 0xf4, 0x01, 0x00, 0x00, 0xf4, 0x01, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };

    memcpy( payload_out, profile->payload, profile->payload_len );
    *payload_len_out = profile->payload_len;

    switch( profile->write_shape )
    {
        case HIK_WRITE_SHAPE_FIXED:
            return true;
        case HIK_WRITE_SHAPE_SCALAR:
            if( profile->payload_len < 2 )
            {
                return false;
            }
            payload_out[1] = opts->value_set ? opts->value : profile->default_value;
            return true;
        case HIK_WRITE_SHAPE_ENHANCEMENT_PATCH:
            *payload_len_out = 0;
            return true;
        case HIK_WRITE_SHAPE_THERMOMETRY_PATCH:
            *payload_len_out = 0;
            return true;
        case HIK_WRITE_SHAPE_EXPERT_CORRECTION_REPLAY:
            memcpy( payload_out, kExpertCorrectionReplay, sizeof( kExpertCorrectionReplay ) );
            *payload_len_out = sizeof( kExpertCorrectionReplay );
            return true;
        case HIK_WRITE_SHAPE_TEMPERATURE_CORRECT_REPLAY:
            memcpy( payload_out, kTemperatureCorrectReplay, sizeof( kTemperatureCorrectReplay ) );
            *payload_len_out = sizeof( kTemperatureCorrectReplay );
            return true;
        case HIK_WRITE_SHAPE_EXPERT_REGIONS_ZERO:
            *payload_len_out = 0;
            return true;
        default:
            return false;
    }
}
