from __future__ import annotations

import re
import sqlite3
from pathlib import Path


ROOT = Path(__file__).resolve().parent
PROJECT_ROOT = ROOT.parent
DATA_DIR = PROJECT_ROOT / "data"
DB_PATH = DATA_DIR / "aimod_catalog.db"
VOICES_DIR = ROOT / "voices"


BASE_MODELS = {
    "es_AR-daniela-high": {"language": "es-AR", "style": "female_story"},
    "es_ES-carlfm-x_low": {"language": "es-ES", "style": "male_calm"},
    "es_ES-davefx-medium": {"language": "es-ES", "style": "male_street"},
    "es_ES-mls_10246-low": {"language": "es-ES", "style": "male_light"},
    "es_ES-mls_9972-low": {"language": "es-ES", "style": "male_rough"},
    "es_ES-sharvard-medium": {"language": "es-ES", "style": "male_authority"},
    "es_MX-ald-medium": {"language": "es-MX", "style": "mx_barrio"},
    "es_MX-claude-high": {"language": "es-MX", "style": "mx_street"},
}

BALLAS_MODELS = {102, 103, 104}
POLICE_MODELS = {280, 281, 282, 283, 284, 288}

KOKORO_MALE_CJ = ["bm_george"]
KOKORO_MALE_STREET = ["em_alex", "am_onyx", "am_michael", "am_fenrir", "bm_daniel", "am_eric", "am_liam"]
KOKORO_MALE_AUTHORITY = ["bm_george", "bm_lewis", "am_adam", "am_echo", "hm_omega", "am_michael"]
KOKORO_MALE_CIVIL = ["am_adam", "am_echo", "am_eric", "am_liam", "bm_fable", "bm_daniel", "em_alex"]
KOKORO_MALE_SPECIAL = ["hm_omega", "am_fenrir", "am_onyx", "bm_george", "bm_fable", "am_michael"]
KOKORO_FEMALE_STREET = ["af_river", "af_sky", "af_nova", "af_jessica", "bf_isabella", "bf_emma"]
KOKORO_FEMALE_CIVIL = ["af_sarah", "af_nicole", "af_bella", "af_heart", "bf_emma", "bf_lily"]
KOKORO_FEMALE_SPECIAL = ["af_aoede", "af_kore", "af_river", "bf_isabella", "af_sky", "af_nova"]

VOICE_POOL_BLUEPRINTS = {
    "player_cj": [("cj_story_01", "CJ Story Voice", "bm_george")],
    "street_male": [
        ("pool_street_male_01", "Street Male 01", "em_alex"),
        ("pool_street_male_02", "Street Male 02", "am_onyx"),
        ("pool_street_male_03", "Street Male 03", "am_michael"),
        ("pool_street_male_04", "Street Male 04", "am_fenrir"),
        ("pool_street_male_05", "Street Male 05", "bm_daniel"),
        ("pool_street_male_06", "Street Male 06", "am_eric"),
        ("pool_street_male_07", "Street Male 07", "am_liam"),
        ("pool_street_male_08", "Street Male 08", "em_alex,am_onyx"),
        ("pool_street_male_09", "Street Male 09", "am_michael,bm_daniel"),
        ("pool_street_male_10", "Street Male 10", "am_fenrir,am_eric"),
    ],
    "street_female": [
        ("pool_street_female_01", "Street Female 01", "af_river"),
        ("pool_street_female_02", "Street Female 02", "af_sky"),
        ("pool_street_female_03", "Street Female 03", "af_nova"),
        ("pool_street_female_04", "Street Female 04", "af_jessica"),
        ("pool_street_female_05", "Street Female 05", "bf_isabella"),
        ("pool_street_female_06", "Street Female 06", "bf_emma"),
        ("pool_street_female_07", "Street Female 07", "af_river,af_sky"),
        ("pool_street_female_08", "Street Female 08", "af_nova,bf_isabella"),
    ],
    "authority_male": [
        ("pool_authority_male_01", "Authority Male 01", "bm_george"),
        ("pool_authority_male_02", "Authority Male 02", "bm_lewis"),
        ("pool_authority_male_03", "Authority Male 03", "am_adam"),
        ("pool_authority_male_04", "Authority Male 04", "am_echo"),
        ("pool_authority_male_05", "Authority Male 05", "hm_omega"),
        ("pool_authority_male_06", "Authority Male 06", "am_michael"),
        ("pool_authority_male_07", "Authority Male 07", "bm_george,am_echo"),
        ("pool_authority_male_08", "Authority Male 08", "bm_lewis,hm_omega"),
    ],
    "authority_female": [
        ("pool_authority_female_01", "Authority Female 01", "af_sarah"),
        ("pool_authority_female_02", "Authority Female 02", "bf_isabella"),
        ("pool_authority_female_03", "Authority Female 03", "af_nicole"),
        ("pool_authority_female_04", "Authority Female 04", "af_sky,bf_isabella"),
    ],
    "civil_male": [
        ("pool_civil_male_01", "Civil Male 01", "am_adam"),
        ("pool_civil_male_02", "Civil Male 02", "am_echo"),
        ("pool_civil_male_03", "Civil Male 03", "am_eric"),
        ("pool_civil_male_04", "Civil Male 04", "am_liam"),
        ("pool_civil_male_05", "Civil Male 05", "bm_fable"),
        ("pool_civil_male_06", "Civil Male 06", "bm_daniel"),
        ("pool_civil_male_07", "Civil Male 07", "em_alex"),
        ("pool_civil_male_08", "Civil Male 08", "am_adam,bm_fable"),
    ],
    "civil_female": [
        ("pool_civil_female_01", "Civil Female 01", "af_sarah"),
        ("pool_civil_female_02", "Civil Female 02", "af_nicole"),
        ("pool_civil_female_03", "Civil Female 03", "af_bella"),
        ("pool_civil_female_04", "Civil Female 04", "af_heart"),
        ("pool_civil_female_05", "Civil Female 05", "bf_emma"),
        ("pool_civil_female_06", "Civil Female 06", "bf_lily"),
        ("pool_civil_female_07", "Civil Female 07", "af_bella,bf_lily"),
        ("pool_civil_female_08", "Civil Female 08", "af_sarah,af_nicole"),
    ],
    "special_male": [
        ("pool_special_male_01", "Special Male 01", "hm_omega"),
        ("pool_special_male_02", "Special Male 02", "am_fenrir"),
        ("pool_special_male_03", "Special Male 03", "am_onyx"),
        ("pool_special_male_04", "Special Male 04", "bm_george"),
        ("pool_special_male_05", "Special Male 05", "bm_fable"),
        ("pool_special_male_06", "Special Male 06", "am_michael"),
        ("pool_special_male_07", "Special Male 07", "hm_omega,am_fenrir"),
        ("pool_special_male_08", "Special Male 08", "am_onyx,bm_fable"),
    ],
    "special_female": [
        ("pool_special_female_01", "Special Female 01", "af_aoede"),
        ("pool_special_female_02", "Special Female 02", "af_kore"),
        ("pool_special_female_03", "Special Female 03", "af_river"),
        ("pool_special_female_04", "Special Female 04", "bf_isabella"),
        ("pool_special_female_05", "Special Female 05", "af_sky"),
        ("pool_special_female_06", "Special Female 06", "af_nova"),
        ("pool_special_female_07", "Special Female 07", "af_aoede,af_kore"),
        ("pool_special_female_08", "Special Female 08", "af_river,af_nova"),
    ],
}


def slugify(value: str) -> str:
    value = (value or "").strip().lower()
    value = re.sub(r"[^a-z0-9]+", "_", value)
    value = re.sub(r"_+", "_", value).strip("_")
    return value or "ped"


def clamp(value: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, value))


def is_female_ped(row: sqlite3.Row) -> bool:
    default_ped_type = (row["default_ped_type"] or "").upper()
    anim_group = (row["anim_group"] or "").lower()
    voice1 = (row["voice1"] or "").upper()
    model_name = (row["model_name"] or "").upper()

    if "FEMALE" in default_ped_type:
        return True
    if "woman" in anim_group or "sexy" in anim_group:
        return True
    if any(token in voice1 for token in ("_BF", "_GF", "_HF", "_VF", "_WF")):
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


def choose_piper_base_model(row: sqlite3.Row, group_name: str, female: bool) -> str:
    model_id = row["model_id"]
    voice1 = (row["voice1"] or "").upper()
    model_name = (row["model_name"] or "").upper()

    if model_id == 0:
        return "es_ES-carlfm-x_low"

    if female:
        return "es_AR-daniela-high"

    if group_name == "ballas":
        return "es_MX-ald-medium" if model_id % 2 == 0 else "es_MX-claude-high"

    if group_name in {"gang", "gfd"}:
        cycle = [
            "es_MX-ald-medium",
            "es_MX-claude-high",
            "es_ES-davefx-medium",
            "es_ES-mls_9972-low",
        ]
        return cycle[model_id % len(cycle)]

    if group_name == "police":
        cycle = [
            "es_ES-sharvard-medium",
            "es_ES-davefx-medium",
            "es_ES-carlfm-x_low",
        ]
        return cycle[model_id % len(cycle)]

    if group_name == "emergency":
        cycle = [
            "es_ES-sharvard-medium",
            "es_ES-carlfm-x_low",
            "es_ES-davefx-medium",
        ]
        return cycle[model_id % len(cycle)]

    if group_name == "special":
        cycle = [
            "es_ES-davefx-medium",
            "es_ES-sharvard-medium",
            "es_MX-claude-high",
            "es_ES-mls_10246-low",
        ]
        return cycle[model_id % len(cycle)]

    if any(token in voice1 for token in ("BLACK", "BMY", "WMY", "OMY", "VLA", "GNG")) or model_name.startswith(("B", "WMY")):
        cycle = [
            "es_MX-ald-medium",
            "es_MX-claude-high",
            "es_ES-davefx-medium",
            "es_ES-mls_9972-low",
        ]
        return cycle[model_id % len(cycle)]

    cycle = [
        "es_ES-carlfm-x_low",
        "es_ES-davefx-medium",
        "es_ES-mls_10246-low",
        "es_ES-mls_9972-low",
        "es_ES-sharvard-medium",
        "es_MX-ald-medium",
        "es_MX-claude-high",
    ]
    return cycle[model_id % len(cycle)]


def build_kokoro_voice_token(pool: list[str], model_id: int) -> str:
    if len(pool) == 1:
        return pool[0]

    primary = pool[model_id % len(pool)]
    secondary = pool[(model_id * 5 + 3) % len(pool)]
    tertiary = pool[(model_id * 7 + 1) % len(pool)]

    if secondary == primary:
        secondary = pool[(model_id + 1) % len(pool)]
    if tertiary in {primary, secondary}:
        tertiary = pool[(model_id + 2) % len(pool)]

    mode = model_id % 5
    if mode == 0:
        return primary
    if mode == 1:
        return f"{primary},{secondary}"
    if mode == 2:
        return f"{secondary},{primary}"
    if mode == 3:
        return f"{primary},{tertiary}"
    return secondary


def choose_kokoro_token(row: sqlite3.Row, group_name: str, female: bool) -> str:
    model_id = row["model_id"]

    if model_id == 0:
        return KOKORO_MALE_CJ[0]

    if female:
        if group_name in {"ballas", "gang"}:
            return build_kokoro_voice_token(KOKORO_FEMALE_STREET, model_id)
        if group_name in {"special"}:
            return build_kokoro_voice_token(KOKORO_FEMALE_SPECIAL, model_id)
        return build_kokoro_voice_token(KOKORO_FEMALE_CIVIL, model_id)

    if group_name == "police":
        return build_kokoro_voice_token(KOKORO_MALE_AUTHORITY, model_id)
    if group_name in {"emergency", "gfd"}:
        return build_kokoro_voice_token(KOKORO_MALE_AUTHORITY, model_id)
    if group_name in {"ballas", "gang"}:
        return build_kokoro_voice_token(KOKORO_MALE_STREET, model_id)
    if group_name == "special":
        return build_kokoro_voice_token(KOKORO_MALE_SPECIAL, model_id)
    return build_kokoro_voice_token(KOKORO_MALE_CIVIL, model_id)


def choose_pitch_and_speed(model_id: int, group_name: str, female: bool, base_model: str) -> tuple[float, float]:
    if model_id == 0:
        return 0.92, 0.95

    if female:
        base_pitch = 1.08
        base_speed = 1.01
    elif group_name == "ballas":
        base_pitch = 0.95
        base_speed = 1.02
    elif group_name == "police":
        base_pitch = 0.98
        base_speed = 0.97
    elif group_name in {"gang", "gfd"}:
        base_pitch = 0.97
        base_speed = 1.01
    elif group_name == "special":
        base_pitch = 1.00
        base_speed = 1.00
    else:
        base_pitch = 1.00
        base_speed = 1.00

    if base_model == "es_ES-sharvard-medium":
        base_pitch -= 0.03
        base_speed -= 0.02
    elif base_model == "es_ES-carlfm-x_low":
        base_pitch -= 0.01
        base_speed -= 0.01
    elif base_model == "es_MX-claude-high":
        base_pitch += 0.01
        base_speed += 0.01

    pitch = round(clamp(base_pitch + (((model_id % 9) - 4) * 0.018), 0.82, 1.22), 3)
    speed = round(clamp(base_speed + (((model_id % 7) - 3) * 0.018), 0.86, 1.18), 3)
    return pitch, speed


def build_voice_row(row: sqlite3.Row) -> dict[str, object]:
    model_id = row["model_id"]
    model_name = row["model_name"]
    group_name = get_group_name(row)
    female = is_female_ped(row)
    base_model = choose_piper_base_model(row, group_name, female)
    kokoro_token = choose_kokoro_token(row, group_name, female)
    pitch, speed = choose_pitch_and_speed(model_id, group_name, female, base_model)

    if model_id == 0:
        voice_id = "cj_story_01"
        display_name = "CJ Story Voice"
    else:
        voice_id = f"ped_{model_id}_{slugify(model_name)}"
        display_name = f"{model_name} Voice"

    base_meta = BASE_MODELS[base_model]
    return {
        "model_id": model_id,
        "group_name": group_name,
        "voice_id": voice_id,
        "pitch": pitch,
        "speed": speed,
        "engine": "kokoro",
        "display_name": display_name,
        "language": "es",
        "speaker_ref": f"{group_name}:{base_meta['style']}:{slugify(model_name)}",
        "sample_path": kokoro_token,
        "enabled": 1,
    }


def ensure_schema(cur: sqlite3.Cursor) -> None:
    cur.execute(
        """
        CREATE TABLE IF NOT EXISTS tts_voice_pools (
            pool_name TEXT NOT NULL,
            voice_id TEXT NOT NULL,
            weight INTEGER NOT NULL DEFAULT 1,
            PRIMARY KEY(pool_name, voice_id)
        )
        """
    )


def build_pool_voice_rows() -> tuple[list[dict[str, object]], list[dict[str, object]]]:
    voice_rows: list[dict[str, object]] = []
    pool_rows: list[dict[str, object]] = []

    for pool_name, entries in VOICE_POOL_BLUEPRINTS.items():
        for voice_id, display_name, sample_path in entries:
            if voice_id != "cj_story_01":
                voice_rows.append(
                    {
                        "voice_id": voice_id,
                        "engine": "kokoro",
                        "display_name": display_name,
                        "language": "es",
                        "speaker_ref": f"{pool_name}:{slugify(display_name)}",
                        "sample_path": sample_path,
                        "enabled": 1,
                    }
                )
            pool_rows.append({"pool_name": pool_name, "voice_id": voice_id, "weight": 1})

    return voice_rows, pool_rows


def main() -> None:
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    cur = conn.cursor()
    ensure_schema(cur)

    rows = list(
        cur.execute(
            """
            SELECT model_id, model_name, default_ped_type, anim_group, ped_audio_type, voice1
            FROM ped_models
            ORDER BY model_id
            """
        )
    )

    voice_rows = [build_voice_row(row) for row in rows]
    pool_voice_rows, pool_rows = build_pool_voice_rows()
    all_voice_rows = {row["voice_id"]: row for row in voice_rows}
    for row in pool_voice_rows:
        all_voice_rows[row["voice_id"]] = row

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
        list(all_voice_rows.values()),
    )

    cur.execute("DELETE FROM ped_tts_assignments")
    cur.executemany(
        """
        INSERT INTO ped_tts_assignments (
            model_id, group_name, voice_id, pitch, speed
        ) VALUES (
            :model_id, :group_name, :voice_id, :pitch, :speed
        )
        """,
        voice_rows,
    )

    cur.execute("DELETE FROM tts_voice_pools")
    cur.executemany(
        """
        INSERT INTO tts_voice_pools (
            pool_name, voice_id, weight
        ) VALUES (
            :pool_name, :voice_id, :weight
        )
        """,
        pool_rows,
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
        (str(len(all_voice_rows)),),
    )
    cur.execute(
        """
        INSERT INTO metadata (key, value)
        VALUES ('tts_assignment_count', ?)
        ON CONFLICT(key) DO UPDATE SET value = excluded.value
        """,
        (str(len(voice_rows)),),
    )
    cur.execute(
        """
        INSERT INTO metadata (key, value)
        VALUES ('tts_pool_count', ?)
        ON CONFLICT(key) DO UPDATE SET value = excluded.value
        """,
        (str(len(VOICE_POOL_BLUEPRINTS)),),
    )

    conn.commit()
    conn.close()

    print(f"Voices synced: {len(all_voice_rows)}")
    print(f"Ped assignments synced: {len(voice_rows)}")
    print(f"Voice pools synced: {len(VOICE_POOL_BLUEPRINTS)}")


if __name__ == "__main__":
    main()
