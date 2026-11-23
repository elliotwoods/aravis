/*
  Copyright 2009 Lennart Poettering
  Copyright 2010 David Henningsson <diwic@ubuntu.com>
  Copyright © 2009-2025 Emmanuel Pacaud <emmanuel.pacaud@free.fr>

  Permission is hereby granted, free of charge, to any person
  obtaining a copy of this software and associated documentation files
  (the "Software"), to deal in the Software without restriction,
  including without limitation the rights to use, copy, modify, merge,
  publish, distribute, sublicense, and/or sell copies of the Software,
  and to permit persons to whom the Software is furnished to do so,
  subject to the following conditions:

  The above copyright notice and this permission notice shall be
  included in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  This file is based on rtkit sources, ported from libdbus to glib gdbus API.

*/

#include <arvrealtimeprivate.h>
#include <arvdebugprivate.h>
#include <memory.h>
#include <errno.h>

#ifdef G_OS_WIN32
#include <windows.h>
#else
#include <sys/param.h>
#include <sched.h>
#include <sys/time.h>
#include <sys/types.h>
#endif

#if !defined(__APPLE__) && !defined(G_OS_WIN32) && !defined(BSD)

#define RTKIT_SERVICE_NAME "org.freedesktop.RealtimeKit1"
#define RTKIT_OBJECT_PATH "/org/freedesktop/RealtimeKit1"

#define ARV_RTKIT_ERROR arv_rtkit_error_quark ()

typedef enum {
	ARV_RTKIT_ERROR_PERMISSION_DENIED,
	ARV_RTKIT_ERROR_WRONG_REPLY
} ArvRtkitError;

static GQuark
arv_rtkit_error_quark (void)
{
  return g_quark_from_static_string ("arv-rtkit-error-quark");
}

static gint64
arv_rtkit_get_int_property (GDBusConnection *connection, const char* propname, GError **error) {

	GDBusMessage *message;
	GDBusMessage *reply;
	GError *local_error = NULL;
	GVariant *body;
	GVariant *parameter;
	GVariant *variant;
	const GVariantType *variant_type;
	gint64 value;

	message = g_dbus_message_new_method_call (RTKIT_SERVICE_NAME,
						  RTKIT_OBJECT_PATH,
						  "org.freedesktop.DBus.Properties",
						  "Get");
	g_dbus_message_set_body (message, g_variant_new ("(ss)", "org.freedesktop.RealtimeKit1", propname));

	reply = g_dbus_connection_send_message_with_reply_sync (connection, message,
								G_DBUS_SEND_MESSAGE_FLAGS_NONE, 1000, NULL, NULL,
								&local_error);
	g_object_unref (message);

	if (local_error != NULL) {
		g_propagate_error (error, local_error);
		return 0;
	}

	if (g_dbus_message_get_message_type (reply) != G_DBUS_MESSAGE_TYPE_METHOD_RETURN) {
		local_error = g_error_new (ARV_RTKIT_ERROR, ARV_RTKIT_ERROR_PERMISSION_DENIED,
					   "%s", g_dbus_message_get_error_name (reply));
		g_propagate_error (error, local_error);
		g_object_unref (reply);
		return 0;
	}

	if (!g_variant_type_equal ("v", g_dbus_message_get_signature (reply))) {
		local_error = g_error_new (ARV_RTKIT_ERROR, ARV_RTKIT_ERROR_WRONG_REPLY,
					   "Invalid reply signature");
		g_propagate_error (error, local_error);
		g_object_unref (reply);
		return 0;
	}

	body = g_dbus_message_get_body (reply);
	parameter = g_variant_get_child_value (body, 0);
	variant = g_variant_get_variant (parameter);

	variant_type = g_variant_get_type (variant);

	if (g_variant_type_equal (variant_type, G_VARIANT_TYPE_INT32))
		value = g_variant_get_int32 (variant);
	else if (g_variant_type_equal (variant_type, G_VARIANT_TYPE_INT64))
		value = g_variant_get_int64 (variant);
	else
		value = 0;

	g_variant_unref (parameter);
	g_variant_unref (variant);
	g_object_unref (reply);

	return value;
}

int
arv_rtkit_get_max_realtime_priority (GDBusConnection *connection, GError **error)
{
	return arv_rtkit_get_int_property (connection, "MaxRealtimePriority", error);
}

int
arv_rtkit_get_min_nice_level (GDBusConnection *connection, GError **error)
{
	return arv_rtkit_get_int_property (connection, "MinNiceLevel", error);
}

gint64
arv_rtkit_get_rttime_usec_max (GDBusConnection *connection, GError **error)
{
	GError *local_error = NULL;
	gint64 rttime_usec_max;

	rttime_usec_max = arv_rtkit_get_int_property (connection, "RTTimeUSecMax", &local_error);
	if (local_error == NULL)
		return rttime_usec_max;

	g_error_free (local_error);

	return arv_rtkit_get_int_property (connection, "RTTimeNSecMax", error) / 1000;
}

void
arv_rtkit_make_realtime (GDBusConnection *connection, pid_t thread, int priority, GError **error)
{
	GDBusMessage *message;
	GDBusMessage *reply;
	GError *local_error = NULL;

	message = g_dbus_message_new_method_call (RTKIT_SERVICE_NAME,
						  RTKIT_OBJECT_PATH,
						  "org.freedesktop.RealtimeKit1",
						  "MakeThreadRealtime");
	g_dbus_message_set_body (message, g_variant_new ("(tu)", thread, priority));

	reply = g_dbus_connection_send_message_with_reply_sync (connection, message,
								G_DBUS_SEND_MESSAGE_FLAGS_NONE, 1000, NULL, NULL,
								&local_error);
	g_object_unref (message);

	if (local_error != NULL) {
		g_propagate_error (error, local_error);
		return;
	}

	if (g_dbus_message_get_message_type (reply) != G_DBUS_MESSAGE_TYPE_METHOD_RETURN) {
		local_error = g_error_new (ARV_RTKIT_ERROR, 0, "%s", g_dbus_message_get_error_name (reply));
		g_propagate_error (error, local_error);
		g_object_unref (reply);
		return;
	}

	g_object_unref (reply);
}

void
arv_rtkit_make_high_priority (GDBusConnection *connection, pid_t thread, int nice_level, GError **error)
{
	GDBusMessage *message;
	GDBusMessage *reply;
	GError *local_error = NULL;

	message = g_dbus_message_new_method_call (RTKIT_SERVICE_NAME,
						  RTKIT_OBJECT_PATH,
						  "org.freedesktop.RealtimeKit1",
						  "MakeThreadHighPriority");
	g_dbus_message_set_body (message, g_variant_new ("(ti)", thread, nice_level));

	reply = g_dbus_connection_send_message_with_reply_sync (connection, message,
								G_DBUS_SEND_MESSAGE_FLAGS_NONE, 1000, NULL, NULL,
								&local_error);
	g_object_unref (message);

	if (local_error != NULL) {
		g_propagate_error (error, local_error);
		return;
	}

	if (g_dbus_message_get_message_type (reply) != G_DBUS_MESSAGE_TYPE_METHOD_RETURN) {
		local_error = g_error_new (ARV_RTKIT_ERROR, 0, "%s", g_dbus_message_get_error_name (reply));
		g_propagate_error (error, local_error);
		g_object_unref (reply);
		return;
	}

	g_object_unref (reply);
}

#ifndef SCHED_RESET_ON_FORK
#define SCHED_RESET_ON_FORK 0x40000000
#endif

#ifndef RLIMIT_RTTIME
#define RLIMIT_RTTIME 15
#endif

#include <sys/resource.h>
#include <sys/syscall.h>

static pid_t _gettid(void) {
        return (pid_t) syscall(SYS_gettid);
}

/**
 * arv_make_thread_realtime:
 * @priority: realtime priority
 *
 * Returns: %TRUE on success.
 *
 * Try to make current thread realtime. It first try to use sched_setscheduler,
 * and if it fails, use rtkit.
 *
 * On macOS, the thread is put in the Mach time constraint policy, which does
 * not require any privilege. The policy is aperiodic, with a computation time
 * of 500 µs within a constraint of 1 ms. Mach has no priority level for this
 * policy, @priority is ignored. The kernel demotes a time constraint thread
 * which runs for a long time without blocking, it is only useful for a thread
 * that regularly waits for data, like a stream receiving thread.
 *
 * Since: 0.4.0
 */

gboolean
arv_make_thread_realtime (int priority)
{
	struct sched_param p;

	memset(&p, 0, sizeof(p));
	p.sched_priority = priority;

	if (sched_setscheduler(_gettid (), SCHED_RR|SCHED_RESET_ON_FORK, &p) < 0
	    && errno == EPERM) {
		struct rlimit rlim;
		GDBusConnection *bus;
		GError *error = NULL;

		memset(&rlim, 0, sizeof(rlim));
		rlim.rlim_cur = rlim.rlim_max = 100000000ULL; /* 100ms */
		if ((setrlimit(RLIMIT_RTTIME, &rlim) < 0)) {
			arv_warning_misc ("Failed to set RLIMIT_RTTIME: %s", strerror (errno));
			return FALSE;
		}

		bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
		if (error != NULL) {
			arv_warning_misc ("Failed to connect to system bus: %s", error->message);
			g_error_free (error);
			return FALSE;
		}

		arv_rtkit_make_realtime(bus, _gettid (), p.sched_priority, &error);
		g_object_unref (bus);

		if (error != NULL) {
			arv_warning_misc ("Failed to connect make realtime: %s", error->message);
			g_error_free (error);
			return FALSE;
		}

		arv_info_misc ("Thread became realtime with priority %d", priority);

		return TRUE;
	}
	return TRUE;
}

/**
 * arv_make_thread_high_priority:
 * @nice_level: new nice level
 *
 * Returns: %TRUE on success.
 *
 * Try to set current thread nice level to high priority, using rtkit.
 *
 * On macOS, the thread quality of service class is set to
 * QOS_CLASS_USER_INTERACTIVE. Mach has no per thread nice level, @nice_level
 * is ignored.
 *
 * Since: 0.4.0
 */

gboolean
arv_make_thread_high_priority (int nice_level)
{
	GDBusConnection *bus;
	GError *error = NULL;

	bus = g_bus_get_sync (G_BUS_TYPE_SYSTEM, NULL, &error);
	if (error != NULL) {
		arv_warning_misc ("Failed to connect to system bus: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	arv_rtkit_make_high_priority (bus, _gettid(), nice_level, &error);
	g_object_unref (bus);

	if (error != NULL) {
		arv_warning_misc ("Failed to connect high priority: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	arv_info_misc ("Nice level successfully changed to %d", nice_level);

	return TRUE;
}

#elif defined(G_OS_WIN32)

gboolean
arv_make_thread_realtime (int priority)
{
	SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);
	if(!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL))
   {
		DWORD err = GetLastError();
		arv_warning_misc ("SetPriorityClass(..., THREAD_PRIORITY_TIME_CRITICAL) failed (%lu)", err);
		return FALSE;
	}
	arv_info_misc ("Thread made realtime!");
	return TRUE;
}

gboolean
arv_make_thread_high_priority (int priority)
{
	if(!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST))
   {
		DWORD err = GetLastError();
		arv_warning_misc ("SetPriorityClass(..., THREAD_PRIORITY_HIGHEST) failed (%lu)", err);
		return FALSE;
	}
	arv_info_misc ("Thread made high-priority!");
	return TRUE;
}

#elif defined(__APPLE__)

#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#include <pthread/qos.h>

#define ARV_MACH_RT_COMPUTATION_US	500
#define ARV_MACH_RT_CONSTRAINT_US	1000

static uint32_t
_us_to_mach_absolute_time (guint64 us, const mach_timebase_info_data_t *timebase)
{
	guint64 ticks;

	us = MIN (us, G_USEC_PER_SEC);
	ticks = us * 1000 * timebase->denom / timebase->numer;

	return ticks > G_MAXUINT32 ? G_MAXUINT32 : (uint32_t) ticks;
}

gboolean
arv_make_thread_realtime (int priority)
{
	thread_time_constraint_policy_data_t policy;
	mach_timebase_info_data_t timebase;
	kern_return_t result;

	if (mach_timebase_info (&timebase) != KERN_SUCCESS || timebase.numer == 0) {
		arv_warning_misc ("mach_timebase_info failed, thread not made realtime");
		return FALSE;
	}

	policy.period = 0;
	policy.computation = _us_to_mach_absolute_time (ARV_MACH_RT_COMPUTATION_US, &timebase);
	policy.constraint = _us_to_mach_absolute_time (ARV_MACH_RT_CONSTRAINT_US, &timebase);
	policy.preemptible = TRUE;

	/* pthread_mach_thread_np does not allocate a port right, unlike mach_thread_self */
	result = thread_policy_set (pthread_mach_thread_np (pthread_self ()),
				    THREAD_TIME_CONSTRAINT_POLICY,
				    (thread_policy_t) &policy,
				    THREAD_TIME_CONSTRAINT_POLICY_COUNT);
	if (result != KERN_SUCCESS) {
		arv_warning_misc ("thread_policy_set (THREAD_TIME_CONSTRAINT_POLICY) failed: %s (%d)",
				  mach_error_string (result), result);
		return FALSE;
	}

	arv_info_misc ("Thread made realtime (Mach time constraint policy, computation %d us, constraint %d us)",
		       ARV_MACH_RT_COMPUTATION_US, ARV_MACH_RT_CONSTRAINT_US);

	return TRUE;
}

gboolean
arv_make_thread_high_priority (int nice_level)
{
	int result;

	result = pthread_set_qos_class_self_np (QOS_CLASS_USER_INTERACTIVE, 0);
	if (result != 0) {
		arv_warning_misc ("pthread_set_qos_class_self_np (QOS_CLASS_USER_INTERACTIVE) failed: %s",
				  g_strerror (result));
		return FALSE;
	}

	arv_info_misc ("Thread made high-priority (QOS_CLASS_USER_INTERACTIVE)");

	return TRUE;
}

#else

gboolean
arv_make_thread_realtime (int priority)
{
	arv_info_misc ("SCHED API not supported on this platform");

	return FALSE;
}

gboolean
arv_make_thread_high_priority (int nice_level)
{
	arv_info_misc ("RtKit not supported on this platform");

	return FALSE;
}
#endif
