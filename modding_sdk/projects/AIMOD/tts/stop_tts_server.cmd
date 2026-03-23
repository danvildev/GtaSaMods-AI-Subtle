@echo off
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "Get-CimInstance Win32_Process | Where-Object { $_.Name -eq 'python.exe' -and $_.CommandLine -like '*aimod_tts_server.py*' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }"
