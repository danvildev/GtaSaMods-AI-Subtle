GTA SA Ped Voice Workspace
==========================

Game root: D:\OldPCGames\Gta San Andreas - TheFenix010
Source: D:\OldPCGames\Gta San Andreas - TheFenix010\data\peds.ide
Total peds parsed: 276
Peds with voice tokens: 266
Unique voice tokens: 270

Important SDK references:
- modding_sdk/plugin-sdk/Plugin_SA/game_sa/CPedModelInfo.h
- modding_sdk/plugin-sdk/Plugin_SA/game_sa/CAEPedSpeechAudioEntity.h
- data/peds.ide

Structure:
- peds/<id_name>/ped.json
- peds/<id_name>/dictionary_es.txt
- peds/<id_name>/observed_phrase_ids.txt
- voices/<voice_token>/voice.json
- voices/<voice_token>/dictionary_es.txt
- exports/peds_complete.json
- exports/peds_complete.csv
- exports/voices_index.json

Recommended next step:
1. Usa el mod para loguear phraseId, ped model y voice token.
2. Mete los audios extraidos en la carpeta audio_raw correspondiente.
3. Traduce primero por voice set y luego ajusta excepciones por ped.
