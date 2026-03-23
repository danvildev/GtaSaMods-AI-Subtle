@echo off
setlocal

set "ROOT=%~dp0"
set "MODEL=%~1"
set "PORT=%~2"

if "%PORT%"=="" set "PORT=8091"
if "%MODEL%"=="" (
  echo Uso: start_whisper_server.cmd RUTA_MODELO_GGML_BIN [PUERTO]
  exit /b 1
)

set "EXE=%ROOT%build\whisper.cpp\bin\whisper-server.exe"
if not exist "%EXE%" set "EXE=%ROOT%build\whisper.cpp\bin\Release\whisper-server.exe"

if not exist "%EXE%" (
  echo whisper-server.exe no encontrado. Ejecuta build_whisper_cpp.cmd primero.
  exit /b 1
)

if not exist "%MODEL%" (
  echo Modelo whisper no encontrado: %MODEL%
  exit /b 1
)

"%EXE%" -m "%MODEL%" --host 127.0.0.1 --port %PORT%
