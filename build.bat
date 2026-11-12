@echo off
setlocal
rem Set IDF_PATH or IDF_PYTHON_ENV_PATH to override automatic discovery.
if not defined IDF_PATH for /d %%D in ("%USERPROFILE%\esp\esp-idf*" "%ProgramFiles%\Espressif\frameworks\esp-idf*" "C:\Espressif\frameworks\esp-idf*") do if not defined IDF_PATH if exist "%%~fD\export.bat" set "IDF_PATH=%%~fD"
if not exist "%IDF_PATH%\export.bat" (
    echo ESP-IDF not found. Set IDF_PATH to the directory containing export.bat. 1>&2
    exit /b 1
)

if not defined IDF_PYTHON_ENV_PATH for /d %%D in ("%IDF_PATH%\..\..\python_env\*" "%IDF_TOOLS_PATH%\python_env\*" "%USERPROFILE%\.espressif\python_env\*") do if not defined IDF_PYTHON_ENV_PATH if exist "%%~fD\Scripts\python.exe" set "IDF_PYTHON_ENV_PATH=%%~fD"
if defined IDF_PYTHON_ENV_PATH if exist "%IDF_PYTHON_ENV_PATH%\Scripts" set "PATH=%IDF_PYTHON_ENV_PATH%\Scripts;%PATH%"
where git.exe >nul 2>&1
if errorlevel 1 for %%D in ("%ProgramFiles%\Git\cmd" "%ProgramFiles(x86)%\Git\cmd" "%LOCALAPPDATA%\Programs\Git\cmd") do if exist "%%~fD\git.exe" set "PATH=%%~fD;%PATH%"

call "%IDF_PATH%\export.bat"
if errorlevel 1 exit /b 1
python "%~dp0script\build_tools\build.py"
exit /b %errorlevel%
