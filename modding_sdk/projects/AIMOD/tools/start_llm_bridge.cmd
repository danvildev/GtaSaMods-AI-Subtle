@echo off
setlocal

set "ROOT=%~dp0"
set "PYTHON=%ROOT%..\tts\.venv\Scripts\python.exe"

if not exist "%PYTHON%" set "PYTHON=python"

"%PYTHON%" "%ROOT%aimod_llm_bridge.py"
