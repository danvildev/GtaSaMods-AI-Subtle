from __future__ import annotations

import argparse
import hashlib
import json
import os
import sqlite3
import threading
import wave
from pathlib import Path

import numpy as np
from flask import Flask, jsonify, request
from piper.config import SynthesisConfig
from piper.voice import PiperVoice


ROOT = Path(__file__).resolve().parent
PROJECT_ROOT = ROOT.parent
DB_PATH = PROJECT_ROOT / "data" / "aimod_catalog.db"
VOICES_DIR = ROOT / "voices"
CACHE_DIR = ROOT / "cache"
HF_CACHE_DIR = ROOT / "hf_cache"

os.environ.setdefault("HF_HOME", str(HF_CACHE_DIR))
os.environ.setdefault("HF_HUB_CACHE", str(HF_CACHE_DIR / "hub"))

try:
    from kokoro import KPipeline
    KOKORO_AVAILABLE = True
except Exception:
    KPipeline = None
    KOKORO_AVAILABLE = False

app = Flask(__name__)
app.config["JSON_AS_ASCII"] = False

_voice_lock = threading.Lock()
_loaded_piper_voices: dict[str, PiperVoice] = {}
_loaded_kokoro_pipelines: dict[str, KPipeline] = {}


def get_db() -> sqlite3.Connection:
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    return conn


def normalize_text(text: str) -> str:
    return " ".join((text or "").strip().split())


def resolve_voice_model_path(sample_path: str | None, voice_id: str) -> Path:
    if sample_path:
        raw_path = Path(sample_path)
        candidates = []
        if raw_path.is_absolute():
            candidates.append(raw_path)
        else:
            candidates.append(ROOT / raw_path)
            candidates.append(PROJECT_ROOT / raw_path)
        for candidate in candidates:
            if candidate.exists():
                return candidate
    return VOICES_DIR / f"{voice_id}.onnx"


def get_voice_row(voice_id: str) -> sqlite3.Row | None:
    with get_db() as conn:
        return conn.execute(
            """
            SELECT voice_id, engine, display_name, language, speaker_ref, sample_path, enabled
            FROM tts_voices
            WHERE voice_id = ? AND enabled = 1
            """,
            (voice_id,),
        ).fetchone()


def get_assignment_row(model_id: int) -> sqlite3.Row | None:
    with get_db() as conn:
        return conn.execute(
            """
            SELECT a.model_id, a.group_name, a.voice_id, a.pitch, a.speed, m.model_name
            FROM ped_tts_assignments a
            LEFT JOIN ped_models m ON m.model_id = a.model_id
            WHERE a.model_id = ?
            """,
            (model_id,),
        ).fetchone()


def normalize_kokoro_lang(language: str | None) -> str:
    value = (language or "es").strip().lower()
    if value in {"es", "es-es", "es-mx", "es-ar", "spanish"}:
        return "e"
    return value[:1] or "e"


def resolve_kokoro_voice_ref(voice_row: sqlite3.Row, voice_id: str) -> str:
    sample_path = str(voice_row["sample_path"] or "").strip()
    if sample_path:
        return sample_path
    speaker_ref = str(voice_row["speaker_ref"] or "").strip()
    if speaker_ref:
        return speaker_ref
    return voice_id


def get_or_load_piper_voice(voice_id: str) -> PiperVoice:
    with _voice_lock:
        voice_row = get_voice_row(voice_id)
        if voice_row is None:
            raise KeyError(f"Unknown voice_id: {voice_id}")

        model_path = resolve_voice_model_path(voice_row["sample_path"], voice_id)
        if not model_path.exists():
            raise FileNotFoundError(f"Missing model file: {model_path}")

        cache_key = str(model_path).lower()
        cached = _loaded_piper_voices.get(cache_key)
        if cached is not None:
            return cached

        config_path = Path(f"{model_path}.json")
        if not config_path.exists():
            raise FileNotFoundError(f"Missing model config: {config_path}")

        loaded = PiperVoice.load(model_path=model_path, config_path=config_path, use_cuda=False)
        _loaded_piper_voices[cache_key] = loaded
        return loaded


def get_or_load_kokoro_pipeline(language: str | None) -> KPipeline:
    if not KOKORO_AVAILABLE or KPipeline is None:
        raise RuntimeError("Kokoro no esta disponible en este entorno")

    lang_code = normalize_kokoro_lang(language)
    with _voice_lock:
        cached = _loaded_kokoro_pipelines.get(lang_code)
        if cached is not None:
            return cached
        pipeline = KPipeline(lang_code=lang_code, repo_id="hexgrad/Kokoro-82M", device="cpu")
        _loaded_kokoro_pipelines[lang_code] = pipeline
        return pipeline


def make_cache_key(voice_row: sqlite3.Row, voice_id: str, text: str, speed: float, pitch: float) -> str:
    cache_payload = json.dumps(
        {
            "voice_id": voice_id,
            "engine": str(voice_row["engine"] or "piper"),
            "voice_ref": str(voice_row["sample_path"] or ""),
            "text": normalize_text(text),
            "speed": round(speed, 3),
            "pitch": round(pitch, 3),
        },
        ensure_ascii=False,
        sort_keys=True,
    )
    return hashlib.sha1(cache_payload.encode("utf-8")).hexdigest()


def write_wav_file(wav_path: Path, sample_rate: int, audio_float: np.ndarray) -> None:
    wav_path.parent.mkdir(parents=True, exist_ok=True)
    audio_i16 = np.clip(audio_float, -1.0, 1.0)
    audio_i16 = (audio_i16 * 32767.0).astype(np.int16)

    with wave.open(str(wav_path), "wb") as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)
        wav_file.writeframes(audio_i16.tobytes())


def synthesize_with_piper(voice_row: sqlite3.Row, voice_id: str, text: str, speed: float) -> tuple[np.ndarray, int]:
    voice = get_or_load_piper_voice(voice_id)
    length_scale = round(max(0.7, min(1.4, 1.0 / max(0.5, speed))), 3)
    syn_config = SynthesisConfig(length_scale=length_scale)

    chunks = list(voice.synthesize(text, syn_config=syn_config))
    if not chunks:
        raise RuntimeError("Piper returned no audio chunks")

    sample_rate = chunks[0].sample_rate
    audio = np.concatenate([chunk.audio_float_array for chunk in chunks])
    return audio, sample_rate


def synthesize_with_kokoro(voice_row: sqlite3.Row, voice_id: str, text: str, speed: float) -> tuple[np.ndarray, int]:
    pipeline = get_or_load_kokoro_pipeline(voice_row["language"])
    voice_ref = resolve_kokoro_voice_ref(voice_row, voice_id)
    audio_parts: list[np.ndarray] = []
    for result in pipeline(text, voice=voice_ref, speed=max(0.7, min(1.3, speed)), split_pattern=r"\n+"):
        if result.audio is not None:
            audio_parts.append(result.audio.numpy())
    if not audio_parts:
        raise RuntimeError(f"Kokoro no genero audio para {voice_ref}")
    return np.concatenate(audio_parts), 24000


def synthesize_wav(voice_id: str, text: str, speed: float, pitch: float) -> tuple[Path, bool, int]:
    normalized_text = normalize_text(text)
    if not normalized_text:
        raise ValueError("Text is empty")

    voice_row = get_voice_row(voice_id)
    if voice_row is None:
        raise KeyError(f"Unknown voice_id: {voice_id}")

    cache_key = make_cache_key(voice_row, voice_id, normalized_text, speed, pitch)
    relative_wav = Path("cache") / voice_id / f"{cache_key}.wav"
    absolute_wav = ROOT / relative_wav

    with get_db() as conn:
        row = conn.execute(
            """
            SELECT id, wav_path
            FROM tts_cache
            WHERE voice_id = ? AND text_hash = ?
            """,
            (voice_id, cache_key),
        ).fetchone()
        if row is not None:
            cached_path = ROOT / row["wav_path"]
            if cached_path.exists():
                conn.execute(
                    """
                    UPDATE tts_cache
                    SET last_used_at = CURRENT_TIMESTAMP,
                        use_count = use_count + 1
                    WHERE id = ?
                    """,
                    (row["id"],),
                )
                conn.commit()
                return cached_path, True, 0

    engine = str(voice_row["engine"] or "piper").strip().lower()
    if engine == "kokoro":
        audio, sample_rate = synthesize_with_kokoro(voice_row, voice_id, normalized_text, speed)
    else:
        audio, sample_rate = synthesize_with_piper(voice_row, voice_id, normalized_text, speed)
    write_wav_file(absolute_wav, sample_rate, audio)

    with get_db() as conn:
        conn.execute(
            """
            INSERT INTO tts_cache (
                voice_id, text_hash, text_es, wav_path, created_at, last_used_at, use_count
            ) VALUES (
                ?, ?, ?, ?, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP, 1
            )
            ON CONFLICT(voice_id, text_hash) DO UPDATE SET
                text_es = excluded.text_es,
                wav_path = excluded.wav_path,
                last_used_at = CURRENT_TIMESTAMP,
                use_count = tts_cache.use_count + 1
            """,
            (voice_id, cache_key, normalized_text, relative_wav.as_posix()),
        )
        conn.commit()

    return absolute_wav, False, sample_rate


@app.get("/health")
def health():
    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    HF_CACHE_DIR.mkdir(parents=True, exist_ok=True)
    return jsonify(
        {
            "ok": True,
            "service": "AIMOD Hybrid TTS",
            "db_path": str(DB_PATH),
            "voices_dir": str(VOICES_DIR),
            "cache_dir": str(CACHE_DIR),
            "hf_cache_dir": str(HF_CACHE_DIR),
            "kokoro_available": KOKORO_AVAILABLE,
            "loaded_piper_models": sorted(_loaded_piper_voices.keys()),
            "loaded_kokoro_pipelines": sorted(_loaded_kokoro_pipelines.keys()),
        }
    )


@app.get("/voices")
def voices():
    with get_db() as conn:
        rows = conn.execute(
            """
            SELECT voice_id, engine, display_name, language, speaker_ref, sample_path, enabled
            FROM tts_voices
            WHERE enabled = 1
            ORDER BY voice_id
            """
        ).fetchall()

    payload = []
    for row in rows:
        model_path = resolve_voice_model_path(row["sample_path"], row["voice_id"])
        payload.append(
            {
                "voice_id": row["voice_id"],
                "engine": row["engine"],
                "display_name": row["display_name"],
                "language": row["language"],
                "speaker_ref": row["speaker_ref"],
                "model_path": str(model_path) if row["engine"] == "piper" else str(resolve_kokoro_voice_ref(row, row["voice_id"])),
                "model_exists": model_path.exists() if row["engine"] == "piper" else KOKORO_AVAILABLE,
            }
        )

    return jsonify(payload)


@app.get("/ped/<int:model_id>")
def ped_assignment(model_id: int):
    row = get_assignment_row(model_id)
    if row is None:
        return jsonify({"ok": False, "error": "ped_assignment_not_found", "model_id": model_id}), 404

    return jsonify(
        {
            "ok": True,
            "model_id": model_id,
            "model_name": row["model_name"],
            "group_name": row["group_name"],
            "voice_id": row["voice_id"],
            "pitch": row["pitch"],
            "speed": row["speed"],
        }
    )


@app.post("/synthesize")
def synthesize():
    payload = request.get_json(force=True, silent=False) or {}
    voice_id = str(payload.get("voice_id") or "").strip()
    text = str(payload.get("text") or "")
    speed = float(payload.get("speed") or 1.0)
    pitch = float(payload.get("pitch") or 1.0)

    if not voice_id:
        return jsonify({"ok": False, "error": "missing_voice_id"}), 400

    try:
        wav_path, cached, sample_rate = synthesize_wav(voice_id, text, speed, pitch)
    except Exception as exc:
        return jsonify({"ok": False, "error": str(exc)}), 500

    return jsonify(
        {
            "ok": True,
            "cached": cached,
            "voice_id": voice_id,
            "text": normalize_text(text),
            "wav_path": str(wav_path),
            "sample_rate": sample_rate,
        }
    )


@app.post("/synthesize-for-ped")
def synthesize_for_ped():
    payload = request.get_json(force=True, silent=False) or {}
    raw_model_id = payload.get("model_id", -1)
    model_id = int(raw_model_id)
    text = str(payload.get("text") or "")

    row = get_assignment_row(model_id)
    if row is None:
        return jsonify({"ok": False, "error": "ped_assignment_not_found", "model_id": model_id}), 404

    voice_id = str(row["voice_id"])
    speed = float(row["speed"] or 1.0)
    pitch = float(row["pitch"] or 1.0)

    try:
        wav_path, cached, sample_rate = synthesize_wav(voice_id, text, speed, pitch)
    except Exception as exc:
        return jsonify({"ok": False, "error": str(exc)}), 500

    return jsonify(
        {
            "ok": True,
            "cached": cached,
            "model_id": model_id,
            "model_name": row["model_name"],
            "group_name": row["group_name"],
            "voice_id": voice_id,
            "pitch": pitch,
            "speed": speed,
            "text": normalize_text(text),
            "wav_path": str(wav_path),
            "sample_rate": sample_rate,
        }
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="AIMOD local Piper TTS server")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5055)
    parser.add_argument("--debug", action="store_true")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    HF_CACHE_DIR.mkdir(parents=True, exist_ok=True)
    app.run(host=args.host, port=args.port, debug=args.debug, threaded=True)


if __name__ == "__main__":
    main()
