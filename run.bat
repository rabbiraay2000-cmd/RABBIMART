@echo off
REM Double-click to start RabbiMart (needs Java 17). Serves public/ on http://localhost:8080
cd /d %~dp0
for /f "usebackq delims=" %%i in ("cp.txt") do set CP=%%i
java -cp "target/classes;%CP%" shop.Main
pause
