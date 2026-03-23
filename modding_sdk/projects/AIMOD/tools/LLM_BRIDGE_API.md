# AIMOD LLM Bridge API

Base URL:

- `http://127.0.0.1:5056`

## GET /health

Respuesta:

```json
{
  "ok": true,
  "service": "AIMOD LLM Bridge",
  "db_path": "D:/.../aimod_catalog.db",
  "llama_up": false
}
```

## POST /npc-chat

Request:

```json
{
  "npc_name": "BALLAS2",
  "group_name": "ballas",
  "profile_name": "Pandillero",
  "player_text": "hola compa",
  "memory": {
    "trust": 1,
    "anger": 0,
    "fear": 0,
    "respect": 0
  }
}
```

Response esperada:

```json
{
  "ok": true,
  "backend": "llama.cpp",
  "intent": "greet",
  "reaction": "friendly",
  "reply_es": "Que onda, carnal."
}
```

## Reglas del contrato

- `reply_es` debe ser corto
- `reaction` debe ser una de:
  - `friendly`
  - `neutral`
  - `warn`
  - `dismiss`
  - `refuse`
  - `flee`
  - `follow`
  - `attack`
- si `llama.cpp` no esta disponible, el bridge cae a `fallback`
- el ASI debe confiar en `reaction`, no en texto libre, para ejecutar comportamiento
