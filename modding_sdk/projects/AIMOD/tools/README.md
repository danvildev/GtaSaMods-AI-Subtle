# AIMOD Tools

Herramientas locales y separadas de `FenixImpossibleMod`.

## Objetivo

Dejar a `AIMOD` con una pila local seria para:

- `LLM local`: dialogo y decisiones via `llama.cpp`
- `STT local`: voz/microfono/transcripcion via `whisper.cpp`
- `TTS local`: voz en espanol y cache via Piper
- `TTS experimental`: clonacion/voz expresiva futura via `fish-speech`

## Estructura

- `vendor/llama.cpp`: motor de inferencia local para NPC chat
- `vendor/whisper.cpp`: transcripcion y voz local offline
- `vendor/piper1-gpl`: fuente de referencia del TTS actual del proyecto
- `vendor/fish-speech`: ruta experimental para voces mas expresivas
- `prompts/`: prompts de sistema para backend local
- `build_llama_cpp.cmd`: compila `llama-server`
- `build_whisper_cpp.cmd`: compila `whisper-cli` y `whisper-server`
- `start_llm_server.cmd`: arranca `llama-server` con un modelo GGUF
- `start_whisper_server.cmd`: arranca `whisper-server`
- `start_llm_bridge.cmd`: levanta el bridge local de dialogo
- `start_ai_stack.cmd`: levanta TTS + bridge local
- `transcribe_wav.cmd`: transcribe un `.wav` con `whisper-cli`
- `aimod_llm_bridge.py`: puente local para `AIMOD`, con fallback determinista
- `LLM_BRIDGE_API.md`: contrato del bridge para conectar el ASI
- `tool_manifest.json`: inventario de repos y commits clonados
- `AI_ROADMAP.md`: hoja de ruta de features vivas para la ciudad
- `MODEL_RECOMMENDATIONS.md`: guia rapida para elegir modelos locales

## Notas

- Estos repos estan clonados localmente para investigacion y builds.
- No se mezclan con el runtime de `FenixImpossibleMod`.
- Los builds y modelos pesados quedan ignorados por `.gitignore`.

## Flujo recomendado

1. Ejecutar `build_llama_cpp.cmd`
2. Ejecutar `build_whisper_cpp.cmd`
3. Colocar modelos en `tools/models/llm` y `tools/models/whisper`
4. Levantar `start_llm_server.cmd`
5. Levantar `aimod_llm_bridge.py`
6. Hacer que `AIMOD.SA.asi` consulte al bridge y no directo al modelo
