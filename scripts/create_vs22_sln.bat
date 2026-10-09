REM create VS 2022 solution in tmp/vs2022/LumixEngine.sln
pushd %~dp0
premake5.exe vs2022
popd