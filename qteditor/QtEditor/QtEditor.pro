#-------------------------------------------------
#
# Project created by QtCreator 2014-03-21T21:15:42
#
#-------------------------------------------------

QT       += core gui opengl

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

Release:TARGET = ../../../bin/Win32_Release/QtEditor
Debug:TARGET = ../../../bin/Win32_Debug/QtEditor
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
    profilerui.cpp \
    profilergraph.cpp \
    pc/file_system_watcher.cpp \
    entity_template_list.cpp \
    notifications.cpp \
    insert_mesh_command.cpp \
    entity_list.cpp

HEADERS  += mainwindow.h \
    sceneview.h \ 
    log_widget.h \
    property_view.h \
    gameview.h \
    assetbrowser.h \
    scripts/scriptcompiler.h \
    scripts/scriptcompilerwidget.h \
    fileserverwidget.h \
    wgl_render_device.h \
    renderdevicewidget.h \
    profilerui.h \
    profilergraph.h \
    file_system_watcher.h \
    entity_template_list.h \
    notifications.h \
    insert_mesh_command.h \
    entity_list.h

FORMS    += mainwindow.ui \
    logwidget.ui \
    property_view.ui \
    gameview.ui \
    assetbrowser.ui \
    scripts/scriptcompilerwidget.ui \
    fileserverwidget.ui \
    profilerui.ui \
    profilergraph.ui \
    entity_template_list.ui \
    entity_list.ui

win32
{
    INCLUDEPATH = ../../src \
	../../external/glew/include
    Release:LIBS = -L../../bin/Win32_Release -lcore -lengine -lopengl32 -lphysics -lanimation
    Debug:LIBS = -L../../bin/Win32_Debug -lcore -lengine -lopengl32 -lphysics -lanimation
}
