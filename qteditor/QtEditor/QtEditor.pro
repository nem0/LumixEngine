#-------------------------------------------------
#
# Project created by QtCreator 2014-03-21T21:15:42
#
#-------------------------------------------------

QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = ../../../bin/Win32_Debug/QtEditor
TEMPLATE = app
CONFIG -= flat


SOURCES += main.cpp\
        mainwindow.cpp \
    sceneview.cpp \ 
    log_widget.cpp \
    property_view.cpp \
    propertywidgets/renderable_widget.cpp \
    property_widget_base.cpp \
    propertywidgets/script_widget.cpp \
    gameview.cpp \
    assetbrowser.cpp \
    scripts/scriptcompiler.cpp \
    scripts/scriptcompilerwidget.cpp \
    fileserverwidget.cpp \
    propertywidgets/animable_widget.cpp \
    propertywidgets/camerawidget.cpp \
    propertywidgets/lightwidget.cpp

HEADERS  += mainwindow.h \
    sceneview.h \ 
    log_widget.h \
    property_view.h \
    propertywidgets/renderable_widget.h \
    property_widget_base.h \
    propertywidgets/script_widget.h \
    gameview.h \
    assetbrowser.h \
    scripts/scriptcompiler.h \
    scripts/scriptcompilerwidget.h \
    fileserverwidget.h \
    propertywidgets/animable_widget.h \
    propertywidgets/camerawidget.h \
    propertywidgets/lightwidget.h

FORMS    += mainwindow.ui \
    logwidget.ui \
    property_view.ui \
    propertywidgets/renderable_widget.ui \
    property_widget_base.ui \
    propertywidgets/script_widget.ui \
    gameview.ui \
    assetbrowser.ui \
    scripts/scriptcompilerwidget.ui \
    fileserverwidget.ui \
    propertywidgets/animable_widget.ui \
    propertywidgets/camerawidget.ui \
    propertywidgets/lightwidget.ui

win32
{
    INCLUDEPATH = ../../src
    LIBS = -L../../bin/Win32_Debug -lcore -lengine -lopengl32
}
