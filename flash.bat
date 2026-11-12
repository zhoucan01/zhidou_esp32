@echo off
setlocal
rem All arguments are forwarded, for example: flash.bat --port COM4 --dry-run
rem ESP_FLASH_PYTHON can point to any Python environment containing esptool.
if defined ESP_FLASH_PYTHON goto run
if defined IDF_PYTHON_ENV_PATH if exist "%IDF_PYTHON_ENV_PATH%\Scripts\python.exe" (
    set "ESP_FLASH_PYTHON=%IDF_PYTHON_ENV_PATH%\Scripts\python.exe"
    goto run
)
if exist "C:\Espressif\python_env\idf5.3_py3.11_env\Scripts\python.exe" (
    set "ESP_FLASH_PYTHON=C:\Espressif\python_env\idf5.3_py3.11_env\Scripts\python.exe"
    goto run
)
set "ESP_FLASH_PYTHON=python"
:run
rem Firmware bundles keep flash.py beside this BAT; the project keeps it in script.
set "ESP_FLASH_SCRIPT=%~dp0script\flash\flash.py"
if exist "%~dp0flasher_args.json" set "ESP_FLASH_SCRIPT=%~dp0flash.py"
"%ESP_FLASH_PYTHON%" "%ESP_FLASH_SCRIPT%" %*
exit /b %errorlevel%
