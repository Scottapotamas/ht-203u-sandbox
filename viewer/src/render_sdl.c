#include "hti_usb_view.h"

#include <SDL2/SDL.h>

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    HTI_FONT_W                       = 5,
    HTI_FONT_H                       = 7,
    HTI_FONT_TRACK                   = 1,
    HTI_OVERLAY_MAX_LINES            = 10,
    HTI_OVERLAY_TEXT_LEN             = 96,
    HTI_OVERLAY_OUTER_PAD            = 8,
    HTI_OVERLAY_INNER_PAD_X          = 6,
    HTI_OVERLAY_INNER_PAD_Y          = 4,
    HTI_OVERLAY_LINE_GAP             = 3,
    HTI_OVERLAY_SHADOW_OFFSET        = 1,
    HTI_OVERLAY_BG_ALPHA             = 144,
    HTI_OVERLAY_BORDER_ALPHA         = 196,
    HTI_OVERLAY_FONT_SCALE_MINIMAL   = 3,
    HTI_OVERLAY_FONT_SCALE_DEBUG     = 2,
    HTI_OVERLAY_FONT_WEIGHT          = 1,
    HTI_OVERLAY_REGISTER_LABEL_WIDTH = 6,
    HTI_OVERLAY_THERMAL_LABEL_WIDTH  = 7,
};

enum hti_overlay_anchor
{
    HTI_OVERLAY_TOP_LEFT = 0,
    HTI_OVERLAY_TOP_CENTER,
    HTI_OVERLAY_TOP_RIGHT,
    HTI_OVERLAY_BOTTOM_LEFT,
    HTI_OVERLAY_BOTTOM_CENTER,
    HTI_OVERLAY_BOTTOM_RIGHT,
};

struct hti_overlay_line
{
    char      text[HTI_OVERLAY_TEXT_LEN];
    SDL_Color color;
};

struct hti_overlay_block
{
    enum hti_overlay_anchor anchor;
    int                     line_count;
    struct hti_overlay_line lines[HTI_OVERLAY_MAX_LINES];
};

static const uint8_t HTI_FONT_5X7[128][HTI_FONT_H] = {
    [' '] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    ['+'] = { 0x00, 0x04, 0x04, 0x1f, 0x04, 0x04, 0x00 },
    ['-'] = { 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00 },
    ['.'] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x04 },
    ['/'] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00 },
    ['0'] = { 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e },
    ['1'] = { 0x04, 0x0c, 0x14, 0x04, 0x04, 0x04, 0x1f },
    ['2'] = { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f },
    ['3'] = { 0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e },
    ['4'] = { 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 },
    ['5'] = { 0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e },
    ['6'] = { 0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e },
    ['7'] = { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
    ['8'] = { 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e },
    ['9'] = { 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e },
    [':'] = { 0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00 },
    ['?'] = { 0x0e, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04 },
    ['A'] = { 0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 },
    ['B'] = { 0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e },
    ['C'] = { 0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e },
    ['D'] = { 0x1c, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1c },
    ['E'] = { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f },
    ['F'] = { 0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10 },
    ['G'] = { 0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f },
    ['H'] = { 0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11 },
    ['I'] = { 0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e },
    ['J'] = { 0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0e },
    ['K'] = { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 },
    ['L'] = { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f },
    ['M'] = { 0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11 },
    ['N'] = { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 },
    ['O'] = { 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e },
    ['P'] = { 0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10 },
    ['Q'] = { 0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d },
    ['R'] = { 0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11 },
    ['S'] = { 0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e },
    ['T'] = { 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 },
    ['U'] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e },
    ['V'] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04 },
    ['W'] = { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a },
    ['X'] = { 0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11 },
    ['Y'] = { 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04 },
    ['Z'] = { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f },
    ['_'] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f },
};

static SDL_Window   *win ( struct hti_renderer *r ) { return (SDL_Window *)  r->window;   }
static SDL_Renderer *sdl ( struct hti_renderer *r ) { return (SDL_Renderer *)r->renderer; }
static SDL_Texture  *tex ( struct hti_renderer *r ) { return (SDL_Texture *) r->texture;  }

static SDL_Color hti_color( uint8_t r, uint8_t g, uint8_t b, uint8_t a )
{
    SDL_Color color = { r, g, b, a };
    return color;
}

static SDL_Rect compute_draw_rect( int win_w, int win_h )
{
    SDL_Rect rect     = { 0, 0, win_w, win_h };
    int      scaled_w = win_w;
    int      scaled_h = ( win_w * HTI_FRAME_HEIGHT ) / HTI_FRAME_WIDTH;

    if( scaled_h > win_h )
    {
        scaled_h = win_h;
        scaled_w = ( win_h * HTI_FRAME_WIDTH ) / HTI_FRAME_HEIGHT;
    }

    rect.w = scaled_w;
    rect.h = scaled_h;
    rect.x = ( win_w - rect.w ) / 2;
    rect.y = ( win_h - rect.h ) / 2;
    return rect;
}

static void draw_marker( SDL_Renderer *renderer, SDL_Rect rect, int x, int y,
                         uint8_t r, uint8_t g, uint8_t b )
{
    int px   = rect.x + ( x * rect.w ) / HTI_FRAME_WIDTH;
    int py   = rect.y + ( y * rect.h ) / HTI_FRAME_HEIGHT;
    int size = rect.w / HTI_FRAME_WIDTH;

    if( size < 4 )
    {
        size = 4;
    }
    
    if( size > 14 )
    {
        size = 14;
    }

    SDL_SetRenderDrawColor( renderer, 0, 0, 0, 255 );
    SDL_Rect outline = { px - size / 2 - 1, py - size / 2 - 1, size + 2, size + 2 };
    SDL_RenderDrawRect( renderer, &outline );

    SDL_SetRenderDrawColor( renderer, r, g, b, 255 );
    SDL_Rect box = { px - size / 2, py - size / 2, size, size };
    SDL_RenderDrawRect( renderer, &box );
    SDL_RenderDrawLine( renderer, px - size, py, px + size, py );
    SDL_RenderDrawLine( renderer, px, py - size, px, py + size );
}

static int overlay_font_scale( enum hti_overlay_mode overlay )
{
    return overlay == HTI_OVERLAY_DEBUG ? HTI_OVERLAY_FONT_SCALE_DEBUG : HTI_OVERLAY_FONT_SCALE_MINIMAL;
}

static int overlay_line_height( int scale )
{
    return HTI_FONT_H * scale;
}

static int overlay_advance( int scale )
{
    return ( HTI_FONT_W + HTI_FONT_TRACK ) * scale;
}

static int measure_text_width( const char *text, int scale )
{
    size_t len = 0;

    if( text == NULL || text[0] == '\0' )
    {
        return 0;
    }

    len = strlen( text );

    return (int)( len * (size_t)overlay_advance( scale ) ) - HTI_FONT_TRACK * scale;
}

static void overlay_block_reset( struct hti_overlay_block *block, enum hti_overlay_anchor anchor )
{
    memset( block, 0, sizeof( *block ) );
    block->anchor = anchor;
}

static void overlay_block_add( struct hti_overlay_block *block, SDL_Color color, const char *fmt, ... )
{
    va_list                  ap;
    struct hti_overlay_line *line = NULL;

    if( block->line_count >= HTI_OVERLAY_MAX_LINES )
    {
        return;
    }
    line = &block->lines[block->line_count++];
    memset( line, 0, sizeof( *line ) );
    line->color = color;

    va_start( ap, fmt );
    vsnprintf( line->text, sizeof( line->text ), fmt, ap );
    va_end( ap );
}

static void overlay_block_add_pair( struct hti_overlay_block *block,
                                    SDL_Color                 color,
                                    int                       label_width,
                                    const char               *label,
                                    const char               *value )
{
    overlay_block_add( block, color, "%-*s %s", label_width, label, value );
}

static void overlay_block_add_u32( struct hti_overlay_block *block,
                                   SDL_Color                 color,
                                   int                       label_width,
                                   const char               *label,
                                   uint32_t                  value )
{
    overlay_block_add( block, color, "%-*s %5u", label_width, label, (unsigned)value );
}

static char overlay_safe_char( char ch )
{
    unsigned char uch = (unsigned char)ch;

    if( uch >= sizeof( HTI_FONT_5X7 ) / sizeof( HTI_FONT_5X7[0] ) )
    {
        return '?';
    }

    if( isalpha( uch ) )
    {
        ch = (char)toupper( uch );
    }

    if( HTI_FONT_5X7[(unsigned char)ch][0] == 0 && ch != ' ' && ch != '-' && ch != '.' && ch != '+' && ch != '/' && ch != ':' && ch != '_' )
    {
        return '?';
    }

    return ch;
}

static void draw_text_run( SDL_Renderer *renderer, int x, int y, const char *text, int scale, SDL_Color color )
{
    int cursor_x = x;

    SDL_SetRenderDrawColor( renderer, color.r, color.g, color.b, color.a );
    for( size_t i = 0; text != NULL && text[i] != '\0'; i++ )
    {
        const uint8_t *glyph = HTI_FONT_5X7[(unsigned char)overlay_safe_char( text[i] )];

        for( int row = 0; row < HTI_FONT_H; row++ )
        {
            for( int col = 0; col < HTI_FONT_W; col++ )
            {
                if( ( glyph[row] & (uint8_t)( 1u << ( HTI_FONT_W - 1 - col ) ) ) == 0 )
                {
                    continue;
                }

                for( int weight = 0; weight < HTI_OVERLAY_FONT_WEIGHT; weight++ )
                {
                    SDL_Rect pixel = { cursor_x + col * scale + weight, y + row * scale, scale, scale };
                    SDL_RenderFillRect( renderer, &pixel );
                }
            }
        }
        cursor_x += overlay_advance( scale );
    }
}

static void draw_text( SDL_Renderer *renderer, int x, int y, const char *text, int scale, SDL_Color color )
{
    SDL_Color shadow = hti_color( 0, 0, 0, color.a );

    draw_text_run( renderer,
                   x + HTI_OVERLAY_SHADOW_OFFSET,
                   y + HTI_OVERLAY_SHADOW_OFFSET,
                   text,
                   scale,
                   shadow );
    draw_text_run( renderer, x, y, text, scale, color );
}

static void draw_overlay_block( SDL_Renderer                   *renderer,
                                int                             win_w,
                                int                             win_h,
                                const struct hti_overlay_block *block,
                                int                             scale )
{
    SDL_Rect rect       = { 0, 0, 0, 0 };
    int      max_text_w = 0;
    int      panel_h    = 0;
    int      line_h     = overlay_line_height( scale );

    if( block->line_count <= 0 )
    {
        return;
    }

    for( int i = 0; i < block->line_count; i++ )
    {
        int width = measure_text_width( block->lines[i].text, scale );

        if( width > max_text_w )
        {
            max_text_w = width;
        }
    }

    panel_h = block->line_count * line_h;
    if( block->line_count > 1 )
    {
        panel_h += ( block->line_count - 1 ) * HTI_OVERLAY_LINE_GAP;
    }

    rect.w = max_text_w + HTI_OVERLAY_INNER_PAD_X * 2;
    rect.h = panel_h + HTI_OVERLAY_INNER_PAD_Y * 2;

    switch( block->anchor )
    {
        case HTI_OVERLAY_TOP_LEFT:
            rect.x = HTI_OVERLAY_OUTER_PAD;
            rect.y = HTI_OVERLAY_OUTER_PAD;
            break;
        case HTI_OVERLAY_TOP_CENTER:
            rect.x = ( win_w - rect.w ) / 2;
            rect.y = HTI_OVERLAY_OUTER_PAD;
            break;
        case HTI_OVERLAY_TOP_RIGHT:
            rect.x = win_w - HTI_OVERLAY_OUTER_PAD - rect.w;
            rect.y = HTI_OVERLAY_OUTER_PAD;
            break;
        case HTI_OVERLAY_BOTTOM_LEFT:
            rect.x = HTI_OVERLAY_OUTER_PAD;
            rect.y = win_h - HTI_OVERLAY_OUTER_PAD - rect.h;
            break;
        case HTI_OVERLAY_BOTTOM_CENTER:
            rect.x = ( win_w - rect.w ) / 2;
            rect.y = win_h - HTI_OVERLAY_OUTER_PAD - rect.h;
            break;
        case HTI_OVERLAY_BOTTOM_RIGHT:
            rect.x = win_w - HTI_OVERLAY_OUTER_PAD - rect.w;
            rect.y = win_h - HTI_OVERLAY_OUTER_PAD - rect.h;
            break;
    }

    if( rect.x < HTI_OVERLAY_OUTER_PAD )
    {
        rect.x = HTI_OVERLAY_OUTER_PAD;
    }

    if( rect.y < HTI_OVERLAY_OUTER_PAD )
    {
        rect.y = HTI_OVERLAY_OUTER_PAD;
    }

    if( rect.x + rect.w > win_w - HTI_OVERLAY_OUTER_PAD )
    {
        rect.x = win_w - HTI_OVERLAY_OUTER_PAD - rect.w;
    }

    if( rect.y + rect.h > win_h - HTI_OVERLAY_OUTER_PAD )
    {
        rect.y = win_h - HTI_OVERLAY_OUTER_PAD - rect.h;
    }

    SDL_SetRenderDrawColor( renderer, 0, 0, 0, HTI_OVERLAY_BG_ALPHA );
    SDL_RenderFillRect( renderer, &rect );
    SDL_SetRenderDrawColor( renderer, 255, 255, 255, HTI_OVERLAY_BORDER_ALPHA );
    SDL_RenderDrawRect( renderer, &rect );

    for( int i = 0; i < block->line_count; i++ )
    {
        int line_y = rect.y + HTI_OVERLAY_INNER_PAD_Y + i * ( line_h + HTI_OVERLAY_LINE_GAP );

        draw_text( renderer,
                   rect.x + HTI_OVERLAY_INNER_PAD_X,
                   line_y,
                   block->lines[i].text,
                   scale,
                   block->lines[i].color );
    }
}

static void format_temp_value( char *dst, size_t dst_size, bool valid, double temp_c, int decimals )
{
    if( !valid || !isfinite( temp_c ) )
    {
        snprintf( dst, dst_size, decimals >= 2 ? "---.--C" : "--.-C" );
        return;
    }

    if( decimals >= 2 )
    {
        snprintf( dst, dst_size, "%7.2fC", temp_c );
    }
    else
    {
        snprintf( dst, dst_size, "%6.1fC", temp_c );
    }
}

static void format_seconds_value( char *dst, size_t dst_size, uint64_t ns )
{
    snprintf( dst, dst_size, "%7.2fS", (double)ns / 1000000000.0 );
}

static void format_bits_value( char *dst, size_t dst_size, bool valid, double bits )
{
    if( !valid || !isfinite( bits ) )
    {
        snprintf( dst, dst_size, "--.--" );
        return;
    }

    snprintf( dst, dst_size, "%5.2f", bits );
}

static void format_frame_label( char *dst, size_t dst_size, const char *name )
{
    size_t j = 0;

    if( dst_size == 0 )
    {
        return;
    }

    for( size_t i = 0; name != NULL && name[i] != '\0' && j + 1 < dst_size; i++ )
    {
        char ch = name[i] == '_' ? ' ' : name[i];
        dst[j++] = (char)toupper( (unsigned char)ch );
    }

    dst[j] = '\0';
}

static SDL_Color status_color( const struct hti_frame *frame, bool paused )
{
    if( paused )
    {
        return hti_color( 255, 224, 128, 255 );
    }

    switch( frame->ffc_state )
    {
        case HTI_FFC_NONE:
            return frame->quantitative_valid ? hti_color( 176, 255, 176, 255 ) : hti_color( 255, 208, 128, 255 );
        case HTI_FFC_TRIGGERED:
            return hti_color( 255, 176, 96, 255 );
        case HTI_FFC_REPLAY:
            return hti_color( 255, 200, 96, 255 );
        case HTI_FFC_RECOVERED:
            return hti_color( 176, 255, 176, 255 );
    }

    return hti_color( 240, 240, 240, 255 );
}

static void format_status_label( char *dst, size_t dst_size, const struct hti_frame *frame, bool paused )
{
    if( paused )
    {
        snprintf( dst, dst_size, "PAUSED" );
        return;
    }

    switch( frame->ffc_state )
    {
        case HTI_FFC_NONE:
            snprintf( dst, dst_size, frame->quantitative_valid ? "LIVE VIEW" : "QUANT MASKED" );
            break;
        case HTI_FFC_TRIGGERED:
            snprintf( dst, dst_size, frame->ffc_manual ? "MANUAL FFC" : "FFC TRIGGERED" );
            break;
        case HTI_FFC_REPLAY:
            snprintf( dst, dst_size, "FFC REPLAY" );
            break;
        case HTI_FFC_RECOVERED:
            snprintf( dst, dst_size, "POST FFC FRESH" );
            break;
    }
}

static void build_overlay_blocks( struct hti_overlay_block           *top_left,
                                  struct hti_overlay_block           *top_center,
                                  struct hti_overlay_block           *top_right,
                                  struct hti_overlay_block           *bottom_left,
                                  struct hti_overlay_block           *bottom_center,
                                  struct hti_overlay_block           *bottom_right,
                                  const struct hti_frame             *frame,
                                  const struct hti_temperature_stats *stats,
                                  const struct hti_options           *opt,
                                  const struct hti_display_range     *range,
                                  bool                                paused )
{
    SDL_Color tool    = hti_color( 232, 232, 232, 255 );
    SDL_Color accent  = hti_color( 160, 224, 255, 255 );
    SDL_Color warm    = hti_color( 255, 96, 64, 255 );
    SDL_Color cold    = hti_color( 64, 160, 255, 255 );
    SDL_Color center  = hti_color( 255, 255, 255, 255 );
    SDL_Color average = hti_color( 176, 255, 176, 255 );
    char      buf0[32];
    char      buf1[32];
    char      buf2[32];
    char      buf3[32];
    char      label[HTI_OVERLAY_TEXT_LEN];
    char      status[HTI_OVERLAY_TEXT_LEN];
    int       decimals    = opt->overlay == HTI_OVERLAY_DEBUG ? 2 : 1;
    bool      lower_valid = frame->display_footer.valid;

    overlay_block_reset( top_left, HTI_OVERLAY_TOP_LEFT );
    overlay_block_reset( top_center, HTI_OVERLAY_TOP_CENTER );
    overlay_block_reset( top_right, HTI_OVERLAY_TOP_RIGHT );
    overlay_block_reset( bottom_left, HTI_OVERLAY_BOTTOM_LEFT );
    overlay_block_reset( bottom_center, HTI_OVERLAY_BOTTOM_CENTER );
    overlay_block_reset( bottom_right, HTI_OVERLAY_BOTTOM_RIGHT );

    format_frame_label( label, sizeof( label ), frame->name );
    overlay_block_add( top_left, tool, "%s", label );
    overlay_block_add( top_left, tool, "CORR %s", hti_correction_name( opt->correction ) );

    if( opt->overlay == HTI_OVERLAY_DEBUG )
    {
        format_seconds_value( buf0, sizeof( buf0 ), frame->stable_run_ns );
        format_seconds_value( buf1, sizeof( buf1 ), frame->previous_stable_run_ns );
        format_seconds_value( buf2, sizeof( buf2 ), frame->ffc_window_ns );
        overlay_block_add( top_left, tool, "STAB %s", buf0 );
        overlay_block_add( top_left, tool, "PREV %s", buf1 );
        overlay_block_add( top_left, tool, "AGE  %s", buf2 );
    }

    format_status_label( status, sizeof( status ), frame, paused );
    overlay_block_add( top_center, status_color( frame, paused ), "%s", status );

    if( opt->overlay == HTI_OVERLAY_DEBUG )
    {
        overlay_block_add( top_center,
                           tool,
                           "%s",
                           frame->quantitative_valid ? "QUANT VALID" : "QUANT MASKED" );
    }

    overlay_block_add( top_right,
                       tool,
                       "%s %s",
                       hti_palette_name( opt->palette ),
                       opt->manual_scale ? "MANUAL" : "AUTO" );
    format_temp_value( buf0, sizeof( buf0 ), true, range->min_c, decimals );
    format_temp_value( buf1, sizeof( buf1 ), true, range->max_c, decimals );
    overlay_block_add( top_right, tool, "MIN %s", buf0 );
    overlay_block_add( top_right, tool, "MAX %s", buf1 );

    if( opt->overlay == HTI_OVERLAY_DEBUG )
    {
        overlay_block_add_u32( bottom_left,
                               accent,
                               HTI_OVERLAY_REGISTER_LABEL_WIDTH,
                               "R1 RAW",
                               frame->footer.r1_u32_10 );
        overlay_block_add_u32( bottom_left, tool, HTI_OVERLAY_REGISTER_LABEL_WIDTH, "W20", frame->footer.r0_w20 );
        overlay_block_add_u32( bottom_left, tool, HTI_OVERLAY_REGISTER_LABEL_WIDTH, "W25", frame->footer.r0_w25 );
        overlay_block_add_u32( bottom_left, tool, HTI_OVERLAY_REGISTER_LABEL_WIDTH, "W03", frame->footer.r0_w03 );
        overlay_block_add_u32( bottom_left, tool, HTI_OVERLAY_REGISTER_LABEL_WIDTH, "R14", frame->footer.r1_u32_14 );
    }
    else
    {
        format_temp_value( buf0,
                           sizeof( buf0 ),
                           frame->footer.valid,
                           (double)frame->footer.r1_u32_10 / 1000.0,
                           decimals );
        overlay_block_add_pair( bottom_left,
                                accent,
                                HTI_OVERLAY_REGISTER_LABEL_WIDTH,
                                "R1 REF",
                                buf0 );
    }

    format_temp_value( buf0, sizeof( buf0 ), frame->quantitative_valid, stats->max_c, decimals );
    format_temp_value( buf1, sizeof( buf1 ), frame->quantitative_valid, stats->min_c, decimals );
    format_temp_value( buf2, sizeof( buf2 ), frame->quantitative_valid, stats->center_c, decimals );
    format_temp_value( buf3, sizeof( buf3 ), frame->quantitative_valid, stats->mean_c, decimals );
    overlay_block_add_pair( bottom_right, warm, HTI_OVERLAY_THERMAL_LABEL_WIDTH, "HOT", buf0 );
    overlay_block_add_pair( bottom_right, cold, HTI_OVERLAY_THERMAL_LABEL_WIDTH, "COLD", buf1 );
    overlay_block_add_pair( bottom_right, center, HTI_OVERLAY_THERMAL_LABEL_WIDTH, "CTR", buf2 );
    overlay_block_add_pair( bottom_right, average, HTI_OVERLAY_THERMAL_LABEL_WIDTH, "AVG", buf3 );

    if( opt->overlay == HTI_OVERLAY_DEBUG )
    {
        format_bits_value( buf0, sizeof( buf0 ), frame->quantitative_valid, stats->scene_dynamic_bits );
        format_temp_value( buf1, sizeof( buf1 ), frame->quantitative_valid, stats->scene_range_c, decimals );
        overlay_block_add( bottom_center, accent, "DYN  %s BITS", buf0 );
        overlay_block_add( bottom_center, tool, "SPAN %s", buf1 );

        format_temp_value( buf0, sizeof( buf0 ), lower_valid, hti_lower_footer_max_c( frame ), decimals );
        format_temp_value( buf1, sizeof( buf1 ), lower_valid, hti_lower_footer_min_c( frame ), decimals );
        format_temp_value( buf2, sizeof( buf2 ), lower_valid, hti_lower_footer_center_c( frame ), decimals );
        format_temp_value( buf3, sizeof( buf3 ), lower_valid, hti_lower_footer_mean_c( frame ), decimals );
        overlay_block_add_pair( bottom_right, warm, HTI_OVERLAY_THERMAL_LABEL_WIDTH, "REF MAX", buf0 );
        overlay_block_add_pair( bottom_right, cold, HTI_OVERLAY_THERMAL_LABEL_WIDTH, "REF MIN", buf1 );
        overlay_block_add_pair( bottom_right, center, HTI_OVERLAY_THERMAL_LABEL_WIDTH, "REF CTR", buf2 );
        overlay_block_add_pair( bottom_right, average, HTI_OVERLAY_THERMAL_LABEL_WIDTH, "REF AVG", buf3 );
    }
}

static void draw_overlay( SDL_Renderer                       *renderer,
                          int                                 win_w,
                          int                                 win_h,
                          const struct hti_frame             *frame,
                          const struct hti_temperature_stats *stats,
                          const struct hti_options           *opt,
                          const struct hti_display_range     *range,
                          bool                                paused )
{
    struct hti_overlay_block top_left;
    struct hti_overlay_block top_center;
    struct hti_overlay_block top_right;
    struct hti_overlay_block bottom_left;
    struct hti_overlay_block bottom_center;
    struct hti_overlay_block bottom_right;
    int                      scale = 0;

    if( opt->overlay == HTI_OVERLAY_NONE )
    {
        return;
    }
    scale = overlay_font_scale( opt->overlay );
    build_overlay_blocks( &top_left,
                          &top_center,
                          &top_right,
                          &bottom_left,
                          &bottom_center,
                          &bottom_right,
                          frame,
                          stats,
                          opt,
                          range,
                          paused );

    draw_overlay_block( renderer, win_w, win_h, &top_left, scale );
    draw_overlay_block( renderer, win_w, win_h, &top_center, scale );
    draw_overlay_block( renderer, win_w, win_h, &top_right, scale );
    draw_overlay_block( renderer, win_w, win_h, &bottom_left, scale );
    draw_overlay_block( renderer, win_w, win_h, &bottom_center, scale );
    draw_overlay_block( renderer, win_w, win_h, &bottom_right, scale );
}

int hti_renderer_init( struct hti_renderer *renderer, int window_scale )
{
    SDL_Window   *window       = NULL;
    SDL_Renderer *sdl_renderer = NULL;
    SDL_Texture  *texture      = NULL;

    if( SDL_Init( SDL_INIT_VIDEO | SDL_INIT_EVENTS ) < 0 )
    {
        const char *display     = getenv( "DISPLAY" );
        const char *runtime_dir = getenv( "XDG_RUNTIME_DIR" );

        fprintf( stderr, "SDL_Init failed: %s\n", SDL_GetError() );
        if( display == NULL || display[0] == '\0' || runtime_dir == NULL || runtime_dir[0] == '\0' )
        {
            fprintf( stderr,
                     "hint: no desktop display environment was detected; run without sudo, use sudo -E, or pass --headless\n" );
        }
        else
        {
            fprintf( stderr, "hint: if you only need CLI output or shutter testing, pass --headless\n" );
        }
        return -1;
    }

    SDL_SetHint( SDL_HINT_RENDER_SCALE_QUALITY, "0" );

    window = SDL_CreateWindow( "hti-usb-view",
                               SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED,
                               HTI_FRAME_WIDTH * window_scale,
                               HTI_FRAME_HEIGHT * window_scale,
                               SDL_WINDOW_RESIZABLE );
    if( window == NULL )
    {
        fprintf( stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError() );
        SDL_Quit();
        return -1;
    }

    sdl_renderer = SDL_CreateRenderer( window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC );

    if( sdl_renderer == NULL )
    {
        sdl_renderer = SDL_CreateRenderer( window, -1, SDL_RENDERER_SOFTWARE );
    }

    if( sdl_renderer == NULL )
    {
        fprintf( stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError() );
        SDL_DestroyWindow( window );
        SDL_Quit();
        return -1;
    }

    SDL_SetRenderDrawBlendMode( sdl_renderer, SDL_BLENDMODE_BLEND );

    texture = SDL_CreateTexture( sdl_renderer,
                                 SDL_PIXELFORMAT_ARGB8888,
                                 SDL_TEXTUREACCESS_STREAMING,
                                 HTI_FRAME_WIDTH,
                                 HTI_FRAME_HEIGHT );
    if( texture == NULL )
    {
        fprintf( stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError() );
        SDL_DestroyRenderer( sdl_renderer );
        SDL_DestroyWindow( window );
        SDL_Quit();
        return -1;
    }

    renderer->window   = window;
    renderer->renderer = sdl_renderer;
    renderer->texture  = texture;
    return 0;
}

void hti_renderer_shutdown( struct hti_renderer *renderer )
{
    SDL_Texture  *texture      = tex( renderer );
    SDL_Renderer *sdl_renderer = sdl( renderer );
    SDL_Window   *window       = win( renderer );

    if( texture != NULL )
    {
        SDL_DestroyTexture( texture );
    }

    if( sdl_renderer != NULL )
    {
        SDL_DestroyRenderer( sdl_renderer );
    }

    if( window != NULL )
    {
        SDL_DestroyWindow( window );
    }

    memset( renderer, 0, sizeof( *renderer ) );
    SDL_Quit();
}

void hti_renderer_present( struct hti_renderer                *renderer,
                           const struct hti_frame             *frame,
                           const struct hti_temperature_stats *stats,
                           const struct hti_options           *opt,
                           const struct hti_display_range     *range,
                           bool                                paused )
{
    SDL_Window   *window       = win( renderer );
    SDL_Renderer *sdl_renderer = sdl( renderer );
    SDL_Texture  *texture      = tex( renderer );
    void         *pixels       = NULL;
    int           pitch        = 0;
    double        denom        = range->max_c - range->min_c;
    char          title[256];

    if( denom <= 0.0 )
    {
        denom = 1.0;
    }

    if( SDL_LockTexture( texture, NULL, &pixels, &pitch ) < 0 )
    {
        fprintf( stderr, "SDL_LockTexture failed: %s\n", SDL_GetError() );
        return;
    }

    for( int y = 0; y < HTI_FRAME_HEIGHT; y++ )
    {
        uint32_t *row = (uint32_t *)( (uint8_t *)pixels + (size_t)y * pitch );

        for( int x = 0; x < HTI_FRAME_WIDTH; x++ )
        {
            int     idx        = y * HTI_FRAME_WIDTH + x;
            double  temp_c     = hti_temperature_at( frame, opt->correction, idx );
            double  normalized = ( temp_c - range->min_c ) / denom;
            uint8_t r          = 0;
            uint8_t g          = 0;
            uint8_t b          = 0;

            hti_palette_map( opt->palette, normalized, &r, &g, &b );
            row[x] = 0xff000000u | ( (uint32_t)r << 16 ) | ( (uint32_t)g << 8 ) | (uint32_t)b;
        }
    }

    SDL_UnlockTexture( texture );

    SDL_SetRenderDrawColor( sdl_renderer, 0, 0, 0, 255 );
    SDL_RenderClear( sdl_renderer );

    {
        int      win_w = 0;
        int      win_h = 0;
        SDL_Rect draw_rect;

        SDL_GetWindowSize( window, &win_w, &win_h );
        draw_rect = compute_draw_rect( win_w, win_h );
        SDL_RenderCopy( sdl_renderer, texture, NULL, &draw_rect );
        if( frame->quantitative_valid )
        {
            draw_marker( sdl_renderer, draw_rect, stats->min_x, stats->min_y, 64, 160, 255 );
            draw_marker( sdl_renderer, draw_rect, stats->max_x, stats->max_y, 255, 96, 64 );
        }
        draw_marker( sdl_renderer, draw_rect, HTI_FRAME_WIDTH / 2, HTI_FRAME_HEIGHT / 2, 255, 255, 255 );
        draw_overlay( sdl_renderer, win_w, win_h, frame, stats, opt, range, paused );
    }

    SDL_RenderPresent( sdl_renderer );

    if( frame->quantitative_valid )
    {
        snprintf( title,
                  sizeof( title ),
                  "hti-usb-view | hot %.2fC cold %.2fC center %.2fC | %s | %s | scale %.2f..%.2f%s",
                  stats->max_c,
                  stats->min_c,
                  stats->center_c,
                  hti_correction_name( opt->correction ),
                  hti_palette_name( opt->palette ),
                  range->min_c,
                  range->max_c,
                  paused ? " | paused" : "" );
    }
    else
    {
        snprintf( title,
                  sizeof( title ),
                  "hti-usb-view | quantitative masked | %s | %s | ffc %s%s",
                  hti_correction_name( opt->correction ),
                  hti_palette_name( opt->palette ),
                  hti_ffc_state_name( frame->ffc_state ),
                  paused ? " | paused" : "" );
    }
    
    SDL_SetWindowTitle( window, title );
}
