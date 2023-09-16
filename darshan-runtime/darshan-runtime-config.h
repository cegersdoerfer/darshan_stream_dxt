/* darshan-runtime-config.h.  Generated from darshan-runtime-config.h.in by configure.  */
/* darshan-runtime-config.h.in.  Generated from configure.ac by autoheader.  */

/*
 * Copyright (C) 2015 University of Chicago.
 * See COPYRIGHT notice in top-level directory.
 *
 */


/* Define if building universal (internal helper macro) */
/* #undef AC_APPLE_UNIVERSAL_BUILD */

/* Define if HDF5 built with parallel support */
/* #undef DARSHAN_HDF5_PAR_BUILD */

/* Define if HDF5 module built with version 1.10+ */
/* #undef DARSHAN_HDF5_VERS_1_10_PLUS */

/* Define to 1 if you have the <dlfcn.h> header file. */
#define HAVE_DLFCN_H 1

/* Define if fscanf is redirected to another function */
/* #undef HAVE_FSCANF_REDIRECT */

/* Define to 1 if you have the `H5Dflush' function. */
/* #undef HAVE_H5DFLUSH */

/* Define to 1 if you have the `H5Oopen_by_token' function. */
/* #undef HAVE_H5OOPEN_BY_TOKEN */

/* Define to 1 if you have the `H5Sget_regular_hyperslab' function. */
/* #undef HAVE_H5SGET_REGULAR_HYPERSLAB */

/* Define to 1 if you have the <inttypes.h> header file. */
#define HAVE_INTTYPES_H 1

/* Define to 1 if you have the `z' library (-lz). */
#define HAVE_LIBZ 1

/* Define to 1 if you have the <mdhim.h> header file. */
/* #undef HAVE_MDHIM_H */

/* Define to 1 if you have the <memory.h> header file. */
#define HAVE_MEMORY_H 1

/* Define to 1 if you have the <mntent.h> header file. */
#define HAVE_MNTENT_H 1

/* Define if build is MPI-enabled */
#define HAVE_MPI /**/

/* Define if MPI prototypes use const qualifier */
#define HAVE_MPI_CONST 1

/* Define to 1 if the system has the type `off64_t'. */
#define HAVE_OFF64_T 1

/* Define if OpenMPI is being used */
#define HAVE_OPEN_MPI 1

/* Define to 1 if you have the `preadv' function. */
#define HAVE_PREADV 1

/* Define to 1 if you have the `preadv2' function. */
/* #undef HAVE_PREADV2 */

/* Define to 1 if you have the `pwritev' function. */
#define HAVE_PWRITEV 1

/* Define to 1 if you have the `pwritev2' function. */
/* #undef HAVE_PWRITEV2 */

/* Define if __rdtscp() intrinsic is available */
#define HAVE_RDTSCP_INTRINSIC 1

/* Define to 1 if you have the <stdatomic.h> header file. */
/* #undef HAVE_STDATOMIC_H */

/* Define to 1 if you have the <stdint.h> header file. */
#define HAVE_STDINT_H 1

/* Define to 1 if you have the <stdlib.h> header file. */
#define HAVE_STDLIB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#define HAVE_STRINGS_H 1

/* Define to 1 if you have the <string.h> header file. */
#define HAVE_STRING_H 1

/* Define to 1 if the system has the type `struct aiocb64'. */
#define HAVE_STRUCT_AIOCB64 1

/* Define to 1 if you have the <sys/mount.h> header file. */
#define HAVE_SYS_MOUNT_H 1

/* Define to 1 if you have the <sys/stat.h> header file. */
#define HAVE_SYS_STAT_H 1

/* Define to 1 if you have the <sys/types.h> header file. */
#define HAVE_SYS_TYPES_H 1

/* Define to 1 if you have the <unistd.h> header file. */
#define HAVE_UNISTD_H 1

/* Define to 1 if you have the <x86intrin.h> header file. */
#define HAVE_X86INTRIN_H 1

/* Define to the sub-directory in which libtool stores uninstalled libraries.
   */
#define LT_OBJDIR ".libs/"

/* Name of package */
#define PACKAGE "darshan-runtime"

/* Define to the address where bug reports for this package should be sent. */
#define PACKAGE_BUGREPORT "darshan-users@lists.mcs.anl.gov"

/* Define to the full name of this package. */
#define PACKAGE_NAME "Darshan Runtime"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "Darshan Runtime 3.4.2"

/* Define to the one symbol short name of this package. */
#define PACKAGE_TARNAME "darshan-runtime"

/* Define to the home page for this package. */
#define PACKAGE_URL "https://www.mcs.anl.gov/research/projects/darshan"

/* Define to the version of this package. */
#define PACKAGE_VERSION "3.4.2"

/* Define if <inttypes.h> exists and defines unusable PRI* macros. */
/* #undef PRI_MACROS_BROKEN */

/* Define to 1 if you have the ANSI C header files. */
#define STDC_HEADERS 1

/* Version number of package */
#define VERSION "3.4.2"

/* Define WORDS_BIGENDIAN to 1 if your processor stores words with the most
   significant byte first (like Motorola and SPARC, unlike Intel). */
#if defined AC_APPLE_UNIVERSAL_BUILD
# if defined __BIG_ENDIAN__
#  define WORDS_BIGENDIAN 1
# endif
#else
# ifndef WORDS_BIGENDIAN
/* #  undef WORDS_BIGENDIAN */
# endif
#endif

/* Define if APMPI should enable sync time calculation for MPI collectives */
/* #undef __APMPI_COLL_SYNC */

/* Define if cuserid() should be enabled */
/* #undef __DARSHAN_ENABLE_CUSERID */

/* Define if Darshan should mmap data structures to log file */
/* #undef __DARSHAN_ENABLE_MMAP_LOGS */

/* Define if Darshan should set log files to be group readable */
/* #undef __DARSHAN_GROUP_READABLE_LOGS */

/* Name of the environment variable that stores the jobid */
#define __DARSHAN_JOBID "PBS_JOBID"

/* Comma separated list of env. variables to use for log path */
/* #undef __DARSHAN_LOG_ENV */

/* Comma-separated list of MPI-IO hints for log file write */
#define __DARSHAN_LOG_HINTS "romio_no_indep_rw=true;cb_nodes=4"

/* Location to store log files at run time */
#define __DARSHAN_LOG_PATH "/darshan-logs"

/* Memory alignment in bytes */
#define __DARSHAN_MEM_ALIGNMENT 8

/* Maximum runtime memory consumption per process (in MiB) across all
   instrumentation modules */
/* #undef __DARSHAN_MOD_MEM_MAX */

/* Maximum runtime memory consumption per process (in MiB) for storing name
   records */
/* #undef __DARSHAN_NAME_MEM_MAX */

/* base frequency of RDTSCP intrinsic */
/* #undef __DARSHAN_RDTSCP_FREQUENCY */

/* Generalized request type for MPI-IO */
#define __D_MPI_REQUEST MPIO_Request