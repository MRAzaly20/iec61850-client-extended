@echo off

REM Get script directory
cd /d %~dp0

echo === Ensure build folder ===
if not exist build mkdir build

cd build

echo === Running CMake ===
cmake ..

echo === Building ===
mingw32-make -j8

echo === Running executable ===
iec61850_client.exe

pause
