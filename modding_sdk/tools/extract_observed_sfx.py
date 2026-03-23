from __future__ import annotations

import csv
import json
import struct
import wave
from collections import OrderedDict
from pathlib import Path


GAME_ROOT = Path(__file__).resolve().parents[2]
CONFIG_DIR = GAME_ROOT / "audio" / "CONFIG"
SFX_DIR = GAME_ROOT / "audio" / "sfx"
WORKSPACE_DIR = GAME_ROOT / "voice_workspace"
LOG_PATH = WORKSPACE_DIR / "logs" / "ballas_police_observed.tsv"
OUTPUT_DIR = WORKSPACE_DIR / "extracted"
MANIFEST_PATH = OUTPUT_DIR / "manifest.json"

PAK_NAME_SIZE = 52
BANK_META_SIZE = 12
BANK_HEADER_SIZE = 4804
SOUND_META_SIZE = 12


def sanitize_name(value: str) -> str:
    return "".join(ch if ch.isalnum() or ch in "._-" else "_" for ch in value).strip("_") or "unknown"


def parse_pakfiles(path: Path) -> list[str]:
    data = path.read_bytes()
    names = []
    for i in range(0, len(data), PAK_NAME_SIZE):
        chunk = data[i : i + PAK_NAME_SIZE]
        name = chunk.split(b"\x00", 1)[0].replace(b"\xCD", b"").decode("ascii", errors="ignore").strip()
        if name:
            names.append(name)
    return names


def parse_bank_lookup(path: Path) -> list[dict]:
    data = path.read_bytes()
    entries = []
    for i in range(0, len(data), BANK_META_SIZE):
        chunk = data[i : i + BANK_META_SIZE]
        if len(chunk) < BANK_META_SIZE:
            continue
        package_index = chunk[0]
        bank_header_offset = struct.unpack_from("<I", chunk, 4)[0]
        bank_size = struct.unpack_from("<I", chunk, 8)[0]
        entries.append(
            {
                "bank_id": len(entries),
                "package_index": package_index,
                "bank_header_offset": bank_header_offset,
                "bank_size": bank_size,
            }
        )
    return entries


def parse_observed_log(path: Path) -> list[dict]:
    if not path.exists():
        return []

    rows = []
    with path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.reader(handle, delimiter="\t")
        for parts in reader:
            if len(parts) < 11:
                continue

            if len(parts) >= 19:
                row = {
                    "group": parts[0],
                    "model_id": int(parts[1]),
                    "model_name": parts[2],
                    "voice_label": parts[3],
                    "phrase_id": int(parts[4]),
                    "bank_id": int(parts[5]),
                    "bank_slot_id": int(parts[6]),
                    "num_sounds_in_bank": int(parts[7]),
                    "bank_offset": int(parts[8]),
                    "bank_length": int(parts[9]),
                    "pak_file_number": int(parts[10]),
                    "sound_id_in_slot": int(parts[11]),
                    "sound_length": int(parts[12]),
                    "play_position": int(parts[13]),
                    "voice_type": int(parts[14]),
                    "voice_id": int(parts[15]),
                    "voice_gender": int(parts[16]),
                    "final_volume": float(parts[17]),
                    "frequency": float(parts[18]),
                }
            else:
                row = {
                    "group": parts[0],
                    "model_id": int(parts[1]),
                    "model_name": parts[2],
                    "voice_label": parts[3],
                    "phrase_id": int(parts[4]),
                    "bank_id": -1,
                    "bank_slot_id": int(parts[5]),
                    "num_sounds_in_bank": -1,
                    "bank_offset": -1,
                    "bank_length": -1,
                    "pak_file_number": -1,
                    "sound_id_in_slot": int(parts[6]),
                    "sound_length": int(parts[7]),
                    "play_position": int(parts[8]),
                    "voice_type": int(parts[9]),
                    "voice_id": int(parts[10]),
                    "voice_gender": int(parts[11]) if len(parts) > 11 else -1,
                    "final_volume": 0.0,
                    "frequency": 0.0,
                }

            rows.append(row)
    return rows


def read_bank_header(package_data: bytes, bank_header_offset: int) -> tuple[int, list[dict]]:
    header = package_data[bank_header_offset : bank_header_offset + BANK_HEADER_SIZE]
    if len(header) < BANK_HEADER_SIZE:
        raise ValueError(f"Bank header truncated at offset {bank_header_offset}")

    num_sounds = struct.unpack_from("<H", header, 0)[0]
    sounds = []
    for idx in range(400):
        base = 4 + idx * SOUND_META_SIZE
        buffer_offset, loop_offset, sample_rate, headroom = struct.unpack_from("<IiHh", header, base)
        sounds.append(
            {
                "sound_id": idx,
                "buffer_offset": buffer_offset,
                "loop_offset": loop_offset,
                "sample_rate": sample_rate,
                "headroom": headroom,
            }
        )
    return num_sounds, sounds


def pcm_range(bank_size: int, sounds: list[dict], sound_id: int, num_sounds: int) -> tuple[int, int]:
    current = sounds[sound_id]
    if sound_id >= num_sounds:
        raise IndexError(f"sound_id {sound_id} outside num_sounds {num_sounds}")

    start = current["buffer_offset"]
    if sound_id >= num_sounds - 1:
        length = bank_size - start
    else:
        length = sounds[sound_id + 1]["buffer_offset"] - start
    return start, length


def write_wav(path: Path, pcm_data: bytes, sample_rate: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(sample_rate if sample_rate > 0 else 22050)
        wav.writeframes(pcm_data)


def extract_all() -> dict:
    pak_files = parse_pakfiles(CONFIG_DIR / "PakFiles.dat")
    bank_lookup = parse_bank_lookup(CONFIG_DIR / "BankLkup.dat")
    observed = parse_observed_log(LOG_PATH)

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    if not observed:
        manifest = {"status": "no_observations", "log_path": str(LOG_PATH)}
        MANIFEST_PATH.write_text(json.dumps(manifest, indent=2, ensure_ascii=False), encoding="utf-8")
        return manifest

    package_cache: dict[str, bytes] = {}
    manifest_entries = []

    dedup: OrderedDict[tuple[int, int], dict] = OrderedDict()
    for row in observed:
        if row["bank_id"] < 0 or row["sound_id_in_slot"] < 0:
            continue
        dedup.setdefault((row["bank_id"], row["sound_id_in_slot"]), row)

    for (bank_id, sound_id), row in dedup.items():
        if bank_id >= len(bank_lookup):
            manifest_entries.append({"status": "missing_bank_lookup", "row": row})
            continue

        bank_meta = bank_lookup[bank_id]
        package_index = bank_meta["package_index"]
        if package_index >= len(pak_files):
            manifest_entries.append({"status": "missing_package", "row": row, "bank_meta": bank_meta})
            continue

        package_name = pak_files[package_index]
        package_path = SFX_DIR / package_name
        if package_name not in package_cache:
            package_cache[package_name] = package_path.read_bytes()

        package_data = package_cache[package_name]
        num_sounds, sounds = read_bank_header(package_data, bank_meta["bank_header_offset"])
        if sound_id >= num_sounds:
            manifest_entries.append(
                {
                    "status": "sound_id_out_of_range",
                    "row": row,
                    "num_sounds": num_sounds,
                    "package_name": package_name,
                }
            )
            continue

        pcm_offset_in_bank, pcm_length = pcm_range(bank_meta["bank_size"], sounds, sound_id, num_sounds)
        absolute_pcm_offset = bank_meta["bank_header_offset"] + BANK_HEADER_SIZE + pcm_offset_in_bank
        pcm_data = package_data[absolute_pcm_offset : absolute_pcm_offset + pcm_length]

        sound_meta = sounds[sound_id]
        group_root = OUTPUT_DIR / sanitize_name(row["group"])
        file_stub = (
            f"{sanitize_name(row['model_name'])}"
            f"__phrase_{row['phrase_id']}"
            f"__bankid_{bank_id}"
            f"__sfx_{sound_id}"
            f"__{sanitize_name(package_name)}"
        )
        wav_path = group_root / f"{file_stub}.wav"
        meta_path = group_root / f"{file_stub}.json"

        write_wav(wav_path, pcm_data, sound_meta["sample_rate"])
        meta_payload = {
            "group": row["group"],
            "model_id": row["model_id"],
            "model_name": row["model_name"],
            "voice_label": row["voice_label"],
            "phrase_id": row["phrase_id"],
            "bank_id": bank_id,
            "bank_slot_id": row["bank_slot_id"],
            "sound_id": sound_id,
            "package_name": package_name,
            "package_index": package_index,
            "bank_header_offset": bank_meta["bank_header_offset"],
            "bank_size": bank_meta["bank_size"],
            "sample_rate": sound_meta["sample_rate"],
            "loop_offset": sound_meta["loop_offset"],
            "headroom": sound_meta["headroom"],
            "pcm_offset": absolute_pcm_offset,
            "pcm_length": pcm_length,
            "wav_path": str(wav_path.relative_to(GAME_ROOT)),
        }
        meta_path.write_text(json.dumps(meta_payload, indent=2, ensure_ascii=False), encoding="utf-8")
        manifest_entries.append({"status": "ok", **meta_payload})

    manifest = {
        "log_path": str(LOG_PATH.relative_to(GAME_ROOT)) if LOG_PATH.exists() else str(LOG_PATH),
        "observed_rows": len(observed),
        "unique_bank_sound_pairs": len(dedup),
        "entries": manifest_entries,
    }
    MANIFEST_PATH.write_text(json.dumps(manifest, indent=2, ensure_ascii=False), encoding="utf-8")
    return manifest


def main() -> None:
    manifest = extract_all()
    print(json.dumps({k: manifest[k] for k in manifest if k != "entries"}, indent=2, ensure_ascii=False))
    ok_count = sum(1 for entry in manifest.get("entries", []) if entry.get("status") == "ok")
    print(f"Extracted wavs: {ok_count}")


if __name__ == "__main__":
    main()
