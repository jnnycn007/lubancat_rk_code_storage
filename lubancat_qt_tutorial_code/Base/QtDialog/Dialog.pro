#-------------------------------------------------
#
# Project created by QtCreator 2021-03-21T09:38:39
#
#-------------------------------------------------

QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets printsupport multimedia multimediawidgets

#TARGET = Dialog
#TEMPLATE = app

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

CONFIG += c++11

SOURCES += \
        custcommessage.cpp \
        main.cpp \
        mainwindow.cpp

HEADERS += \
        custcommessage.h \
        mainwindow.h

FORMS += \
        mainwindow.ui


win32 {
    src_dir = $$PWD\\..\\thirdpart\\libqui\\lib\\*.dll
    skin_lib = $$PWD\\..\\thirdpart\\libskin\\lib\\Skin.dll
    dst_dir = $$PWD\\..\\app_bin\\
    # dst_dir 最后的 \\ 是必须的，用来标示 xcopy 到一个文件夹，若不存在，创建之

    # Replace slashes in paths with backslashes for Windows
    src_dir ~= s,/,\\,g
    skin_lib ~= s,/,\\,g
    dst_dir ~= s,/,\\,g

    system(xcopy $$src_dir $$dst_dir /y /e)
    system(xcopy $$skin_lib $$dst_dir /y /e)
}
