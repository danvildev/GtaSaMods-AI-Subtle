AIMOD TTS local

1. Ejecuta start_tts_server.cmd
2. El servidor queda en http://127.0.0.1:5055
3. Voces Piper instaladas en .\voices
4. Cache WAV en .\cache

Endpoints:
- GET  /health
- GET  /voices
- GET  /ped/<model_id>
- POST /synthesize
- POST /synthesize-for-ped

Ejemplo JSON /synthesize-for-ped
{
  "model_id": 102,
  "text": "Que miras?"
}
