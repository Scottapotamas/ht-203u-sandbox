#include "hik_probe.h"

#include <stdio.h>

typedef struct
{
    const char *label;
    size_t      offset;
} HikEnhancementUnknownByte;

static const HikEnhancementUnknownByte kUnknownEnhancementBytes[] = {
    { "unknown-2", 2 },
    { "unknown-8", 8 },
    { "unknown-9", 9 },
    { "unknown-10", 10 },
    { "unknown-11", 11 },
    { "unknown-12", 12 },
    { "unknown-13", 13 },
    { "unknown-14", 14 },
    { "unknown-15", 15 },
    { "unknown-16", 16 },
    { "unknown-22", 22 },
};

static void print_unknown_bytes( const uint8_t *data )
{
    printf( "  undecoded bytes:" );
    for( size_t i = 0; i < sizeof( kUnknownEnhancementBytes ) / sizeof( kUnknownEnhancementBytes[0] ); ++i )
    {
        printf( " [%zu]=0x%02x", kUnknownEnhancementBytes[i].offset, data[kUnknownEnhancementBytes[i].offset] );
    }
    putchar( '\n' );
}

static void maybe_patch_byte( uint8_t *buf, size_t index, uint8_t value, size_t *changed_count )
{
    if( buf[index] != value )
    {
        buf[index] = value;
        ++( *changed_count );
    }
}

bool hik_enhancement_overrides_requested( const HikOptions *opts )
{
    return opts->palette_set || opts->noise_reduce_mode_set || opts->frame_noise_level_set || opts->interframe_noise_level_set || opts->lse_detail_enabled_set || opts->lse_detail_level_set || opts->bird_watching_mode_set || opts->highlight_mode_set || opts->highlight_level_set || opts->hook_edge_mode_set || opts->hook_edge_level_set || opts->agc_mode_set;
}

void hik_print_enhancement_summary( const uint8_t *data, size_t len )
{
    const char *palette_name = NULL;

    if( len < HIK_EXPECTED_ENHANCEMENT_LEN )
    {
        printf( "Image enhancement payload too short to decode (%zu byte(s), expected %u)\n",
                len,
                HIK_EXPECTED_ENHANCEMENT_LEN );
        return;
    }

    palette_name = hik_palette_name( data[HIK_ENHANCEMENT_BYTE_PALETTE] );

    puts( "Image enhancement fields:" );
    printf( "  channel/sub-id: %u\n", data[HIK_ENHANCEMENT_BYTE_CHANNEL] );
    printf( "  noise-reduce-mode: %u\n", data[HIK_ENHANCEMENT_BYTE_NOISE_REDUCE_MODE] );
    printf( "  frame-noise-level: %u\n", data[HIK_ENHANCEMENT_BYTE_FRAME_NOISE_LEVEL] );
    printf( "  interframe-noise-level: %u\n", data[HIK_ENHANCEMENT_BYTE_INTERFRAME_NOISE_LEVEL] );
    if( palette_name != NULL )
    {
        printf( "  palette: %s (0x%02x)\n", palette_name, data[HIK_ENHANCEMENT_BYTE_PALETTE] );
    }
    else
    {
        printf( "  palette: 0x%02x\n", data[HIK_ENHANCEMENT_BYTE_PALETTE] );
    }
    printf( "  lse-detail-enabled: %u\n", data[HIK_ENHANCEMENT_BYTE_LSE_DETAIL_ENABLED] );
    printf( "  lse-detail-level: %u\n", data[HIK_ENHANCEMENT_BYTE_LSE_DETAIL_LEVEL] );
    printf( "  bird-watching-mode: %u\n", data[HIK_ENHANCEMENT_BYTE_BIRD_WATCHING_MODE] );
    printf( "  highlight-mode: %u\n", data[HIK_ENHANCEMENT_BYTE_HIGHLIGHT_MODE] );
    printf( "  highlight-level: %u\n", data[HIK_ENHANCEMENT_BYTE_HIGHLIGHT_LEVEL] );
    printf( "  hook-edge-mode: %u\n", data[HIK_ENHANCEMENT_BYTE_HOOK_EDGE_MODE] );
    printf( "  hook-edge-level: %u\n", data[HIK_ENHANCEMENT_BYTE_HOOK_EDGE_LEVEL] );
    printf( "  agc-mode: %u\n", data[HIK_ENHANCEMENT_BYTE_AGC_MODE] );
    print_unknown_bytes( data );
}

size_t hik_apply_enhancement_patch( uint8_t *buf, size_t len, const HikOptions *opts )
{
    size_t changed = 0;

    if( len < HIK_EXPECTED_ENHANCEMENT_LEN )
    {
        return 0;
    }

    if( opts->noise_reduce_mode_set )
    {
        maybe_patch_byte( buf, HIK_ENHANCEMENT_BYTE_NOISE_REDUCE_MODE, opts->noise_reduce_mode, &changed );
    }
    if( opts->frame_noise_level_set )
    {
        maybe_patch_byte( buf, HIK_ENHANCEMENT_BYTE_FRAME_NOISE_LEVEL, opts->frame_noise_level, &changed );
    }
    if( opts->interframe_noise_level_set )
    {
        maybe_patch_byte( buf,
                          HIK_ENHANCEMENT_BYTE_INTERFRAME_NOISE_LEVEL,
                          opts->interframe_noise_level,
                          &changed );
    }
    if( opts->palette_set )
    {
        maybe_patch_byte( buf, HIK_ENHANCEMENT_BYTE_PALETTE, opts->palette, &changed );
    }
    if( opts->lse_detail_enabled_set )
    {
        maybe_patch_byte( buf, HIK_ENHANCEMENT_BYTE_LSE_DETAIL_ENABLED, opts->lse_detail_enabled, &changed );
    }
    if( opts->lse_detail_level_set )
    {
        maybe_patch_byte( buf, HIK_ENHANCEMENT_BYTE_LSE_DETAIL_LEVEL, opts->lse_detail_level, &changed );
    }
    if( opts->bird_watching_mode_set )
    {
        maybe_patch_byte( buf, HIK_ENHANCEMENT_BYTE_BIRD_WATCHING_MODE, opts->bird_watching_mode, &changed );
    }
    if( opts->highlight_mode_set )
    {
        maybe_patch_byte( buf, HIK_ENHANCEMENT_BYTE_HIGHLIGHT_MODE, opts->highlight_mode, &changed );
    }
    if( opts->highlight_level_set )
    {
        maybe_patch_byte( buf, HIK_ENHANCEMENT_BYTE_HIGHLIGHT_LEVEL, opts->highlight_level, &changed );
    }
    if( opts->hook_edge_mode_set )
    {
        maybe_patch_byte( buf, HIK_ENHANCEMENT_BYTE_HOOK_EDGE_MODE, opts->hook_edge_mode, &changed );
    }
    if( opts->hook_edge_level_set )
    {
        maybe_patch_byte( buf, HIK_ENHANCEMENT_BYTE_HOOK_EDGE_LEVEL, opts->hook_edge_level, &changed );
    }
    if( opts->agc_mode_set )
    {
        maybe_patch_byte( buf, HIK_ENHANCEMENT_BYTE_AGC_MODE, opts->agc_mode, &changed );
    }

    return changed;
}
