@echo off
echo Building IFC2GLTF Project...

REM 设置VS2022路径
set VS_PATH="D:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE"
set MSBUILD_PATH="D:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin"

REM 添加MSBuild到PATH
set PATH=%MSBUILD_PATH%;%PATH%

REM 编译项目
echo Compiling Release x64...
msbuild LoadFileExample.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143

if %ERRORLEVEL% EQU 0 (
    echo Build succeeded!
    echo Executable location: bin\LoadFileExample.exe
) else (
    echo Build failed with error code %ERRORLEVEL%
)

pause