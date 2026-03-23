# StreetSpeech-SA

Mod de subtítulos/burbujas de voz para `GTA San Andreas` orientado a speech en mundo abierto.

## Qué incluye este repo

- Plugin `.asi` en C++ con `plugin-sdk`
- Pipeline de observación de voces en runtime
- Extracción offline de audios observados
- Catálogo SQLite en `voice_workspace/catalog.db`
- Seeds de texto en base de datos para pruebas de burbujas

## Estructura

- `modding_sdk/projects/FenixImpossibleMod`
- `modding_sdk/tools`
- `modding_sdk/plugin-sdk`
- `voice_workspace`

## Estado actual

- Las burbujas usan `catalog.db` como fuente de textos semilla
- El logger ya captura observaciones de peds hablantes
- La extracción offline ya recupera varios `.wav` reales
- Policía/emergency speech todavía requiere un hook más profundo para resolver todos los bancos

## Nota

Este repo no incluye los assets completos del juego ni la instalación de GTA SA.
