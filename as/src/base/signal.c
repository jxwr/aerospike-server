/*
 * signal.c
 *
 * Copyright (C) 2010-2014 Aerospike, Inc.
 *
 * Portions may be licensed to Aerospike, Inc. under one or more contributor
 * license agreements.
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/
 */

#include <execinfo.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include "fault.h"


// The mutex that the main function deadlocks on after starting the service.
extern pthread_mutex_t g_NONSTOP;
extern bool g_startup_complete;

// String constants in version.c, generated by make.
extern const char aerospike_build_type[];
extern const char aerospike_build_id[];

extern void xdr_sig_handler(int signum);

#define MAX_BACKTRACE_DEPTH 50

// We get here on normal shutdown.
sighandler_t g_old_term_handler = 0;
void
as_sig_handle_term(int sig_num)
{
	cf_warning(AS_AS, "SIGTERM received, shutting down");

	as_xdr_stop();
	xdr_sig_handler(sig_num);

	if (g_old_term_handler) {
		g_old_term_handler(sig_num);
	}

	if (! g_startup_complete) {
		cf_warning(AS_AS, "startup was not complete, exiting immediately");
		_exit(0);
	}

	pthread_mutex_unlock(&g_NONSTOP);
}

// We get here on cf_crash(), cf_assert().
sighandler_t g_old_abort_handler = 0;
void
as_sig_handle_abort(int sig_num)
{
	cf_warning(AS_AS, "SIGABRT received, aborting %s build %s",
			aerospike_build_type, aerospike_build_id);

	xdr_sig_handler(sig_num);

	PRNSTACK();

	if (g_old_abort_handler) {
		g_old_abort_handler(sig_num);
	}
}

sighandler_t g_old_fpe_handler = 0;
void
as_sig_handle_fpe(int sig_num)
{
	cf_warning(AS_AS, "SIGFPE received, aborting %s build %s",
			aerospike_build_type, aerospike_build_id);

	xdr_sig_handler(sig_num);

	PRNSTACK();

	if (g_old_fpe_handler) {
		g_old_fpe_handler(sig_num);
	}
}

// We get here on cf_crash_nostack(), cf_assert_nostack().
sighandler_t g_old_int_handler = 0;
void
as_sig_handle_int(int sig_num)
{
	cf_warning(AS_AS, "SIGINT received, shutting down");

	as_xdr_stop();
	xdr_sig_handler(sig_num);

	if (g_old_int_handler) {
		g_old_int_handler(sig_num);
	}

	if (! g_startup_complete) {
		cf_warning(AS_AS, "startup was not complete, exiting immediately");
		_exit(0);
	}

	pthread_mutex_unlock(&g_NONSTOP);
}

sighandler_t g_old_hup_handler = 0;
void
as_sig_handle_hup(int sig_num)
{
	if (g_old_hup_handler) {
		g_old_hup_handler(sig_num);
	}

	cf_info(AS_AS, "SIGHUP received, rolling log");

	cf_fault_sink_logroll();
}

sighandler_t g_old_segv_handler = 0;
void
as_sig_handle_segv(int sig_num)
{
	cf_warning(AS_AS, "SIGSEGV received, aborting %s build %s",
			aerospike_build_type, aerospike_build_id);

	PRNSTACK();

	xdr_sig_handler(sig_num);

	if (g_old_segv_handler) {
		g_old_segv_handler (sig_num);
	}

	_exit(-1);
}

sighandler_t g_old_bus_handler = 0;
void
as_sig_handle_bus(int sig_num)
{
	cf_warning(AS_AS, "SIGBUS received, aborting %s build %s",
			aerospike_build_type, aerospike_build_id);

	xdr_sig_handler(sig_num);

	void *bt[MAX_BACKTRACE_DEPTH];
	int sz = backtrace(bt, MAX_BACKTRACE_DEPTH);
	char **strings = backtrace_symbols(bt, sz);

	for (int i = 0; i < sz; i++) {
		cf_warning(AS_AS, "stacktrace: frame %d: %s", i, strings[i]);
	}

	fflush(NULL);
}


void
as_signal_setup() {
	// Clean shutdowns
	g_old_int_handler = signal(SIGINT , as_sig_handle_int);
	g_old_term_handler = signal(SIGTERM , as_sig_handle_term);

	// "Crash" handlers
	g_old_segv_handler = signal(SIGSEGV, as_sig_handle_segv);
	g_old_abort_handler = signal(SIGABRT , as_sig_handle_abort);
	g_old_fpe_handler = signal(SIGFPE , as_sig_handle_fpe);
	g_old_bus_handler = signal(SIGBUS , as_sig_handle_bus);

	// Signal for log roation	
	g_old_hup_handler = signal(SIGHUP, as_sig_handle_hup);

	// Block SIGPIPE signal when there is some error while writing to pipe. The
	// write() call will return with a normal error which we can handle.
	struct sigaction sigact;

	memset(&sigact, 0, sizeof(sigact));
	sigact.sa_handler = SIG_IGN;
	sigemptyset(&sigact.sa_mask);
	sigaddset(&sigact.sa_mask, SIGPIPE);

	if (sigaction(SIGPIPE, &sigact, NULL) != 0) {
		cf_warning(AS_AS, "Not able to block the SIGPIPE signal");
	}
}
