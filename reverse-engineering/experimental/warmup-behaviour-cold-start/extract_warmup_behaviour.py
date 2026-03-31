#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import re
import statistics
from dataclasses import dataclass
from pathlib import Path


LIVE_RE = re.compile(r"^live_(\d+)\s+(.*)$")
LOWER_RE = re.compile(r"^\s+lower footer:\s+(.*)$")


@dataclass
class Frame:
    frame: int
    hot_c: float | None
    cold_c: float | None
    center_c: float | None
    mean_c: float | None
    ffc: str | None
    manual: str | None
    replay: str | None
    stable_ms: float | None
    prev_stable_ms: float | None
    ffc_age_ms: float | None
    w20: int | None
    w25: int | None
    r1_10: int | None
    w2: int | None = None
    w5: int | None = None
    w8: int | None = None
    w9: int | None = None


def parse_temp(text: str, key: str) -> float | None:
    match = re.search(rf"{re.escape(key)}=([0-9.]+)C", text)
    return float(match.group(1)) if match else None


def parse_int(text: str, key: str) -> int | None:
    match = re.search(rf"{re.escape(key)}=(\d+)", text)
    return int(match.group(1)) if match else None


def parse_word(text: str, key: str) -> str | None:
    match = re.search(rf"{re.escape(key)}=(\w+)", text)
    return match.group(1) if match else None


def parse_ms(text: str, key: str) -> float | None:
    match = re.search(rf"{re.escape(key)}=([0-9.]+)ms", text)
    return float(match.group(1)) if match else None


def parse_log(log_path: Path) -> list[Frame]:
    frames: list[Frame] = []
    last_frame: Frame | None = None

    for line in log_path.read_text(errors="replace").splitlines():
        live_match = LIVE_RE.match(line)
        if live_match:
            payload = live_match.group(2)
            last_frame = Frame(
                frame=int(live_match.group(1)),
                hot_c=parse_temp(payload, "hot"),
                cold_c=parse_temp(payload, "cold"),
                center_c=parse_temp(payload, "center"),
                mean_c=parse_temp(payload, "mean"),
                ffc=parse_word(payload, "ffc"),
                manual=parse_word(payload, "manual"),
                replay=parse_word(payload, "replay"),
                stable_ms=parse_ms(payload, "stable"),
                prev_stable_ms=parse_ms(payload, "prev_stable"),
                ffc_age_ms=parse_ms(payload, "ffc_age"),
                w20=parse_int(payload, "w20"),
                w25=parse_int(payload, "w25"),
                r1_10=parse_int(payload, "r1_10"),
            )
            frames.append(last_frame)
            continue

        lower_match = LOWER_RE.match(line)
        if lower_match and last_frame is not None:
            payload = lower_match.group(1)
            last_frame.w2 = parse_int(payload, "w2")
            last_frame.w5 = parse_int(payload, "w5")
            last_frame.w8 = parse_int(payload, "w8")
            last_frame.w9 = parse_int(payload, "w9")

    return frames


def median_or_blank(values: list[float | int | None]) -> str:
    clean = [value for value in values if value is not None]
    if not clean:
        return ""
    return f"{statistics.median(clean):.3f}"


def max_or_blank(values: list[float | int | None]) -> str:
    clean = [value for value in values if value is not None]
    if not clean:
        return ""
    return f"{max(clean):.3f}"


def min_or_blank(values: list[float | int | None]) -> str:
    clean = [value for value in values if value is not None]
    if not clean:
        return ""
    return f"{min(clean):.3f}"


def write_events_csv(frames: list[Frame], out_path: Path, total_seconds: float) -> None:
    fps = (frames[-1].frame - frames[0].frame) / total_seconds

    pending_trigger: Frame | None = None
    last_ffc: str | None = None
    event_rows: list[dict[str, str]] = []
    event_index = 0

    for frame in frames:
        if frame.ffc != last_ffc:
            if frame.ffc == "triggered":
                pending_trigger = frame
            elif frame.ffc == "recovered":
                event_index += 1
                trigger = pending_trigger
                event_rows.append(
                    {
                        "event_index": str(event_index),
                        "trigger_frame": str(trigger.frame if trigger else ""),
                        "recovered_frame": str(frame.frame),
                        "trigger_elapsed_s": (
                            f"{((trigger.frame - frames[0].frame) / fps):.3f}"
                            if trigger
                            else ""
                        ),
                        "recovered_elapsed_s": f"{((frame.frame - frames[0].frame) / fps):.3f}",
                        "stable_before_trigger_s": (
                            f"{(trigger.prev_stable_ms or 0.0) / 1000.0:.3f}" if trigger else ""
                        ),
                        "recovery_s": f"{(frame.ffc_age_ms or 0.0) / 1000.0:.3f}",
                        "manual": trigger.manual if trigger and trigger.manual else "",
                        "w20": str(trigger.w20 if trigger and trigger.w20 is not None else ""),
                        "w25": str(trigger.w25 if trigger and trigger.w25 is not None else ""),
                        "r1_10": str(trigger.r1_10 if trigger and trigger.r1_10 is not None else ""),
                    }
                )
                pending_trigger = None

        last_ffc = frame.ffc

    with out_path.open("w", newline="") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=[
                "event_index",
                "trigger_frame",
                "recovered_frame",
                "trigger_elapsed_s",
                "recovered_elapsed_s",
                "stable_before_trigger_s",
                "recovery_s",
                "manual",
                "w20",
                "w25",
                "r1_10",
            ],
        )
        writer.writeheader()
        writer.writerows(event_rows)


def write_minute_summary_csv(frames: list[Frame], out_path: Path, total_seconds: float) -> None:
    fps = (frames[-1].frame - frames[0].frame) / total_seconds

    trigger_rows: list[Frame] = []
    recovery_rows: list[Frame] = []
    last_ffc: str | None = None
    for frame in frames:
        if frame.ffc != last_ffc:
            if frame.ffc == "triggered":
                trigger_rows.append(frame)
            elif frame.ffc == "recovered":
                recovery_rows.append(frame)
        last_ffc = frame.ffc

    max_minute = int(total_seconds // 60)
    rows: list[dict[str, str]] = []
    for minute in range(max_minute):
        lo = minute * 60 * fps + frames[0].frame
        hi = (minute + 1) * 60 * fps + frames[0].frame

        stable_frames = [
            frame
            for frame in frames
            if lo <= frame.frame < hi and frame.ffc == "none" and frame.mean_c is not None
        ]
        minute_triggers = [frame for frame in trigger_rows if lo <= frame.frame < hi]
        minute_recoveries = [frame for frame in recovery_rows if lo <= frame.frame < hi]

        rows.append(
            {
                "minute_start": str(minute),
                "stable_frame_count": str(len(stable_frames)),
                "median_mean_c": median_or_blank([frame.mean_c for frame in stable_frames]),
                "median_center_c": median_or_blank([frame.center_c for frame in stable_frames]),
                "median_hot_c": median_or_blank([frame.hot_c for frame in stable_frames]),
                "median_cold_c": median_or_blank([frame.cold_c for frame in stable_frames]),
                "median_w20": median_or_blank([frame.w20 for frame in stable_frames]),
                "median_w25": median_or_blank([frame.w25 for frame in stable_frames]),
                "median_r1_10": median_or_blank([frame.r1_10 for frame in stable_frames]),
                "median_lower_w2": median_or_blank([frame.w2 for frame in stable_frames]),
                "median_lower_w5": median_or_blank([frame.w5 for frame in stable_frames]),
                "median_lower_w8": median_or_blank([frame.w8 for frame in stable_frames]),
                "median_lower_w9": median_or_blank([frame.w9 for frame in stable_frames]),
                "stable_run_max_s": max_or_blank([frame.stable_ms / 1000.0 for frame in stable_frames]),
                "trigger_count": str(len(minute_triggers)),
                "trigger_stable_min_s": min_or_blank(
                    [frame.prev_stable_ms / 1000.0 for frame in minute_triggers if frame.prev_stable_ms is not None]
                ),
                "trigger_stable_med_s": median_or_blank(
                    [frame.prev_stable_ms / 1000.0 for frame in minute_triggers if frame.prev_stable_ms is not None]
                ),
                "trigger_stable_max_s": max_or_blank(
                    [frame.prev_stable_ms / 1000.0 for frame in minute_triggers if frame.prev_stable_ms is not None]
                ),
                "recovery_count": str(len(minute_recoveries)),
                "recovery_min_s": min_or_blank(
                    [frame.ffc_age_ms / 1000.0 for frame in minute_recoveries if frame.ffc_age_ms is not None]
                ),
                "recovery_med_s": median_or_blank(
                    [frame.ffc_age_ms / 1000.0 for frame in minute_recoveries if frame.ffc_age_ms is not None]
                ),
                "recovery_max_s": max_or_blank(
                    [frame.ffc_age_ms / 1000.0 for frame in minute_recoveries if frame.ffc_age_ms is not None]
                ),
            }
        )

    with out_path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("log", type=Path)
    parser.add_argument("--events-csv", type=Path, required=True)
    parser.add_argument("--minute-summary-csv", type=Path, required=True)
    parser.add_argument("--duration-seconds", type=float, default=900.0)
    args = parser.parse_args()

    frames = parse_log(args.log)
    if not frames:
        raise SystemExit("no live frames parsed")

    write_events_csv(frames, args.events_csv, args.duration_seconds)
    write_minute_summary_csv(frames, args.minute_summary_csv, args.duration_seconds)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
