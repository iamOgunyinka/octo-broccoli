QT       += core gui network websockets printsupport

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17
INCLUDEPATH += "include"
INCLUDEPATH += "third-party/rapidjson/include"
INCLUDEPATH += "E:\\boost_1_78_0\\include" \
               "E:\\vcpkg\\installed\\x64-windows\\include"

win32:LIBS += "E:\\vcpkg\\installed\\x64-windows\\debug\\lib\\libcrypto.lib" \
              "E:\\vcpkg\\installed\\x64-windows\\debug\\lib\\libssl.lib"

QMAKE_CXXFLAGS += -bigobj
# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += main.cpp \
  src/cwebsocket.cpp \
  src/kc_websocket.cpp \
  src/maindialog.cpp \
  src/order_model.cpp \
  src/qcustomplot.cpp \
  src/uri.cpp

HEADERS += \
  include/cwebsocket.hpp \
  include/kc_websocket.hpp \
  include/maindialog.hpp \
  include/order_model.hpp \
  include/qcustomplot.h \
  include/sthread.hpp \
  include/uri.hpp \
  include/websocket_base.hpp \
  utils.hpp

FORMS += \
    ui/maindialog.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
