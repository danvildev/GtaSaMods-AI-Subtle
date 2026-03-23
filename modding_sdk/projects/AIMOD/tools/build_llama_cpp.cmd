@echo off
setlocal

set "ROOT=%~dp0"
set "VSVARS=C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvarsall.bat"
set "CMAKE=C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "SRC=%ROOT%vendor\llama.cpp"
set "BUILD=%ROOT%build\llama.cpp"

if not exist "%VSVARS%" (
  echo No se encontro vcvarsall.bat
  exit /b 1
)

if not exist "%CMAKE%" (
  echo No se encontro cmake.exe
  exit /b 1
)

if not exist "%SRC%\CMakeLists.txt" (
  echo No se encontro llama.cpp en tools\vendor
  exit /b 1
)

call "%VSVARS%" x64
if errorlevel 1 exit /b 1

"%CMAKE%" -S "%SRC%" -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1

"%CMAKE%" --build "%BUILD%" --config Release --target llama-server -j 4
if errorlevel 1 exit /b 1

echo llama.cpp listo en %BUILD%
