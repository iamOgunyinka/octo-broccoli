QT       += core gui network websockets printsupport

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17
INCLUDEPATH += "include"
INCLUDEPATH += "third-party/rapidjson/include"
INCLUDEPATH += "D:\\boost_1_78\\include" \
               "D:\\vcpkg\\installed\\x64-windows\\include"

Debug:LIBS += "D:\\vcpkg\\installed\\x64-windows\\debug\\lib\\libcrypto.lib" \
              "D:\\vcpkg\\installed\\x64-windows\\debug\\lib\\libssl.lib" \
              "D:\\vcpkg\\installed\\x64-windows\\debug\\lib\\libsodium.lib"

Release:LIBS += "D:\\vcpkg\\installed\\x64-windows\\lib\\libcrypto.lib" \
              "D:\\vcpkg\\installed\\x64-windows\\lib\\libssl.lib" \
              "D:\\vcpkg\\installed\\x64-windows\\lib\\libsodium.lib"

QMAKE_CXXFLAGS += -bigobj
# DEFINES += TESTNET=1

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += main.cpp \
  src/double_trader.cpp \
  src/mainwindow.cpp \
  src/binance_symbols.cpp \
  src/ftx_futures_plug.cpp \
  src/ftx_https_request.cpp \
  src/ftx_spots_plug.cpp \
  src/ftx_symbols.cpp \
  src/ftx_websocket.cpp \
  src/binance_futures_plug.cpp \
  src/binance_spots_plug.cpp \
  src/binance_https_request.cpp \
  src/binance_websocket.cpp \
  src/constants.cpp \
  src/crypto.cpp \
  src/kucoin_futures_plug.cpp \
  src/kucoin_spots_plug.cpp \
  src/kucoin_https_request.cpp \
  src/kucoin_symbols.cpp \
  src/kucoin_websocket.cpp \
  src/settingsdialog.cpp \
  src/maindialog.cpp \
  src/order_model.cpp \
  src/qcustomplot.cpp \
  src/single_trader.cpp \
  src/uri.cpp \
  src/websocket_manager.cpp

HEADERS += include/binance_symbols.hpp \
  include/double_trader.hpp \
  include/ftx_futures_plug.hpp \
  include/ftx_https_request.hpp \
  include/ftx_spots_plug.hpp \
  include/ftx_symbols.hpp \
  include/binance_futures_plug.hpp \
  include/binance_https_request.hpp \
  include/binance_spots_plug.hpp \
  include/binance_websocket.hpp \
  include/constants.hpp \
  include/container.hpp \
  include/crypto.hpp \
  include/ftx_websocket.hpp \
  include/kucoin_futures_plug.hpp \
  include/kucoin_https_request.hpp \
  include/kucoin_spots_plug.hpp \
  include/kucoin_websocket.hpp \
  include/maindialog.hpp \
  include/order_model.hpp \
  include/plug_data.hpp \
  include/qcustomplot.h \
  include/single_trader.hpp \
  include/sthread.hpp \
  include/uri.hpp \
  include/utils.hpp \
  include/tokens.hpp \
  include/websocket_manager.hpp \
  include/settingsdialog.hpp \
  include/kucoin_symbols.hpp \
  include/mainwindow.hpp

FORMS += ui/settingsdialog.ui \
    ui/mainwindow.ui \
    ui/maindialog.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
elsD: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += resources/image_resouce.qrc
