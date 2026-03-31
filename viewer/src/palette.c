#include "hti_usb_view.h"

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

const char *hti_palette_name( enum hti_palette_mode palette )
{
    switch( palette )
    {
        case HTI_PALETTE_IRON:
            return "iron";
        case HTI_PALETTE_GRAY:
            return "gray";
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
