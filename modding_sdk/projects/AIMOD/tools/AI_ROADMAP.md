# AIMOD AI Roadmap

## Meta

Volver San Andreas una ciudad que reacciona, recuerda y propaga consecuencias.

## Capa 1: Conversacion local consistente

- `CJ` habla con voz fija y texto libre
- cada ped responde segun `grupo`, `perfil`, `memoria` y `contexto`
- backend local via `llama.cpp`
- fallback determinista si el LLM no esta disponible

## Capa 2: Ciudad vivida

- memoria por grupo y por barrio
- reputacion local del jugador
- testigos que difunden rumores
- policias que escalan segun historial, no solo accion instantanea
- pandillas que recuerdan insultos, favores y agresiones

## Capa 3: Conversaciones encadenadas

- peds cercanos reaccionan a una charla hostil
- amigos del objetivo apoyan o se alejan
- vendedores, vagos, prostitutas y polis usan tonos distintos
- ambient gossip: los peatones comentan peleas, disparos, persecuciones y fama

## Capa 4: Voz viva

- TTS local cacheado por `voice_id + text_hash`
- mas perfiles mexicanos para vagos y pandillas
- voces de autoridad para policias y seguridad
- opcion premium futura con `fish-speech` para clonacion/expresividad

## Capa 5: Voz del jugador

- `whisper.cpp` para microfono local
- comando de voz opcional
- transcripcion a texto
- CJ responde con su propia voz TTS fija

## Capa 6: Director de ciudad

- bridge local que mantenga:
  - temas del momento
  - nivel de tension del barrio
  - eventos recientes
  - facciones activas
- eso alimenta el prompt de cada ped para respuestas mas coherentes

## Reglas de consistencia

- el LLM nunca decide tareas del juego directamente
- el LLM devuelve `intencion`, `tono`, `respuesta` y `riesgo`
- el ASI traduce eso a comportamiento seguro usando el SDK
- si el LLM falla, se vuelve al sistema predefinido actual
