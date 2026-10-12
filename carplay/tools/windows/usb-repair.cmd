@echo off
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -STA -WindowStyle Hidden -File "%~dp0tools\usb-repair\repair-usb-ncm.ps1" -Interactive
if errorlevel 1 (
  echo Unable to open the USB repair tool. Keep the tools folder beside this file.
  pause
)
