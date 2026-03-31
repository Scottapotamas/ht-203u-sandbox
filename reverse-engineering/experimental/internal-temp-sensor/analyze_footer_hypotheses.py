#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import re
from dataclasses import dataclass
from pathlib import Path


FULL_W = 256
FULL_H = 392
IMAGE_H = 192
LINE_BYTES = FULL_W * 2
FULL_FRAME_BYTES = FULL_W * FULL_H * 2
LOWER_FOOTER_START = 196 + IMAGE_H

# March 30, 2026 ambient lower-footer fits from lower-footer-fit-summary.csv.
LOWER_FITS = {
    "min_c": (0.012144011236077576, -29.264595328649747, "w5"),
    "max_c": (0.01819579796284898, -59.33271889714222, "w2"),
    "mean_c": (0.017148345378002642, -53.19828742364504, "w8"),
    "center_c": (0.017119332829198475, -52.03280037562912, "w9"),
}


@dataclass
class DatasetFrame:
    name: str
    min_c: float
    max_c: float
    mean_c: float
    center_c: float
    w20: int
    w25: int
    r1: int
    w2: int
    w5: int
    w8: int
    w9: int


@dataclass
class WarmupRow:
    frame: int
    ffc: str | None
    min_c: float | None
    max_c: float | None
    mean_c: float | None
    center_c: float | None
    w20: int | None
    w25: int | None
    r1: int | None
    w2: int | None = None
    w5: int | None = None
    w8: int | None = None
    w9: int | None = None


def le16(buf: bytes, offset: int) -> int:
    return buf[offset] | (buf[offset + 1] << 8)


def le32(buf: bytes, offset: int) -> int:
    return (
        buf[offset]
        | (buf[offset + 1] << 8)
        | (buf[offset + 2] << 16)
        | (buf[offset + 3] << 24)
    )


def code_to_c(code: float) -> float:
    return (code - 4096.0) / 29.5


def lower_ref_c(field_name: str, value: int) -> float:
    slope, bias, _ = LOWER_FITS[field_name]
    return slope * value + bias


def parse_dataset_frame(path: Path) -> DatasetFrame:
    raw = path.read_bytes()
    if len(raw) != FULL_FRAME_BYTES:
        raise ValueError(f"{path}: expected {FULL_FRAME_BYTES} bytes, got {len(raw)}")

    pixels = [le16(raw, i * 2) for i in range(FULL_W * IMAGE_H)]
    meta = raw[IMAGE_H * LINE_BYTES : IMAGE_H * LINE_BYTES + 4 * LINE_BYTES]
    lower0 = raw[LOWER_FOOTER_START * LINE_BYTES : (LOWER_FOOTER_START + 1) * LINE_BYTES]

    return DatasetFrame(
        name=path.name.replace("_full_yuyv.raw", ""),
        min_c=code_to_c(min(pixels)),
        max_c=code_to_c(max(pixels)),
        mean_c=code_to_c(sum(pixels) / len(pixels)),
        center_c=code_to_c(pixels[96 * FULL_W + 128]),
        w20=le16(meta, 0x28),
        w25=le16(meta, 0x32),
        r1=le32(meta, 0x210),
        w2=le16(lower0, 0x04),
        w5=le16(lower0, 0x0A),
        w8=le16(lower0, 0x10),
        w9=le16(lower0, 0x12),
    )


def parse_temp_c(line: str, key: str) -> float | None:
    match = re.search(rf"{re.escape(key)}=([0-9.]+)C", line)
    return float(match.group(1)) if match else None


def parse_int(line: str, key: str) -> int | None:
    match = re.search(rf"{re.escape(key)}=(\d+)", line)
    return int(match.group(1)) if match else None


def parse_word(line: str, key: str) -> str | None:
    match = re.search(rf"{re.escape(key)}=(\w+)", line)
    return match.group(1) if match else None


def parse_warmup_log(path: Path) -> list[WarmupRow]:
    rows: list[WarmupRow] = []
    current: WarmupRow | None = None

    for line in path.read_text(errors="replace").splitlines():
        if line.startswith("live_"):
            frame = int(re.match(r"^live_(\d+)", line).group(1))  # type: ignore[union-attr]
            current = WarmupRow(
                frame=frame,
                ffc=parse_word(line, "ffc"),
                min_c=parse_temp_c(line, "cold"),
                max_c=parse_temp_c(line, "hot"),
                mean_c=parse_temp_c(line, "mean"),
                center_c=parse_temp_c(line, "center"),
                w20=parse_int(line, "w20"),
                w25=parse_int(line, "w25"),
                r1=parse_int(line, "r1_10"),
            )
            rows.append(current)
            continue

        if line.startswith("    lower footer:") and current is not None:
            current.w2 = parse_int(line, "w2")
            current.w5 = parse_int(line, "w5")
            current.w8 = parse_int(line, "w8")
            current.w9 = parse_int(line, "w9")

    return rows


def fit_1d(xs: list[float], ys: list[float]) -> tuple[float, float]:
    mean_x = sum(xs) / len(xs)
    mean_y = sum(ys) / len(ys)
    var_x = sum((x - mean_x) ** 2 for x in xs)
    if var_x == 0.0:
        return mean_y, 0.0
    cov_xy = sum((x - mean_x) * (y - mean_y) for x, y in zip(xs, ys))
    slope = cov_xy / var_x
    intercept = mean_y - slope * mean_x
    return intercept, slope


def solve_3x3(mat: list[list[float]], vec: list[float]) -> list[float]:
    aug = [mat[0][:] + [vec[0]], mat[1][:] + [vec[1]], mat[2][:] + [vec[2]]]

    for i in range(3):
        pivot = max(range(i, 3), key=lambda row: abs(aug[row][i]))
        aug[i], aug[pivot] = aug[pivot], aug[i]
        div = aug[i][i]
        for col in range(i, 4):
            aug[i][col] /= div
        for row in range(3):
            if row == i:
                continue
            factor = aug[row][i]
            for col in range(i, 4):
                aug[row][col] -= factor * aug[i][col]

    return [aug[row][3] for row in range(3)]


def fit_2d(xs1: list[float], xs2: list[float], ys: list[float]) -> tuple[float, float, float]:
    mat = [
        [len(ys), sum(xs1), sum(xs2)],
        [sum(xs1), sum(x * x for x in xs1), sum(x * y for x, y in zip(xs1, xs2))],
        [sum(xs2), sum(x * y for x, y in zip(xs1, xs2)), sum(x * x for x in xs2)],
    ]
    vec = [
        sum(ys),
        sum(x * y for x, y in zip(xs1, ys)),
        sum(x * y for x, y in zip(xs2, ys)),
    ]
    return tuple(solve_3x3(mat, vec))  # type: ignore[return-value]


def rmse_mae(pred: list[float], target: list[float]) -> tuple[float, float]:
    errs = [p - t for p, t in zip(pred, target)]
    rmse = (sum(err * err for err in errs) / len(errs)) ** 0.5
    mae = sum(abs(err) for err in errs) / len(errs)
    return rmse, mae


def warmup_reference(row: WarmupRow, target: str) -> float:
    _, _, field = LOWER_FITS[target]
    value = getattr(row, field)
    assert value is not None
    return lower_ref_c(target, value)


def write_warmup_model_summary(rows: list[WarmupRow], out_path: Path) -> dict[str, dict[str, tuple[float, ...]]]:
    usable = [
        row
        for row in rows
        if row.ffc == "none"
        and row.mean_c is not None
        and row.center_c is not None
        and row.min_c is not None
        and row.max_c is not None
        and row.w20 is not None
        and row.w25 is not None
        and row.r1 is not None
        and row.w2 is not None
        and row.w5 is not None
        and row.w8 is not None
        and row.w9 is not None
    ]

    fitted: dict[str, dict[str, tuple[float, ...]]] = {}
    stat_pairs = [
        ("min_c", "min_c"),
        ("max_c", "max_c"),
        ("mean_c", "mean_c"),
        ("center_c", "center_c"),
    ]

    with out_path.open("w", newline="") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=[
                "target",
                "model",
                "rmse_c",
                "mae_c",
                "intercept_c",
                "coef_w20_c",
                "coef_w25_c",
                "coef_r1_c",
                "n",
            ],
        )
        writer.writeheader()

        for raw_key, target_key in stat_pairs:
            target = [warmup_reference(row, target_key) for row in usable]
            raw = [getattr(row, raw_key) for row in usable]
            w20_c = [row.w20 / 100.0 for row in usable]  # type: ignore[operator]
            w25_c = [row.w25 / 100.0 for row in usable]  # type: ignore[operator]
            r1_c = [row.r1 / 1000.0 for row in usable]  # type: ignore[operator]

            fitted[target_key] = {}

            raw_rmse, raw_mae = rmse_mae(raw, target)  # type: ignore[arg-type]
            writer.writerow(
                {
                    "target": target_key,
                    "model": "raw",
                    "rmse_c": f"{raw_rmse:.6f}",
                    "mae_c": f"{raw_mae:.6f}",
                    "intercept_c": "0.000000",
                    "coef_w20_c": "0.000000",
                    "coef_w25_c": "0.000000",
                    "coef_r1_c": "0.000000",
                    "n": str(len(usable)),
                }
            )
            fitted[target_key]["raw"] = (0.0, 0.0, 0.0, 0.0)

            for model_name, feature in (("r1_only", r1_c), ("w20_only", w20_c), ("w25_only", w25_c)):
                intercept, slope = fit_1d(feature, [t - r for t, r in zip(target, raw)])  # type: ignore[arg-type]
                pred = [r + intercept + slope * x for r, x in zip(raw, feature)]  # type: ignore[arg-type]
                rmse, mae = rmse_mae(pred, target)
                writer.writerow(
                    {
                        "target": target_key,
                        "model": model_name,
                        "rmse_c": f"{rmse:.6f}",
                        "mae_c": f"{mae:.6f}",
                        "intercept_c": f"{intercept:.6f}",
                        "coef_w20_c": f"{slope:.6f}" if model_name == "w20_only" else "0.000000",
                        "coef_w25_c": f"{slope:.6f}" if model_name == "w25_only" else "0.000000",
                        "coef_r1_c": f"{slope:.6f}" if model_name == "r1_only" else "0.000000",
                        "n": str(len(usable)),
                    }
                )
                if model_name == "r1_only":
                    fitted[target_key][model_name] = (intercept, 0.0, 0.0, slope)
                elif model_name == "w20_only":
                    fitted[target_key][model_name] = (intercept, slope, 0.0, 0.0)
                else:
                    fitted[target_key][model_name] = (intercept, 0.0, slope, 0.0)

            intercept, coef_w20, coef_r1 = fit_2d(w20_c, r1_c, [t - r for t, r in zip(target, raw)])  # type: ignore[arg-type]
            pred = [r + intercept + coef_w20 * x1 + coef_r1 * x2 for r, x1, x2 in zip(raw, w20_c, r1_c)]  # type: ignore[arg-type]
            rmse, mae = rmse_mae(pred, target)
            writer.writerow(
                {
                    "target": target_key,
                    "model": "w20_plus_r1",
                    "rmse_c": f"{rmse:.6f}",
                    "mae_c": f"{mae:.6f}",
                    "intercept_c": f"{intercept:.6f}",
                    "coef_w20_c": f"{coef_w20:.6f}",
                    "coef_w25_c": "0.000000",
                    "coef_r1_c": f"{coef_r1:.6f}",
                    "n": str(len(usable)),
                }
            )
            fitted[target_key]["w20_plus_r1"] = (intercept, coef_w20, 0.0, coef_r1)

    return fitted


def write_dataset_hypothesis_table(
    frames: list[DatasetFrame],
    coefficients: dict[str, dict[str, tuple[float, ...]]],
    out_path: Path,
) -> None:
    with out_path.open("w", newline="") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=[
                "capture",
                "w20",
                "w25",
                "r1_c",
                "raw_mean_c",
                "ref_mean_c",
                "w20_mean_c",
                "w20_plus_r1_mean_c",
                "raw_center_c",
                "ref_center_c",
                "w20_center_c",
                "w20_plus_r1_center_c",
            ],
        )
        writer.writeheader()

        mean_w20 = coefficients["mean_c"]["w20_only"]
        mean_w20_r1 = coefficients["mean_c"]["w20_plus_r1"]
        center_w20 = coefficients["center_c"]["w20_only"]
        center_w20_r1 = coefficients["center_c"]["w20_plus_r1"]

        for frame in frames:
            w20_c = frame.w20 / 100.0
            r1_c = frame.r1 / 1000.0
            ref_mean_c = lower_ref_c("mean_c", frame.w8)
            ref_center_c = lower_ref_c("center_c", frame.w9)
            writer.writerow(
                {
                    "capture": frame.name,
                    "w20": str(frame.w20),
                    "w25": str(frame.w25),
                    "r1_c": f"{r1_c:.3f}",
                    "raw_mean_c": f"{frame.mean_c:.6f}",
                    "ref_mean_c": f"{ref_mean_c:.6f}",
                    "w20_mean_c": f"{frame.mean_c + mean_w20[0] + mean_w20[1] * w20_c:.6f}",
                    "w20_plus_r1_mean_c": (
                        f"{frame.mean_c + mean_w20_r1[0] + mean_w20_r1[1] * w20_c + mean_w20_r1[3] * r1_c:.6f}"
                    ),
                    "raw_center_c": f"{frame.center_c:.6f}",
                    "ref_center_c": f"{ref_center_c:.6f}",
                    "w20_center_c": f"{frame.center_c + center_w20[0] + center_w20[1] * w20_c:.6f}",
                    "w20_plus_r1_center_c": (
                        f"{frame.center_c + center_w20_r1[0] + center_w20_r1[1] * w20_c + center_w20_r1[3] * r1_c:.6f}"
                    ),
                }
            )


def write_w20_plateaus(rows: list[WarmupRow], out_path: Path) -> tuple[int, int]:
    usable = [
        row
        for row in rows
        if row.ffc == "none" and row.w20 is not None and row.w25 is not None and row.r1 is not None
    ]
    plateaus: list[dict[str, int]] = []
    prev: tuple[int, int] | None = None
    for row in usable:
        state = (row.w20, row.w25)  # type: ignore[arg-type]
        if state != prev:
            plateaus.append(
                {
                    "w20": state[0],
                    "w25": state[1],
                    "first_r1": row.r1,  # type: ignore[arg-type]
                    "last_r1": row.r1,  # type: ignore[arg-type]
                    "frames": 1,
                }
            )
        else:
            plateaus[-1]["last_r1"] = row.r1  # type: ignore[index]
            plateaus[-1]["frames"] += 1
        prev = state

    lag_matches = 0
    with out_path.open("w", newline="") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=[
                "plateau_index",
                "w20",
                "w25",
                "first_r1_u32_10",
                "last_r1_u32_10",
                "frames",
                "w25_matches_previous_w20",
            ],
        )
        writer.writeheader()
        for idx, state in enumerate(plateaus):
            match = idx > 0 and state["w25"] == plateaus[idx - 1]["w20"]
            if match:
                lag_matches += 1
            writer.writerow(
                {
                    "plateau_index": str(idx + 1),
                    "w20": str(state["w20"]),
                    "w25": str(state["w25"]),
                    "first_r1_u32_10": str(state["first_r1"]),
                    "last_r1_u32_10": str(state["last_r1"]),
                    "frames": str(state["frames"]),
                    "w25_matches_previous_w20": "yes" if match else "no",
                }
            )

    return len(plateaus), lag_matches


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dataset", type=Path, required=True)
    parser.add_argument("--warmup-log", type=Path, required=True)
    parser.add_argument("--outdir", type=Path, required=True)
    args = parser.parse_args()

    args.outdir.mkdir(parents=True, exist_ok=True)

    dataset_frames = [parse_dataset_frame(path) for path in sorted(args.dataset.glob("*_full_yuyv.raw"))]
    warmup_rows = parse_warmup_log(args.warmup_log)

    coefficients = write_warmup_model_summary(
        warmup_rows, args.outdir / "warmup-hypothesis-summary.csv"
    )
    write_dataset_hypothesis_table(
        dataset_frames, coefficients, args.outdir / "dataset-hypothesis-table.csv"
    )
    plateau_count, lag_matches = write_w20_plateaus(
        warmup_rows, args.outdir / "warmup-w20-plateaus.csv"
    )

    with (args.outdir / "analysis-summary.txt").open("w") as handle:
        handle.write(f"dataset_frames={len(dataset_frames)}\n")
        handle.write(f"warmup_rows={len(warmup_rows)}\n")
        handle.write(f"w20_plateaus={plateau_count}\n")
        handle.write(f"w25_prev_w20_matches={lag_matches}\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
