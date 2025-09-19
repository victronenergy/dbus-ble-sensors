#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <event2/event.h>

#include <velib/platform/console.h>
#include <velib/platform/plt.h>
#include <velib/platform/task.h>
#include <velib/types/ve_dbus_item.h>
#include <velib/types/ve_values.h>
#include <velib/utils/ve_item_utils.h>

#include "ble-dbus.h"
#include "ble-scan.h"
#include "task.h"

static struct VeItem *settings;
static struct VeItem *control;

struct VeItem *get_settings(void)
{
	return settings;
}

struct VeItem *get_control(void)
{
	return control;
}

static void connect_dbus(void)
{
	const char *service = "com.victronenergy.settings";
	struct VeItem *root = veValueTree();
	struct VeDbus *dbus;
	int tries = 10;

	dbus = veDbusGetDefaultBus();
	if (!dbus) {
		fprintf(stderr, "dbus connection failed\n");
		pltExit(5);
	}

	veDbusSetListeningDbus(dbus);
	settings = veItemGetOrCreateUid(root, service);

	while (tries--) {
		if (veDbusAddRemoteService(service, settings, veTrue))
			break;
		sleep(2);
	}

	if (tries < 0) {
		fprintf(stderr, "error connecting to settings service\n");
		pltExit(1);
	}

	dbus = veDbusConnectString(veDbusGetDefaultConnectString());
	if (!dbus) {
		fprintf(stderr, "dbus connection failed\n");
		pltExit(5);
	}

	control = veItemAlloc(NULL, "");
	veDbusItemInit(dbus, control);
	veDbusChangeName(dbus, "com.victronenergy.ble");
}

static void sighand(int sig)
{
	event_base_loopbreak(pltGetLibEventBase());
}

void taskInit(void)
{
	struct sigaction sa = { 0 };
	int err;

	connect_dbus();
	ble_dbus_init();

	sa.sa_handler = sighand;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	err = ble_scan_open();
	if (err < 0) {
		fprintf(stderr, "no device found\n");
		pltExit(1);
	}

	atexit(ble_scan_close);
}

void taskUpdate(void)
{
	ble_scan();
}

void taskTick(void)
{
	ble_dbus_tick();
	ble_scan_tick();
}

char const *pltProgramVersion(void)
{
	return VERSION;
}
