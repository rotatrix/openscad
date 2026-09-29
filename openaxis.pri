# Build with cmake -S cmake/openaxis-qmake -B openaxis-build first.
contains(CONFIG, openaxis) {
  CONFIG -= c++14
  CONFIG += c++17
  DEFINES += ENABLE_OPENAXIS
  SOURCES += src/OpenAxisController.cc
  HEADERS += src/OpenAxisController.h
  !exists(openaxis-build/openaxis-sdk.pri): error(Build cmake/openaxis-qmake before enabling OpenAxis)
  include(openaxis-build/openaxis-sdk.pri)
  win32: LIBS += -lbcrypt -lwsock32 -lws2_32 -lshlwapi
  unix {
    include(openaxis-build/openaxis-crypto.pri)
    LIBS += -lpthread
  }
}
