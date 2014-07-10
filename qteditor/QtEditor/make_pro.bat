set olddir=%CD%
call "qtenv2.bat"
cd %olddir%
qmake -tp vc qteditor.pro
