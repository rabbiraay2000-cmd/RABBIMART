@echo off
REM First-time setup: needs Java 17+ and Maven on PATH. Builds classes + local classpath.
cd /d %~dp0
java -version
mvn -version
mvn clean compile
mvn -q dependency:build-classpath -Dmdep.outputFile=cp.txt
echo SETUP DONE. Start the app with run.bat
pause
