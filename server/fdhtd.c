/**
* Copyright (C) 2008 Happy Fish / YuQing
*
* FastDFS may be copied only under the terms of the GNU General
* Public License V3, which may be found in the FastDFS source kit.
* Please visit the FastDFS Home Page http://www.csource.org/ for more detail.
**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <event.h>
#include "shared_func.h"
#include "logger.h"
#include "fdht_global.h"
#include "global.h"
#include "ini_file_reader.h"
#include "sockopt.h"
#include "sched_thread.h"
#include "task_queue.h"
#include "recv_thread.h"
#include "send_thread.h"
#include "work_thread.h"
#include "func.h"
#include "sync.h"
#include "db_recovery.h"

static ScheduleArray scheduleArray;
static pthread_t schedule_tid;

static int fdht_init_schedule();
static void sigQuitHandler(int sig);
static void sigHupHandler(int sig);
static void sigUsrHandler(int sig);

static int create_sock_io_threads(int server_sock);

int main(int argc, char *argv[])
{
	char *conf_filename;
	char bind_addr[IP_ADDRESS_SIZE];
	
	int result;
	int sock;
	struct sigaction act;

	memset(bind_addr, 0, sizeof(bind_addr));
	if (argc < 2)
	{
		fprintf(stderr, "Usage: %s <config_file>\n", argv[0]);
		return 1;
	}

	conf_filename = argv[1];
	if ((result=fdht_func_init(conf_filename, bind_addr, \
		sizeof(bind_addr))) != 0)
	{
		return result;
	}

	if ((result=init_pthread_lock(&g_storage_thread_lock)) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"init_pthread_lock fail, program exit!", __LINE__);
		fdht_func_destroy();
		return result;
	}

	g_log_level = LOG_DEBUG;
	sock = socketServer(bind_addr, g_server_port, &result);
	if (sock < 0)
	{
		fdht_func_destroy();
		return result;
	}

	if ((result=tcpsetnonblockopt(sock, g_network_timeout)) != 0)
	{
		fdht_func_destroy();
		return result;
	}

	daemon_init(true);
	umask(0);
	
	memset(&act, 0, sizeof(act));
	sigemptyset(&act.sa_mask);

	act.sa_handler = sigUsrHandler;
	if(sigaction(SIGUSR1, &act, NULL) < 0 || \
		sigaction(SIGUSR2, &act, NULL) < 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"call sigaction fail, errno: %d, error info: %s", \
			__LINE__, errno, strerror(errno));
		fdht_func_destroy();
		return errno;
	}

	act.sa_handler = sigHupHandler;
	if(sigaction(SIGHUP, &act, NULL) < 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"call sigaction fail, errno: %d, error info: %s", \
			__LINE__, errno, strerror(errno));
		fdht_func_destroy();
		return errno;
	}
	
	act.sa_handler = SIG_IGN;
	if(sigaction(SIGPIPE, &act, NULL) < 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"call sigaction fail, errno: %d, error info: %s", \
			__LINE__, errno, strerror(errno));
		fdht_func_destroy();
		return errno;
	}

	act.sa_handler = sigQuitHandler;
	if(sigaction(SIGINT, &act, NULL) < 0 || \
		sigaction(SIGTERM, &act, NULL) < 0 || \
		sigaction(SIGABRT, &act, NULL) < 0 || \
		sigaction(SIGSEGV, &act, NULL) < 0 || \
		sigaction(SIGQUIT, &act, NULL) < 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"call sigaction fail, errno: %d, error info: %s", \
			__LINE__, errno, strerror(errno));
		fdht_func_destroy();
		return errno;
	}

	if ((result=fdht_sync_init()) != 0)
	{
		fdht_func_destroy();
		return result;
	}

	if ((result=fdht_db_recovery_init()) != 0)
	{
		fdht_func_destroy();
		return result;
	}

	if ((result=task_queue_init()) != 0)
	{
		g_continue_flag = false;
		fdht_func_destroy();
		return result;
	}

	if ((result=work_thread_init()) != 0)
	{
		g_continue_flag = false;
		fdht_func_destroy();
		return result;
	}

	if ((result=fdht_init_schedule()) != 0)
	{
		g_continue_flag = false;
		work_thread_destroy();
		fdht_func_destroy();
		return result;
	}

	log_set_cache(true);

	if ((result=create_sock_io_threads(sock)) != 0)
	{
		fdht_terminate();
		work_thread_destroy();
		fdht_func_destroy();
		log_destory();
		return result;
	}

	pthread_mutex_destroy(&g_storage_thread_lock);

	work_thread_destroy();

	task_queue_destroy();

	close(sock);

	while (g_schedule_flag) //waiting for schedule thread exit
	{
		sleep(1);
	}

	fdht_sync_destroy();
	fdht_memp_trickle_dbs((void *)1);

	fdht_func_destroy();

	logInfo("exit nomally.\n");
	log_destory();
	
	return 0;
}

static void sigQuitHandler(int sig)
{
	if (g_continue_flag)
	{
		pthread_kill(schedule_tid, SIGINT);
		fdht_terminate();
		logCrit("file: "__FILE__", line: %d, " \
			"catch signal %d, program exiting...", \
			__LINE__, sig);

		/*
		//printf("free queue count: %d, recv queue count: %d, " \
			"work queue count=%d, send queue count=%d\n", \
			free_queue_count(), recv_queue_count(),  \
			work_queue_count(), send_queue_count());
		fflush(stdout);
		*/
	}
}

static void sigHupHandler(int sig)
{
}

static void sigUsrHandler(int sig)
{
	/*
	logInfo("current thread count=%d, " \
		"mo count=%d, success count=%d", g_thread_count, \
		nMoCount, nSuccMoCount);
	*/
}

static int create_sock_io_threads(int server_sock)
{
	int result;
	pthread_t recv_tid;
	pthread_t send_tid;

	result = 0;

	if ((result=pthread_create(&recv_tid, NULL, \
		recv_thread_entrance, (void *)server_sock)) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"create recv thread failed, " \
			"errno: %d, error info: %s", \
			__LINE__, result, strerror(result));
		return result;
	}

	if ((result=pthread_create(&send_tid, NULL, \
		send_thread_entrance, NULL)) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"create send thread failed, " \
			"errno: %d, error info: %s", \
			__LINE__, result, strerror(result));
		return result;
	}

	if ((result=pthread_join(recv_tid, NULL)) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"pthread_join fail, " \
			"errno: %d, error info: %s", \
			__LINE__, result, strerror(result));
	}

	if ((result=pthread_join(send_tid, NULL)) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"pthread_join fail, " \
			"errno: %d, error info: %s", \
			__LINE__, result, strerror(result));
	}

	return result;
}

static int fdht_init_schedule()
{
	int entry_count;
	int i;
	ScheduleEntry *pScheduleEntry;

	entry_count = 1;
	if (g_sync_db_interval > 0)
	{
		entry_count++;
	}
	if (g_clear_expired_interval > 0)
	{
		for (i=0; i<g_db_count; i++)
        	{
               		if (g_db_list[i] != NULL)
			{
				entry_count++;
			}
		}
	}

	scheduleArray.entries = (ScheduleEntry *)malloc( \
				sizeof(ScheduleEntry) * entry_count);
	if (scheduleArray.entries == NULL)
	{
		logError("file: "__FILE__", line: %d, " \
			"malloc %d bytes fail, " \
			"errno: %d, error info: %s", \
			__LINE__, sizeof(ScheduleEntry) * entry_count, \
			errno, strerror(errno));
		return errno != 0 ? errno : ENOMEM;
	}

	pScheduleEntry = scheduleArray.entries;
	scheduleArray.count = entry_count;

	memset(pScheduleEntry, 0, sizeof(ScheduleEntry) * entry_count);
	pScheduleEntry->id = pScheduleEntry - scheduleArray.entries + 1;
	pScheduleEntry->time_base.hour = TIME_NONE;
	pScheduleEntry->time_base.minute = TIME_NONE;
	pScheduleEntry->interval = g_sync_log_buff_interval;
	pScheduleEntry->task_func = log_sync_func;
	pScheduleEntry->func_args = NULL;

	if (g_sync_db_interval > 0)
	{
		pScheduleEntry++;
		pScheduleEntry->id = pScheduleEntry - scheduleArray.entries+1;
		pScheduleEntry->time_base.hour = g_sync_db_time_base.hour;
		pScheduleEntry->time_base.minute = g_sync_db_time_base.minute;
		pScheduleEntry->interval = g_sync_db_interval;
		pScheduleEntry->task_func = fdht_memp_trickle_dbs;
		pScheduleEntry->func_args = NULL;
	}

	for (i=0; i<g_db_count; i++)
       	{
		if (g_db_list[i] == NULL)
		{
			continue;
		}

		pScheduleEntry++;
		pScheduleEntry->id = pScheduleEntry - scheduleArray.entries+1;
		pScheduleEntry->time_base.hour = g_clear_expired_time_base.hour;
		pScheduleEntry->time_base.minute = g_clear_expired_time_base.minute;
		pScheduleEntry->interval = g_clear_expired_interval;
		pScheduleEntry->task_func = db_clear_expired_keys;
		pScheduleEntry->func_args = (void *)i;
	}

	return sched_start(&scheduleArray, &schedule_tid);
}

