# StreetSpeech-SA

Mod de subtitulos y burbujas de voz para `GTA San Andreas`, orientado a speech en mundo abierto.

## Que incluye este repo

- Plugin `.asi` en C++ con `plugin-sdk`
- Pipeline de observacion de voces en runtime
- Extraccion offline de audios observados
- Catalogo SQLite embebido por mod, por ejemplo `modding_sdk/projects/AIMOD/data/catalog.db`
- Seeds de texto en base de datos para pruebas de burbujas

## Estructura

- `modding_sdk/projects/FenixImpossibleMod`
- `modding_sdk/projects/AIMOD`
- `modding_sdk/tools`
- `modding_sdk/plugin-sdk`
- `voice_workspace`

## Estado actual

- `AIMOD` usa su propia base en `modding_sdk/projects/AIMOD/data/catalog.db`
- Las burbujas ya pueden leer textos semilla desde SQLite
- El logger ya captura observaciones de peds hablantes
- La extraccion offline ya recupera varios `.wav` reales
- Police/emergency speech todavia requiere un hook mas profundo para resolver todos los bancos

## Nota

Este repo no incluye los assets completos del juego ni la instalacion de GTA SA.
