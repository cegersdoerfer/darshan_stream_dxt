/*
 * Copyright (C) 2016 Intel Corporation.
 * See COPYRIGHT notice in top-level directory.
 *
 */

#ifdef HAVE_CONFIG_H
# include <darshan-runtime-config.h>
#endif

#define _XOPEN_SOURCE 500
#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <search.h>
#include <assert.h>
#include <libgen.h>
#include <pthread.h>
#include <regex.h>

#include "utlist.h"
#include "uthash.h"
#include "darshan.h"
#include "darshan-dynamic.h"
#include "darshan-dxt.h"

#ifndef HAVE_OFF64_T
typedef int64_t off64_t;
#endif

/* maximum amount of memory to use for storing DXT records */
#ifdef __DARSHAN_MOD_MEM_MAX
#define DXT_IO_TRACE_MEM_MAX (__DARSHAN_MOD_MEM_MAX * 1024L * 1024L * 1024L)
#else
/* 2 MiB default */
#define DXT_IO_TRACE_MEM_MAX (1024 * 1024 * 1024)
#endif

/* Darshan core expects modules to express memory requirements in terms
 * of a fixed-length record size and a total number of records. DXT records
 * are naturally variable in-length, but for simplicity we define each record
 * as being 1 KiB in size
 */
// #define DXT_DEF_RECORD_SIZE 1024
#define DXT_DEF_RECORD_SIZE 4096

/* initial size of read/write trace buffer (in number of segments) */
/* NOTE: when this size is exceeded, the buffer size is doubled */
#define IO_TRACE_BUF_SIZE 64

/* The dxt_file_record_ref structure maintains necessary runtime metadata
 * for the DXT file record (dxt_file_record structure, defined in
 * darshan-dxt-log-format.h) pointed to by 'file_rec'. This metadata
 * assists with the instrumenting of specific statistics in the file record.
 *
 * NOTE: we use the 'darshan_record_ref' interface (in darshan-common) to
 * associate different types of handles with this dxt_file_record_ref struct.
 * This allows us to index this struct (and the underlying file record) by using
 * either the corresponding Darshan record identifier (derived from the filename)
 * or by a generated file descriptor, for instance. Note that, while there should
 * only be a single Darshan record identifier that indexes a dxt_file_record_ref,
 * there could be multiple open file descriptors that index it.
 */
struct dxt_file_record_ref
{
    struct dxt_file_record *file_rec;

    int64_t write_available_buf;
    int64_t read_available_buf;
    int64_t open_available_buf;
    int64_t stat_available_buf;

    segment_info *write_traces;
    segment_info *read_traces;
    segment_info *open_traces;
    segment_info *stat_traces;
};

/* The dxt_runtime structure maintains necessary state for storing
 * DXT file records and for coordinating with darshan-core at
 * shutdown time.
 */
struct dxt_runtime
{
    void *rec_id_hash;
    int file_rec_count;
    size_t mem_allocated;
    size_t mem_used;
    char *record_buf;
    int record_buf_size;
    int frozen; /* flag to indicate that the counters should no longer be modified */
};

/* internal helper routines */
static void check_wr_trace_buf(
    struct dxt_file_record_ref *rec_ref, darshan_module_id mod_id,
    struct dxt_runtime *runtime);
static void check_rd_trace_buf(
    struct dxt_file_record_ref *rec_ref, darshan_module_id mod_id,
    struct dxt_runtime *runtime);
static void check_open_trace_buf(
    struct dxt_file_record_ref *rec_ref, darshan_module_id mod_id,
    struct dxt_runtime *runtime);
static void check_stat_trace_buf(
    struct dxt_file_record_ref *rec_ref, darshan_module_id mod_id,
    struct dxt_runtime *runtime);
static struct dxt_file_record_ref *dxt_posix_track_new_file_record(
    darshan_record_id rec_id);
static struct dxt_file_record_ref *dxt_mpiio_track_new_file_record(
    darshan_record_id rec_id);

/* DXT output/cleanup routines for darshan-core */
static void dxt_posix_output(
    void **dxt_buf, int *dxt_buf_sz);
static void dxt_mpiio_output(
    void **dxt_buf, int *dxt_buf_sz);
static void dxt_posix_cleanup(
    void);
static void dxt_mpiio_cleanup(
    void);

/* POSIX module helper for filtering DXT trace records */
extern struct darshan_posix_file *darshan_posix_rec_id_to_file(
    darshan_record_id rec_id);

static struct dxt_runtime *dxt_posix_runtime = NULL;
static struct dxt_runtime *dxt_mpiio_runtime = NULL;
static pthread_mutex_t dxt_runtime_mutex =
            PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

static int dxt_my_rank = -1;

#define DXT_LOCK() pthread_mutex_lock(&dxt_runtime_mutex)
#define DXT_UNLOCK() pthread_mutex_unlock(&dxt_runtime_mutex)

/************************************************************
 *  DXT routines exposed to Darshan core and other modules  *
 ************************************************************/

/* initialize internal DXT module data structures and register with darshan-core */
void dxt_posix_runtime_initialize()
{
    /* calculate how many "records" to request from Darshan core using DXT's
     * configured max memory consumption and the default record size
     */
    size_t dxt_psx_rec_count = DXT_IO_TRACE_MEM_MAX / DXT_DEF_RECORD_SIZE;
    printf("DXT_IO_TRACE_MEM_MAX: %zu\n", DXT_IO_TRACE_MEM_MAX);
    fflush(stdout);
    darshan_module_funcs mod_funcs = {
#ifdef HAVE_MPI
    .mod_redux_func = NULL,
#endif
    .mod_output_func = &dxt_posix_output,
    .mod_cleanup_func = &dxt_posix_cleanup
    };
    printf("mod_funcs: %p\n", mod_funcs);
    fflush(stdout);
    int ret;

    /* register the DXT module with darshan core */
    printf("enter darshan_core_register_module\n");
    fflush(stdout);
    ret = darshan_core_register_module(
        DXT_POSIX_MOD,
        mod_funcs,
        DXT_DEF_RECORD_SIZE,
        &dxt_psx_rec_count,
        &dxt_my_rank,
        NULL);
    printf("dxt_psx_rec_count: %zu\n", dxt_psx_rec_count);

    
    if(ret < 0)
        return;

    DXT_LOCK();
    dxt_posix_runtime = malloc(sizeof(*dxt_posix_runtime));
    printf("dxt_posix_runtime: %p\n", dxt_posix_runtime);
    if(!dxt_posix_runtime)
    {
        darshan_core_unregister_module(DXT_POSIX_MOD);
        DXT_UNLOCK();
        return;
    }
    memset(dxt_posix_runtime, 0, sizeof(*dxt_posix_runtime));
    dxt_posix_runtime->mem_used = 0;
    dxt_posix_runtime->mem_allocated = dxt_psx_rec_count * DXT_DEF_RECORD_SIZE;
    printf("dxt_posix_runtime->mem_allocated: %zu\n", dxt_posix_runtime->mem_allocated);
    DXT_UNLOCK();

    return;
}

void dxt_mpiio_runtime_initialize()
{
    /* calculate how many "records" to request from Darshan core using DXT's
     * configured max memory consumption and the default record size
     */
    size_t dxt_mpiio_rec_count = DXT_IO_TRACE_MEM_MAX / DXT_DEF_RECORD_SIZE;
    darshan_module_funcs mod_funcs = {
#ifdef HAVE_MPI
    .mod_redux_func = NULL,
#endif
    .mod_output_func = &dxt_mpiio_output,
    .mod_cleanup_func = &dxt_mpiio_cleanup
    };
    int ret;

    /* register the DXT module with darshan core */
    ret = darshan_core_register_module(
        DXT_MPIIO_MOD,
        mod_funcs,
        DXT_DEF_RECORD_SIZE,
        &dxt_mpiio_rec_count,
        &dxt_my_rank,
        NULL);
    if(ret < 0)
        return;

    DXT_LOCK();
    dxt_mpiio_runtime = malloc(sizeof(*dxt_mpiio_runtime));
    if(!dxt_mpiio_runtime)
    {
        darshan_core_unregister_module(DXT_MPIIO_MOD);
        DXT_UNLOCK();
        return;
    }
    memset(dxt_mpiio_runtime, 0, sizeof(*dxt_mpiio_runtime));
    dxt_mpiio_runtime->mem_used = 0;
    dxt_mpiio_runtime->mem_allocated = dxt_mpiio_rec_count * DXT_DEF_RECORD_SIZE;
    DXT_UNLOCK();

    return;
}

void dxt_posix_write(darshan_record_id rec_id, int64_t offset,
        int64_t length, double start_time, double end_time)
{
    struct dxt_file_record_ref* rec_ref = NULL;
    struct dxt_file_record *file_rec;

    DXT_LOCK();

    if(!dxt_posix_runtime || dxt_posix_runtime->frozen)
    {
        DXT_UNLOCK();
        return;
    }

    rec_ref = darshan_lookup_record_ref(dxt_posix_runtime->rec_id_hash,
        &rec_id, sizeof(darshan_record_id));
    if(!rec_ref)
    {
        /* track new dxt file record */
        rec_ref = dxt_posix_track_new_file_record(rec_id);
        if(!rec_ref)
        {
            DXT_UNLOCK();
            return;
        }
    }

    file_rec = rec_ref->file_rec;
    check_wr_trace_buf(rec_ref, DXT_POSIX_MOD, dxt_posix_runtime);
    if(file_rec->write_count == rec_ref->write_available_buf)
    {
        /* no more memory for i/o segments ... back out */
        DXT_UNLOCK();
        return;
    }

    rec_ref->write_traces[file_rec->write_count].offset = offset;
    rec_ref->write_traces[file_rec->write_count].length = length;
    rec_ref->write_traces[file_rec->write_count].start_time = start_time;
    rec_ref->write_traces[file_rec->write_count].end_time = end_time;
    file_rec->write_count += 1;

    DXT_UNLOCK();
}

void dxt_posix_read(darshan_record_id rec_id, int64_t offset,
        int64_t length, double start_time, double end_time)
{
    struct dxt_file_record_ref* rec_ref = NULL;
    struct dxt_file_record *file_rec;

    DXT_LOCK();

    if(!dxt_posix_runtime || dxt_posix_runtime->frozen)
    {
        DXT_UNLOCK();
        return;
    }

    rec_ref = darshan_lookup_record_ref(dxt_posix_runtime->rec_id_hash,
                &rec_id, sizeof(darshan_record_id));
    if (!rec_ref)
    {
        /* track new dxt file record */
        rec_ref = dxt_posix_track_new_file_record(rec_id);
        if(!rec_ref)
        {
            DXT_UNLOCK();
            return;
        }
    }

    file_rec = rec_ref->file_rec;
    check_rd_trace_buf(rec_ref, DXT_POSIX_MOD, dxt_posix_runtime);
    if(file_rec->read_count == rec_ref->read_available_buf)
    {
        /* no more memory for i/o segments ... back out */
        DXT_UNLOCK();
        return;
    }

    rec_ref->read_traces[file_rec->read_count].offset = offset;
    rec_ref->read_traces[file_rec->read_count].length = length;
    rec_ref->read_traces[file_rec->read_count].start_time = start_time;
    rec_ref->read_traces[file_rec->read_count].end_time = end_time;
    file_rec->read_count += 1;

    DXT_UNLOCK();
}

void dxt_posix_open(darshan_record_id rec_id, double start_time,
        double end_time)
{
    struct dxt_file_record_ref* rec_ref = NULL;
    struct dxt_file_record *file_rec;

    DXT_LOCK();

    if(!dxt_posix_runtime || dxt_posix_runtime->frozen)
    {
        DXT_UNLOCK();
        return;
    }

    rec_ref = darshan_lookup_record_ref(dxt_posix_runtime->rec_id_hash,
                &rec_id, sizeof(darshan_record_id));
    if(!rec_ref)
    {
        /* track new dxt file record */
        rec_ref = dxt_posix_track_new_file_record(rec_id);
        if(!rec_ref)
        {
            DXT_UNLOCK();
            return;
        }
    }

    file_rec = rec_ref->file_rec;
    check_open_trace_buf(rec_ref, DXT_POSIX_MOD, dxt_posix_runtime);
    if(file_rec->open_count == rec_ref->open_available_buf)
    {
        /* no more memory for i/o segments ... back out */
        DXT_UNLOCK();
        return;
    }

    rec_ref->open_traces[file_rec->open_count].start_time = start_time;
    rec_ref->open_traces[file_rec->open_count].end_time = end_time;
    file_rec->open_count += 1;

    DXT_UNLOCK();
}

void write_data_to_file(const char *filename, const char *data) {
    FILE *f = fopen(filename, "a"); // open in append mode
    if (!f) {
        perror("Failed to open file");
        return;
    }
    fprintf(f, "%s\n", data);
    fclose(f);
}

void dxt_posix_stat(darshan_record_id rec_id, double start_time,
        double end_time)
{
    struct dxt_file_record_ref* rec_ref = NULL;
    struct dxt_file_record *file_rec;
    struct timespec tspec_start, tspec_end;
    uint64_t micro_s;
    char jb11[1024];
    printf("dxt_posix_stat\n");

    DXT_LOCK();

    if(!dxt_posix_runtime || dxt_posix_runtime->frozen)
    {
        DXT_UNLOCK();
        return;
    }
    printf("dxt_posix_runtime: %p\n", dxt_posix_runtime);
    rec_ref = darshan_lookup_record_ref(dxt_posix_runtime->rec_id_hash,
                &rec_id, sizeof(darshan_record_id));
    if(!rec_ref)
    {
        /* track new dxt file record */
        printf("track new dxt file record\n");
        rec_ref = dxt_posix_track_new_file_record(rec_id);
        if(!rec_ref)
        {
            printf("rec_ref is NULL\n");
            DXT_UNLOCK();
            return;
        }
    }

    file_rec = rec_ref->file_rec;
    check_stat_trace_buf(rec_ref, DXT_POSIX_MOD, dxt_posix_runtime);
    printf("file_rec->stat_count: %d\n", file_rec->stat_count);
    printf("rec_ref->stat_available_buf: %d\n", rec_ref->stat_available_buf);
    if(file_rec->stat_count == rec_ref->stat_available_buf)
    {
        /* no more memory for i/o segments ... back out */
        DXT_UNLOCK();
        printf("no more memory for i/o segments ... back out\n");
        return;
    }

    rec_ref->stat_traces[file_rec->stat_count].start_time = start_time;
    rec_ref->stat_traces[file_rec->stat_count].end_time = end_time;
    file_rec->stat_count += 1;


    /* convert the start and end times to timespecs and report absolute timestamps */
    /*
    tspec_start = darshan_core_abs_timespec_from_wtime(start_time);
    tspec_end = darshan_core_abs_timespec_from_wtime(end_time);
    micro_s = tspec_end.tv_nsec/1.0e3;
    */

    sprintf(jb11,"{\"id\":%ld \"start\":%ld \"end\":%ld}", rec_id, start_time, end_time);
    write_data_to_file("/mnt/dirlab/IOLustre/test.txt", jb11);
    printf("file_rec->stat_count: %d\n", file_rec->stat_count);

    DXT_UNLOCK();
}

void dxt_mpiio_write(darshan_record_id rec_id, int64_t offset,
        int64_t length, double start_time, double end_time)
{
    struct dxt_file_record_ref* rec_ref = NULL;
    struct dxt_file_record *file_rec;

    DXT_LOCK();

    if(!dxt_mpiio_runtime || dxt_mpiio_runtime->frozen)
    {
        DXT_UNLOCK();
        return;
    }

    rec_ref = darshan_lookup_record_ref(dxt_mpiio_runtime->rec_id_hash,
                &rec_id, sizeof(darshan_record_id));
    if(!rec_ref)
    {
        /* track new dxt file record */
        rec_ref = dxt_mpiio_track_new_file_record(rec_id);
        if(!rec_ref)
        {
            DXT_UNLOCK();
            return;
        }
    }

    file_rec = rec_ref->file_rec;
    check_wr_trace_buf(rec_ref, DXT_MPIIO_MOD, dxt_mpiio_runtime);
    if(file_rec->write_count == rec_ref->write_available_buf)
    {
        /* no more memory for i/o segments ... back out */
        DXT_UNLOCK();
        return;
    }

    rec_ref->write_traces[file_rec->write_count].length = length;
    rec_ref->write_traces[file_rec->write_count].offset = offset;
    rec_ref->write_traces[file_rec->write_count].start_time = start_time;
    rec_ref->write_traces[file_rec->write_count].end_time = end_time;
    file_rec->write_count += 1;

    DXT_UNLOCK();
}

void dxt_mpiio_read(darshan_record_id rec_id, int64_t offset,
        int64_t length, double start_time, double end_time)
{
    struct dxt_file_record_ref* rec_ref = NULL;
    struct dxt_file_record *file_rec;

    DXT_LOCK();

    if(!dxt_mpiio_runtime || dxt_mpiio_runtime->frozen)
    {
        DXT_UNLOCK();
        return;
    }

    rec_ref = darshan_lookup_record_ref(dxt_mpiio_runtime->rec_id_hash,
                &rec_id, sizeof(darshan_record_id));
    if(!rec_ref)
    {
        /* track new dxt file record */
        rec_ref = dxt_mpiio_track_new_file_record(rec_id);
        if(!rec_ref)
        {
            DXT_UNLOCK();
            return;
        }
    }

    file_rec = rec_ref->file_rec;
    check_rd_trace_buf(rec_ref, DXT_MPIIO_MOD, dxt_mpiio_runtime);
    if(file_rec->read_count == rec_ref->read_available_buf)
    {
        /* no more memory for i/o segments ... back out */
        DXT_UNLOCK();
        return;
    }

    rec_ref->read_traces[file_rec->read_count].length = length;
    rec_ref->read_traces[file_rec->read_count].offset = offset;
    rec_ref->read_traces[file_rec->read_count].start_time = start_time;
    rec_ref->read_traces[file_rec->read_count].end_time = end_time;
    file_rec->read_count += 1;

    DXT_UNLOCK();
}

static void dxt_posix_filter_traces_iterator(void *rec_ref_p, void *user_ptr)
{
    printf("dxt_posix_filter_traces_iterator\n");
    struct dxt_file_record_ref *psx_rec_ref, *mpiio_rec_ref;
    struct darshan_posix_file *psx_file;
    struct dxt_trigger *trigger = (struct dxt_trigger *)user_ptr;
    int should_keep = 0;

    psx_rec_ref = (struct dxt_file_record_ref *)rec_ref_p;
    printf("psx_rec_ref: %p\n", psx_rec_ref);
    psx_file = darshan_posix_rec_id_to_file(psx_rec_ref->file_rec->base_rec.id);
    printf("psx_file: %p\n", psx_file);

    /* analyze dynamic triggers to determine whether we should keep the record */
    switch(trigger->type)
    {
        case DXT_SMALL_IO_TRIGGER:
        {
            int total_ops = psx_file->counters[POSIX_WRITES] +
                psx_file->counters[POSIX_READS];
            int small_ops = psx_file->counters[POSIX_SIZE_WRITE_0_100] +
                psx_file->counters[POSIX_SIZE_WRITE_100_1K] +
                psx_file->counters[POSIX_SIZE_WRITE_1K_10K] +
                psx_file->counters[POSIX_SIZE_READ_0_100] +
                psx_file->counters[POSIX_SIZE_READ_100_1K] +
                psx_file->counters[POSIX_SIZE_READ_1K_10K];
            double small_pct = (small_ops / (double)(total_ops));
            if(small_pct >= trigger->u.small_io.thresh_pct)
                should_keep = 1;
            break;
        }
        case DXT_UNALIGNED_IO_TRIGGER:
        {
            int total_ops = psx_file->counters[POSIX_WRITES] +
                psx_file->counters[POSIX_READS];
            int unaligned_ops = psx_file->counters[POSIX_FILE_NOT_ALIGNED];
            double unaligned_pct = (unaligned_ops / (double)(total_ops));
            if(unaligned_pct >= trigger->u.unaligned_io.thresh_pct)
                should_keep = 1;
            break;
        }
    }

    /* drop the record if no dynamic trace triggers occurred */
    if(!should_keep)
    {
        if(dxt_mpiio_runtime && dxt_mpiio_runtime->rec_id_hash)
        {
            /* first check the MPI-IO traces to see if we should drop there */
            mpiio_rec_ref = darshan_delete_record_ref(&dxt_mpiio_runtime->rec_id_hash,
                &psx_file->base_rec.id, sizeof(darshan_record_id));
            if(mpiio_rec_ref)
            {
                free(mpiio_rec_ref->write_traces);
                free(mpiio_rec_ref->read_traces);
                free(mpiio_rec_ref->file_rec);
                free(mpiio_rec_ref);
            }
        }

        if(dxt_posix_runtime && dxt_posix_runtime->rec_id_hash)
        {
            /* then delete the POSIX trace records */
            psx_rec_ref = darshan_delete_record_ref(&dxt_posix_runtime->rec_id_hash,
                &psx_file->base_rec.id, sizeof(darshan_record_id));
            if(psx_rec_ref)
            {
                free(psx_rec_ref->write_traces);
                free(psx_rec_ref->read_traces);
                free(psx_rec_ref->open_traces);
                free(psx_rec_ref->stat_traces);
                free(psx_rec_ref->file_rec);
                free(psx_rec_ref);
            }
        }
    }

    return;
}

void dxt_posix_apply_trace_filter(
    struct dxt_trigger *trigger)
{
    DXT_LOCK();

    if(!dxt_posix_runtime)
    {
        DXT_UNLOCK();
        return;
    }

    darshan_iter_record_refs(dxt_posix_runtime->rec_id_hash,
        dxt_posix_filter_traces_iterator, trigger);

    DXT_UNLOCK();

    return;
}

/***********************************
 *  internal DXT helper routines   *
 ***********************************/

static void check_wr_trace_buf(struct dxt_file_record_ref *rec_ref,
    darshan_module_id mod_id, struct dxt_runtime *runtime)
{
    struct dxt_file_record *file_rec = rec_ref->file_rec;

    int write_count = file_rec->write_count;
    int write_available_buf = rec_ref->write_available_buf;

    if (write_count >= write_available_buf)
    {
        int write_count_inc;
        if(write_available_buf == 0)
            write_count_inc = IO_TRACE_BUF_SIZE;
        else
            write_count_inc = write_available_buf;

        size_t mem_left = runtime->mem_allocated - runtime->mem_used;
        size_t mem_req = write_count_inc * sizeof(segment_info);
        if(mem_req > mem_left)
        {
            write_count_inc = mem_left / sizeof(segment_info);
            if(write_count_inc == 0)
            {
                /* we need to request at least one record, even if we
                 * know there is not enough memory left, so that Darshan
                 * core can mark this module as having ran out of data
                 */
                write_count_inc = 1;
            }
            mem_req = write_count_inc * sizeof(segment_info);
        }

        /* register the increased write buffer size with Darshan core */
        /* NOTE: register_record() does not handle DXT memory allocations,
         * it just checks that there is enough memory for the record -- if
         * there is not enough memory, this function will return NULL
         */
        if(darshan_core_register_record(
             file_rec->base_rec.id,
             NULL, /* no name registration needed, handled in initial record alloc */
             mod_id,
             mem_req,
             NULL))
        {
            /* there is enough memory for these additional trace segments,
             * but we have to (re)allocate them ourselves
             */
            write_available_buf += write_count_inc;
            rec_ref->write_traces =
                (segment_info *)realloc(rec_ref->write_traces,
                        write_available_buf * sizeof(segment_info));

            rec_ref->write_available_buf = write_available_buf;
        }
        runtime->mem_used += mem_req;
    }
}

static void check_rd_trace_buf(struct dxt_file_record_ref *rec_ref,
    darshan_module_id mod_id, struct dxt_runtime *runtime)
{
    struct dxt_file_record *file_rec = rec_ref->file_rec;

    int read_count = file_rec->read_count;
    int read_available_buf = rec_ref->read_available_buf;

    if (read_count >= read_available_buf)
    {
        int read_count_inc;
        if(read_available_buf == 0)
            read_count_inc = IO_TRACE_BUF_SIZE;
        else
            read_count_inc = read_available_buf;

        size_t mem_left = runtime->mem_allocated - runtime->mem_used;
        size_t mem_req = read_count_inc * sizeof(segment_info);
        if(mem_req > mem_left)
        {
            read_count_inc = mem_left / sizeof(segment_info);
            if(read_count_inc == 0)
            {
                /* we need to request at least one record, even if we
                 * know there is not enough memory left, so that Darshan
                 * core can mark this module as having ran out of data
                 */
                read_count_inc = 1;
            }
            mem_req = read_count_inc * sizeof(segment_info);
        }

        /* register the increased read buffer size with Darshan core */
        /* NOTE: register_record() does not handle DXT memory allocations,
         * it just checks that there is enough memory for the record -- if
         * there is not enough memory, this function will return NULL
         */
        if(darshan_core_register_record(
             file_rec->base_rec.id,
             NULL, /* no name registration needed, handled in initial record alloc */
             mod_id,
             mem_req,
             NULL))
        {
            /* there is enough memory for these additional trace segments,
             * but we have to (re)allocate them ourselves
             */
            read_available_buf += read_count_inc;
            rec_ref->read_traces =
                (segment_info *)realloc(rec_ref->read_traces,
                        read_available_buf * sizeof(segment_info));

            rec_ref->read_available_buf = read_available_buf;
        }
        runtime->mem_used += mem_req;
    }
}

static void check_open_trace_buf(struct dxt_file_record_ref *rec_ref,
    darshan_module_id mod_id, struct dxt_runtime *runtime)
{
    struct dxt_file_record *file_rec = rec_ref->file_rec;

    int open_count = file_rec->open_count;
    int open_available_buf = rec_ref->open_available_buf;

    if (open_count >= open_available_buf)
    {
        int open_count_inc;
        if(open_available_buf == 0)
            open_count_inc = IO_TRACE_BUF_SIZE;
        else
            open_count_inc = open_available_buf;

        size_t mem_left = runtime->mem_allocated - runtime->mem_used;
        size_t mem_req = open_count_inc * sizeof(segment_info);
        if(mem_req > mem_left)
        {
            open_count_inc = mem_left / sizeof(segment_info);
            if(open_count_inc == 0)
            {
                /* we need to request at least one record, even if we
                 * know there is not enough memory left, so that Darshan
                 * core can mark this module as having ran out of data
                 */
                open_count_inc = 1;
            }
            mem_req = open_count_inc * sizeof(segment_info);
        }

        /* register the increased open buffer size with Darshan core */
        /* NOTE: register_record() does not handle DXT memory allocations,
         * it just checks that there is enough memory for the record -- if
         * there is not enough memory, this function will return NULL
         */
        if(darshan_core_register_record(
             file_rec->base_rec.id,
             NULL, /* no name registration needed, handled in initial record alloc */
             mod_id,
             mem_req,
             NULL))
        {
            /* there is enough memory for these additional trace segments,
             * but we have to (re)allocate them ourselves
             */
            open_available_buf += open_count_inc;
            rec_ref->open_traces =
                (segment_info *)realloc(rec_ref->open_traces,
                        open_available_buf * sizeof(segment_info));

            rec_ref->open_available_buf = open_available_buf;
        }
        runtime->mem_used += mem_req;
    }
}

static void check_stat_trace_buf(struct dxt_file_record_ref *rec_ref,
    darshan_module_id mod_id, struct dxt_runtime *runtime)
{
    struct dxt_file_record *file_rec = rec_ref->file_rec;

    int stat_count = file_rec->stat_count;
    int stat_available_buf = rec_ref->stat_available_buf;

    if (stat_count >= stat_available_buf)
    {
        int stat_count_inc;
        if(stat_available_buf == 0)
            stat_count_inc = IO_TRACE_BUF_SIZE;
        else
            stat_count_inc = stat_available_buf;

        size_t mem_left = runtime->mem_allocated - runtime->mem_used;
        printf("mem_left: %d\n", mem_left);
        size_t mem_req = stat_count_inc * sizeof(segment_info);
        if(mem_req > mem_left)
        {
            stat_count_inc = mem_left / sizeof(segment_info);
            if(stat_count_inc == 0)
            {
                /* we need to request at least one record, even if we
                 * know there is not enough memory left, so that Darshan
                 * core can mark this module as having ran out of data
                 */
                stat_count_inc = 1;
            }
            mem_req = stat_count_inc * sizeof(segment_info);
        }

        /* register the increased stat buffer size with Darshan core */
        /* NOTE: register_record() does not handle DXT memory allocations,
         * it just checks that there is enough memory for the record -- if
         * there is not enough memory, this function will return NULL
         */
        if(darshan_core_register_record(
             file_rec->base_rec.id,
             NULL, /* no name registration needed, handled in initial record alloc */
             mod_id,
             mem_req,
             NULL))
        {
            /* there is enough memory for these additional trace segments,
             * but we have to (re)allocate them ourselves
             */
            stat_available_buf += stat_count_inc;
            printf("stat_available_buf: %d\n", stat_available_buf);
            rec_ref->stat_traces =
                (segment_info *)realloc(rec_ref->stat_traces,
                        stat_available_buf * sizeof(segment_info));
            if (rec_ref->stat_traces == NULL)
            {
                printf("rec_ref->stat_traces is NULL\n");
            }
            printf("rec_ref->stat_traces: %p\n", rec_ref->stat_traces);
            rec_ref->stat_available_buf = stat_available_buf;
        }
        runtime->mem_used += mem_req;
        printf("mem_used: %d\n", runtime->mem_used);
    }
}

static struct dxt_file_record_ref *dxt_posix_track_new_file_record(
    darshan_record_id rec_id)
{
    struct dxt_file_record_ref *rec_ref = NULL;
    struct dxt_file_record *file_rec = NULL;
    int ret;

    DXT_LOCK();

    rec_ref = malloc(sizeof(*rec_ref));
    if(!rec_ref)
    {
        DXT_UNLOCK();
        return(NULL);
    }
    memset(rec_ref, 0, sizeof(*rec_ref));

    /* add a reference to this file record based on record id */
    ret = darshan_add_record_ref(&(dxt_posix_runtime->rec_id_hash), &rec_id,
            sizeof(darshan_record_id), rec_ref);
    printf("ret: %d\n", ret);
    if(ret == 0)
    {
        free(rec_ref);
        DXT_UNLOCK();
        return(NULL);
    }

    /* register base DXT record with with darshan core for now */
    /* NOTE: register_record() does not handle DXT memory allocations,
     * it just checks that there is enough memory for the record -- if
     * there is not enough memory, this function will return NULL
     */
    if(darshan_core_register_record(
         rec_id,
         darshan_core_lookup_record_name(rec_id),
         DXT_POSIX_MOD,
         sizeof(*file_rec),
         NULL) == NULL)
    {
        printf("darshan_core_register_record failed\n");
        darshan_delete_record_ref(&(dxt_posix_runtime->rec_id_hash),
            &rec_id, sizeof(darshan_record_id));
        free(rec_ref);
        DXT_UNLOCK();
        return(NULL);
    }

    /* allocate DXT record ourselves if Darshan core registration succeeded */
    file_rec = malloc(sizeof(*file_rec));
    printf("file_rec: %p\n", file_rec);
    if(!file_rec)
    {
        printf("file_rec is NULL\n");
        darshan_delete_record_ref(&(dxt_posix_runtime->rec_id_hash),
            &rec_id, sizeof(darshan_record_id));
        free(rec_ref);
        DXT_UNLOCK();
        return(NULL);
    }
    memset(file_rec, 0, sizeof(*file_rec));

    dxt_posix_runtime->file_rec_count++;
    printf("dxt_posix_runtime->file_rec_count: %d\n", dxt_posix_runtime->file_rec_count);
    dxt_posix_runtime->mem_used += sizeof(*file_rec);
    DXT_UNLOCK();

    /* initialize record and record reference fields */
    file_rec->base_rec.id = rec_id;
    file_rec->base_rec.rank = dxt_my_rank;
    gethostname(file_rec->hostname, HOSTNAME_SIZE);

    rec_ref->file_rec = file_rec;
    printf("rec_ref->file_rec: %p\n", rec_ref->file_rec);

    return(rec_ref);
}

static struct dxt_file_record_ref *dxt_mpiio_track_new_file_record(
    darshan_record_id rec_id)
{
    struct dxt_file_record *file_rec = NULL;
    struct dxt_file_record_ref *rec_ref = NULL;
    int ret;

    DXT_LOCK();

    rec_ref = malloc(sizeof(*rec_ref));
    if(!rec_ref)
    {
        DXT_UNLOCK();
        return(NULL);
    }
    memset(rec_ref, 0, sizeof(*rec_ref));

    /* add a reference to this file record based on record id */
    ret = darshan_add_record_ref(&(dxt_mpiio_runtime->rec_id_hash), &rec_id,
            sizeof(darshan_record_id), rec_ref);
    if(ret == 0)
    {
        free(rec_ref);
        DXT_UNLOCK();
        return(NULL);
    }

    /* register base DXT record with with darshan core for now */
    /* NOTE: register_record() does not handle DXT memory allocations,
     * it just checks that there is enough memory for the record -- if
     * there is not enough memory, this function will return NULL
     */
    if(darshan_core_register_record(
         rec_id,
         darshan_core_lookup_record_name(rec_id),
         DXT_MPIIO_MOD,
         sizeof(*file_rec),
         NULL) == NULL)
    {
        darshan_delete_record_ref(&(dxt_mpiio_runtime->rec_id_hash),
            &rec_id, sizeof(darshan_record_id));
        free(rec_ref);
        DXT_UNLOCK();
        return(NULL);
    }

    /* allocate DXT record ourselves if Darshan core registration succeeded */
    file_rec = malloc(sizeof(*file_rec));
    if(!file_rec)
    {
        darshan_delete_record_ref(&(dxt_mpiio_runtime->rec_id_hash),
            &rec_id, sizeof(darshan_record_id));
        free(rec_ref);
        DXT_UNLOCK();
        return(NULL);
    }
    memset(file_rec, 0, sizeof(*file_rec));

    dxt_mpiio_runtime->file_rec_count++;
    dxt_mpiio_runtime->mem_used += sizeof(*file_rec);
    DXT_UNLOCK();

    /* initialize record and record reference fields */
    file_rec->base_rec.id = rec_id;
    file_rec->base_rec.rank = dxt_my_rank;
    gethostname(file_rec->hostname, HOSTNAME_SIZE);

    rec_ref->file_rec = file_rec;

    return(rec_ref);
}

static void dxt_free_record_data(void *rec_ref_p, void *user_ptr)
{
    struct dxt_file_record_ref *dxt_rec_ref = (struct dxt_file_record_ref *)rec_ref_p;

    free(dxt_rec_ref->write_traces);
    free(dxt_rec_ref->read_traces);
    free(dxt_rec_ref->open_traces);
    free(dxt_rec_ref->stat_traces);
    free(dxt_rec_ref->file_rec);
}

/********************************************************************************
 *     functions exported by this module for coordinating with darshan-core     *
 ********************************************************************************/

static void dxt_serialize_posix_records(void *rec_ref_p, void *user_ptr)
{
    printf("dxt_serialize_posix_records\n");
    struct dxt_file_record_ref *rec_ref = (struct dxt_file_record_ref *)rec_ref_p;
    struct dxt_file_record *file_rec;
    int64_t record_size = 0;
    int64_t record_write_count = 0;
    int64_t record_read_count = 0;
    int64_t record_open_count = 0;
    int64_t record_stat_count = 0;
    void *tmp_buf_ptr;

    printf("rec_ref: %p\n", rec_ref);
    assert(rec_ref);
    file_rec = rec_ref->file_rec;
    printf("file_rec: %p\n", file_rec);
    assert(file_rec);

    record_write_count = file_rec->write_count;
    record_read_count = file_rec->read_count;
    record_open_count = file_rec->open_count;
    record_stat_count = file_rec->stat_count;
    printf("record_write_count: %d\n", record_write_count);
    printf("record_read_count: %d\n", record_read_count);
    printf("record_open_count: %d\n", record_open_count);
    printf("record_stat_count: %d\n", record_stat_count);
    if (record_write_count == 0 && record_read_count == 0 && record_open_count == 0 && record_stat_count == 0)
        return;

    /*
     * Buffer format:
     * dxt_file_record + write_traces + read_traces
     */
    record_size = sizeof(struct dxt_file_record) +
            (record_write_count + record_read_count + record_open_count + record_stat_count) * sizeof(segment_info);
    printf("record_size: %d\n", record_size);



    tmp_buf_ptr = (void *)(dxt_posix_runtime->record_buf +
        dxt_posix_runtime->record_buf_size);

    /*Copy struct dxt_file_record */
    memcpy(tmp_buf_ptr, (void *)file_rec, sizeof(struct dxt_file_record));
    tmp_buf_ptr = (void *)(tmp_buf_ptr + sizeof(struct dxt_file_record));

    /*Copy write record */
    memcpy(tmp_buf_ptr, (void *)(rec_ref->write_traces),
            record_write_count * sizeof(segment_info));
    tmp_buf_ptr = (void *)(tmp_buf_ptr +
                record_write_count * sizeof(segment_info));

    /*Copy read record */
    memcpy(tmp_buf_ptr, (void *)(rec_ref->read_traces),
            record_read_count * sizeof(segment_info));
    tmp_buf_ptr = (void *)(tmp_buf_ptr +
                record_read_count * sizeof(segment_info));

    /*Copy open record */
    memcpy(tmp_buf_ptr, (void *)(rec_ref->open_traces),
            record_open_count * sizeof(segment_info));
    tmp_buf_ptr = (void *)(tmp_buf_ptr +
                record_open_count * sizeof(segment_info));

    /*Copy stat record */
    memcpy(tmp_buf_ptr, (void *)(rec_ref->stat_traces),
            record_stat_count * sizeof(segment_info));
    tmp_buf_ptr = (void *)(tmp_buf_ptr +
                record_stat_count * sizeof(segment_info));

    dxt_posix_runtime->record_buf_size += record_size;
}

static void dxt_posix_output(
    void **dxt_posix_buf,
    int *dxt_posix_buf_sz)
{
    printf("dxt_posix_output\n");
    assert(dxt_posix_runtime);
    printf("dxt_posix_runtime: %p\n", dxt_posix_runtime);

    *dxt_posix_buf_sz = 0;

    dxt_posix_runtime->record_buf = malloc(dxt_posix_runtime->mem_allocated);
    printf("dxt_posix_runtime->record_buf: %p\n", dxt_posix_runtime->record_buf);
    if(!(dxt_posix_runtime->record_buf))
    {
        printf("dxt_posix_runtime->record_buf is NULL\n");
        return;
    }
    memset(dxt_posix_runtime->record_buf, 0, dxt_posix_runtime->mem_allocated);
    dxt_posix_runtime->record_buf_size = 0;

    printf("dxt_posix_runtime->record_buf_size: %d\n", dxt_posix_runtime->record_buf_size);
    printf("dxt_posix_runtime->rec_id_hash: %p\n", dxt_posix_runtime->rec_id_hash);
    /* iterate all dxt posix records and serialize them to the output buffer */
    darshan_iter_record_refs(dxt_posix_runtime->rec_id_hash,
        dxt_serialize_posix_records, NULL);

    printf("dxt_posix_runtime->record_buf_size: %d\n", dxt_posix_runtime->record_buf_size);
    /* set output */
    *dxt_posix_buf = dxt_posix_runtime->record_buf;
    *dxt_posix_buf_sz = dxt_posix_runtime->record_buf_size;

    dxt_posix_runtime->frozen = 1;

    return;
}

static void dxt_posix_cleanup()
{
    assert(dxt_posix_runtime);

    free(dxt_posix_runtime->record_buf);

    /* cleanup internal structures used for instrumenting */
    darshan_iter_record_refs(dxt_posix_runtime->rec_id_hash,
        dxt_free_record_data, NULL);
    darshan_clear_record_refs(&(dxt_posix_runtime->rec_id_hash), 1);

    free(dxt_posix_runtime);
    dxt_posix_runtime = NULL;

    return;
}

static void dxt_serialize_mpiio_records(void *rec_ref_p, void *user_ptr)
{
    struct dxt_file_record_ref *rec_ref = (struct dxt_file_record_ref *)rec_ref_p;
    struct dxt_file_record *file_rec;
    int64_t record_size = 0;
    int64_t record_write_count = 0;
    int64_t record_read_count = 0;
    void *tmp_buf_ptr;

    assert(rec_ref);
    file_rec = rec_ref->file_rec;
    assert(file_rec);

    record_write_count = file_rec->write_count;
    record_read_count = file_rec->read_count;
    if (record_write_count == 0 && record_read_count == 0)
        return;

    /*
     * Buffer format:
     * dxt_file_record + write_traces + read_traces
     */
    record_size = sizeof(struct dxt_file_record) +
            (record_write_count + record_read_count) * sizeof(segment_info);

    tmp_buf_ptr = (void *)(dxt_mpiio_runtime->record_buf +
        dxt_mpiio_runtime->record_buf_size);

    /*Copy struct dxt_file_record */
    memcpy(tmp_buf_ptr, (void *)file_rec, sizeof(struct dxt_file_record));
    tmp_buf_ptr = (void *)(tmp_buf_ptr + sizeof(struct dxt_file_record));

    /*Copy write record */
    memcpy(tmp_buf_ptr, (void *)(rec_ref->write_traces),
            record_write_count * sizeof(segment_info));
    tmp_buf_ptr = (void *)(tmp_buf_ptr +
                record_write_count * sizeof(segment_info));

    /*Copy read record */
    memcpy(tmp_buf_ptr, (void *)(rec_ref->read_traces),
            record_read_count * sizeof(segment_info));
    tmp_buf_ptr = (void *)(tmp_buf_ptr +
                record_read_count * sizeof(segment_info));

    dxt_mpiio_runtime->record_buf_size += record_size;
}

static void dxt_mpiio_output(
    void **dxt_mpiio_buf,
    int *dxt_mpiio_buf_sz)
{
    assert(dxt_mpiio_runtime);

    *dxt_mpiio_buf_sz = 0;

    dxt_mpiio_runtime->record_buf = malloc(dxt_mpiio_runtime->mem_allocated);
    if(!(dxt_mpiio_runtime->record_buf))
        return;
    memset(dxt_mpiio_runtime->record_buf, 0, dxt_mpiio_runtime->mem_allocated);
    dxt_mpiio_runtime->record_buf_size = 0;

    /* iterate all dxt posix records and serialize them to the output buffer */
    darshan_iter_record_refs(dxt_mpiio_runtime->rec_id_hash,
        dxt_serialize_mpiio_records, NULL);

    /* set output */ 
    *dxt_mpiio_buf = dxt_mpiio_runtime->record_buf;
    *dxt_mpiio_buf_sz = dxt_mpiio_runtime->record_buf_size;

    dxt_mpiio_runtime->frozen = 1;

    return;
}

static void dxt_mpiio_cleanup()
{
    assert(dxt_mpiio_runtime);

    free(dxt_mpiio_runtime->record_buf);

    /* cleanup internal structures used for instrumenting */
    darshan_iter_record_refs(dxt_mpiio_runtime->rec_id_hash,
        dxt_free_record_data, NULL);
    darshan_clear_record_refs(&(dxt_mpiio_runtime->rec_id_hash), 1);

    free(dxt_mpiio_runtime);
    dxt_mpiio_runtime = NULL;

    return;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */

/* structure to store details of open call */
