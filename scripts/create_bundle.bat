REM create bundle - bundle data dir inside the exe
echo Creating bundle...
.\genie.exe --embed-resources --static-physx vs2022
cd ..\data
tar -cvf data.tar .
move data.tar ../src/studio
cd ..\scripts\
%msbuild_cmd% tmp/vs2022/LumixEngine.sln /p:Configuration=RelWithDebInfo
del ..\src\studio\data.tar
pause
