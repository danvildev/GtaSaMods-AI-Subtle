@echo off
setlocal
set "ROOT=%~dp0"
set "PYTHON=%ROOT%.venv\Scripts\python.exe"

if not exist "%PYTHON%" (
    echo No se encontro Python del entorno local: %PYTHON%
    exit /b 1
)

"%PYTHON%" "%ROOT%sync_tts_catalog.py"
if errorlevel 1 exit /b %errorlevel%

"%PYTHON%" "%ROOT%..\\tools\\seed_dialogue_content.py"
if errorlevel 1 exit /b %errorlevel%

"%PYTHON%" "%ROOT%aimod_tts_server.py" --host 127.0.0.1 --port 5055
