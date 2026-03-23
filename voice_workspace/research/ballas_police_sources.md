# Ballas y policia: semilla de investigacion

Este archivo resume lo que ya pude consolidar para no arrancar desde cero.

## Lo seguro

- Los modelos de Ballas y sus voces salen de [peds.ide](d:/OldPCGames/Gta%20San%20Andreas%20-%20TheFenix010/data/peds.ide).
- Los modelos de policia y sus voces tambien salen de [peds.ide](d:/OldPCGames/Gta%20San%20Andreas%20-%20TheFenix010/data/peds.ide).
- El formato tecnico de extraccion `package -> bank -> sound -> wav` esta documentado en `SFX (SA)` y ya esta implementado en nuestro extractor.

## Lo parcialmente clasificado por la comunidad

- El hilo `GTA: San Andreas - Sounds List` menciona:
  - `SPC_NA` como paquete de voces de pandillas y algunos personajes.
  - `Bank_011` como `Ballas taunts from Cleaning the Hood`.
  - `Bank_171` como `Officer in need of backup`.
  - `Sound_094-121` como gritos relacionados con persecucion policial.
  - `Bank_053 sound_025` y `Bank_137 sound_011` como audio de `police dispatch`.
- El hilo `GTA San Andreas Speech ID List` lista meanings semanticos de varios speech IDs de voces de gang y contexto de arresto/insultos/reacciones.
- El directorio `GTA:SA SFX Directory by pdescobar` aporta clasificacion parcial de bancos, offsets y cantidad de sonidos por archivo SFX.

## Lo que sigue siendo ambiguo

- No hay una base publica completa y limpia `ped -> bank -> sound -> texto original`.
- Algunas entradas del hilo de sonidos describen `dispatcher/radio` y no siempre `ped speech` directo del policia/NPC.
- `Bank_011` de Ballas parece real, pero puede ser mas de mision que de ambient free-roam.

## Estrategia recomendada

1. Extraer offline todo lo observado por el mod.
2. Priorizar primero:
   - Ballas
   - policia
3. Escuchar y etiquetar:
   - `ped_speech`
   - `dispatch_radio`
   - `mission_only`
4. Pasar eso despues a `catalog.db`.

## Fuentes

- GTAMods SFX (SA): https://gtamods.com/wiki/SFX_(SA)
- GTAForums Sounds List: https://gtaforums.com/topic/923407-gta-san-andreas-sounds-list/
- GTAForums Speech ID List: https://gtaforums.com/topic/505421-gta-san-andreas-speech-id-list/
- pdescobar SFX Directory: https://www.zazmahall.de/ZAZGTASANATORIUM/GTASA_SFX_Directory.htm
