VERSION = 0.5
DEFINES += APP_VERSION=\\\"$$VERSION\\\"

QT       += core gui widgets quick quickwidgets

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

TARGET = GM40_Remote
TEMPLATE = app

LIBS += -lasound

FORMS += mainwindow.ui \
         libraryeditordialog.ui \
         settingsdialog.ui \
         presetselectordialog.ui \
         shortcutsdialog.ui

HEADERS += mainwindow.h \
           libraryeditordialog.h \
           settingsdialog.h \
           presetselectordialog.h \
           shortcutsdialog.h

SOURCES += main.cpp \
           libraryeditordialog.cpp \
           mainwindow.cpp \
           settingsdialog.cpp \
           presetselectordialog.cpp \
           shortcutsdialog.cpp

RESOURCES += \
    ressources.qrc
