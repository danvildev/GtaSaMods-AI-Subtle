from __future__ import annotations

import json
import sqlite3
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


ROOT = Path(__file__).resolve().parent
PROJECT_ROOT = ROOT.parent
DB_PATH = PROJECT_ROOT / "data" / "aimod_catalog.db"
PROMPTS_DIR = ROOT / "prompts"
LLAMA_BASE_URL = "http://127.0.0.1:8084"


def load_prompt(name: str) -> str:
    path = PROMPTS_DIR / name
    if path.exists():
        return path.read_text(encoding="utf-8")
    return ""


NPC_SYSTEM_PROMPT = load_prompt("npc_chat_system_prompt.txt")


def json_response(handler: BaseHTTPRequestHandler, payload: dict, status: int = 200) -> None:
    data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    handler.send_response(status)
    handler.send_header("Content-Type", "application/json; charset=utf-8")
    handler.send_header("Content-Length", str(len(data)))
    handler.end_headers()
    handler.wfile.write(data)


def normalize_text(text: str) -> str:
    return " ".join((text or "").strip().split())


def load_profile(group_name: str) -> dict:
    if not DB_PATH.exists():
        return {}

    with sqlite3.connect(DB_PATH) as conn:
        conn.row_factory = sqlite3.Row
        row = conn.execute(
            """
            SELECT group_name, profile_name, aggression, bravery, authority, warmth, sociability, loyalty
            FROM interaction_profiles
            WHERE group_name = ?
            """,
            (group_name,),
        ).fetchone()
        if row is None:
            row = conn.execute(
                """
                SELECT group_name, profile_name, aggression, bravery, authority, warmth, sociability, loyalty
                FROM interaction_profiles
                WHERE group_name = 'default'
                """
            ).fetchone()
        return dict(row) if row is not None else {}


def classify_intent(text: str) -> str:
    lower = text.lower()
    if any(word in lower for word in ("hola", "buenas", "que onda", "oye")):
        return "greet"
    if any(word in lower for word in ("calmate", "tranquilo", "paz", "baja")):
        return "calm"
    if any(word in lower for word in ("ven", "sigueme", "ayudame", "vente")):
        return "recruit"
    if any(word in lower for word in ("largo", "vete", "fuera", "dejame")):
        return "dismiss"
    if any(word in lower for word in ("te mato", "disparo", "rompo", "amenaza")):
        return "threaten"
    if any(word in lower for word in ("idiota", "pendejo", "perro", "imbecil", "mierda")):
        return "insult"
    return "ask"


def fallback_response(payload: dict) -> dict:
    player_text = normalize_text(payload.get("player_text", ""))
    group_name = payload.get("group_name", "default") or "default"
    npc_name = payload.get("npc_name", "peaton") or "peaton"
    profile = load_profile(group_name)
    intent = classify_intent(player_text)

    if intent == "insult":
        reaction = "attack" if profile.get("aggression", 2) >= 4 else "warn"
        reply = "Bajale dos, compa." if group_name in ("ballas", "gang") else "No me hables asi."
    elif intent == "threaten":
        reaction = "attack" if group_name == "police" else "flee"
        reply = "A ver si muy bravo." if group_name in ("ballas", "gang") else "Tranquilo, tranquilo."
    elif intent == "recruit":
        reaction = "follow" if profile.get("loyalty", 1) >= 3 else "refuse"
        reply = "Camara, voy contigo." if reaction == "follow" else "No me conviene meterme."
    elif intent == "calm":
        reaction = "neutral"
        reply = "Bueno, ya estuvo."
    elif intent == "greet":
        reaction = "friendly"
        reply = "Que onda." if group_name in ("ballas", "gang", "ambient") else "Buenas."
    elif intent == "dismiss":
        reaction = "dismiss"
        reply = "Como quieras."
    else:
        reaction = "neutral"
        reply = f"No se, {npc_name}, pero te escuche."

    return {
        "ok": True,
        "backend": "fallback",
        "intent": intent,
        "reaction": reaction,
        "reply_es": reply,
        "prompt_used": "fallback_rules"
    }


def llama_health() -> bool:
    try:
        with urllib.request.urlopen(f"{LLAMA_BASE_URL}/health", timeout=2) as response:
            return response.status == 200
    except Exception:
        return False


def llama_chat(payload: dict) -> dict | None:
    if not llama_health():
        return None

    body = {
        "model": payload.get("model", "local-gguf"),
        "temperature": 0.5,
        "top_p": 0.9,
        "max_tokens": 120,
        "response_format": {
            "type": "json_object"
        },
        "messages": [
            {
                "role": "system",
                "content": NPC_SYSTEM_PROMPT
            },
            {
                "role": "user",
                "content": json.dumps(payload, ensure_ascii=False)
            }
        ]
    }

    request = urllib.request.Request(
        f"{LLAMA_BASE_URL}/v1/chat/completions",
        data=json.dumps(body, ensure_ascii=False).encode("utf-8"),
        headers={"Content-Type": "application/json"},
        method="POST",
    )

    try:
        with urllib.request.urlopen(request, timeout=20) as response:
            raw = json.loads(response.read().decode("utf-8"))
    except (urllib.error.URLError, json.JSONDecodeError, TimeoutError):
        return None

    try:
        content = raw["choices"][0]["message"]["content"]
        parsed = json.loads(content)
    except Exception:
        return None

    if "reply_es" not in parsed:
        return None

    parsed.setdefault("ok", True)
    parsed.setdefault("backend", "llama.cpp")
    parsed.setdefault("intent", classify_intent(payload.get("player_text", "")))
    parsed.setdefault("reaction", "neutral")
    return parsed


class AimodHandler(BaseHTTPRequestHandler):
    def do_GET(self) -> None:
        if self.path == "/health":
            json_response(
                self,
                {
                    "ok": True,
                    "service": "AIMOD LLM Bridge",
                    "db_path": str(DB_PATH),
                    "llama_up": llama_health(),
                },
            )
            return

        json_response(self, {"ok": False, "error": "not_found"}, status=404)

    def do_POST(self) -> None:
        if self.path != "/npc-chat":
            json_response(self, {"ok": False, "error": "not_found"}, status=404)
            return

        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length) if length > 0 else b"{}"

        try:
            payload = json.loads(body.decode("utf-8"))
        except json.JSONDecodeError:
            json_response(self, {"ok": False, "error": "invalid_json"}, status=400)
            return

        payload["player_text"] = normalize_text(payload.get("player_text", ""))
        if not payload["player_text"]:
            json_response(self, {"ok": False, "error": "empty_player_text"}, status=400)
            return

        result = llama_chat(payload) or fallback_response(payload)
        json_response(self, result)


def main() -> None:
    server = ThreadingHTTPServer(("127.0.0.1", 5056), AimodHandler)
    print("AIMOD LLM Bridge listening on http://127.0.0.1:5056")
    server.serve_forever()


if __name__ == "__main__":
    main()
