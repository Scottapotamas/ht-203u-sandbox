# Cold Start Warmup Behaviour

This was a cold-start behavioral check using the live `viewer/hti-usb-view` in headless mode after leaving the camera in a fridge for about 20 min before connection.

The outcome is knowing that a cold camera should not be treated as immediately usable just because it can stream thermal data:

- the first several minutes are strongly biased and heavily correction-driven
- even after `15 min`, the camera was still not back to warmed steady-state behavior

Later follow-up analysis in [`../2026-03-31-internal-temp-and-ffc-hypothesis/README.md`](/home/scott/src/ht-203u-sandbox/reverse-engineering/2026-03-31-internal-temp-and-ffc-hypothesis/README.md) suggests the rapid repeated “spontaneous correction detected” lines in this run are better interpreted as upper-footer baseline transitions, not confirmed audible mechanical shutter closes.

The scene was intentionally static and close to the lens, with the camera looking at a surface about `1 cm` away. The rough environmental references were:

- fridge setpoint around `3 C`, but not trusted as a calibrated value
- room ambient around `23 C`

The question here was whether the footer terms, apparent scene drift, and automatic shutter behavior changed in a recognizable way as the camera warmed up.

The raw log used for this pass was kept outside the repo in `/tmp/hti-fridge-warmup-viewer.log`.

## Method

```bash
sudo timeout 900 ./viewer/hti-usb-view --headless --correction raw > /tmp/hti-fridge-warmup-viewer.log 2>&1
```

The saved log was then parsed with:

```bash
./extract_warmup_behaviour.py \
    /tmp/hti-fridge-warmup-viewer.log \
    --events-csv warmup-events.csv \
    --minute-summary-csv warmup-minute-summary.csv
```

## Results

The camera did not behave like a warmed unit with a bad initial offset. It spent the whole `15 min` run warming and re-baselining.

Apparent scene drift:

- first logged frame: `hot 47.97 C`, `cold 46.10 C`, `center 47.56 C`, `mean 47.10 C`
- last logged frame: `hot 32.07 C`, `cold 30.03 C`, `center 31.32 C`, `mean 31.07 C`
- change over the run: about `-16 C` on all four upper-image summary stats

The real scene change should be near-zero for a rough isothermal close target. So the cold-start offset and drift appear to dominate the raw interpretation.

Footer drift:

- `r0_w20`: `771 -> 1717`
- `r0_w25`: `725 -> 1679`
- `r1_u32_10`: `12320 -> 22920`
- lower processed footer `w2/w5/w8/w9`: each rose by about `+300..311` codes

The upper footer low-side pair `w20/w25` moved in a staircase, not smoothly.

Across this run they passed through `22` distinct plateaus and those step changes landed on shutter-side frames. The lower processed footer moved much more continuously, with small frame-to-frame jitter but a clear monotonic rise in its minute medians.

Selected minute buckets from `warmup-minute-summary.csv`:

- minute `0`: `mean 44.56 C`, `w20/w25 859/815`, lower `w5 4424`, trigger median stable time `15.72 s`
- minute `2`: `mean 38.31 C`, `w20/w25 1165/1112`, lower `w5 4467`, trigger median stable time `9.80 s`
- minute `3`: `mean 36.54 C`, `w20/w25 1294/1251`, lower `w5 4509`, trigger median stable time `0.316 s`
- minute `7`: `mean 32.90 C`, `w20/w25 1549/1504`, lower `w5 4625`, trigger median stable time `0.400 s`
- minute `12`: `mean 31.33 C`, `w20/w25 1717/1679`, lower `w5 4738`, trigger median stable time `1.999 s`
- minute `14`: `mean 31.15 C`, `w20/w25 1717/1679`, lower `w5 4730`, trigger median stable time `1.820 s`

Automatic shutter behavior:

- The run appears to start during or immediately adjacent to an initial startup correction window, with the first recovered frame at about `4.00 s`
- After that, the first clearly visible spontaneous triggers were after about `13.9 s`, `17.5 s`, `18.9 s`, `24.0 s`, and `17.5 s` of stable frames
- Around minute `3`, the camera dropped into a much more aggressive correction regime
- Between minutes `3` and `10`, the typical stable run before the next spontaneous trigger was only about `0.3..1.1 s`
- By minutes `12..14`, that had only partially relaxed to about `1.8..2.2 s` median, with occasional `4.7..6.0 s` runs

Recovery behavior also changed:

- early in the run, recoveries were often around `1.0..2.6 s`
- in the aggressive warmup regime, median recovery age was often around `4 s`
- late in the run, recovery medians shortened again to roughly `2.6..3.4 s`

One wrinkle is that the headless log sometimes shows a recovered event without an explicit preceding `ffc=triggered` line. That already showed up in the March 30 warmed-camera note too. The event CSV keeps those rows with blank trigger-side fields rather than guessing.

## Interpretation

This looks like cold-start warmup behavior, not steady automatic shuttering.

Three things are happening together:

- the apparent image temperature is falling rapidly even though the scene is static
- the upper footer baseline terms are ratcheting upward in large discrete steps
- the camera is repeatedly forcing new shutter / correction cycles at very short intervals

The regime change near minute `3` is the clearest marker. Up to that point the unit still allowed tens of seconds of stable runtime between spontaneous corrections. After that, it started correcting again every fraction of a second to couple of seconds, and it never returned to the warmed-room behavior seen on March 30 where the steady-state cadence was closer to `10 min`.

The upper and lower footer paths also do not drift the same way:

- `w20/w25` behave like internal baseline state, advancing in discrete steps at correction boundaries
- lower `w2/w5/w8/w9` behave more like a processed-path summary that keeps sliding as the unit warms
- `r1_u32_10` climbs strongly through the whole run and is clearly carrying warmup state, but not in a way that directly gives corrected scene temperature

# Ambient Warmup Behaviour

Repeated the test after letting the camera cool naturally overnight to room temperature (~23 celcius) rather than chilling it in the fridge.

The scene was again a short-range static target with little expected temperature spread, slightly angled but intended to stay effectively isothermal for this purpose.

## Result summary

This run was less severe than the fridge startup, but the hardware was clearly not ready immediately.

Upper-image apparent temperature drift:

- First stable frame: `mean 43.45 C`, `center 43.49 C`, `hot 44.10 C`, `cold 42.85 C`
- Last stable frame: `mean 31.45 C`, `center 31.66 C`, `hot 32.37 C`, `cold 30.47 C`
- Yotal change over `15 min`: about `-12 C`

Upper and lower footer drift:

- `w20`: `1238 -> 1771`
- `w25`: `1185 -> 1734`
- `r1_u32_10`: `23840 -> 24980`
- lower `w8` median only moved from about `4812` to `4860`

Using the existing lower-footer ambient fit, that lower `w8` shift corresponds to only about `29.32 C -> 30.14 C`. So the lower processed summary stayed comparatively steady while the raw upper path started far too hot and drifted downward toward it.

The upper-footer staircase was also smaller and simpler than the fridge-start run:

- `13` distinct `w20/w25` plateau states here
- versus `22` in the fridge-start run
- `w25` matched the previous `w20` on all `12` observed transitions after the first plateau, supporting previous results.

Shutter intervals were less frequent as well:

- Minute `0`: several short early transitions after connection
- Minute `1`: one transition after `28.9 s`
- Minute `6`: one transition after `62.4 s`
- Minute `10`: one transition after `137.3 s`
- Minute `13`: one transition after `203.9 s`

`r1_u32_10` behaved differently too:

- moved by about `+1.14 C` if read as `r1 / 1000`
- reached its final `24980` plateau within roughly the first minute

## Footer-Correction Fit

Using the lower processed footer as the comparison reference on all `ffc=none` frames:

- raw mean RMSE: `6.03 C`
- `r1` mean correction RMSE: `3.35 C`
- `w20` mean correction RMSE: `0.20 C`
- `w20 + r1` mean correction RMSE: `0.19 C`

The same pattern held for min, max, and center.

So the room-temperature run agrees with prior results:

- `w20` carries the dominant per-frame additive baseline correction signal
- `r1` helps a little
- `w25` provides previous-baseline history

One notable difference from the fridge-start run is that `r1` saturates here. That makes `r1` even less useful as a standalone correction input on a room-temperature startup.