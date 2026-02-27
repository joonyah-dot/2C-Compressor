#!/usr/bin/env python3
import argparse
import json
import math
import os
import struct
import subprocess
import sys
import wave
from pathlib import Path


def db_to_gain(db_value: float) -> float:
    return 10.0 ** (db_value / 20.0)


def gain_to_db(gain: float, floor_db: float = -300.0) -> float:
    if gain <= 0.0:
        return floor_db
    return 20.0 * math.log10(gain)


def parse_dump_params(text: str):
    params = {}
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        parts = line.split("\t")
        if len(parts) < 3:
            continue
        index = int(parts[0].strip())
        name = parts[1].strip()
        default_norm = float(parts[2].strip())
        params[name] = {"index": index, "default_norm": default_norm}
    return params


def build_set_params(params, overrides):
    values = {name: data["default_norm"] for name, data in params.items()}
    values.update(overrides)

    pairs = []
    for name, value in values.items():
        if name not in params:
            continue
        if value < 0.0 or value > 1.0:
            raise ValueError(f"Normalized value out of range for '{name}': {value}")
        index = params[name]["index"]
        pairs.append((index, value))

    pairs.sort(key=lambda item: item[0])
    return ",".join(f"{idx}={val:.6f}" for idx, val in pairs)


def run_command(args):
    completed = subprocess.run(args, capture_output=True, text=True)
    if completed.returncode != 0:
        sys.stderr.write(completed.stdout)
        sys.stderr.write(completed.stderr)
        raise RuntimeError(f"Command failed with exit code {completed.returncode}: {' '.join(args)}")
    return completed.stdout


def write_stepped_sine(path: Path, sample_rate: int, channels: int):
    levels_db = list(range(-48, -5, 3))
    tone_samples = int(sample_rate * 0.25)
    silence_samples = int(sample_rate * 0.05)
    settle_samples = int(sample_rate * 0.12)
    measure_samples = int(sample_rate * 0.10)
    frequency = 1000.0

    signal = []
    measure_windows = []
    phase = 0.0
    phase_inc = (2.0 * math.pi * frequency) / float(sample_rate)

    for level_db in levels_db:
        start = len(signal)
        rms = db_to_gain(level_db)
        peak = min(0.999, rms * math.sqrt(2.0))

        for _ in range(tone_samples):
            sample = math.sin(phase) * peak
            phase += phase_inc
            if phase >= 2.0 * math.pi:
                phase -= 2.0 * math.pi
            signal.append(sample)

        measure_start = start + settle_samples
        measure_windows.append((measure_start, measure_samples, level_db))

        for _ in range(silence_samples):
            signal.append(0.0)

    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as wf:
        wf.setnchannels(channels)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)

        frames = bytearray()
        for sample in signal:
            s = max(-0.999969, min(0.999969, sample))
            int16_val = int(round(s * 32767.0))
            packed = struct.pack("<h", int16_val)
            for _ in range(channels):
                frames.extend(packed)
        wf.writeframes(frames)

    return levels_db, measure_windows


def decode_pcm(data: bytes, sample_width: int):
    if sample_width == 1:
        return [(b - 128) / 128.0 for b in data]
    if sample_width == 2:
        count = len(data) // 2
        ints = struct.unpack("<" + "h" * count, data)
        return [v / 32768.0 for v in ints]
    if sample_width == 3:
        out = []
        for i in range(0, len(data), 3):
            b0 = data[i]
            b1 = data[i + 1]
            b2 = data[i + 2]
            raw = b0 | (b1 << 8) | (b2 << 16)
            if raw & 0x800000:
                raw -= 0x1000000
            out.append(raw / 8388608.0)
        return out
    if sample_width == 4:
        count = len(data) // 4
        ints = struct.unpack("<" + "i" * count, data)
        return [v / 2147483648.0 for v in ints]
    raise ValueError(f"Unsupported WAV sample width: {sample_width}")


def read_wav_float(path: Path):
    with wave.open(str(path), "rb") as wf:
        channels = wf.getnchannels()
        sample_width = wf.getsampwidth()
        sample_rate = wf.getframerate()
        frame_count = wf.getnframes()
        raw = wf.readframes(frame_count)

    samples = decode_pcm(raw, sample_width)
    if channels <= 0:
        raise ValueError("Invalid channel count in WAV")

    frames = len(samples) // channels
    data = [[0.0] * frames for _ in range(channels)]
    pos = 0
    for frame in range(frames):
        for ch in range(channels):
            data[ch][frame] = samples[pos]
            pos += 1
    return sample_rate, data


def rms_db_window(data, start, length):
    channels = len(data)
    if channels == 0:
        return -300.0
    total = 0.0
    count = 0
    for ch in range(channels):
        channel = data[ch]
        end = min(len(channel), start + length)
        for i in range(start, end):
            s = channel[i]
            total += s * s
            count += 1
    if count == 0:
        return -300.0
    rms = math.sqrt(total / float(count))
    return gain_to_db(rms, -300.0)


def gain_computer_output_db(input_db: float, threshold_db: float, ratio: float, knee_db: float):
    if knee_db <= 0.0:
        if input_db > threshold_db:
            return threshold_db + ((input_db - threshold_db) / ratio)
        return input_db

    lower = threshold_db - 0.5 * knee_db
    upper = threshold_db + 0.5 * knee_db

    if input_db < lower:
        return input_db
    if input_db > upper:
        return threshold_db + ((input_db - threshold_db) / ratio)

    x = input_db - lower
    slope_delta = (1.0 / ratio) - 1.0
    return input_db + slope_delta * ((x * x) / (2.0 * knee_db))


def main():
    parser = argparse.ArgumentParser(description="Static transfer-curve calibration test using vst3_harness.")
    parser.add_argument("--harness", required=True, help="Path to vst3_harness executable.")
    parser.add_argument("--plugin", required=True, help="Path to built .vst3 plugin folder.")
    parser.add_argument("--outdir", required=True, help="Output directory for generated files.")
    parser.add_argument("--sr", type=int, default=48000)
    parser.add_argument("--bs", type=int, default=512)
    parser.add_argument("--ch", type=int, default=2)
    parser.add_argument("--max-error-db", type=float, default=0.75)
    args = parser.parse_args()

    outdir = Path(args.outdir)
    if outdir.exists():
        for root, dirs, files in os.walk(outdir, topdown=False):
            for name in files:
                Path(root, name).unlink()
            for name in dirs:
                Path(root, name).rmdir()
    outdir.mkdir(parents=True, exist_ok=True)

    harness = str(Path(args.harness))
    plugin = str(Path(args.plugin))
    input_wav = outdir / "transfer_input.wav"

    levels_db, windows = write_stepped_sine(input_wav, args.sr, args.ch)

    dump_output = run_command([harness, "dump-params", "--plugin", plugin])
    param_info = parse_dump_params(dump_output)
    if not param_info:
        raise RuntimeError("Failed to parse parameters from dump-params output.")

    if "Knee" not in param_info:
        raise RuntimeError("Expected parameter 'Knee' not found in plugin.")

    set_params = build_set_params(param_info, {
        "Threshold": 0.700000,  # -18 dB on -60..0 range
        "Ratio": 0.500000,      # exactly 4:1 at midpoint
        "Knee": param_info["Knee"]["default_norm"],  # default = 6 dB
        "Character": 0.0,       # Clean
        "Timing": 0.0,          # Manual
        "Attack": 0.0,          # Fastest attack
        "Release": 1.0,         # Slowest release
        "Drive": 0.0,
        "Sat Mix": 0.0,
        "Oversampling": 0.0,
        "Mix": 1.0,
        "Input": 0.5,
        "Makeup": 0.333333,
        "Output": 0.5,
        "Bypass": 0.0,
    })

    run_command([
        harness, "render",
        "--plugin", plugin,
        "--in", str(input_wav),
        "--outdir", str(outdir),
        "--sr", str(args.sr),
        "--bs", str(args.bs),
        "--ch", str(args.ch),
        "--warmup", "10",
        "--set-params", set_params,
    ])

    wet_wav = outdir / "wet.wav"
    if not wet_wav.exists():
        raise RuntimeError(f"Expected rendered file not found: {wet_wav}")

    wet_sr, wet_data = read_wav_float(wet_wav)
    if wet_sr != args.sr:
        raise RuntimeError(f"Unexpected sample rate in wet WAV. Expected {args.sr}, got {wet_sr}.")

    threshold_db = -18.0
    ratio = 4.0
    knee_db = 6.0

    rows = []
    max_abs_error = 0.0

    for level_db, (start, length, _) in zip(levels_db, windows):
        measured_db = rms_db_window(wet_data, start, length)
        expected_db = gain_computer_output_db(level_db, threshold_db, ratio, knee_db)
        error_db = measured_db - expected_db
        abs_error = abs(error_db)
        max_abs_error = max(max_abs_error, abs_error)
        rows.append({
            "input_db": level_db,
            "expected_db": expected_db,
            "measured_db": measured_db,
            "error_db": error_db,
        })

    for row in rows:
        print(
            f"step in={row['input_db']:>6.1f} dBFS "
            f"expected={row['expected_db']:>7.2f} dBFS "
            f"measured={row['measured_db']:>7.2f} dBFS "
            f"error={row['error_db']:>+6.2f} dB"
        )

    print(f"MAX_ERROR_DB={max_abs_error:.4f}")

    metrics = {
        "max_error_db": max_abs_error,
        "threshold_db": threshold_db,
        "ratio": ratio,
        "knee_db": knee_db,
        "passed": max_abs_error < args.max_error_db,
        "rows": rows,
    }
    (outdir / "transfer_metrics.json").write_text(json.dumps(metrics, indent=2), encoding="utf-8")

    return 0 if max_abs_error < args.max_error_db else 1


if __name__ == "__main__":
    sys.exit(main())
