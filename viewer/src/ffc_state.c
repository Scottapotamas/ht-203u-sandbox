#include "hti_usb_view.h"

#include <string.h>

static bool frame_footer_changed( const struct hti_live_state *state, const struct hti_frame *frame )
{
    if( !state->have_previous_frame )
    {
        return false;
    }

    return frame->footer.r0_w20 != state->previous_r0_w20 || frame->footer.r0_w25 != state->previous_r0_w25 || frame->footer.r0_w03 != state->previous_r0_w03 || frame->footer.r1_u32_10 != state->previous_r1_u32_10 || frame->display_footer.w2 != state->previous_w2 || frame->display_footer.w5 != state->previous_w5 || frame->display_footer.w8 != state->previous_w8 || frame->display_footer.w9 != state->previous_w9;
}

/*
 * r0_w03 changes from 0x1F (normal) to 0x20 during FFC shutter events.
 * We include it alongside w20/w25 so both discrete baseline steps and
 * mode-switch transitions are treated as FFC triggers.
 */
static bool frame_upper_baseline_changed( const struct hti_live_state *state, const struct hti_frame *frame )
{
    if( !state->have_previous_frame )
    {
        return false;
    }

    return frame->footer.r0_w20 != state->previous_r0_w20 || frame->footer.r0_w25 != state->previous_r0_w25 || frame->footer.r0_w03 != state->previous_r0_w03;
}

static bool frame_radiometric_replay( const struct hti_live_state *state, const struct hti_frame *frame )
{
    if( !state->have_previous_frame )
    {
        return false;
    }

    return memcmp( frame->radiometric, state->previous_radiometric, sizeof( state->previous_radiometric ) ) == 0;
}
