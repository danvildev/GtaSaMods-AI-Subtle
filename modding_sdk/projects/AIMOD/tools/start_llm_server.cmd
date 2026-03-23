@echo off
setlocal

set "ROOT=%~dp0"
set "MODEL=%~1"
set "PORT=%~2"

if "%PORT%"=="" set "PORT=8084"
if "%MODEL%"=="" (
  echo Uso: start_llm_server.cmd RUTA_AL_MODELO_GGUF [PUERTO]
  exit /b 1
)

set "EXE=%ROOT%build\llama.cpp\bin\llama-server.exe"
if not exist "%EXE%" set "EXE=%ROOT%build\llama.cpp\bin\Release\llama-server.exe"

if not exist "%EXE%" (
  echo llama-server.exe no encontrado. Ejecuta build_llama_cpp.cmd primero.
  exit /b 1
)

if /I "%MODEL:~0,3%"=="hf:" (
  set "HF_REPO=%MODEL:~3%"
  "%EXE%" -hf "%HF_REPO%" --host 127.0.0.1 --port %PORT% -c 4096 -ngl 0 --jinja
  exit /b %errorlevel%
)

if not exist "%MODEL%" (
  echo Modelo no encontrado: %MODEL%
  exit /b 1
)

"%EXE%" -m "%MODEL%" --host 127.0.0.1 --port %PORT% -c 4096 -ngl 0 --jinja
