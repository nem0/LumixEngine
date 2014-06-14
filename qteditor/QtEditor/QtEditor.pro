#-------------------------------------------------
#
# Project created by QtCreator 2014-03-21T21:15:42
#
#-------------------------------------------------

QT       += core gui opengl

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = ../../../bin/Win32_Debug/QtEditor
TEMPLATE = app
CONFIG -= flat


SOURCES += main.cpp\
        mainwindow.cpp \
    sceneview.cpp \ 
    log_widget.cpp \
    property_view.cpp \
    gameview.cpp \
    assetbrowser.cpp \
    scripts/scriptcompiler.cpp \
    scripts/scriptcompilerwidget.cpp \
    fileserverwidget.cpp \
    materialmanager.cpp \
    profilerui.cpp

HEADERS  += mainwindow.h \
    sceneview.h \ 
    log_widget.h \
    property_view.h \
    gameview.h \
    assetbrowser.h \
    scripts/scriptcompiler.h \
    scripts/scriptcompilerwidget.h \
    fileserverwidget.h \
    materialmanager.h \
    wgl_render_device.h \
    renderdevicewidget.h \
    profilerui.h 

FORMS    += mainwindow.ui \
    logwidget.ui \
    property_view.ui \
    gameview.ui \
    assetbrowser.ui \
    scripts/scriptcompilerwidget.ui \
    fileserverwidget.ui \
    materialmanager.ui \
    profilerui.ui

win32
{
    INCLUDEPATH = ../../src \
	../../external/glew/include
    LIBS = -L../../bin/Win32_Debug -lcore -lengine -lopengl32 -lphysics
}
