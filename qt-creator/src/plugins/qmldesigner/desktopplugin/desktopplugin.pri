TARGET = desktopplugin
TEMPLATE = lib
CONFIG += plugin

include (../designercore/iwidgetplugin.pri)

SOURCES += $$PWD/desktopplugin.cpp

HEADERS += $$PWD/desktopplugin.h  $$PWD/../designercore/include/iwidgetplugin.h

RESOURCES += $$PWD/desktopplugin.qrc

OTHER_FILES += $$PWD/desktop.metainfo
