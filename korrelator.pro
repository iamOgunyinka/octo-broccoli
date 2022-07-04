QT       += core gui network websockets printsupport

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17
INCLUDEPATH += "include"
INCLUDEPATH += "third-party/rapidjson/include"
INCLUDEPATH += "E:\\boost_1_78_0\\include" \
               "E:\\vcpkg\\installed\\x64-windows\\include"

Debug:LIBS += "E:\\vcpkg\\installed\\x64-windows\\debug\\lib\\libcrypto.lib" \
              "E:\\vcpkg\\installed\\x64-windows\\debug\\lib\\libssl.lib" \
              "E:\\vcpkg\\installed\\x64-windows\\debug\\lib\\libsodium.lib"

Release:LIBS += "E:\\vcpkg\\installed\\x64-windows\\lib\\libcrypto.lib" \
              "E:\\vcpkg\\installed\\x64-windows\\lib\\libssl.lib" \
              "E:\\vcpkg\\installed\\x64-windows\\lib\\libsodium.lib"

QMAKE_CXXFLAGS += -bigobj
# DEFINES += TESTNET=1

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += main.cpp \
  binance_symbols.cpp \
  kucoin_symbols.cpp \
  src/binance_futures_plug.cpp \
  src/binance_https_request.cpp \
  src/binance_spots_plug.cpp \
  src/constants.cpp \
  src/crypto.cpp \
  src/kucoin_futures_plug.cpp \
  src/kucoin_https_request.cpp \
  src/kucoin_spots_plug.cpp \
  src/kucoin_websocket.cpp \
  src/settingsdialog.cpp \
  src/binance_websocket.cpp \
  src/maindialog.cpp \
  src/order_model.cpp \
  src/qcustomplot.cpp \
  src/uri.cpp \
  src/websocket_manager.cpp

HEADERS += \
  binance_symbols.hpp \
  include/binance_futures_plug.hpp \
  include/binance_https_request.hpp \
  include/binance_spots_plug.hpp \
  include/binance_websocket.hpp \
  include/constants.hpp \
  include/container.hpp \
  include/crypto.hpp \
  include/kucoin_futures_plug.hpp \
  include/kucoin_https_request.hpp \
  include/kucoin_spots_plug.hpp \
  include/kucoin_websocket.hpp \
  include/maindialog.hpp \
  include/order_model.hpp \
  include/qcustomplot.h \
  include/sthread.hpp \
  include/uri.hpp \
  include/utils.hpp \
  include/tokens.hpp \
  include/websocket_manager.hpp \
  include/settingsdialog.hpp \
  kucoin_symbols.hpp

FORMS += \
    ui/settingsdialog.ui \
    ui/maindialog.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
