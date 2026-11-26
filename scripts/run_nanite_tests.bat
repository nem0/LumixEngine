@echo off
REM Builds studio.exe and app.exe, compiles the resources of the demo project (studio.exe --compile_all_resources) and runs
REM the nanite benchmark (demo/scripts/bench.evox) on the high poly meshes of demo/models/bench (rock, coil, terrain, tree_plain, tree_plain_big),
REM each with cluster streaming on and off. The app is started twice, once per streaming mode, and runs the five meshes one
REM after another in that launch (-bench_spawn ... -bench_spawn5). tree_plain_big is 100 x 100 instances of one instanced model.
REM
REM Usage: run_nanite_tests.bat [config] [-counters] [-- extra app arguments]
REM   config    RelWithDebInfo (default) or Debug
REM   -counters also records GPU hardware counters per profile block (see below), needs an NVIDIA GPU and the Nsight Perf SDK in external\nsight_perf
REM   extra   arguments for app.exe, e.g. -bench_frames 1200 -bench_cam 0 3 0 90 -20 -cluster_stream_mb 1
REM
REM Two more launches measure the voxel levels: tree_plain_big seen from far away with a fixed camera at 1080p (results tree_plain_big_voxels_far_a.csv and
REM tree_plain_big_voxels_far_b.csv). The scenes above have almost no voxel bricks, these have 250k-590k in the main view.
REM
REM With -counters, extra launches record one hardware counter at a time (NVIDIA only, Nsight Perf SDK, see src\renderer\gpu\gpu_nvperf.h) for every profile
REM block of a fixed scene, they are separate launches because recording stalls the app. Results: counters_<scene>_<metric>.csv (path of the block, value).
REM Counters need profiling permissions: admin rights or "Allow access to the GPU performance counters to all users" in the NVIDIA Control Panel
REM (Developer / Manage GPU Performance Counters), no other profiler (Nsight, PIX) may be attached, and use RelWithDebInfo (the D3D12 debug layer of Debug builds
REM is not supported by the SDK). The default metrics are the occupancy of the SMs (all warps, compute shader warps, pixel shader warps) and the SM throughput,
REM set the variable "counters" to a space separated list of metrics to change them, "counters_depth" is the nesting of the blocks,
REM "counters_warmup" the frames before recording. The output of the SDK in the app log is in the error message when a launch fails.
REM
REM Every scene runs twice per streaming mode: at 4K and at 800x600 (results <mesh>_<mode>_800x600.csv).
REM The 4K scenes are rendered at 3840 x 2160 (-resolution, the window stays small) and the grids are dense, so that the passes of the clusters have millions of
REM triangles at the default error of clusters (1 px): at the size of the default window they take 0.2-0.5 ms and nothing about them can be measured.
REM The window is never shown (-hidden), so the app takes neither the focus nor the mouse and the machine stays usable during the runs.
REM The runs are without vsync (-no_vsync) and with 100 warmup frames (-bench_warmup, cluster streaming needs about 100 to settle, 10-50 give streaming runs 7-37% too fast geom pass times) and 50 measured frames (-bench_frames, extra arguments win over it). With vsync the GPU
REM is idle most of every frame, so it drops its clocks and the timings are 3-4 times longer and full of spikes. Without it the GPU
REM stays busy at its top clocks. (-gpu_stable_power_state would lock the clocks, but it needs the developer mode of Windows.)
REM
REM Every scene is an empty world with a sun, a grid of instances of one mesh and a camera turning around in the middle of it
REM (see bench.evox). "counter," lines are the value of the last frame whose GPU query was ready, which can be one or two frames old and the camera turns every frame,
REM so compare triangle counts with the "counter_avg," lines (average over the measured frames). Results are CSV files in "%USERPROFILE%\Saved Games\nanite_bench\" (<mesh>_<streaming|no_streaming>.csv).
REM Resources are compiled by Studio because the app can't compile them (scripts, models, shaders).

pushd %~dp0
setlocal enabledelayedexpansion

set config=RelWithDebInfo
if /i "%~1"=="Debug" set config=Debug
if /i "%~1"=="RelWithDebInfo" set config=RelWithDebInfo

REM everything after "--" goes to the app
set extra=
set found_sep=
set counters_on=
for %%A in (%*) do (
  if defined found_sep set extra=!extra! %%A
  if not defined found_sep if /i "%%A"=="-counters" set counters_on=1
  if "%%A"=="--" set found_sep=1
)

REM detect msbuild
set msbuild_cmd=
where /q msbuild.exe
if %errorlevel%==0 (
  for /f "usebackq delims=" %%A in (`where msbuild.exe`) do set msbuild_cmd=%%A & goto :msbuild_found
)
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
  for /f "usebackq delims=" %%A in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set msbuild_cmd=%%A
)
if "%msbuild_cmd%"=="" (
  set msbuild_cmd=C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\amd64\MSBuild.exe
)
:msbuild_found

REM the solution is generated by `premake5 vs2022` (not regenerated here, it is the one Studio is built with)
if not exist "tmp\vs2022\app.vcxproj" (
  echo tmp\vs2022\app.vcxproj not found, run "premake5.exe vs2022" in the scripts directory first.
  popd
  exit /b 1
)

echo Building studio.exe and app.exe (%config%)...
for %%P in (studio app) do (
  "%msbuild_cmd%" "tmp\vs2022\%%P.vcxproj" /m /p:Configuration=%config% /p:Platform=x64 /verbosity:minimal
  if not !errorlevel!==0 (
    echo Build of %%P failed.
    popd
    exit /b !errorlevel!
  )
)

set bin=%~dp0tmp\vs2022\bin\%config%
set app=%bin%\app.exe
set studio=%bin%\studio.exe
if not exist "%app%" (
  echo %app% not found.
  popd
  exit /b 2
)
if not exist "%studio%" (
  echo %studio% not found.
  popd
  exit /b 2
)

echo.
echo Compiling resources of the demo project...
pushd "%~dp0..\data"
"%studio%" -data_dir "%~dp0..\demo" --compile_all_resources
set rc=!errorlevel!
popd
if not !rc!==0 (
  echo Compiling resources failed ^(exit code !rc!^), see %~dp0..\data\lumix.log
  popd
  exit /b !rc!
)

set out_dir=%USERPROFILE%\Saved Games\nanite_bench
if not exist "%out_dir%" mkdir "%out_dir%"

REM scenes of every launch: mesh, instances along x and z, distance between instances (m)
set failed=0
call :run streaming "" 3840 2160
call :run no_streaming "-cluster_stream_off" 3840 2160
REM the same scenes at the size of a small window, the names of the results end with _800x600
call :run streaming_800x600 "" 800 600
call :run no_streaming_800x600 "-cluster_stream_off" 800 600
REM voxel scenes: tree_plain_big seen from far away, where the coarse levels of its clusters are voxels (hundreds of thousands of bricks in the main view). The camera is fixed
REM (-bench_still), at 1080p. Results: tree_plain_big_voxels_far_a.csv and tree_plain_big_voxels_far_b.csv. Compare "voxel bricks", "geom pass" and "frame".
call :run_voxels voxels_far_a "0 25 650 0 -4"
call :run_voxels voxels_far_b "0 80 600 0 -12"
if defined counters_on call :run_all_counters

echo.
echo ==== Results (%out_dir%) ====
for %%M in (rock coil terrain tree_plain tree_plain_big) do (
  for %%N in (streaming no_streaming streaming_800x600 no_streaming_800x600) do (
    echo.
    echo --- %%M, %%N ---
    if exist "%out_dir%\%%M_%%N.csv" (
      findstr /i /b /c:"info," /c:"frame_ms" /c:"gpu_ms,frame" /c:"gpu_ms,geom pass" /c:"gpu_ms,shadow pass" /c:"gpu_ms,prepare view" /c:"gpu_ms,scene instance cull" /c:"gpu_ms,cluster cull" /c:"gpu_ms,scene variants" /c:"gpu_ms,render bucket" /c:"gpu_ms,fill clusters" /c:"counter,GPU triangles: " /c:"counter_avg,GPU triangles: " /c:"counter,Cluster" /c:"counter_avg,Cluster" "%out_dir%\%%M_%%N.csv"
    ) else (
      echo no result
    )
  )
)

for %%N in (voxels_far_a voxels_far_b) do (
  echo.
  echo --- tree_plain_big, %%N ---
  if exist "%out_dir%\tree_plain_big_%%N.csv" (
    findstr /i /b /c:"info," /c:"frame_ms" /c:"gpu_ms,frame" /c:"gpu_ms,geom pass" /c:"gpu_ms,shadow pass" /c:"gpu_ms,voxel bricks" /c:"gpu_ms,voxel expand" /c:"gpu_ms,voxel retest" /c:"gpu_ms,voxel draw" /c:"counter_avg,GPU triangles: " /c:"counter_avg,Voxel bricks" /c:"counter_avg,Selected clusters (main)" "%out_dir%\tree_plain_big_%%N.csv"
  ) else (
    echo no result
  )
)

if defined counters_on (
  echo.
  echo ==== GPU counters ^(%out_dir%^) ====
  for %%F in ("%out_dir%\counters_*.csv") do (
    echo.
    echo --- %%~nxF ---
    findstr /i /c:"metric," /c:"geom pass" /c:"shadow pass" /c:"scene instance cull" /c:"cluster cull" /c:"fill clusters" /c:"voxel bricks" /c:"voxel draw" "%%F"
  )
)

popd
if not "%failed%"=="0" exit /b 1
exit /b 0

REM records GPU hardware counters, one launch per scene and metric. The scenes are fixed (-bench_still): the passes of the SDK are recorded in different frames.
REM Stops at the first failure, it is most likely a missing permission or SDK and the rest would fail the same way.
:run_all_counters
if not exist "%bin%\nvperf_grfx_host.dll" (
  echo.
  echo GPU counters skipped: Nsight Perf SDK not built in. Unpack it to external\nsight_perf and run "premake5.exe vs2022" in the scripts directory.
  set failed=1
  exit /b 0
)
if not defined counters set counters=sm__warps_active.avg.pct_of_peak_sustained_active tpc__warps_active_shader_cs_realtime.avg.pct_of_peak_sustained_elapsed tpc__warps_active_shader_ps_realtime.avg.pct_of_peak_sustained_elapsed sm__throughput.avg.pct_of_peak_sustained_elapsed
if not defined counters_depth set counters_depth=5
if not defined counters_warmup set counters_warmup=600
set counters_failed=
for %%M in (%counters%) do (
  if not defined counters_failed call :run_counters rock "-bench_spawn rock 30 30 2.5 -bench_still" 3840 2160 %%M
  if not defined counters_failed call :run_counters tree_plain_big_voxels_far_a "-bench_spawn tree_plain_big 100 100 8 -bench_cam 0 25 650 0 -4 -bench_still" 1920 1080 %%M
)
exit /b 0

REM :run_counters <scene name> "<scene arguments>" <width> <height> <metric>
:run_counters
set cout=%out_dir%\counters_%~1_%~5.csv
if exist "%cout%" del "%cout%"
echo.
echo Recording %~5 in "%~1" at %~3x%~4...
pushd "%~dp0..\data"
REM -bench makes the app run the scene (spawn, camera), the benchmark itself is far longer than the capture: the app quits when the capture is done
"%app%" -window -data_dir "%~dp0..\demo" -bench "nanite_bench/counters_unused.csv" %~2 %extra% -no_vsync -hidden -resolution %~3 %~4 -bench_warmup 100 -bench_frames 1000000 -gpu_counters %~5 -gpu_counters_out "%cout%" -gpu_counters_warmup %counters_warmup% -gpu_counters_depth %counters_depth% -gpu_counters_quit
popd
if not exist "%cout%" (
  echo "%~1 / %~5" produced no result. See %bin%\lumix_app.log ^(profiling permissions? a profiler attached? Debug build?^)
  set failed=1
  set counters_failed=1
)
exit /b 0

REM :run_voxels <name: voxels_far_a|voxels_far_b> "<camera: x y z yaw pitch>"
REM one launch with one scene, 100 x 100 instances of tree_plain_big (spacing 8 m) at 1080p, writes tree_plain_big_<name>.csv
:run_voxels
set vname=%~1
set vcam=%~2
if exist "%out_dir%\tree_plain_big_%vname%.csv" del "%out_dir%\tree_plain_big_%vname%.csv"
echo.
echo Running "%vname%" at 1920x1080 (tree_plain_big, camera %vcam%)...
pushd "%~dp0..\data"
"%app%" -window -data_dir "%~dp0..\demo" -bench "nanite_bench/tree_plain_big_%vname%.csv" -bench_spawn tree_plain_big 100 100 8 -bench_cam %vcam% -bench_still %extra% -no_vsync -hidden -resolution 1920 1080 -bench_warmup 150 -bench_frames 100
popd
if not exist "%out_dir%\tree_plain_big_%vname%.csv" (
  echo "tree_plain_big_%vname%" produced no result. See %bin%\lumix_app.log
  set failed=1
)
exit /b 0

REM :run <result name suffix: streaming|no_streaming, with _800x600 at a small resolution> <extra app flags> <width> <height>
REM one launch of the app, it runs the scenes one after another and writes <mesh>_<mode>.csv for each
:run
set mode=%~1
set flags=%~2
set res_w=%~3
set res_h=%~4
for %%M in (rock coil terrain tree_plain tree_plain_big) do if exist "%out_dir%\%%M_%mode%.csv" del "%out_dir%\%%M_%mode%.csv"
echo.
echo Running "%mode%" at %res_w%x%res_h% (rock, coil, terrain, tree_plain, tree_plain_big)...
pushd "%~dp0..\data"
"%app%" -window -data_dir "%~dp0..\demo" -bench "nanite_bench/*_%mode%.csv" -bench_spawn rock 30 30 2.5 -bench_spawn2 coil 30 30 2.5 -bench_spawn3 terrain 9 9 8 -bench_spawn4 tree_plain 20 20 6 -bench_spawn5 tree_plain_big 100 100 8 %flags% %extra% -no_vsync -hidden -resolution %res_w% %res_h% -bench_warmup 100 -bench_frames 50
popd
for %%M in (rock coil terrain tree_plain tree_plain_big) do (
  if not exist "%out_dir%\%%M_%mode%.csv" (
    echo "%%M_%mode%" produced no result. See %bin%\lumix_app.log
    set failed=1
  )
)
exit /b 0
