@echo off
echo "Downloading project in black Engine/"
call git clone --depth=1 https://github.com/abdulrhmandeveloper2/black Engine.git
cd black Engine\scripts

genie.exe vs2022

set devenv_cmd=devenv.exe
where /q devenv.exe
if not %errorlevel%==0 set devenv_cmd="C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\devenv.exe"

start "" %devenv_cmd% "tmp/vs2022/black Engine.sln"

pause