@echo off
setlocal
rem Clean up portable firmware bundles under build/, keeping only the newest.
rem Usage:
rem   clean_firmware.bat            Keep the newest 1 bundle
rem   clean_firmware.bat --keep 3   Keep the newest 3 bundles
rem   clean_firmware.bat --keep 0   Remove all bundles
rem   clean_firmware.bat --dry-run  Preview without deleting
python "%~dp0script\build_tools\clean_firmware.py" %*
exit /b %errorlevel%
