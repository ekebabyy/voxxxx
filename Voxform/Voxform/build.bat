@echo off
rem Builds Voxform (VST3 + standalone app) on Windows.
rem Requirements: Visual Studio 2022 (or newer) with "Desktop development with C++", and CMake 3.22+.
rem Run this from the project folder (a "Developer Command Prompt" is not required).
setlocal

where cmake >nul 2>nul
if errorlevel 1 (
    echo CMake was not found. Install it from https://cmake.org/download/ and tick "Add CMake to PATH".
    exit /b 1
)

cmake -B build -A x64 || exit /b 1
cmake --build build --config Release --parallel || exit /b 1

echo.
echo ==================================================================
echo  Build finished.
echo    VST3 plugin : build\Voxform_artefacts\Release\VST3\Voxform.vst3
echo    Standalone  : build\Voxform_artefacts\Release\Standalone\Voxform.exe
echo.
echo  To install the plugin, copy the whole "Voxform.vst3" folder to
echo    C:\Program Files\Common Files\VST3\
echo  (needs administrator rights), then rescan plugins in your DAW.
echo ==================================================================
endlocal
