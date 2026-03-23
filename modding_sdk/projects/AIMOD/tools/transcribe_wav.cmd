@echo off
setlocal

set "ROOT=%~dp0"
set "MODEL=%~1"
set "AUDIO=%~2"

if "%MODEL%"=="" (
  echo Uso: transcribe_wav.cmd RUTA_MODELO_GGML_BIN RUTA_AUDIO_WAV
  exit /b 1
)

if "%AUDIO%"=="" (
  echo Uso: transcribe_wav.cmd RUTA_MODELO_GGML_BIN RUTA_AUDIO_WAV
  exit /b 1
)

set "EXE=%ROOT%build\whisper.cpp\bin\whisper-cli.exe"
if not exist "%EXE%" set "EXE=%ROOT%build\whisper.cpp\bin\Release\whisper-cli.exe"

if not exist "%EXE%" (
  echo whisper-cli.exe no encontrado. Ejecuta build_whisper_cpp.cmd primero.
  exit /b 1
)

if not exist "%MODEL%" (
  echo Modelo whisper no encontrado: %MODEL%
  exit /b 1
)

if not exist "%AUDIO%" (
  echo Audio no encontrado: %AUDIO%
  exit /b 1
)

"%EXE%" -m "%MODEL%" -f "%AUDIO%"
