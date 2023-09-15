@@ -0,0 +1,271 @@
/*
 *   Copyright (C) 2016 Intel Corporation.
 *   See COPYRIGHT notice in top-level directory.
 *    
 */

#ifdef HAVE_CONFIG_H
# include <darshan-runtime-config.h>
#endif

#define _XOPEN_SOURCE 500
#define _GNU_SOURCE
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "darshan-dxt-streamer.h"
#include "darshan.h"

/* Check for DXT streamer libraries if Darshan is built --with-dxt-streamer */
#ifdef HAVE_DXT_STREAMER

struct darshanConnector dC = {
     .schema = "darshanConnector",
     .dxt_streamer_darsh = NULL,
     .exename = NULL,
     .dxt_streamer_lib = 0,
     .jobid = 0,
     };

void darshan_dxt_streamer_initialize(struct darshan_core_runtime *init_core)
{
    /* Set flags for various DXT Streamer environment variables */
    if (getenv("POSIX_ENABLE_DXT_STREAMER"))
        dC.posix_enable_dxt_streamer = 0;
    else
        dC.posix_enable_dxt_streamer = 1;

    if (getenv("MPIIO_ENABLE_DXT_STREAMER"))
        dC.mpiio_enable_dxt_streamer = 0;
    else
        dC.mpiio_enable_dxt_streamer = 1;

    /* Disable STDIO if verbose is enabled to avoid a recursive
    function for darshan_dxt_streamer_connector_send() */
    if (getenv("STDIO_ENABLE_DXT_STREAMER"))
        if (!getenv("DARSHAN_DXT_STREAMER_VERBOSE"))
            dC.stdio_enable_dxt_streamer = 0;
        else
            dC.stdio_enable_dxt_streamer = 1;
    else
        dC.stdio_enable_dxt_streamer = 1;

    dC.env_dxt_streamer_stream = "darshanConnector";


    if (getenv("HDF5_ENABLE_DXT_STREAMER"))
        dC.hdf5_enable_dxt_streamer = 0;
    else
        dC.hdf5_enable_dxt_streamer = 1;

    /*
    pthread_mutex_lock(&dC.ln_lock);
    dC.ldms_darsh = setup_connection(env_ldms_xprt, env_ldms_host, env_ldms_port, env_ldms_auth);
        if (dC.conn_status != 0) {
            printf("Error setting up connection to LDMS streams daemon: %i -- exiting\n", dC.conn_status);
            pthread_mutex_unlock(&dC.ln_lock);
            return;
        }
        else if (dC.ldms_darsh->disconnected){
            printf("Disconnected from LDMS streams daemon -- exiting\n");
            pthread_mutex_unlock(&dC.ln_lock);
            return;
        }
    pthread_mutex_unlock(&dC.ln_lock);
    */
    return;
}

void darshan_dxt_streamer_connector_send(uint64_t record_id, int64_t rank, int64_t record_count, char *rwo, int64_t offset, int64_t length, int64_t max_byte, int64_t rw_switch, int64_t flushes,  double start_time, double end_time, double total_time, char *mod_name, char *data_type)
{
    char jb11[1024];
    int rc, ret, i, size, exists;
    const char *filepath;
    struct timespec tspec_start, tspec_end;
    uint64_t micro_s;

    dC.env_dxt_streamer_stream  = getenv("DARSHAN_DXT_STRREAMER_STREAM");

    /* get the full file path from record ID */
    filepath = darshan_core_lookup_record_name(record_id);

    if (strcmp(rwo, "open") == 0)
        dC.open_count = record_count;

    /* set record count of closes to number of opens since we are closing the same file we opened.*/
    if (strcmp(rwo, "close") == 0)
        record_count = dC.open_count;

    if (strcmp(mod_name, "H5D") != 0){
        size = sizeof(dC.hdf5_data)/sizeof(dC.hdf5_data[0]);
        for (i=0; i < size; i++)
            dC.hdf5_data[i] = -1;
    }

    /* set following fields for module data to N/A to reduce message size */
    if (strcmp(data_type, "MOD") == 0)
    {
        filepath = "N/A";
        dC.exename = "N/A";
	dC.schema = "N/A";
    }

    /* convert the start and end times to timespecs and report absolute timestamps */
    tspec_start = darshan_core_abs_timespec_from_wtime(start_time);
    tspec_end = darshan_core_abs_timespec_from_wtime(end_time);
    micro_s = tspec_end.tv_nsec/1.0e3;

    sprintf(jb11,"{\"schema\":%s, \"uid\":%ld, \"exe\":\"%s\",\"job_id\":%ld,\"rank\":%ld,\"ProducerName\":\"%s\",\"file\":\"%s\",\"record_id\":%"PRIu64",\"module\":\"%s\",\"type\":\"%s\",\"max_byte\":%ld,\"switches\":%ld,\"flushes\":%ld,\"cnt\":%ld,\"op\":\"%s\",\"seg\":[{\"pt_sel\":%ld,\"irreg_hslab\":%ld,\"reg_hslab\":%ld,\"ndims\":%ld,\"npoints\":%ld,\"off\":%ld,\"len\":%ld,\"start\":%0.6f,\"dur\":%0.6f,\"total\":%0.6f,\"timestamp\":%lu.%.6lu}]}", dC.schema, dC.uid, dC.exename, dC.jobid, rank, dC.hname, filepath, record_id, mod_name, data_type, max_byte, rw_switch, flushes, record_count, rwo, dC.hdf5_data[0], dC.hdf5_data[1], dC.hdf5_data[2], dC.hdf5_data[3], dC.hdf5_data[4], offset, length, start_time, end_time-start_time, total_time, tspec_end.tv_sec, micro_s);

    if (getenv("DARSHAN_LDMS_VERBOSE"))
            printf("JSON Message: %s\n", jb11);

    rc = dxt_streamerd_stream_publish(dC.dxt_streamer_darsh, dC.env_dxt_streamer_stream, DXT_STREAMERD_STREAM_JSON, jb11, strlen(jb11) + 1);
    if (rc)
        printf("Error %d publishing data.\n", rc);

    out_1:
         return;
}
#else

struct darshanConnector dC = {
 .ldms_lib = 1
 };

void darshan_ldms_connector_initialize(struct darshan_core_runtime *init_core)
{
    return;
}

void darshan_ldms_connector_send(uint64_t record_id, int64_t rank, int64_t record_count, char *rwo, int64_t offset, int64_t length, int64_t max_byte, int64_t rw_switch, int64_t flushes,  double start_time, double end_time, double total_time, char *mod_name, char *data_type)
{
    return;
}
#endif