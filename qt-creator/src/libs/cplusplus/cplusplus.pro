TEMPLATE = lib
TARGET = CPlusPlus

DEFINES += NDEBUG
#DEFINES += DEBUG_LOOKUP
unix:QMAKE_CXXFLAGS_DEBUG += -O2

include(../../qtcreatorlibrary.pri)
include(cplusplus-lib.pri)
include(../languageutils/languageutils.pri)
