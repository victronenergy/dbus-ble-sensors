T = dbus-ble-sensors$(EXT)
VERSION = 0.7

TARGETS += $T
INSTALL_BIN += $T

SUBDIRS += ext/velib
$T_DEPS += $(call subtree_tgts,$(d)/ext/velib)

SUBDIRS += src
$T_DEPS += $(call subtree_tgts,$(d)/src)

DEFINES += DBUS
DBUS_CFLAGS := $(shell pkg-config --cflags dbus-1)
DBUS_LIBS := $(shell pkg-config --libs dbus-1)

override CFLAGS += $(DBUS_CFLAGS) -DVERSION=\"$(VERSION)\" -Werror
$T_LIBS += -lbluetooth -lpthread -levent -levent_pthreads -ldl -lm $(DBUS_LIBS)

INCLUDES += src
INCLUDES += ext/velib/inc
