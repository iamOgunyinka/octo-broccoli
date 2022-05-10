QT       += core gui network websockets printsupport

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets multimedia

CONFIG += c++17
INCLUDEPATH += "include"
INCLUDEPATH += "third-party/rapidjson/include"

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
  main.cpp \
  src/cwebsocket.cpp \
  src/maindialog.cpp \
  src/qcustomplot.cpp

HEADERS += \
  include/container.hpp \
  include/cwebsocket.hpp \
  include/maindialog.hpp \
  include/qcustomplot.h \
  include/sthread.hpp

FORMS += \
    ui/maindialog.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
