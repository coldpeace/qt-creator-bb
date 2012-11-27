include(../qttest.pri)
include($$IDE_SOURCE_TREE/src/libs/utils/utils.pri)

UTILSDIR    = $$IDE_SOURCE_TREE/src/libs

DEBUGGERDIR = $$IDE_SOURCE_TREE/src/plugins/debugger

INCLUDEPATH += $$DEBUGGERDIR $$UTILSDIR

SOURCES += \
    tst_gdb.cpp \
    $$DEBUGGERDIR/gdb/gdbmi.cpp \
