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

/*
 * FFC (Flat Field Correction) shutter detection state machine.
 *
 * The camera periodically closes its internal shutter for uniformity correction.
 * During this window the pixel data is held constant (replay).
 * The detector watches for footer baseline changes and pixel-level duplication 
 * to track the 4-state cycle:
 *
 * NONE:        normal streaming, accumulating stable_run_ns
 * TRIGGERED:   upper baseline (w20/w25/w03) changed, or manual trigger
 * REPLAY:      pixel-for-pixel duplicate of previous frame
 * RECOVERED:   first fresh frame after replay ends
 *
 * A manual trigger (user presses 's' or --trigger-after fires) enters TRIGGERED
 * immediately, regardless of footer state.
 */
void hti_ffc_update( struct hti_live_state *state, struct hti_frame *frame, uint64_t now_ns )
{
    bool footer_changed         = frame_footer_changed( state, frame );
    bool upper_baseline_changed = frame_upper_baseline_changed( state, frame );
    bool replay_detected        = frame_radiometric_replay( state, frame );
    bool start_ffc_window       = false;
    bool manual_ffc             = false;

    frame->capture_monotonic_ns   = now_ns;
    frame->frame_delta_ns         = state->last_frame_monotonic_ns > 0 && now_ns >= state->last_frame_monotonic_ns
                                        ? now_ns - state->last_frame_monotonic_ns
                                        : 0;
    frame->footer_changed         = footer_changed;
    frame->replay_detected        = replay_detected;
    frame->ffc_state              = HTI_FFC_NONE;
    frame->ffc_manual             = false;
    frame->quantitative_valid     = true;
    frame->ffc_window_ns          = 0;
    frame->stable_run_ns          = 0;
    frame->previous_stable_run_ns = state->last_completed_stable_run_ns;

    if( !state->ffc_window_active && state->manual_trigger_pending )
    {
        start_ffc_window = true;
        manual_ffc       = true;
    }
    else if( !state->ffc_window_active && upper_baseline_changed )
    {
        start_ffc_window = true;
        manual_ffc       = false;
    }

    if( start_ffc_window )
    {
        if( state->stable_run_start_ns > 0 && now_ns >= state->stable_run_start_ns )
        {
            state->last_completed_stable_run_ns = now_ns - state->stable_run_start_ns;
        }
        state->stable_run_start_ns    = 0;
        state->ffc_window_active      = true;
        state->current_ffc_manual     = manual_ffc;
        state->ffc_window_start_ns    = now_ns;
        state->manual_trigger_pending = false;
        frame->previous_stable_run_ns = state->last_completed_stable_run_ns;
    }

    if( state->ffc_window_active )
    {
        frame->ffc_manual         = state->current_ffc_manual;
        frame->ffc_window_ns      = now_ns >= state->ffc_window_start_ns ? now_ns - state->ffc_window_start_ns : 0;
        frame->quantitative_valid = false;

        if( replay_detected )
        {
            frame->ffc_state   = HTI_FFC_REPLAY;
            state->replay_seen = true;
        }
        else if( state->replay_seen )
        {
            frame->ffc_state           = HTI_FFC_RECOVERED;
            state->ffc_window_active   = false;
            state->replay_seen         = false;
            state->stable_run_start_ns = now_ns;
            frame->quantitative_valid  = true;
        }
        else
        {
            frame->ffc_state = HTI_FFC_TRIGGERED;
        }
    }

    if( !state->ffc_window_active && frame->ffc_state == HTI_FFC_NONE )
    {
        if( state->stable_run_start_ns == 0 )
        {
            state->stable_run_start_ns = now_ns;
        }

        frame->stable_run_ns = now_ns >= state->stable_run_start_ns ? now_ns - state->stable_run_start_ns : 0;
    }
    else if( frame->ffc_state == HTI_FFC_RECOVERED )
    {
        frame->stable_run_ns = 0;
    }

    state->last_frame_monotonic_ns = now_ns;
    state->previous_r0_w20         = frame->footer.r0_w20;
    state->previous_r0_w25         = frame->footer.r0_w25;
    state->previous_r0_w03         = frame->footer.r0_w03;
    state->previous_r1_u32_10      = frame->footer.r1_u32_10;
    state->previous_w2             = frame->display_footer.w2;
    state->previous_w5             = frame->display_footer.w5;
    state->previous_w8             = frame->display_footer.w8;
    state->previous_w9             = frame->display_footer.w9;
    memcpy( state->previous_radiometric, frame->radiometric, sizeof( state->previous_radiometric ) );
    state->have_previous_frame = true;
}
