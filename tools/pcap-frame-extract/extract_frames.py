#!/usr/bin/env python3
"""Rebuild logical thermal-camera frames from Android USB .pcapng captures."""
"""This quick script was written by GPT5.4 and worked first attempt on existing sniffed pcaps"""
from __future__ import annotations

import argparse
import json
import shutil
import struct
import subprocess
import sys
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Iterator, Optional


ENDPOINT_FILTER = "usb.endpoint_address==0x81 && usb.data_len>0"
FRAME_WIDTH = 256
FRAME_HEIGHT = 192
BYTES_PER_PIXEL = 2
PLANE_BYTES = FRAME_WIDTH * FRAME_HEIGHT * BYTES_PER_PIXEL
MARKER_BYTES = 4
EOF_FLAG = 0x02
FID_FLAG = 0x01


@dataclass
class PacketRecord:
    tshark_frame_number: int
    usb_data_len: int
    header_len: int
    flags: int
    payload: bytes


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Extract logical frames from the thermal camera's Android USB sniff "
            "captures by rebuilding endpoint-0x81 bulk payloads."
        )
    )
    parser.add_argument(
        "--input",
        dest="inputs",
        action="append",
        required=True,
        help="Input .pcapng path. Repeat for multiple captures.",
    )
    parser.add_argument(
        "--out",
        required=True,
        help="Output directory. Multiple inputs create one subdirectory per capture stem.",
    )
    parser.add_argument(
        "--limit",
        type=int,
        default=None,
        help="Optional maximum number of frames to write per input capture.",
    )
    parser.add_argument(
        "--no-previews",
        action="store_true",
        help="Skip preview PGM generation and only write raw binaries plus JSON.",
    )
    return parser.parse_args()


def ensure_tshark() -> str:
    tshark = shutil.which("tshark")
    if not tshark:
        raise SystemExit("tshark not found in PATH; install Wireshark/tshark first.")
    return tshark


def iter_packets(tshark: str, input_path: Path) -> Iterator[PacketRecord]:
    cmd = [
        tshark,
        "-r",
        str(input_path),
        "-Y",
        ENDPOINT_FILTER,
        "-T",
        "fields",
        "-e",
        "frame.number",
        "-e",
        "usb.data_len",
        "-e",
        "usb.capdata",
    ]
    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    assert proc.stdout is not None
    try:
        for raw_line in proc.stdout:
            line = raw_line.rstrip("\n")
            if not line:
                continue
            parts = line.split("\t")
            if len(parts) != 3:
                raise ValueError(f"unexpected tshark field layout: {line!r}")
            frame_no_text, data_len_text, cap_hex = parts
            data = bytes.fromhex(cap_hex)
            usb_data_len = int(data_len_text)
            if len(data) != usb_data_len:
                raise ValueError(
                    f"USB length mismatch in tshark frame {frame_no_text}: "
                    f"field says {usb_data_len}, bytes decode to {len(data)}"
                )
            if len(data) < 2:
                raise ValueError(f"short packet in tshark frame {frame_no_text}")
            header_len = data[0]
            if header_len != 2:
                raise ValueError(
                    f"unexpected packet header length {header_len} "
                    f"in tshark frame {frame_no_text}; expected 2"
                )
            yield PacketRecord(
                tshark_frame_number=int(frame_no_text),
                usb_data_len=usb_data_len,
                header_len=header_len,
                flags=data[1],
                payload=data[header_len:],
            )
    finally:
        if proc.poll() is None:
            proc.terminate()
        stderr = proc.stderr.read() if proc.stderr is not None else ""
        rc = proc.wait()
        if rc not in (0, -15):
            raise RuntimeError(
                f"tshark failed for {input_path} with exit code {rc}:\n{stderr.strip()}"
            )


def iter_frames(packet_iter: Iterable[PacketRecord]) -> Iterator[list[PacketRecord]]:
    current: list[PacketRecord] = []
    for packet in packet_iter:
        current.append(packet)
        if packet.flags & EOF_FLAG:
            yield current
            current = []
    if current:
        yield current


def u32le(buf: bytes, offset: int) -> int:
    return struct.unpack_from("<I", buf, offset)[0]


def split_u32_words(value: int) -> dict[str, int]:
    return {
        "u32": value,
        "hex_le": f"0x{value:08x}",
        "low16": value & 0xFFFF,
        "high16": (value >> 16) & 0xFFFF,
    }


def write_pgm(path: Path, width: int, height: int, pixels: bytes) -> None:
    with path.open("wb") as f:
        f.write(f"P5\n{width} {height}\n255\n".encode("ascii"))
        f.write(pixels)


def make_u16_preview(u16le_plane: bytes) -> tuple[bytes, dict[str, int]]:
    values = memoryview(u16le_plane).cast("H")
    min_v = min(values)
    max_v = max(values)
    if max_v == min_v:
        scaled = bytes(len(values))
    else:
        span = max_v - min_v
        scaled = bytes(((v - min_v) * 255) // span for v in values)
    return scaled, {"min": int(min_v), "max": int(max_v)}


def make_luma_preview(yuyv_like_plane: bytes) -> bytes:
    return yuyv_like_plane[0::2]


def parse_frame_metadata(frame_payload: bytes, packet_group: list[PacketRecord]) -> dict:
    if len(frame_payload) < 8:
        raise ValueError("frame too short to contain the recovered offset field")
    data_offset = u32le(frame_payload, 4)
    if data_offset > len(frame_payload):
        raise ValueError(
            f"frame offset {data_offset} exceeds reconstructed length {len(frame_payload)}"
        )

    header = frame_payload[:data_offset]
    marker = frame_payload[data_offset : data_offset + MARKER_BYTES]
    payload_after_marker = frame_payload[data_offset + MARKER_BYTES :]

    plane0 = payload_after_marker[:PLANE_BYTES]
    plane1 = payload_after_marker[PLANE_BYTES : PLANE_BYTES * 2]
    trailing = payload_after_marker[PLANE_BYTES * 2 :]

    plane_descriptors = []
    if len(header) >= 84:
        plane_descriptors.append(
            {
                "descriptor_offset": 40,
                "counter_word": split_u32_words(u32le(header, 40)),
                "field_0x30": split_u32_words(u32le(header, 48)),
                "field_0x34": split_u32_words(u32le(header, 52)),
                "field_0x38": split_u32_words(u32le(header, 56)),
                "stamp_word": split_u32_words(u32le(header, 60)),
                "width": u32le(header, 64),
                "height": u32le(header, 68),
                "payload_bytes": u32le(header, 72),
                "frame_rate_like": u32le(header, 76),
                "field_0x50": split_u32_words(u32le(header, 80)),
            }
        )
    if len(header) >= 124:
        plane_descriptors.append(
            {
                "descriptor_offset": 84,
                "counter_word": split_u32_words(u32le(header, 84)),
                "width": u32le(header, 88),
                "height": u32le(header, 92),
                "payload_bytes": u32le(header, 96),
                "field_0x64": split_u32_words(u32le(header, 100)),
                "field_0x68": split_u32_words(u32le(header, 104)),
                "field_0x6c": split_u32_words(u32le(header, 108)),
                "field_0x70": split_u32_words(u32le(header, 112)),
                "stamp_word": split_u32_words(u32le(header, 116)),
                "field_0x78": split_u32_words(u32le(header, 120)),
            }
        )

    meta = {
        "reconstructed_length": len(frame_payload),
        "data_offset": data_offset,
        "header_magic_u32": split_u32_words(u32le(frame_payload, 0)),
        "frame_header_length": len(header),
        "marker_hex": marker.hex(),
        "packet_count": len(packet_group),
        "packet_usb_lengths": [pkt.usb_data_len for pkt in packet_group],
        "packet_header_flags_hex": [f"0x{pkt.flags:02x}" for pkt in packet_group],
        "packet_fid_values": [pkt.flags & FID_FLAG for pkt in packet_group],
        "tshark_frame_range": {
            "first": packet_group[0].tshark_frame_number,
            "last": packet_group[-1].tshark_frame_number,
        },
        "planes": [
            {
                "index": 0,
                "inferred_type": "u16le_radiometric_like",
                "bytes": len(plane0),
            },
            {
                "index": 1,
                "inferred_type": "packed_display_like_2bytes_per_pixel",
                "bytes": len(plane1),
            },
        ],
        "trailing_bytes_after_two_planes": len(trailing),
        "header_fields": {
            "raw_prefix_hex_0_64": header[:64].hex(),
            "raw_suffix_hex_32": header[-32:].hex() if header else "",
            "plane_descriptors": plane_descriptors,
            "dynamic_words": {
                "header_u32_0x90": split_u32_words(u32le(header, 144))
                if len(header) >= 148
                else None,
                "header_u32_0x94": split_u32_words(u32le(header, 148))
                if len(header) >= 152
                else None,
                "header_u32_0x9c": split_u32_words(u32le(header, 156))
                if len(header) >= 160
                else None,
                "header_u32_0xa0": split_u32_words(u32le(header, 160))
                if len(header) >= 164
                else None,
                "header_u32_0xa4": split_u32_words(u32le(header, 164))
                if len(header) >= 168
                else None,
                "header_u32_tail_minus_4": split_u32_words(u32le(header, len(header) - 4))
                if len(header) >= 4
                else None,
            },
        },
    }
    return meta


def write_frame_outputs(
    out_dir: Path,
    frame_index: int,
    frame_payload: bytes,
    meta: dict,
    write_previews: bool,
) -> None:
    stem = f"frame_{frame_index:04d}"
    data_offset = meta["data_offset"]
    header = frame_payload[:data_offset]
    marker = frame_payload[data_offset : data_offset + MARKER_BYTES]
    remainder = frame_payload[data_offset + MARKER_BYTES :]
    plane0 = remainder[:PLANE_BYTES]
    plane1 = remainder[PLANE_BYTES : PLANE_BYTES * 2]

    (out_dir / f"{stem}.bin").write_bytes(frame_payload)
    (out_dir / f"{stem}_header.bin").write_bytes(header)
    (out_dir / f"{stem}_marker.bin").write_bytes(marker)
    (out_dir / f"{stem}_plane0.raw").write_bytes(plane0)
    (out_dir / f"{stem}_plane1.raw").write_bytes(plane1)

    if write_previews and len(plane0) == PLANE_BYTES:
        plane0_preview, plane0_stats = make_u16_preview(plane0)
        write_pgm(out_dir / f"{stem}_plane0_preview.pgm", FRAME_WIDTH, FRAME_HEIGHT, plane0_preview)
        meta["planes"][0]["preview"] = {
            "format": "pgm",
            "path": f"{stem}_plane0_preview.pgm",
            "stretch": plane0_stats,
        }

    if write_previews and len(plane1) == PLANE_BYTES:
        plane1_preview = make_luma_preview(plane1)
        write_pgm(out_dir / f"{stem}_plane1_luma_preview.pgm", FRAME_WIDTH, FRAME_HEIGHT, plane1_preview)
        meta["planes"][1]["preview"] = {
            "format": "pgm",
            "path": f"{stem}_plane1_luma_preview.pgm",
            "interpretation": "every other byte treated as luma",
        }

    with (out_dir / f"{stem}.json").open("w", encoding="utf-8") as f:
        json.dump(meta, f, indent=2, sort_keys=True)
        f.write("\n")


def process_capture(
    tshark: str,
    input_path: Path,
    out_dir: Path,
    limit: Optional[int],
    write_previews: bool,
) -> dict:
    out_dir.mkdir(parents=True, exist_ok=True)

    packet_len_hist = Counter()
    packet_flag_hist = Counter()
    frame_len_hist = Counter()
    offset_hist = Counter()
    marker_hist = Counter()
    packet_count_hist = Counter()
    first_fid_hist = Counter()

    sample_frames = []
    skipped_groups = []
    written_frames = 0

    for group_index, packet_group in enumerate(iter_frames(iter_packets(tshark, input_path))):
        if limit is not None and written_frames >= limit:
            break

        if not (packet_group[-1].flags & EOF_FLAG):
            skipped = {
                "group_index": group_index,
                "reason": "capture ended mid-frame without EOF marker",
                "reconstructed_length": sum(len(pkt.payload) for pkt in packet_group),
                "packet_count": len(packet_group),
                "tshark_frame_range": {
                    "first": packet_group[0].tshark_frame_number,
                    "last": packet_group[-1].tshark_frame_number,
                },
            }
            skipped_groups.append(skipped)
            print(
                f"[skip] group={group_index} len={skipped['reconstructed_length']} "
                f"frames={skipped['tshark_frame_range']} reason={skipped['reason']}",
                file=sys.stderr,
            )
            continue

        for pkt in packet_group:
            packet_len_hist[pkt.usb_data_len] += 1
            packet_flag_hist[f"0x{pkt.flags:02x}"] += 1

        frame_payload = b"".join(pkt.payload for pkt in packet_group)
        try:
            meta = parse_frame_metadata(frame_payload, packet_group)
        except ValueError as exc:
            skipped = {
                "group_index": group_index,
                "reason": str(exc),
                "reconstructed_length": len(frame_payload),
                "packet_count": len(packet_group),
                "tshark_frame_range": {
                    "first": packet_group[0].tshark_frame_number,
                    "last": packet_group[-1].tshark_frame_number,
                },
            }
            skipped_groups.append(skipped)
            print(
                f"[skip] group={group_index} len={len(frame_payload)} "
                f"frames={skipped['tshark_frame_range']} reason={exc}",
                file=sys.stderr,
            )
            continue
        meta["source_pcapng"] = str(input_path)
        meta["frame_index"] = written_frames
        meta["source_group_index"] = group_index

        write_frame_outputs(out_dir, written_frames, frame_payload, meta, write_previews)

        frame_len_hist[meta["reconstructed_length"]] += 1
        offset_hist[meta["data_offset"]] += 1
        marker_hist[meta["marker_hex"]] += 1
        packet_count_hist[meta["packet_count"]] += 1
        first_fid_hist[meta["packet_fid_values"][0]] += 1

        if len(sample_frames) < 5:
            sample_frames.append(
                {
                    "frame_index": written_frames,
                    "source_group_index": group_index,
                    "data_offset": meta["data_offset"],
                    "marker_hex": meta["marker_hex"],
                    "packet_count": meta["packet_count"],
                    "tshark_frame_range": meta["tshark_frame_range"],
                }
            )
        written_frames += 1

    summary = {
        "input": str(input_path),
        "frames_written": written_frames,
        "output_dir": str(out_dir),
        "expected_plane_bytes": PLANE_BYTES,
        "frame_width": FRAME_WIDTH,
        "frame_height": FRAME_HEIGHT,
        "packet_endpoint_filter": ENDPOINT_FILTER,
        "packet_length_histogram": dict(sorted(packet_len_hist.items())),
        "packet_flag_histogram": dict(sorted(packet_flag_hist.items())),
        "frame_length_histogram": dict(sorted(frame_len_hist.items())),
        "data_offset_histogram": dict(sorted(offset_hist.items())),
        "marker_histogram": dict(sorted(marker_hist.items())),
        "packet_count_histogram": dict(sorted(packet_count_hist.items())),
        "first_fid_histogram": dict(sorted(first_fid_hist.items())),
        "skipped_group_count": len(skipped_groups),
        "skipped_groups": skipped_groups[:20],
        "sample_frames": sample_frames,
    }

    with (out_dir / "summary.json").open("w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2, sort_keys=True)
        f.write("\n")
    return summary


def resolve_capture_out_dir(base_out: Path, input_path: Path, multi_input: bool) -> Path:
    return base_out / input_path.stem if multi_input else base_out


def main() -> int:
    args = parse_args()
    tshark = ensure_tshark()

    base_out = Path(args.out)
    inputs = [Path(p) for p in args.inputs]
    multi_input = len(inputs) > 1

    all_summaries = []
    for input_path in inputs:
        if not input_path.is_file():
            raise SystemExit(f"input capture not found: {input_path}")
        capture_out = resolve_capture_out_dir(base_out, input_path, multi_input)
        print(f"[extract] {input_path} -> {capture_out}", file=sys.stderr)
        summary = process_capture(
            tshark=tshark,
            input_path=input_path,
            out_dir=capture_out,
            limit=args.limit,
            write_previews=not args.no_previews,
        )
        print(
            f"[done] frames={summary['frames_written']} "
            f"offsets={summary['data_offset_histogram']} "
            f"lengths={summary['frame_length_histogram']}",
            file=sys.stderr,
        )
        all_summaries.append(summary)

    if multi_input:
        base_out.mkdir(parents=True, exist_ok=True)
        with (base_out / "captures.json").open("w", encoding="utf-8") as f:
            json.dump(all_summaries, f, indent=2, sort_keys=True)
            f.write("\n")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
