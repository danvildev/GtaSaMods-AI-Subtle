# Model Recommendations

## LLM local para NPC chat

Objetivo:

- respuestas cortas
- tono callejero y coherente
- uso razonable en CPU

Recomendaciones practicas:

- `3B-4B instruct GGUF Q4_K_M`: mejor equilibrio para CPU
- `1B-2B instruct GGUF`: bueno para pruebas rapidas
- `7B instruct GGUF`: mejor calidad, pero ya mas pesado

Lo importante para `AIMOD` no es solo el modelo, sino el formato:

- `GGUF`
- cuantizacion `Q4_K_M` o parecida si vas por CPU
- server via `llama-server`

## STT local

Para `whisper.cpp`:

- `base`: buen punto de partida
- `small`: mejor calidad, mas pesado
- `tiny`: solo para pruebas rapidas

## TTS

Hoy:

- Piper para runtime estable y cacheable

Futuro:

- `fish-speech` si quieres clonacion de voz y mas expresividad
