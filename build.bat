@echo off
setlocal EnableExtensions

rem Native Windows counterpart to the Makefile. Keep generated files under build\.
cd /d "%~dp0"

if not defined BUILD_DIR set "BUILD_DIR=build"
if not defined ROM set "ROM=SuperMetroid.sfc"
set "DEBUG_DIR=%BUILD_DIR%\debug"
set "RELEASE_DIR=%BUILD_DIR%\release"

set "ACTION=%~1"
if not defined ACTION set "ACTION=build"

if /i "%ACTION%"=="help" goto :usage
if /i "%ACTION%"=="--help" goto :usage
if /i "%ACTION%"=="-h" goto :usage
if /i "%ACTION%"=="build" goto :build
if /i "%ACTION%"=="release" goto :release
if /i "%ACTION%"=="run" goto :run
if /i "%ACTION%"=="play" goto :play
if /i "%ACTION%"=="test" goto :test
if /i "%ACTION%"=="test-release" goto :test_release
if /i "%ACTION%"=="package" goto :package
if /i "%ACTION%"=="dist" goto :package
if /i "%ACTION%"=="clean" goto :clean

echo Unknown command: %ACTION% 1>&2
echo. 1>&2
goto :usage_error

:prepare
where cmake >nul 2>nul
if not errorlevel 1 (
  where ninja >nul 2>nul
  if not errorlevel 1 (
    where gcc >nul 2>nul
    if not errorlevel 1 exit /b 0
    where clang >nul 2>nul
    if not errorlevel 1 exit /b 0
    where cl >nul 2>nul
    if not errorlevel 1 if /i "%VSCMD_ARG_TGT_ARCH%"=="x64" exit /b 0
  )
)

rem An ordinary shell does not inherit the Visual Studio compiler environment.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto :missing_tools
set "VS_INSTALL="
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_INSTALL=%%I"
if not defined VS_INSTALL goto :missing_tools
if not exist "%VS_INSTALL%\VC\Auxiliary\Build\vcvarsall.bat" goto :missing_tools
call "%VS_INSTALL%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
if errorlevel 1 exit /b %errorlevel%

where cmake >nul 2>nul || goto :missing_tools
where ninja >nul 2>nul || goto :missing_tools
where cl >nul 2>nul || goto :missing_tools
if /i not "%VSCMD_ARG_TGT_ARCH%"=="x64" goto :missing_tools
exit /b 0

:missing_tools
echo CMake, Ninja, and a C11 compiler are required. 1>&2
echo Install Visual Studio with the Desktop development with C++ workload, 1>&2
echo including its CMake and Ninja components, and run this command again. 1>&2
exit /b 1

:configure_debug
call :prepare || exit /b %errorlevel%
cmake -S . -B "%DEBUG_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Debug %CMAKE_ARGS%
exit /b %errorlevel%

:configure_release
call :prepare || exit /b %errorlevel%
cmake -S . -B "%RELEASE_DIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release %CMAKE_ARGS%
exit /b %errorlevel%

:build
call :configure_debug || exit /b %errorlevel%
cmake --build "%DEBUG_DIR%"
exit /b %errorlevel%

:release
call :configure_release || exit /b %errorlevel%
cmake --build "%RELEASE_DIR%"
exit /b %errorlevel%

:run
call :build || exit /b %errorlevel%
"%DEBUG_DIR%\SuperMetroidX.exe" %ARGS% "%ROM%"
exit /b %errorlevel%

:play
call :release || exit /b %errorlevel%
"%RELEASE_DIR%\SuperMetroidX.exe" %ARGS% "%ROM%"
exit /b %errorlevel%

:test
call :build || exit /b %errorlevel%
cmake --build "%DEBUG_DIR%" --target smx_tests || exit /b %errorlevel%
ctest --test-dir "%DEBUG_DIR%" --output-on-failure --parallel 4 %CTEST_ARGS%
exit /b %errorlevel%

:test_release
call :release || exit /b %errorlevel%
ctest --test-dir "%RELEASE_DIR%" --output-on-failure --parallel 4 %CTEST_ARGS%
exit /b %errorlevel%

:package
call :release || exit /b %errorlevel%
ctest --test-dir "%RELEASE_DIR%" --output-on-failure %CTEST_ARGS% || exit /b %errorlevel%
cpack --config "%RELEASE_DIR%\CPackConfig.cmake" -C Release || exit /b %errorlevel%
echo Package in %RELEASE_DIR%\dist:
dir /b "%RELEASE_DIR%\dist" | findstr /v /x "_CPack_Packages"
exit /b %errorlevel%

:clean
call :prepare || exit /b %errorlevel%
cmake -E rm -rf "%BUILD_DIR%" "docs\html"
exit /b %errorlevel%

:usage
echo Usage: build.bat ^<command^>
echo.
echo Commands:
echo   build          Build Debug ^(the default^)
echo   release        Build Release
echo   run            Build Debug and run the game
echo   play           Build Release and run the game
echo   test           Build Debug and run the tests
echo   test-release   Build Release and run the tests
echo   package        Build and test Release, then create the Windows ZIP
echo   dist           Alias for package
echo   clean          Remove build output and generated documentation
echo   help           Show this help
echo.
echo Optional environment variables:
echo   CMAKE_ARGS     Extra CMake settings, such as -DSMX_EMBED_ROM=OFF
echo   CTEST_ARGS     Extra CTest arguments, such as -LE rom
echo   ROM            ROM passed to run or play ^(default: SuperMetroid.sfc^)
echo   ARGS           Additional game arguments
echo   BUILD_DIR      Build root ^(default: build^)
exit /b 0

:usage_error
call :usage
exit /b 2
