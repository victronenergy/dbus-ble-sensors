#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <event2/event.h>
#include <sys/capability.h>
#include <sys/prctl.h>

#include <velib/platform/console.h>
#include <velib/platform/plt.h>
#include <velib/platform/task.h>
#include <velib/types/ve_dbus_item.h>
#include <velib/utils/ve_logger.h>
#include <velib/utils/ve_item_utils.h>
#include <velib/types/ve_values.h>

#include "ble-dbus.h"
#include "ble-scan.h"
#include "ble-socket.h"
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

static void change_user(void)
{
	cap_t caps = NULL;
	cap_value_t cap_list[] = { CAP_NET_ADMIN, CAP_NET_RAW };
	struct passwd *pw = getpwnam("ble-sensors");

	if (!pw) {
		logE("task", "no getpwnam ble-sensors found!");
		pltExit(6);
	}

	/* Keep capabilities after dropping root */
	if (prctl(PR_SET_KEEPCAPS, 1L) < 0) {
		perror("prctl");
		pltExit(6);
	}

	/* Set supplementary groups */
	if (initgroups(pw->pw_name, pw->pw_gid) < 0) {
		perror("initgroups");
		pltExit(6);
	}

	if (setgid(pw->pw_gid) < 0) {
		perror("setgid");
		pltExit(6);
	}

	if (setuid(pw->pw_uid) < 0) {
		perror("setuid");
		pltExit(6);
	}

	caps = cap_init();
	if (!caps) {
		perror("cap_init");
		pltExit(6);
	}

	if (cap_set_flag(caps, CAP_PERMITTED, sizeof(cap_list) / sizeof(cap_list[0]), cap_list, CAP_SET) < 0) {
		perror("cap_set_flag(PERMITTED)");
		cap_free(caps);
		pltExit(6);
	}

	if (cap_set_flag(caps, CAP_EFFECTIVE, sizeof(cap_list) / sizeof(cap_list[0]), cap_list, CAP_SET) < 0) {
		perror("cap_set_flag(EFFECTIVE)");
		cap_free(caps);
		pltExit(6);
	}

	/* Apply capability set */
	if (cap_set_proc(caps) < 0) {
		perror("cap_set_proc");
		cap_free(caps);
		pltExit(6);
	}

	if (cap_free(caps) < 0) {
		perror("cap_free");
		pltExit(6);
	}

	logI("task", "Running as %s (uid=%d gid=%d)", pw->pw_name, getuid(), getgid());
}

void taskInit(void)
{
	struct sigaction sa = { 0 };
	int err;

	change_user();

	connect_dbus();
	ble_dbus_init();
	ble_scan_init();
	ble_socket_init();

	sa.sa_handler = sighand;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	err = ble_scan_open();
	if (err < 0) {
		fprintf(stderr, "failed to open bluetooth scan control socket\n");
		pltExit(1);
	}

	ble_socket_open();

	atexit(ble_scan_close);
	atexit(ble_socket_close);
}

void taskUpdate(void)
{
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
