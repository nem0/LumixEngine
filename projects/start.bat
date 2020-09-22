genie.exe --static-plugins vs2019

set devenv_cmd=devenv.exe
where /q devenv.exe
if not %errorlevel%==0 set devenv_cmd="C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\IDE\devenv.exe"

start "" %devenv_cmd% "tmp/vs2019/LumixEngine.sln"