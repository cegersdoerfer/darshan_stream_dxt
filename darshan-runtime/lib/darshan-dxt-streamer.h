/*
 * Copyright (C) 2019 University of Chicago.
 * See COPYRIGHT notice in top-level directory.
 *
 */

#ifndef __DARSHAN_DXT_STREAMER_H
#define __DARSHAN_DXT_STREAMER_H
#include "darshan.h"

#ifdef HAVE_DXT_STREAMER
#include <ovis_util/util.h>
#include <semaphore.h>

typedef struct darshanConnector {
        int to;
        int dxt_streamer_lib;
        int posix_enable_dxt_streamer;
        int mpiio_enable_dxt_streamer;
        int stdio_enable_dxt_streamer;
        int hdf5_enable_dxt_streamer;
        char *exename;
	const char *schema;
        const char* env_dxt_streamer_stream;
        const char* env_dxt_streamer_reinit;
        int server_rc;
        int64_t jobid;
        int64_t uid;
        int64_t hdf5_data[5];
        int64_t open_count;
        int64_t write_count;
        int64_t read_count;
        int64_t stat_count;
        struct timespec ts;
        pthread_mutex_t ln_lock;
        ldms_t dxt_streamer_darsh;
} darshanConnector;

#else

typedef struct darshanConnector {
        int dxt_streamer_lib;
        int posix_enable_dxt_streamer;
        int mpiio_enable_dxt_streamer;
        int stdio_enable_dxt_streamer;
        int hdf5_enable_dxt_streamer;
} darshanConnector;

#endif

/* darshan_ldms_connector_initialize(), darshan_ldms_connector_send()
 *
 * LDMS related function to intialize LDMSD streams plugin for realtime data
 * output of the Darshan modules.
 *
 * LDMS related function to retrieve and send the realitme data output of the Darshan
 * specified module from the set environment variables (i.e. *MODULENAME*_ENABLE_LDMS)
 * to LDMSD streams plugin.
 *
 * LDMS related function to retrieve and set the meta data of each Darshan
 * run (i.e. record id, rank, etc.). These values will not be updated unless a different module
 * is detected or a new run is executed.
 *
 */
void darshan_dxt_streamer_initialize(struct darshan_core_runtime *);

void darshan_dxt_streamer_connector_send(uint64_t record_id, int64_t rank, int64_t record_count, char *rwo, int64_t offset, int64_t length, int64_t max_byte, int64_t rw_switch, int64_t flushes, double start_time, double end_time, double total_time, char *mod_name, char *data_type);

#endif /* __DARSHAN_DXT_STREAMER_H */