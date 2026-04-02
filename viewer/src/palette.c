#include "hti_usb_view.h"

struct hti_palette_stop
{
    double  position;
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

static const struct hti_palette_stop viridis_stops[] = {
    { 0.000000, 68, 1, 84 },
    { 0.066667, 72, 26, 108 },
    { 0.133333, 71, 47, 125 },
    { 0.200000, 65, 68, 135 },
    { 0.266667, 57, 86, 140 },
    { 0.333333, 49, 104, 142 },
    { 0.400000, 42, 120, 142 },
    { 0.466667, 35, 136, 142 },
    { 0.533333, 31, 152, 139 },
    { 0.600000, 34, 168, 132 },
    { 0.666667, 53, 183, 121 },
    { 0.733333, 84, 197, 104 },
    { 0.800000, 122, 209, 81 },
    { 0.866667, 165, 219, 54 },
    { 0.933333, 210, 226, 27 },
    { 1.000000, 253, 231, 37 },
};

static const struct hti_palette_stop inferno_stops[] = {
    { 0.000000, 0, 0, 4 },
    { 0.066667, 12, 8, 38 },
    { 0.133333, 36, 12, 79 },
    { 0.200000, 66, 10, 104 },
    { 0.266667, 93, 18, 110 },
    { 0.333333, 120, 28, 109 },
    { 0.400000, 147, 38, 103 },
    { 0.466667, 174, 48, 92 },
    { 0.533333, 199, 62, 76 },
    { 0.600000, 221, 81, 58 },
    { 0.666667, 237, 105, 37 },
    { 0.733333, 248, 133, 15 },
    { 0.800000, 252, 165, 10 },
    { 0.866667, 250, 198, 45 },
    { 0.933333, 242, 230, 97 },
    { 1.000000, 252, 255, 164 },
};

static double clamp01( double value )
{
    if( value < 0.0 )
    {
        return 0.0;
    }
    
    if( value > 1.0 )
    {
        return 1.0;
    }

    return value;
}

static uint8_t lerp_u8( uint8_t a, uint8_t b, double t )
{
    double out = (double)a + ( (double)b - (double)a ) * t;

    if( out < 0.0 )
    {
        out = 0.0;
    }

    if( out > 255.0 )
    {
        out = 255.0;
    }

    return (uint8_t)( out + 0.5 );
}

static void map_from_stops( const struct hti_palette_stop *stops, size_t stop_count,
                            double t, uint8_t *r, uint8_t *g, uint8_t *b )
{
    if( stop_count == 0 )
    {
        *r = 0;
        *g = 0;
        *b = 0;
        return;
    }

    if( t <= stops[0].position )
    {
        *r = stops[0].r;
        *g = stops[0].g;
        *b = stops[0].b;
        return;
    }

    for( size_t i = 1; i < stop_count; i++ )
    {
        if( t <= stops[i].position )
        {
            double span = stops[i].position - stops[i - 1].position;
            double u    = span > 0.0 ? ( t - stops[i - 1].position ) / span : 0.0;
            *r          = lerp_u8( stops[i - 1].r, stops[i].r, u );
            *g          = lerp_u8( stops[i - 1].g, stops[i].g, u );
            *b          = lerp_u8( stops[i - 1].b, stops[i].b, u );
            return;
        }
    }

    *r = stops[stop_count - 1].r;
    *g = stops[stop_count - 1].g;
    *b = stops[stop_count - 1].b;
}

const char *hti_palette_name( enum hti_palette_mode palette )
{
    switch( palette )
    {
        case HTI_PALETTE_IRON:
            return "iron";
        case HTI_PALETTE_GRAY:
            return "gray";
        case HTI_PALETTE_VIRIDIS:
            return "viridis";
        case HTI_PALETTE_INFERNO:
            return "inferno";
        case HTI_PALETTE_COUNT:
            break;
    }
    return "unknown";
}

enum hti_palette_mode hti_next_palette( enum hti_palette_mode palette )
{
    switch( palette )
    {
        case HTI_PALETTE_IRON:
            return HTI_PALETTE_GRAY;
        case HTI_PALETTE_GRAY:
            return HTI_PALETTE_VIRIDIS;
        case HTI_PALETTE_VIRIDIS:
            return HTI_PALETTE_INFERNO;
        case HTI_PALETTE_INFERNO:
            return HTI_PALETTE_IRON;
        case HTI_PALETTE_COUNT:
            break;
    }
    return HTI_PALETTE_IRON;
}

void hti_palette_map( enum hti_palette_mode palette, double normalized,
                      uint8_t *r, uint8_t *g, uint8_t *b )
{
    double t = clamp01( normalized );

    if( palette == HTI_PALETTE_GRAY )
    {
        uint8_t v = lerp_u8( 0, 255, t );
        *r        = v;
        *g        = v;
        *b        = v;
        return;
    }

    if( palette == HTI_PALETTE_VIRIDIS )
    {
        map_from_stops( viridis_stops, sizeof( viridis_stops ) / sizeof( viridis_stops[0] ),
                        t, r, g, b );
        return;
    }

    if( palette == HTI_PALETTE_INFERNO )
    {
        map_from_stops( inferno_stops, sizeof( inferno_stops ) / sizeof( inferno_stops[0] ),
                        t, r, g, b );
        return;
    }

    // Iron scheme
    if( t < 0.18 )
    {
        double u = t / 0.18;
        *r       = lerp_u8( 0, 0, u );
        *g       = lerp_u8( 0, 0, u );
        *b       = lerp_u8( 0, 120, u );
    }
    else if( t < 0.45 )
    {
        double u = ( t - 0.18 ) / 0.27;
        *r       = lerp_u8( 0, 160, u );
        *g       = lerp_u8( 0, 0, u );
        *b       = lerp_u8( 120, 0, u );
    }
    else if( t < 0.72 )
    {
        double u = ( t - 0.45 ) / 0.27;
        *r       = lerp_u8( 160, 255, u );
        *g       = lerp_u8( 0, 120, u );
        *b       = lerp_u8( 0, 0, u );
    }
    else if( t < 0.90 )
    {
        double u = ( t - 0.72 ) / 0.18;
        *r       = lerp_u8( 255, 255, u );
        *g       = lerp_u8( 120, 255, u );
        *b       = lerp_u8( 0, 0, u );
    }
    else
    {
        double u = ( t - 0.90 ) / 0.10;
        *r       = lerp_u8( 255, 255, u );
        *g       = lerp_u8( 255, 255, u );
        *b       = lerp_u8( 0, 255, u );
    }
}
