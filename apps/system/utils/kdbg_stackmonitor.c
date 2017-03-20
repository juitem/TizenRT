/****************************************************************************
 *
 * Copyright 2016 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ****************************************************************************/
/****************************************************************************
 * apps/system/utils/kdbg_stackmonitor.c
 *
 *   Copyright (C) 2013 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include <tinyara/config.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sched.h>
#include <pthread.h>
#include <errno.h>
#include <tinyara/arch.h>
#include <tinyara/clock.h>
#include <tinyara/sched.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/
#define STKMON_PREFIX "Stack Monitor: "

/* Configuration ************************************************************/

#define STACKMONITOR_STACKSIZE 1024

#ifndef CONFIG_STACKMONITOR_PRIORITY
#define CONFIG_STACKMONITOR_PRIORITY 100
#endif

#ifndef CONFIG_STACKMONITOR_INTERVAL
#define CONFIG_STACKMONITOR_INTERVAL 5
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/
struct stkmon_save_s {
	systime_t timestamp;
	pid_t chk_pid;
	size_t chk_stksize;
	size_t chk_peaksize;
#ifdef CONFIG_DEBUG_MM_HEAPINFO
	int chk_peakheap;
#endif
#if (CONFIG_TASK_NAME_SIZE > 0)
	char chk_name[CONFIG_TASK_NAME_SIZE + 1];
#endif
};

/****************************************************************************
 * Private Data
 ****************************************************************************/
static volatile bool stkmon_started;
struct stkmon_save_s stkmon_arr[CONFIG_MAX_TASKS * 2];
static int stkmon_chk_idx;

/****************************************************************************
 * Private Functions
 ****************************************************************************/
static void stkmon_title_print(void)
{
#ifdef CONFIG_DEBUG_MM_HEAPINFO
	printf("%-5s %s %8s %5s %7s %7s ", "PID", "STATUS", "SIZE", "PEAK_STACK", "PEAK_HEAP", "TIME");
#else
	printf("%-5s %s %8s %5s %7s\n", "PID", "STATUS", "SIZE", "PEAK_STACK", "TIME");
#endif
#if (CONFIG_TASK_NAME_SIZE > 0)
	printf("THREAD NAME\n");
#else
	printf("\n");
#endif
}

static void stkmon_inactive_check(void)
{
	int inactive_idx;
	for (inactive_idx = 0; inactive_idx < CONFIG_MAX_TASKS * 2; inactive_idx++) {
		if (stkmon_arr[inactive_idx].timestamp != 0) {
#ifdef CONFIG_DEBUG_MM_HEAPINFO
			printf("%5d %s %6d %9d %10d %7lld ", stkmon_arr[inactive_idx].chk_pid, "INACTIVE", stkmon_arr[inactive_idx].chk_stksize, stkmon_arr[inactive_idx].chk_peaksize, stkmon_arr[inactive_idx].chk_peakheap, (uint64_t)((systime_t)stkmon_arr[inactive_idx].timestamp));
#else
			printf("%5d %s %6d %9d %7lld ", stkmon_arr[inactive_idx].chk_pid, "INACTIVE", stkmon_arr[inactive_idx].chk_stksize, stkmon_arr[inactive_idx].chk_peaksize, (uint64_t)((systime_t)stkmon_arr[inactive_idx].timestamp));
#endif
#if (CONFIG_TASK_NAME_SIZE > 0)
			printf("%s\n", stkmon_arr[inactive_idx].chk_name);
#else
			printf("\n");
#endif
			stkmon_arr[inactive_idx].timestamp = 0;
		}
	}
}

static void stkmon_active_check(struct tcb_s *tcb, void *arg)
{
	if (tcb->pid == 0) {
		tcb->adj_stack_size = CONFIG_IDLETHREAD_STACKSIZE;
	}
#ifdef CONFIG_DEBUG_MM_HEAPINFO
	printf("%5d %s %9d %8d %10d %7lld ", tcb->pid, "ACTIVE", tcb->adj_stack_size, up_check_tcbstack(tcb), tcb->peak_alloc_size, (uint64_t)((systime_t)clock_systimer()));
#else
	printf("%5d %s %9d %8d %7lld", tcb->pid, "ACTIVE", tcb->adj_stack_size, up_check_tcbstack(tcb), (uint64_t)((systime_t)clock_systimer()));
#endif
#if (CONFIG_TASK_NAME_SIZE > 0)
	printf("%s\n", tcb->name);
#else
	printf("\n");
#endif
}

static void *stackmonitor_daemon(void *arg)
{
	printf(STKMON_PREFIX "Running\n");

	/* Loop until we detect that there is a request to stop. */
	while (stkmon_started) {
		printf("===============================================================\n");
		stkmon_title_print();
		printf("---------------------------------------------------------------\n");
		stkmon_inactive_check();
		printf("---------------------------------------------------------------\n");
		sched_foreach(stkmon_active_check, NULL);
		sleep(CONFIG_STACKMONITOR_INTERVAL);
	}

	/* Stopped */
	printf(STKMON_PREFIX "Stopped well\n");
	return OK;
}

static void stackmonitor_stop(void)
{
	/* Has the monitor already started? */

	if (stkmon_started) {
		/* Stop the stack monitor.  The next time the monitor wakes up,
		 * it will see the the stop indication and will exist.
		 */
		printf(STKMON_PREFIX "Stopping, not stopped yet\n");
		stkmon_started = FALSE;
	} else {
		printf(STKMON_PREFIX "There is no active StackMonitor\n");
	}
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/
void stkmon_logging(struct tcb_s *tcb)
{
	stkmon_arr[stkmon_chk_idx % (CONFIG_MAX_TASKS * 2)].timestamp = clock_systimer();
	stkmon_arr[stkmon_chk_idx % (CONFIG_MAX_TASKS * 2)].chk_pid = tcb->pid;
	stkmon_arr[stkmon_chk_idx % (CONFIG_MAX_TASKS * 2)].chk_stksize = tcb->adj_stack_size;
	stkmon_arr[stkmon_chk_idx % (CONFIG_MAX_TASKS * 2)].chk_peaksize = up_check_tcbstack(tcb);
#ifdef CONFIG_DEBUG_MM_HEAPINFO
	stkmon_arr[stkmon_chk_idx % (CONFIG_MAX_TASKS * 2)].chk_peakheap = tcb->peak_alloc_size;
#endif
#if (CONFIG_TASK_NAME_SIZE > 0)
	for (name_idx = 0; name_idx < CONFIG_TASK_NAME_SIZE + 1; name_idx++) {
		stkmon_arr[stkmon_chk_idx % (CONFIG_MAX_TASKS * 2)].chk_name[name_idx] = tcb->name[name_idx];
	}
#endif
	stkmon_chk_idx %= (CONFIG_MAX_TASKS * 2);
	stkmon_chk_idx++;
}

int kdbg_stackmonitor(int argc, char **args)
{
	pthread_t stkmon;
	pthread_attr_t stkmon_attr;

	/* stop the stackmonitor */
	if (argc > 1 && strcmp(args[1], "stop") == 0) {
		stackmonitor_stop();
		return OK;
	}

	/* Has the monitor already started? */

	if (!stkmon_started) {
		int ret;

		/* No.. start it now */

		/* Then start the stack monitoring daemon */

		stkmon_started = TRUE;

		pthread_attr_init(&stkmon_attr);
		stkmon_attr.stacksize = STACKMONITOR_STACKSIZE;
		stkmon_attr.priority = CONFIG_STACKMONITOR_PRIORITY;
		stkmon_attr.inheritsched = PTHREAD_EXPLICIT_SCHED;

		ret = pthread_create(&stkmon, &stkmon_attr, stackmonitor_daemon, NULL);
		if (ret != OK) {
			printf(STKMON_PREFIX "ERROR: Failed to start the stack monitor: %d\n", errno);
			stkmon_started = FALSE;
			return ERROR;
		}
		pthread_setname_np(stkmon, "StackMonitor");

		ret = pthread_detach(stkmon);
		if (ret != OK) {
			printf(STKMON_PREFIX "ERROR: Failed to detach the stack monitor: %d\n", errno);
			pthread_cancel(stkmon);
		}
	} else {
		printf(STKMON_PREFIX "already started\n");
	}

	return OK;
}
