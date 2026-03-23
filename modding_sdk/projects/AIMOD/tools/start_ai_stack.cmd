@echo off
setlocal

set "ROOT=%~dp0"
set "TTS_START=%ROOT%..\tts\start_tts_server.cmd"
set "BRIDGE_START=%ROOT%start_llm_bridge.cmd"

if not exist "%TTS_START%" (
  echo No se encontro start_tts_server.cmd
  exit /b 1
)

if not exist "%BRIDGE_START%" (
  echo No se encontro start_llm_bridge.cmd
  exit /b 1
)

start "AIMOD_TTS" /min cmd /c "\"%TTS_START%\""
timeout /t 2 >nul
start "AIMOD_LLM_BRIDGE" /min cmd /c "\"%BRIDGE_START%\""

echo AIMOD stack lanzado:
echo - TTS: http://127.0.0.1:5055
echo - LLM bridge: http://127.0.0.1:5056
echo - Llama server: opcional, esperado en http://127.0.0.1:8084
