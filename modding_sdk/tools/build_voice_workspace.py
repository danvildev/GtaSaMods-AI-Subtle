from __future__ import annotations

import csv
import json
import re
from collections import defaultdict
from pathlib import Path


GAME_ROOT = Path(__file__).resolve().parents[2]
PEDS_IDE_PATH = GAME_ROOT / "data" / "peds.ide"
OUTPUT_ROOT = GAME_ROOT / "voice_workspace"

PEDS_DIR = OUTPUT_ROOT / "peds"
VOICES_DIR = OUTPUT_ROOT / "voices"
EXPORTS_DIR = OUTPUT_ROOT / "exports"

NULL_TOKENS = {"", "null", "NULL", "none", "NONE"}


def normalize_token(value: str) -> str:
    return value.strip().strip('"')


def sanitize_name(value: str) -> str:
    safe = re.sub(r"[^A-Za-z0-9_.-]+", "_", value.strip())
    safe = safe.strip("_")
    return safe or "unknown"


def parse_peds_ide(path: Path) -> list[dict]:
    records: list[dict] = []

    with path.open("r", encoding="latin-1") as handle:
        in_peds_block = False
        for line_number, raw_line in enumerate(handle, start=1):
            line = raw_line.strip()
            if not line or line.startswith("#"):
                continue

            lower = line.lower()
            if lower == "peds":
                in_peds_block = True
                continue
            if lower == "end":
                in_peds_block = False
                continue
            if not in_peds_block:
                continue

            if "PED_TYPE_" in line:
                line = re.sub(r"(\d)\s+(PED_TYPE_)", r"\1,\2", line)

            parts = [normalize_token(part) for part in line.split(",")]
            if len(parts) < 14:
                continue
            if len(parts) > 14:
                parts = parts[:14]

            try:
                model_id = int(parts[0], 10)
            except ValueError:
                continue

            record = {
                "line_number": line_number,
                "model_id": model_id,
                "model_name": parts[1],
                "txd_name": parts[2],
                "default_ped_type": parts[3],
                "stat_type": parts[4],
                "anim_group": parts[5],
                "cars_can_drive_mask": parts[6],
                "ped_flags": parts[7],
                "anim_file": parts[8],
                "radio1": parts[9],
                "radio2": parts[10],
                "ped_audio_type": parts[11],
                "voice1": parts[12],
                "voice2": parts[13],
                "raw_line": raw_line.rstrip("\n"),
            }

            voices = []
            for voice in (record["voice1"], record["voice2"]):
                if voice.startswith("VOICE_") and voice not in voices:
                    voices.append(voice)
            record["voices"] = voices
            record["has_voice"] = bool(voices)

            records.append(record)

    return records


def ensure_workspace_dirs() -> None:
    for path in (OUTPUT_ROOT, PEDS_DIR, VOICES_DIR, EXPORTS_DIR):
        path.mkdir(parents=True, exist_ok=True)


def write_text(path: Path, content: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")


def write_json(path: Path, payload: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8")


def build_ped_workspace(records: list[dict]) -> None:
    for record in records:
        if not record["has_voice"]:
            continue

        folder_name = f"{record['model_id']:03d}_{sanitize_name(record['model_name'])}"
        ped_root = PEDS_DIR / folder_name
        audio_root = ped_root / "audio_raw"
        audio_root.mkdir(parents=True, exist_ok=True)

        write_json(
            ped_root / "ped.json",
            {
                "model_id": record["model_id"],
                "model_name": record["model_name"],
                "txd_name": record["txd_name"],
                "default_ped_type": record["default_ped_type"],
                "stat_type": record["stat_type"],
                "anim_group": record["anim_group"],
                "cars_can_drive_mask": record["cars_can_drive_mask"],
                "ped_flags": record["ped_flags"],
                "anim_file": record["anim_file"],
                "radio1": record["radio1"],
                "radio2": record["radio2"],
                "ped_audio_type": record["ped_audio_type"],
                "voice1": record["voice1"],
                "voice2": record["voice2"],
                "voices": record["voices"],
                "source_file": str(PEDS_IDE_PATH.relative_to(GAME_ROOT)),
                "source_line": record["line_number"],
            },
        )

        write_text(
            ped_root / "dictionary_es.txt",
            "\n".join(
                [
                    "# Traduccion manual por ped.",
                    "# Formato sugerido: PHRASE_ID = texto en espanol",
                    f"# model_name = {record['model_name']}",
                    f"# voices = {', '.join(record['voices'])}",
                    "",
                ]
            ),
        )

        write_text(
            ped_root / "observed_phrase_ids.txt",
            "\n".join(
                [
                    "# Anota aqui los phraseId que vayas descubriendo para este ped.",
                    "# Ejemplo: 1234",
                    "",
                ]
            ),
        )

        write_text(
            ped_root / "notes.txt",
            "\n".join(
                [
                    f"Ped: {record['model_name']}",
                    f"Ped audio type: {record['ped_audio_type']}",
                    f"Voice 1: {record['voice1']}",
                    f"Voice 2: {record['voice2']}",
                    "",
                    "Pon aqui contexto, insultos recurrentes, tono, slang, etc.",
                ]
            ),
        )


def build_voice_workspace(records: list[dict]) -> dict[str, list[dict]]:
    voice_to_peds: dict[str, list[dict]] = defaultdict(list)

    for record in records:
        for voice in record["voices"]:
            voice_to_peds[voice].append(record)

    for voice, voice_records in voice_to_peds.items():
        voice_root = VOICES_DIR / sanitize_name(voice)
        audio_root = voice_root / "audio_raw"
        audio_root.mkdir(parents=True, exist_ok=True)

        write_json(
            voice_root / "voice.json",
            {
                "voice": voice,
                "ped_count": len(voice_records),
                "peds": [
                    {
                        "model_id": record["model_id"],
                        "model_name": record["model_name"],
                        "ped_audio_type": record["ped_audio_type"],
                    }
                    for record in sorted(voice_records, key=lambda item: item["model_id"])
                ],
            },
        )

        write_text(
            voice_root / "peds_using_this_voice.txt",
            "\n".join(
                [
                    f"{record['model_id']:03d} {record['model_name']}"
                    for record in sorted(voice_records, key=lambda item: item["model_id"])
                ]
            )
            + "\n"
        )

        write_text(
            voice_root / "dictionary_es.txt",
            "\n".join(
                [
                    "# Traduccion manual por voice set.",
                    "# Formato sugerido: PHRASE_ID = texto en espanol",
                    f"# voice = {voice}",
                    "",
                ]
            ),
        )

        write_text(
            voice_root / "notes.txt",
            "\n".join(
                [
                    f"Voice token: {voice}",
                    "Pon aqui notas globales de la voz, slang, tono y equivalencias.",
                    "",
                ]
            ),
        )

    return dict(sorted(voice_to_peds.items(), key=lambda item: item[0]))


def export_indexes(records: list[dict], voice_to_peds: dict[str, list[dict]]) -> None:
    voice_records = [record for record in records if record["has_voice"]]

    write_json(EXPORTS_DIR / "peds_complete.json", voice_records)
    write_json(
        EXPORTS_DIR / "voices_index.json",
        {
            voice: [
                {
                    "model_id": record["model_id"],
                    "model_name": record["model_name"],
                    "ped_audio_type": record["ped_audio_type"],
                }
                for record in records_for_voice
            ]
            for voice, records_for_voice in voice_to_peds.items()
        },
    )

    with (EXPORTS_DIR / "peds_complete.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=[
                "model_id",
                "model_name",
                "txd_name",
                "default_ped_type",
                "stat_type",
                "anim_group",
                "cars_can_drive_mask",
                "ped_flags",
                "anim_file",
                "radio1",
                "radio2",
                "ped_audio_type",
                "voice1",
                "voice2",
            ],
        )
        writer.writeheader()
        for record in voice_records:
            writer.writerow({key: record[key] for key in writer.fieldnames})

    summary_lines = [
        "GTA SA Ped Voice Workspace",
        "==========================",
        "",
        f"Game root: {GAME_ROOT}",
        f"Source: {PEDS_IDE_PATH}",
        f"Total peds parsed: {len(records)}",
        f"Peds with voice tokens: {len(voice_records)}",
        f"Unique voice tokens: {len(voice_to_peds)}",
        "",
        "Important SDK references:",
        "- modding_sdk/plugin-sdk/Plugin_SA/game_sa/CPedModelInfo.h",
        "- modding_sdk/plugin-sdk/Plugin_SA/game_sa/CAEPedSpeechAudioEntity.h",
        "- data/peds.ide",
        "",
        "Structure:",
        "- peds/<id_name>/ped.json",
        "- peds/<id_name>/dictionary_es.txt",
        "- peds/<id_name>/observed_phrase_ids.txt",
        "- voices/<voice_token>/voice.json",
        "- voices/<voice_token>/dictionary_es.txt",
        "- exports/peds_complete.json",
        "- exports/peds_complete.csv",
        "- exports/voices_index.json",
        "",
        "Recommended next step:",
        "1. Usa el mod para loguear phraseId, ped model y voice token.",
        "2. Mete los audios extraidos en la carpeta audio_raw correspondiente.",
        "3. Traduce primero por voice set y luego ajusta excepciones por ped.",
        "",
    ]
    write_text(OUTPUT_ROOT / "README-ES.txt", "\n".join(summary_lines))


def main() -> None:
    ensure_workspace_dirs()
    records = parse_peds_ide(PEDS_IDE_PATH)
    build_ped_workspace(records)
    voice_to_peds = build_voice_workspace(records)
    export_indexes(records, voice_to_peds)

    print(f"Workspace generated at: {OUTPUT_ROOT}")
    print(f"Peds parsed: {len(records)}")
    print(f"Peds with voices: {sum(1 for record in records if record['has_voice'])}")
    print(f"Unique voice tokens: {len(voice_to_peds)}")


if __name__ == "__main__":
    main()
