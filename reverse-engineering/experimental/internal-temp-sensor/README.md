# Internal Temperature And FFC Baseline Hypothesis

Following from watching the behaviour during warmup, there were some clues to better interprete the values in the ratiometric footer:

- `r1_u32_10` as a possible internal temperature or warmup-state signal
- `r0_w20` as the current FFC / NUC baseline state
- `r0_w25` as the previous FFC / NUC baseline state

Then checking those fields with different decode strategies aiming for improved correlation when compared against the lower camera-processed image for scenes where that lower footer is already known to behave like a good thermometry summary (i.e. not clipped).

## Experiment Notes

Dataset pass:

- decode each saved `dataset/*_full_yuyv.raw`
- extract upper raw image min/max/mean/center in the current simple `u16 -> C` model
- extract upper footer `w20`, `w25`, `r1_u32_10`
- extract lower footer `w2/w5/w8/w9`
- turn the lower footer into reference `min/max/mean/center` using the March 30 ambient lower-footer fits

Warmup pass:

- keep only `ffc=none` frames with valid lower footer
- compare the upper raw stats against lower-footer-derived reference stats on that same static scene
- fit simple additive corrections of the form:
  - raw only
  - raw plus `a + b * (r1_u32_10 / 1000)`
  - raw plus `a + b * (w20 / 100)`
  - raw plus `a + b * (w25 / 100)`
  - raw plus `a + b * (w20 / 100) + c * (r1_u32_10 / 1000)`

These aren't final models or known to be correct, but better than it was.

## Results

For dataset captures `001` through `010`, the upper footer state is effectively constant because I captured most of those scenes in a fairly short period of time:

- `w20 = 1832`
- `w25 = 1831`
- `r1_u32_10 = 25300`

So the dataset at this point does not have enough internal-state variation to tell much about warmup compensation. It is useful for scene-level validation, but not for identifying internal thermal state.

The interesting dataset rows from startup and FFC captures:

- `011_lenscoverstartup`: `w20 = 1443`, `w25 = 0`, `r1 = 27.880`
- `012_lenscoverffc`: `w20 = 1491`, `w25 = 1446`, `r1 = 28.860`
- `013_lenscoverffc2`: `w20 = 1532`, `w25 = 1491`, `r1 = 29.660`

### `w20` used for thermal drift?

Against the lower-footer reference on the static cold-start scene, raw upper-image stats were badly biased:

- min RMSE `11.49 C`
- max RMSE `13.59 C`
- mean RMSE `12.61 C`
- center RMSE `11.76 C`

Using only `r1_u32_10 / 1000` as an additive correction helps a lot, but not enough:

- min RMSE `2.06 C`
- max RMSE `2.12 C`
- mean RMSE `2.11 C`
- center RMSE `2.13 C`

Using only `w20 / 100` as the additive correction works far better:

- min RMSE `0.30 C`
- max RMSE `0.19 C`
- mean RMSE `0.19 C`
- center RMSE `0.26 C`

Adding `r1` on top of `w20` improves things a little further:

- min RMSE `0.18 C`
- max RMSE `0.16 C`
- mean RMSE `0.14 C`
- center RMSE `0.22 C`

So on the cold-start same-scene run, `w20` carries the dominant correction signal. `r1` looks useful as a smaller residual term, possibly a smoother internal temperature value.

### `w25` is the previous `w20`

The plateau table from `warmup-w20-plateaus.csv` shows it fairly clearly.

- During the warmup run there were `22` distinct `w20/w25` staircase states.
- Across the `21` state transitions after the first plateau, `w25` matched the previous plateau’s `w20` every time

So it's probably fair to assume that:

- `w20` is the current baseline state
- `w25` is the previous baseline state

which is why building a ratio from them in early-stage decode tests didn't work too well.

The same table also shows `r1_u32_10` continuing to climb within a fixed `w20/w25` plateau.

### Checking `w20` correction performance

Applying the warmup-fitted mean correction back onto the saved startup / FFC captures improves them a lot against the lower-footer mean reference:

- raw startup / FFC mean RMSE: `9.32 C`
- `w20`-corrected startup / FFC mean RMSE: `2.16 C`
- `w20 + r1` corrected startup / FFC mean RMSE: `1.22 C`

For the ordinary steady captures `001..010`, the same correction only changes mean RMSE from about `1.30 C` to about `1.29 C`. Makes sense given they were probably all captured within a single FFC window.

The center statistic is less clean on the ordinary dataset because lower-footer `w9` is itself noisier as a center proxy on some non-ambient scenes.

## Summary

These behaviours in the ratiometric metadata pixels seem more sensible at a gut-feel level:

`r1_u32_10`:

- Probably an internal temperature or warmup-state sensor.
- Useful as a smooth state variable.
- Not the main per-frame correction signal by itself.

`r0_w20`:

- Best candidate for the active FFC / NUC baseline term
- Explains most of the additive offset drift on the static warmup scene
- Looks like the current baseline (latched on FFC events) rather than a raw free-running sensor

`r0_w25`:

- Best candidate for the previous FFC / NUC baseline term?
- Lags `w20` by a FFC event.
- Much less useful than `w20` as the primary correction term when taken alone
- Unsure how this would be used, or why it's included other than for FFC identification?

The lower-half hardware-processed image footer has `w2/w5/w8/w9` bytes:

- Still the best current reference path during warmup as long as the scene is ordinary and not AGC-saturated
- Strong enough to score upper-footer correction ideas?
- Seems to clip at 210 celcius
