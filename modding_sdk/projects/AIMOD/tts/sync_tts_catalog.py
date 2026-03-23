from __future__ import annotations

import sqlite3
from pathlib import Path


ROOT = Path(__file__).resolve().parent
PROJECT_ROOT = ROOT.parent
DATA_DIR = PROJECT_ROOT / "data"
DB_PATH = DATA_DIR / "aimod_catalog.db"
VOICES_DIR = ROOT / "voices"


BASE_MODEL_IDS = {
    "es_AR-daniela-high",
    "es_ES-carlfm-x_low",
    "es_ES-davefx-medium",
    "es_ES-mls_10246-low",
    "es_ES-mls_9972-low",
    "es_ES-sharvard-medium",
    "es_MX-ald-medium",
    "es_MX-claude-high",
}


BALLAS_MODELS = {102, 103, 104}
POLICE_MODELS = {280, 281, 282, 283, 284, 288}


def make_voice_preset(
    voice_id: str,
    display_name: str,
    language: str,
    speaker_ref: str,
    sample_model_id: str,
) -> dict[str, object]:
    return {
        "voice_id": voice_id,
        "engine": "piper",
        "display_name": display_name,
        "language": language,
        "speaker_ref": speaker_ref,
        "sample_path": f"voices/{sample_model_id}.onnx",
        "enabled": 1,
    }


def build_voice_defs() -> list[dict[str, object]]:
    voices: list[dict[str, object]] = []

    for index in range(1, 13):
        voices.append(
            make_voice_preset(
                voice_id=f"mx_barrio_ald_{index:02d}",
                display_name=f"MX Barrio Ald {index:02d}",
                language="es-MX",
                speaker_ref=f"mx_barrio_ald_{index:02d}",
                sample_model_id="es_MX-ald-medium",
            )
        )

    for index in range(1, 9):
        voices.append(
            make_voice_preset(
                voice_id=f"mx_barrio_claude_{index:02d}",
                display_name=f"MX Barrio Claude {index:02d}",
                language="es-MX",
                speaker_ref=f"mx_barrio_claude_{index:02d}",
                sample_model_id="es_MX-claude-high",
            )
        )

    for index in range(1, 5):
        voices.append(
            make_voice_preset(
                voice_id=f"mx_calle_ald_{index:02d}",
                display_name=f"MX Calle Ald {index:02d}",
                language="es-MX",
                speaker_ref=f"mx_calle_ald_{index:02d}",
                sample_model_id="es_MX-ald-medium",
            )
        )

    for index in range(1, 5):
        voices.append(
            make_voice_preset(
                voice_id=f"mx_calle_claude_{index:02d}",
                display_name=f"MX Calle Claude {index:02d}",
                language="es-MX",
                speaker_ref=f"mx_calle_claude_{index:02d}",
                sample_model_id="es_MX-claude-high",
            )
        )

    for index in range(1, 4):
        voices.append(
            make_voice_preset(
                voice_id=f"es_autoridad_sharvard_{index:02d}",
                display_name=f"ES Autoridad Sharvard {index:02d}",
                language="es-ES",
                speaker_ref=f"es_autoridad_sharvard_{index:02d}",
                sample_model_id="es_ES-sharvard-medium",
            )
        )

    for index in range(1, 3):
        voices.append(
            make_voice_preset(
                voice_id=f"es_autoridad_dave_{index:02d}",
                display_name=f"ES Autoridad Dave {index:02d}",
                language="es-ES",
                speaker_ref=f"es_autoridad_dave_{index:02d}",
                sample_model_id="es_ES-davefx-medium",
            )
        )

    for index in range(1, 3):
        voices.append(
            make_voice_preset(
                voice_id=f"es_autoridad_carl_{index:02d}",
                display_name=f"ES Autoridad Carlfm {index:02d}",
                language="es-ES",
                speaker_ref=f"es_autoridad_carl_{index:02d}",
                sample_model_id="es_ES-carlfm-x_low",
            )
        )

    for index in range(1, 5):
        voices.append(
            make_voice_preset(
                voice_id=f"es_calle_dave_{index:02d}",
                display_name=f"ES Calle Dave {index:02d}",
                language="es-ES",
                speaker_ref=f"es_calle_dave_{index:02d}",
                sample_model_id="es_ES-davefx-medium",
            )
        )

    for index in range(1, 4):
        voices.append(
            make_voice_preset(
                voice_id=f"es_calle_carl_{index:02d}",
                display_name=f"ES Calle Carlfm {index:02d}",
                language="es-ES",
                speaker_ref=f"es_calle_carl_{index:02d}",
                sample_model_id="es_ES-carlfm-x_low",
            )
        )

    for index in range(1, 3):
        voices.append(
            make_voice_preset(
                voice_id=f"es_calle_mls10246_{index:02d}",
                display_name=f"ES Calle MLS10246 {index:02d}",
                language="es-ES",
                speaker_ref=f"es_calle_mls10246_{index:02d}",
                sample_model_id="es_ES-mls_10246-low",
            )
        )

    for index in range(1, 3):
        voices.append(
            make_voice_preset(
                voice_id=f"es_calle_mls9972_{index:02d}",
                display_name=f"ES Calle MLS9972 {index:02d}",
                language="es-ES",
                speaker_ref=f"es_calle_mls9972_{index:02d}",
                sample_model_id="es_ES-mls_9972-low",
            )
        )

    for index in range(1, 5):
        voices.append(
            make_voice_preset(
                voice_id=f"es_calle_sharvard_{index:02d}",
                display_name=f"ES Calle Sharvard {index:02d}",
                language="es-ES",
                speaker_ref=f"es_calle_sharvard_{index:02d}",
                sample_model_id="es_ES-sharvard-medium",
            )
        )

    for index in range(1, 7):
        voices.append(
            make_voice_preset(
                voice_id=f"ar_fem_daniela_{index:02d}",
                display_name=f"AR Daniela Fem {index:02d}",
                language="es-AR",
                speaker_ref=f"ar_fem_daniela_{index:02d}",
                sample_model_id="es_AR-daniela-high",
            )
        )

    voices.append(
        make_voice_preset(
            voice_id="cj_story_01",
            display_name="CJ Story Voice",
            language="es-MX",
            speaker_ref="cj_story_01",
            sample_model_id="es_MX-claude-high",
        )
    )

    return voices


VOICE_DEFS = build_voice_defs()


def is_female_ped(row: sqlite3.Row) -> bool:
    default_ped_type = (row["default_ped_type"] or "").upper()
    anim_group = (row["anim_group"] or "").lower()
    voice1 = (row["voice1"] or "").upper()
    model_name = (row["model_name"] or "").upper()

    if "FEMALE" in default_ped_type:
        return True
    if "WOMAN" in anim_group:
        return True
    if any(token in voice1 for token in ("_BF", "_GF", "_HF", "_WF", "_VF")):
        return True
    if model_name.startswith(("BF", "HF", "VF", "WF", "SWF", "VWF")):
        return True
    return False


def get_group_name(row: sqlite3.Row) -> str:
    model_id = row["model_id"]
    ped_audio_type = (row["ped_audio_type"] or "").upper()

    if model_id in BALLAS_MODELS:
        return "ballas"
    if model_id in POLICE_MODELS:
        return "police"
    if ped_audio_type == "PED_TYPE_GANG":
        return "gang"
    if ped_audio_type == "PED_TYPE_EMG":
        return "emergency"
    if ped_audio_type == "PED_TYPE_GFD":
        return "gfd"
    if ped_audio_type == "PED_TYPE_SPC":
        return "special"
    if ped_audio_type == "PED_TYPE_PLAYER":
        return "player"
    return "ambient"


def choose_voice(row: sqlite3.Row) -> tuple[str, float, float, str]:
    model_id = row["model_id"]
    group_name = get_group_name(row)
    female = is_female_ped(row)

    if model_id == 0:
        return "cj_story_01", 0.98, 0.97, "player"

    if female:
        voice_pool = [
            "ar_fem_daniela_01",
            "ar_fem_daniela_02",
            "ar_fem_daniela_03",
            "ar_fem_daniela_04",
            "ar_fem_daniela_05",
            "ar_fem_daniela_06",
        ]
        base_pitch = 1.05
        base_speed = 1.02
    elif group_name == "ballas":
        voice_pool = [
            "mx_barrio_ald_01",
            "mx_barrio_ald_02",
            "mx_barrio_ald_03",
            "mx_barrio_ald_04",
            "mx_barrio_ald_05",
            "mx_barrio_ald_06",
            "mx_barrio_ald_07",
            "mx_barrio_ald_08",
            "mx_barrio_ald_09",
            "mx_barrio_ald_10",
            "mx_barrio_ald_11",
            "mx_barrio_ald_12",
            "mx_barrio_claude_01",
            "mx_barrio_claude_02",
            "mx_barrio_claude_03",
            "mx_barrio_claude_04",
            "mx_barrio_claude_05",
            "mx_barrio_claude_06",
            "mx_barrio_claude_07",
            "mx_barrio_claude_08",
        ]
        base_pitch = 0.96
        base_speed = 1.01
    elif group_name == "police":
        voice_pool = [
            "es_autoridad_sharvard_01",
            "es_autoridad_sharvard_02",
            "es_autoridad_sharvard_03",
            "es_autoridad_dave_01",
            "es_autoridad_dave_02",
            "es_autoridad_carl_01",
            "es_autoridad_carl_02",
        ]
        base_pitch = 0.98
        base_speed = 0.96
    elif group_name in {"gang", "gfd"}:
        voice_pool = [
            "mx_barrio_ald_03",
            "mx_barrio_ald_05",
            "mx_barrio_ald_07",
            "mx_barrio_claude_02",
            "mx_barrio_claude_04",
            "mx_barrio_claude_06",
            "mx_calle_ald_01",
            "mx_calle_claude_01",
        ]
        base_pitch = 0.99
        base_speed = 1.00
    elif group_name in {"emergency", "player"}:
        voice_pool = [
            "es_autoridad_sharvard_01",
            "es_autoridad_sharvard_02",
            "es_autoridad_dave_01",
            "es_calle_sharvard_01",
            "es_calle_dave_01",
            "es_calle_mls10246_01",
        ]
        base_pitch = 1.00
        base_speed = 0.98
    elif group_name == "special":
        voice_pool = [
            "es_calle_dave_02",
            "es_calle_sharvard_02",
            "mx_calle_claude_02",
            "es_calle_mls9972_01",
            "mx_calle_ald_02",
        ]
        base_pitch = 1.00
        base_speed = 1.00
    else:
        voice_pool = [
            "es_calle_carl_01",
            "es_calle_carl_02",
            "es_calle_carl_03",
            "es_calle_dave_01",
            "es_calle_dave_02",
            "es_calle_dave_03",
            "es_calle_dave_04",
            "es_calle_mls10246_01",
            "es_calle_mls10246_02",
            "es_calle_mls9972_01",
            "es_calle_mls9972_02",
            "es_calle_sharvard_01",
            "es_calle_sharvard_02",
            "es_calle_sharvard_03",
            "es_calle_sharvard_04",
            "mx_calle_ald_01",
            "mx_calle_ald_02",
            "mx_calle_ald_03",
            "mx_calle_ald_04",
            "mx_calle_claude_01",
            "mx_calle_claude_02",
            "mx_calle_claude_03",
            "mx_calle_claude_04",
        ]
        base_pitch = 1.00
        base_speed = 1.00

    voice_id = voice_pool[model_id % len(voice_pool)]
    pitch = round(base_pitch + (((model_id % 7) - 3) * 0.02), 3)
    speed = round(base_speed + (((model_id % 5) - 2) * 0.02), 3)
    pitch = min(1.18, max(0.84, pitch))
    speed = min(1.14, max(0.88, speed))
    return voice_id, pitch, speed, group_name


def main() -> None:
    missing_models = [model_id for model_id in BASE_MODEL_IDS if not (VOICES_DIR / f"{model_id}.onnx").exists()]
    if missing_models:
        raise SystemExit(f"Missing Piper models: {', '.join(missing_models)}")

    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    cur = conn.cursor()

    cur.execute("BEGIN")

    cur.execute("DELETE FROM tts_voices")
    cur.executemany(
        """
        INSERT INTO tts_voices (
            voice_id, engine, display_name, language, speaker_ref, sample_path, enabled
        ) VALUES (
            :voice_id, :engine, :display_name, :language, :speaker_ref, :sample_path, :enabled
        )
        """,
        VOICE_DEFS,
    )

    rows = list(
        cur.execute(
            """
            SELECT model_id, model_name, default_ped_type, anim_group, ped_audio_type, voice1
            FROM ped_models
            ORDER BY model_id
            """
        )
    )

    cur.execute("DELETE FROM ped_tts_assignments")
    assignments = []
    for row in rows:
        voice_id, pitch, speed, group_name = choose_voice(row)
        assignments.append(
            {
                "model_id": row["model_id"],
                "group_name": group_name,
                "voice_id": voice_id,
                "pitch": pitch,
                "speed": speed,
            }
        )

    cur.executemany(
        """
        INSERT INTO ped_tts_assignments (
            model_id, group_name, voice_id, pitch, speed
        ) VALUES (
            :model_id, :group_name, :voice_id, :pitch, :speed
        )
        """,
        assignments,
    )

    cur.execute(
        """
        INSERT INTO metadata (key, value)
        VALUES ('tts_catalog_last_sync_utc', datetime('now'))
        ON CONFLICT(key) DO UPDATE SET value = excluded.value
        """
    )
    cur.execute(
        """
        INSERT INTO metadata (key, value)
        VALUES ('tts_voice_count', ?)
        ON CONFLICT(key) DO UPDATE SET value = excluded.value
        """,
        (str(len(VOICE_DEFS)),),
    )
    cur.execute(
        """
        INSERT INTO metadata (key, value)
        VALUES ('tts_assignment_count', ?)
        ON CONFLICT(key) DO UPDATE SET value = excluded.value
        """,
        (str(len(assignments)),),
    )

    conn.commit()
    conn.close()

    print(f"Voices synced: {len(VOICE_DEFS)}")
    print(f"Ped assignments synced: {len(assignments)}")


if __name__ == "__main__":
    main()
