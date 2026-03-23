# AIMOD

Proyecto independiente de `FenixImpossibleMod`.

## Que hace ahora

- Burbujas de dialogo desde `aimod_catalog.db`
- Sistema de interaccion con `E` sobre peatones
- Chat libre con `TAB` dentro de la interaccion
- Memoria social por ped y por grupo
- Respuestas y perfiles desde SQLite
- TTS local con servidor propio dentro de `AIMOD/tts`
- Voz fija de CJ y reproduccion secuencial de voz para lo que dice el jugador y el ped via cache `.wav`
- Recarga en caliente de BD con `F5`
- Verificacion/reinicio del TTS con `F6`

## Estructura

- `source/Main.cpp`: runtime principal del ASI
- `data/aimod_catalog.db`: base de datos del mod
- `data/logs`: observaciones del runtime
- `tts/voices`: modelos locales Piper
- `tts/cache`: audio sintetizado reutilizable
- `tts/aimod_tts_server.py`: servidor local TTS
- `tts/sync_tts_catalog.py`: pobla voces y asignaciones TTS
- `tools/aimod_llm_bridge.py`: puente local para dialogo inteligente
- `tools/vendor`: repos locales clonados para LLM/STT/TTS
- `tools/prompts`: prompts base para la capa IA futura

## Arranque del TTS

1. Ejecutar `tts/start_tts_server.cmd`
2. El servicio queda en `http://127.0.0.1:5055`
3. El ASI usa `POST /synthesize-for-ped` para respuestas de interaccion
4. Si el servidor no esta arriba, el ASI intenta levantarlo solo

## Stack local futuro

- `llama.cpp`: backend local de dialogo y decisiones
- `whisper.cpp`: transcripcion local de voz y audio
- `Piper`: TTS actual del mod
- `fish-speech`: opcion experimental para voces mas expresivas

Ver:

- `tools/README.md`
- `tools/AI_ROADMAP.md`
- `tools/tool_manifest.json`

## Controles

- `E`: abrir/cerrar interaccion con el ped objetivo
- `1..7`: acciones rapidas predefinidas
- `TAB`: abrir chat libre
- `Enter`: enviar texto escrito
- `Backspace`: borrar dentro del chat libre
- `Esc`: cerrar chat o interaccion
- `F5`: recargar `aimod_catalog.db`
- `F6`: verificar/relanzar TTS local

## Nota

`AIMOD` solo usa `data/aimod_catalog.db`. No depende de `voice_workspace` ni de la DB vieja `catalog.db`.
