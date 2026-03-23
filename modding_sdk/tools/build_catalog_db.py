from __future__ import annotations

import csv
import json
import sqlite3
from pathlib import Path


GAME_ROOT = Path(__file__).resolve().parents[2]
WORKSPACE_DIR = GAME_ROOT / "voice_workspace"
EXPORTS_DIR = WORKSPACE_DIR / "exports"
RESEARCH_DIR = WORKSPACE_DIR / "research"
LOGS_DIR = WORKSPACE_DIR / "logs"
EXTRACTED_DIR = WORKSPACE_DIR / "extracted"

DB_PATH = WORKSPACE_DIR / "catalog.db"
PEDS_COMPLETE_JSON = EXPORTS_DIR / "peds_complete.json"
BALLAS_POLICE_SEED_JSON = RESEARCH_DIR / "ballas_police_seed.json"
OBSERVED_LOG = LOGS_DIR / "ballas_police_observed.tsv"
EXTRACTED_MANIFEST = EXTRACTED_DIR / "manifest.json"


SCHEMA_SQL = """
PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS metadata (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS sources (
    source_id TEXT PRIMARY KEY,
    title TEXT NOT NULL,
    url TEXT NOT NULL,
    kind TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS ped_models (
    model_id INTEGER PRIMARY KEY,
    model_name TEXT NOT NULL,
    txd_name TEXT,
    default_ped_type TEXT,
    stat_type TEXT,
    anim_group TEXT,
    cars_can_drive_mask TEXT,
    ped_flags TEXT,
    anim_file TEXT,
    radio1 TEXT,
    radio2 TEXT,
    ped_audio_type TEXT,
    voice1 TEXT,
    voice2 TEXT,
    source_file TEXT,
    source_line INTEGER
);

CREATE TABLE IF NOT EXISTS ped_model_voices (
    model_id INTEGER NOT NULL,
    voice_label TEXT NOT NULL,
    PRIMARY KEY (model_id, voice_label),
    FOREIGN KEY (model_id) REFERENCES ped_models(model_id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS target_groups (
    group_name TEXT PRIMARY KEY
);

CREATE TABLE IF NOT EXISTS target_group_models (
    group_name TEXT NOT NULL,
    model_id INTEGER NOT NULL,
    PRIMARY KEY (group_name, model_id),
    FOREIGN KEY (group_name) REFERENCES target_groups(group_name) ON DELETE CASCADE,
    FOREIGN KEY (model_id) REFERENCES ped_models(model_id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS candidate_audio_sets (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    group_name TEXT NOT NULL,
    candidate_type TEXT NOT NULL,
    package_name TEXT,
    bank_id INTEGER,
    sound_id INTEGER,
    sound_range TEXT,
    confidence TEXT,
    inference INTEGER NOT NULL DEFAULT 0,
    reason TEXT,
    FOREIGN KEY (group_name) REFERENCES target_groups(group_name) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS speech_semantics (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    group_name TEXT NOT NULL,
    speech_id INTEGER NOT NULL,
    meaning_en TEXT NOT NULL,
    confidence TEXT,
    UNIQUE (group_name, speech_id, meaning_en),
    FOREIGN KEY (group_name) REFERENCES target_groups(group_name) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS observations (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    group_name TEXT NOT NULL,
    model_id INTEGER,
    model_name TEXT,
    voice_label TEXT,
    phrase_id INTEGER NOT NULL,
    bank_id INTEGER,
    bank_slot_id INTEGER,
    num_sounds_in_bank INTEGER,
    bank_offset INTEGER,
    bank_length INTEGER,
    pak_file_number INTEGER,
    sound_id_in_slot INTEGER,
    sound_length INTEGER,
    play_position INTEGER,
    voice_type INTEGER,
    voice_id INTEGER,
    voice_gender INTEGER,
    final_volume REAL,
    frequency REAL,
    UNIQUE (group_name, model_id, phrase_id, bank_id, sound_id_in_slot)
);

CREATE TABLE IF NOT EXISTS extracted_audio (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    group_name TEXT NOT NULL,
    model_id INTEGER,
    model_name TEXT,
    voice_label TEXT,
    phrase_id INTEGER,
    bank_id INTEGER NOT NULL,
    bank_slot_id INTEGER,
    sound_id INTEGER NOT NULL,
    package_name TEXT,
    package_index INTEGER,
    bank_header_offset INTEGER,
    bank_size INTEGER,
    sample_rate INTEGER,
    loop_offset INTEGER,
    headroom INTEGER,
    pcm_offset INTEGER,
    pcm_length INTEGER,
    wav_path TEXT,
    UNIQUE (bank_id, sound_id, wav_path)
);

CREATE TABLE IF NOT EXISTS translations (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    group_name TEXT,
    model_id INTEGER,
    voice_label TEXT,
    phrase_id INTEGER,
    bank_id INTEGER,
    sound_id INTEGER,
    text_en TEXT,
    text_es TEXT,
    notes TEXT,
    status TEXT DEFAULT 'draft'
);

CREATE TABLE IF NOT EXISTS runtime_seed_texts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    group_name TEXT NOT NULL,
    model_id INTEGER,
    text_es TEXT NOT NULL,
    source TEXT NOT NULL DEFAULT 'seed',
    UNIQUE (group_name, model_id, text_es)
);

CREATE INDEX IF NOT EXISTS idx_observations_group_phrase ON observations(group_name, phrase_id);
CREATE INDEX IF NOT EXISTS idx_observations_bank_sound ON observations(bank_id, sound_id_in_slot);
CREATE INDEX IF NOT EXISTS idx_extracted_bank_sound ON extracted_audio(bank_id, sound_id);
CREATE INDEX IF NOT EXISTS idx_translations_lookup ON translations(group_name, model_id, phrase_id, bank_id, sound_id);
CREATE UNIQUE INDEX IF NOT EXISTS idx_translations_unique
ON translations(group_name, model_id, voice_label, phrase_id, bank_id, sound_id);
CREATE INDEX IF NOT EXISTS idx_runtime_seed_model ON runtime_seed_texts(model_id);
CREATE INDEX IF NOT EXISTS idx_runtime_seed_group ON runtime_seed_texts(group_name);
"""


def load_json(path: Path):
    if not path.exists():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def reset_dynamic_tables(cur: sqlite3.Cursor) -> None:
    for table in (
        "sources",
        "ped_model_voices",
        "target_group_models",
        "candidate_audio_sets",
        "speech_semantics",
        "observations",
        "extracted_audio",
        "runtime_seed_texts",
        "target_groups",
        "ped_models",
    ):
        cur.execute(f"DELETE FROM {table}")


def upsert_metadata(cur: sqlite3.Cursor, key: str, value: str) -> None:
    cur.execute(
        "INSERT INTO metadata(key, value) VALUES(?, ?) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value",
        (key, value),
    )


def import_peds(cur: sqlite3.Cursor, payload: list[dict]) -> None:
    for ped in payload:
        cur.execute(
            """
            INSERT INTO ped_models (
                model_id, model_name, txd_name, default_ped_type, stat_type,
                anim_group, cars_can_drive_mask, ped_flags, anim_file,
                radio1, radio2, ped_audio_type, voice1, voice2, source_file, source_line
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                ped["model_id"],
                ped["model_name"],
                ped.get("txd_name"),
                ped.get("default_ped_type"),
                ped.get("stat_type"),
                ped.get("anim_group"),
                ped.get("cars_can_drive_mask"),
                ped.get("ped_flags"),
                ped.get("anim_file"),
                ped.get("radio1"),
                ped.get("radio2"),
                ped.get("ped_audio_type"),
                ped.get("voice1"),
                ped.get("voice2"),
                ped.get("source_file"),
                ped.get("source_line"),
            ),
        )

        for voice_label in ped.get("voices", []):
            cur.execute(
                "INSERT INTO ped_model_voices(model_id, voice_label) VALUES(?, ?)",
                (ped["model_id"], voice_label),
            )


GENERIC_SEED_TEXTS = [
    "eh...",
    "que pasa?",
    "sigue caminando.",
    "no molestes.",
    "oye, tu.",
    "que miras?",
    "mmm...",
    "ya, ya.",
]

GANG_SEED_TEXTS = [
    "fuera de aqui.",
    "no eres de este barrio.",
    "muevete.",
    "te estoy mirando.",
    "sigue de largo.",
    "no provoques.",
]

EMERGENCY_SEED_TEXTS = [
    "alto ahi!",
    "retrocede.",
    "despeja la zona.",
    "quieto.",
    "a un lado.",
    "controla la situacion.",
]

SPECIAL_SEED_TEXTS = [
    "escucha bien.",
    "esto se puso raro.",
    "no pierdas detalle.",
    "tengo algo que decir.",
]

BALLAS_RUNTIME_TEXTS = [
    "que miras, tonto?",
    "fuera de nuestro barrio.",
    "te voy a reventar.",
    "ballas manda aqui.",
    "muevete.",
    "no eres bienvenido aqui.",
    "sigue caminando.",
    "te crees muy duro?",
    "largate antes de que te rompa.",
    "ahi va ese payaso.",
]

POLICE_RUNTIME_TEXTS = [
    "alto, policia!",
    "quieto ahi!",
    "no te muevas!",
    "sal del vehiculo!",
    "manos arriba!",
    "estas arrestado!",
    "necesito refuerzos!",
    "tirate al suelo!",
    "retirate ahora!",
    "a un lado, ya!",
]


def infer_runtime_group(ped: dict) -> str:
    model_id = int(ped["model_id"])
    if model_id in {102, 103, 104}:
        return "ballas"
    if model_id in {280, 281, 282, 283, 284, 288}:
        return "police"

    voice_blob = " ".join(str(v) for v in ped.get("voices", []))
    ped_audio = str(ped.get("ped_audio_type") or "")

    if "VOICE_GNG_" in voice_blob:
        return "gang"
    if "VOICE_EMG_" in voice_blob:
        return "emergency"
    if "VOICE_SPC_" in voice_blob or "PED_TYPE_SPC" in ped_audio:
        return "special"
    if "PED_TYPE_GFD" in ped_audio:
        return "gfd"
    return "ambient"


def get_runtime_seed_texts(group_name: str) -> list[str]:
    if group_name == "ballas":
        return BALLAS_RUNTIME_TEXTS
    if group_name == "police":
        return POLICE_RUNTIME_TEXTS
    if group_name in {"gang", "gfd"}:
        return GANG_SEED_TEXTS
    if group_name == "emergency":
        return EMERGENCY_SEED_TEXTS
    if group_name == "special":
        return SPECIAL_SEED_TEXTS
    return GENERIC_SEED_TEXTS


def populate_runtime_seed_texts(cur: sqlite3.Cursor, payload: list[dict]) -> None:
    for group_name in ("ambient", "gang", "gfd", "emergency", "special", "ballas", "police"):
        for text in get_runtime_seed_texts(group_name):
            cur.execute(
                """
                INSERT OR IGNORE INTO runtime_seed_texts(group_name, model_id, text_es, source)
                VALUES (?, NULL, ?, 'group_seed')
                """,
                (group_name, text),
            )

    for ped in payload:
        group_name = infer_runtime_group(ped)
        model_id = int(ped["model_id"])
        for text in get_runtime_seed_texts(group_name):
            cur.execute(
                """
                INSERT OR IGNORE INTO runtime_seed_texts(group_name, model_id, text_es, source)
                VALUES (?, ?, ?, 'model_seed')
                """,
                (group_name, model_id, text),
            )


def import_seed(cur: sqlite3.Cursor, payload: dict) -> None:
    for source in payload.get("sources", []):
        cur.execute(
            "INSERT INTO sources(source_id, title, url, kind) VALUES(?, ?, ?, ?)",
            (source["id"], source["title"], source["url"], source["kind"]),
        )

    for group_name, group_data in payload.get("groups", {}).items():
        cur.execute("INSERT INTO target_groups(group_name) VALUES(?)", (group_name,))

        for model in group_data.get("models", []):
            cur.execute(
                "INSERT INTO target_group_models(group_name, model_id) VALUES(?, ?)",
                (group_name, model["model_id"]),
            )

            for voice_label in model.get("voice_labels", []):
                cur.execute(
                    "INSERT OR IGNORE INTO ped_model_voices(model_id, voice_label) VALUES(?, ?)",
                    (model["model_id"], voice_label),
                )

        for candidate in group_data.get("candidate_audio_sets", []):
            cur.execute(
                """
                INSERT INTO candidate_audio_sets(
                    group_name, candidate_type, package_name, bank_id, sound_id,
                    sound_range, confidence, inference, reason
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    group_name,
                    candidate.get("type"),
                    candidate.get("package"),
                    candidate.get("bank_id"),
                    candidate.get("sound_id"),
                    candidate.get("sound_range"),
                    candidate.get("confidence"),
                    1 if candidate.get("inference") else 0,
                    candidate.get("reason"),
                ),
            )

        for semantic in group_data.get("speech_id_hypotheses", []):
            cur.execute(
                """
                INSERT INTO speech_semantics(group_name, speech_id, meaning_en, confidence)
                VALUES (?, ?, ?, ?)
                """,
                (
                    group_name,
                    semantic["speech_id"],
                    semantic["meaning_en"],
                    semantic.get("confidence"),
                ),
            )


def import_observations(cur: sqlite3.Cursor, log_path: Path) -> int:
    if not log_path.exists():
        return 0

    count = 0
    with log_path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.reader(handle, delimiter="\t")
        for parts in reader:
            if len(parts) < 11:
                continue

            if len(parts) >= 19:
                row = (
                    parts[0],
                    int(parts[1]),
                    parts[2],
                    parts[3],
                    int(parts[4]),
                    int(parts[5]),
                    int(parts[6]),
                    int(parts[7]),
                    int(parts[8]),
                    int(parts[9]),
                    int(parts[10]),
                    int(parts[11]),
                    int(parts[12]),
                    int(parts[13]),
                    int(parts[14]),
                    int(parts[15]),
                    int(parts[16]),
                    float(parts[17]),
                    float(parts[18]),
                )
            elif len(parts) >= 8:
                row = (
                    parts[0],
                    int(parts[1]),
                    parts[2],
                    parts[3],
                    int(parts[4]),
                    -1,
                    int(parts[5]),
                    -1,
                    -1,
                    -1,
                    -1,
                    int(parts[6]),
                    -1,
                    -1,
                    int(parts[7]),
                    -1,
                    -1,
                    None,
                    None,
                )
            else:
                continue

            cur.execute(
                """
                INSERT OR IGNORE INTO observations(
                    group_name, model_id, model_name, voice_label, phrase_id,
                    bank_id, bank_slot_id, num_sounds_in_bank, bank_offset,
                    bank_length, pak_file_number, sound_id_in_slot, sound_length,
                    play_position, voice_type, voice_id, voice_gender, final_volume, frequency
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                row,
            )
            count += 1

    return count


def import_extracted(cur: sqlite3.Cursor, manifest: dict | None) -> int:
    if not manifest or "entries" not in manifest:
        return 0

    inserted = 0
    for entry in manifest["entries"]:
        if entry.get("status") != "ok":
            continue
        cur.execute(
            """
            INSERT OR IGNORE INTO extracted_audio(
                group_name, model_id, model_name, voice_label, phrase_id, bank_id,
                bank_slot_id, sound_id, package_name, package_index, bank_header_offset,
                bank_size, sample_rate, loop_offset, headroom, pcm_offset, pcm_length, wav_path
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            (
                entry.get("group"),
                entry.get("model_id"),
                entry.get("model_name"),
                entry.get("voice_label"),
                entry.get("phrase_id"),
                entry.get("bank_id"),
                entry.get("bank_slot_id"),
                entry.get("sound_id"),
                entry.get("package_name"),
                entry.get("package_index"),
                entry.get("bank_header_offset"),
                entry.get("bank_size"),
                entry.get("sample_rate"),
                entry.get("loop_offset"),
                entry.get("headroom"),
                entry.get("pcm_offset"),
                entry.get("pcm_length"),
                entry.get("wav_path"),
            ),
        )
        inserted += 1
    return inserted


def ensure_translation_placeholders(cur: sqlite3.Cursor) -> None:
    cur.execute(
        """
        INSERT OR IGNORE INTO translations(group_name, model_id, voice_label, phrase_id, bank_id, sound_id, status)
        SELECT o.group_name, o.model_id, o.voice_label, o.phrase_id, o.bank_id, o.sound_id_in_slot, 'pending'
        FROM observations o
        """
    )


def build_db() -> dict:
    peds_payload = load_json(PEDS_COMPLETE_JSON) or []
    seed_payload = load_json(BALLAS_POLICE_SEED_JSON) or {}
    manifest_payload = load_json(EXTRACTED_MANIFEST)

    conn = sqlite3.connect(DB_PATH)
    try:
        cur = conn.cursor()
        cur.executescript(SCHEMA_SQL)
        reset_dynamic_tables(cur)

        import_peds(cur, peds_payload)
        populate_runtime_seed_texts(cur, peds_payload)
        import_seed(cur, seed_payload)
        observed_count = import_observations(cur, OBSERVED_LOG)
        extracted_count = import_extracted(cur, manifest_payload)
        ensure_translation_placeholders(cur)

        upsert_metadata(cur, "game_root", str(GAME_ROOT))
        upsert_metadata(cur, "workspace_dir", str(WORKSPACE_DIR))
        upsert_metadata(cur, "seed_json_path", str(BALLAS_POLICE_SEED_JSON))
        upsert_metadata(cur, "observed_log_path", str(OBSERVED_LOG))
        upsert_metadata(cur, "extracted_manifest_path", str(EXTRACTED_MANIFEST))

        conn.commit()

        summary = {
            "db_path": str(DB_PATH),
            "ped_models": cur.execute("SELECT COUNT(*) FROM ped_models").fetchone()[0],
            "ped_model_voices": cur.execute("SELECT COUNT(*) FROM ped_model_voices").fetchone()[0],
            "target_groups": cur.execute("SELECT COUNT(*) FROM target_groups").fetchone()[0],
            "candidate_audio_sets": cur.execute("SELECT COUNT(*) FROM candidate_audio_sets").fetchone()[0],
            "speech_semantics": cur.execute("SELECT COUNT(*) FROM speech_semantics").fetchone()[0],
            "observations": cur.execute("SELECT COUNT(*) FROM observations").fetchone()[0],
            "extracted_audio": cur.execute("SELECT COUNT(*) FROM extracted_audio").fetchone()[0],
            "runtime_seed_texts": cur.execute("SELECT COUNT(*) FROM runtime_seed_texts").fetchone()[0],
            "translations": cur.execute("SELECT COUNT(*) FROM translations").fetchone()[0],
            "observed_rows_read": observed_count,
            "extracted_rows_read": extracted_count,
        }
        return summary
    finally:
        conn.close()


def main() -> None:
    summary = build_db()
    print(json.dumps(summary, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
