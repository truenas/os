#ifndef _RFB_RFBCONFIG_H
#define _RFB_RFBCONFIG_H 1
 
/* rfb/rfbconfig.h. Generated automatically at end of configure. */
/* rfbconfig.h.  Generated from rfbconfig.h.in by configure.  */
/* rfbconfig.h.in.  Generated from configure.ac by autoheader.  */

/* Enable 24 bit per pixel in native framebuffer */
#ifndef LIBVNCSERVER_ALLOW24BPP
#define LIBVNCSERVER_ALLOW24BPP 1
#endif

/* work around when write() returns ENOENT but does not mean it */
/* #undef ENOENT_WORKAROUND */

/* Use ffmpeg (for vnc2mpg) */
/* #undef FFMPEG */

/* Android host system detected */
/* #undef HAVE_ANDROID */

/* Define to 1 if you have the <arpa/inet.h> header file. */
#ifndef LIBVNCSERVER_HAVE_ARPA_INET_H
#define LIBVNCSERVER_HAVE_ARPA_INET_H 1
#endif

/* Define to 1 if you have the `crypt' function. */
/* #undef HAVE_CRYPT */

/* Define to 1 if you have the <dlfcn.h> header file. */
#ifndef LIBVNCSERVER_HAVE_DLFCN_H
#define LIBVNCSERVER_HAVE_DLFCN_H 1
#endif

/* Define to 1 if you don't have `vprintf' but do have `_doprnt.' */
/* #undef HAVE_DOPRNT */

/* Define to 1 if you have the <endian.h> header file. */
/* #undef HAVE_ENDIAN_H */

/* Define to 1 if you have the <fcntl.h> header file. */
#ifndef LIBVNCSERVER_HAVE_FCNTL_H
#define LIBVNCSERVER_HAVE_FCNTL_H 1
#endif

/* Define to 1 if you have the `fork' function. */
#ifndef LIBVNCSERVER_HAVE_FORK
#define LIBVNCSERVER_HAVE_FORK 1
#endif

/* Define to 1 if you have the `ftime' function. */
/* #undef HAVE_FTIME */

/* Define to 1 if you have the `gethostbyname' function. */
#ifndef LIBVNCSERVER_HAVE_GETHOSTBYNAME
#define LIBVNCSERVER_HAVE_GETHOSTBYNAME 1
#endif

/* Define to 1 if you have the `gethostname' function. */
#ifndef LIBVNCSERVER_HAVE_GETHOSTNAME
#define LIBVNCSERVER_HAVE_GETHOSTNAME 1
#endif

/* Define to 1 if you have the `gettimeofday' function. */
#ifndef LIBVNCSERVER_HAVE_GETTIMEOFDAY
#define LIBVNCSERVER_HAVE_GETTIMEOFDAY 1
#endif

/* GnuTLS library present */
#ifndef LIBVNCSERVER_HAVE_GNUTLS
#define LIBVNCSERVER_HAVE_GNUTLS 1
#endif

/* Define to 1 if you have the `inet_ntoa' function. */
#ifndef LIBVNCSERVER_HAVE_INET_NTOA
#define LIBVNCSERVER_HAVE_INET_NTOA 1
#endif

/* Define to 1 if you have the <inttypes.h> header file. */
#ifndef LIBVNCSERVER_HAVE_INTTYPES_H
#define LIBVNCSERVER_HAVE_INTTYPES_H 1
#endif

/* libcrypt library present */
#ifndef LIBVNCSERVER_HAVE_LIBCRYPT
#define LIBVNCSERVER_HAVE_LIBCRYPT 1
#endif

/* openssl libcrypto library present */
/* #undef HAVE_LIBCRYPTO */

/* Define to 1 if you have the `cygipc' library (-lcygipc). */
/* #undef HAVE_LIBCYGIPC */

/* libjpeg support enabled */
/* #undef HAVE_LIBJPEG */

/* Define to 1 if you have the `nsl' library (-lnsl). */
/* #undef HAVE_LIBNSL */

/* Define to 1 if you have the `png' library (-lpng). */
/* #undef HAVE_LIBPNG */

/* Define to 1 if you have the `pthread' library (-lpthread). */
#ifndef LIBVNCSERVER_HAVE_LIBPTHREAD
#define LIBVNCSERVER_HAVE_LIBPTHREAD 1
#endif

/* Define to 1 if you have the `socket' library (-lsocket). */
/* #undef HAVE_LIBSOCKET */

/* openssl libssl library present */
/* #undef HAVE_LIBSSL */

/* Define to 1 if you have the `z' library (-lz). */
#ifndef LIBVNCSERVER_HAVE_LIBZ
#define LIBVNCSERVER_HAVE_LIBZ 1
#endif

/* Define to 1 if you have the `memmove' function. */
#ifndef LIBVNCSERVER_HAVE_MEMMOVE
#define LIBVNCSERVER_HAVE_MEMMOVE 1
#endif

/* Define to 1 if you have the <memory.h> header file. */
#ifndef LIBVNCSERVER_HAVE_MEMORY_H
#define LIBVNCSERVER_HAVE_MEMORY_H 1
#endif

/* Define to 1 if you have the `memset' function. */
#ifndef LIBVNCSERVER_HAVE_MEMSET
#define LIBVNCSERVER_HAVE_MEMSET 1
#endif

/* Define to 1 if you have the `mkfifo' function. */
#ifndef LIBVNCSERVER_HAVE_MKFIFO
#define LIBVNCSERVER_HAVE_MKFIFO 1
#endif

/* Define to 1 if you have the `mmap' function. */
#ifndef LIBVNCSERVER_HAVE_MMAP
#define LIBVNCSERVER_HAVE_MMAP 1
#endif

/* Define to 1 if you have the <netdb.h> header file. */
#ifndef LIBVNCSERVER_HAVE_NETDB_H
#define LIBVNCSERVER_HAVE_NETDB_H 1
#endif

/* Define to 1 if you have the <netinet/in.h> header file. */
#ifndef LIBVNCSERVER_HAVE_NETINET_IN_H
#define LIBVNCSERVER_HAVE_NETINET_IN_H 1
#endif

/* Define to 1 if you have the `select' function. */
#ifndef LIBVNCSERVER_HAVE_SELECT
#define LIBVNCSERVER_HAVE_SELECT 1
#endif

/* Define to 1 if you have the `socket' function. */
#ifndef LIBVNCSERVER_HAVE_SOCKET
#define LIBVNCSERVER_HAVE_SOCKET 1
#endif

/* Define to 1 if `stat' has the bug that it succeeds when given the
   zero-length file name argument. */
/* #undef HAVE_STAT_EMPTY_STRING_BUG */

/* Define to 1 if you have the <stdint.h> header file. */
#ifndef LIBVNCSERVER_HAVE_STDINT_H
#define LIBVNCSERVER_HAVE_STDINT_H 1
#endif

/* Define to 1 if you have the <stdlib.h> header file. */
#ifndef LIBVNCSERVER_HAVE_STDLIB_H
#define LIBVNCSERVER_HAVE_STDLIB_H 1
#endif

/* Define to 1 if you have the `strchr' function. */
#ifndef LIBVNCSERVER_HAVE_STRCHR
#define LIBVNCSERVER_HAVE_STRCHR 1
#endif

/* Define to 1 if you have the `strcspn' function. */
#ifndef LIBVNCSERVER_HAVE_STRCSPN
#define LIBVNCSERVER_HAVE_STRCSPN 1
#endif

/* Define to 1 if you have the `strdup' function. */
#ifndef LIBVNCSERVER_HAVE_STRDUP
#define LIBVNCSERVER_HAVE_STRDUP 1
#endif

/* Define to 1 if you have the `strerror' function. */
#ifndef LIBVNCSERVER_HAVE_STRERROR
#define LIBVNCSERVER_HAVE_STRERROR 1
#endif

/* Define to 1 if you have the `strftime' function. */
#ifndef LIBVNCSERVER_HAVE_STRFTIME
#define LIBVNCSERVER_HAVE_STRFTIME 1
#endif

/* Define to 1 if you have the <strings.h> header file. */
#ifndef LIBVNCSERVER_HAVE_STRINGS_H
#define LIBVNCSERVER_HAVE_STRINGS_H 1
#endif

/* Define to 1 if you have the <string.h> header file. */
#ifndef LIBVNCSERVER_HAVE_STRING_H
#define LIBVNCSERVER_HAVE_STRING_H 1
#endif

/* Define to 1 if you have the `strstr' function. */
#ifndef LIBVNCSERVER_HAVE_STRSTR
#define LIBVNCSERVER_HAVE_STRSTR 1
#endif

/* Define to 1 if you have the <syslog.h> header file. */
#ifndef LIBVNCSERVER_HAVE_SYSLOG_H
#define LIBVNCSERVER_HAVE_SYSLOG_H 1
#endif

/* Define to 1 if you have the <sys/endian.h> header file. */
#ifndef LIBVNCSERVER_HAVE_SYS_ENDIAN_H
#define LIBVNCSERVER_HAVE_SYS_ENDIAN_H 1
#endif

/* Define to 1 if you have the <sys/socket.h> header file. */
#ifndef LIBVNCSERVER_HAVE_SYS_SOCKET_H
#define LIBVNCSERVER_HAVE_SYS_SOCKET_H 1
#endif

/* Define to 1 if you have the <sys/stat.h> header file. */
#ifndef LIBVNCSERVER_HAVE_SYS_STAT_H
#define LIBVNCSERVER_HAVE_SYS_STAT_H 1
#endif

/* Define to 1 if you have the <sys/timeb.h> header file. */
#ifndef LIBVNCSERVER_HAVE_SYS_TIMEB_H
#define LIBVNCSERVER_HAVE_SYS_TIMEB_H 1
#endif

/* Define to 1 if you have the <sys/time.h> header file. */
#ifndef LIBVNCSERVER_HAVE_SYS_TIME_H
#define LIBVNCSERVER_HAVE_SYS_TIME_H 1
#endif

/* Define to 1 if you have the <sys/types.h> header file. */
#ifndef LIBVNCSERVER_HAVE_SYS_TYPES_H
#define LIBVNCSERVER_HAVE_SYS_TYPES_H 1
#endif

/* Define to 1 if you have <sys/wait.h> that is POSIX.1 compatible. */
#ifndef LIBVNCSERVER_HAVE_SYS_WAIT_H
#define LIBVNCSERVER_HAVE_SYS_WAIT_H 1
#endif

/* Define to 1 if compiler supports __thread */
#ifndef LIBVNCSERVER_HAVE_TLS
#define LIBVNCSERVER_HAVE_TLS 1
#endif

/* Define to 1 if you have the <unistd.h> header file. */
#ifndef LIBVNCSERVER_HAVE_UNISTD_H
#define LIBVNCSERVER_HAVE_UNISTD_H 1
#endif

/* Define to 1 if you have the `vfork' function. */
#ifndef LIBVNCSERVER_HAVE_VFORK
#define LIBVNCSERVER_HAVE_VFORK 1
#endif

/* Define to 1 if you have the <vfork.h> header file. */
/* #undef HAVE_VFORK_H */

/* Define to 1 if you have the `vprintf' function. */
#ifndef LIBVNCSERVER_HAVE_VPRINTF
#define LIBVNCSERVER_HAVE_VPRINTF 1
#endif

/* Define to 1 if `fork' works. */
#ifndef LIBVNCSERVER_HAVE_WORKING_FORK
#define LIBVNCSERVER_HAVE_WORKING_FORK 1
#endif

/* Define to 1 if `vfork' works. */
#ifndef LIBVNCSERVER_HAVE_WORKING_VFORK
#define LIBVNCSERVER_HAVE_WORKING_VFORK 1
#endif

/* Define to 1 if you have the <ws2tcpip.h> header file. */
/* #undef HAVE_WS2TCPIP_H */

/* open ssl X509_print_ex_fp available */
/* #undef HAVE_X509_PRINT_EX_FP */

/* Enable IPv6 support */
#ifndef LIBVNCSERVER_IPv6
#define LIBVNCSERVER_IPv6 1
#endif

/* Define to 1 if `lstat' dereferences a symlink specified with a trailing
   slash. */
#ifndef LIBVNCSERVER_LSTAT_FOLLOWS_SLASHED_SYMLINK
#define LIBVNCSERVER_LSTAT_FOLLOWS_SLASHED_SYMLINK 1
#endif

/* Define to the sub-directory where libtool stores uninstalled libraries. */
#ifndef LIBVNCSERVER_LT_OBJDIR
#define LIBVNCSERVER_LT_OBJDIR ".libs/"
#endif

/* Need a typedef for in_addr_t */
/* #undef NEED_INADDR_T */

/* Name of package */
#ifndef LIBVNCSERVER_PACKAGE
#define LIBVNCSERVER_PACKAGE "libvncserver"
#endif

/* Define to the address where bug reports for this package should be sent. */
#ifndef LIBVNCSERVER_PACKAGE_BUGREPORT
#define LIBVNCSERVER_PACKAGE_BUGREPORT "https://github.com/LibVNC/libvncserver"
#endif

/* Define to the full name of this package. */
#ifndef LIBVNCSERVER_PACKAGE_NAME
#define LIBVNCSERVER_PACKAGE_NAME "LibVNCServer"
#endif

/* Define to the full name and version of this package. */
#ifndef LIBVNCSERVER_PACKAGE_STRING
#define LIBVNCSERVER_PACKAGE_STRING "LibVNCServer 0.9.10"
#endif

/* Define to the one symbol short name of this package. */
#ifndef LIBVNCSERVER_PACKAGE_TARNAME
#define LIBVNCSERVER_PACKAGE_TARNAME "libvncserver"
#endif

/* Define to the home page for this package. */
#ifndef LIBVNCSERVER_PACKAGE_URL
#define LIBVNCSERVER_PACKAGE_URL ""
#endif

/* Define to the version of this package. */
#ifndef LIBVNCSERVER_PACKAGE_VERSION
#define LIBVNCSERVER_PACKAGE_VERSION "0.9.10"
#endif

/* Define to 1 if you have the ANSI C header files. */
#ifndef LIBVNCSERVER_STDC_HEADERS
#define LIBVNCSERVER_STDC_HEADERS 1
#endif

/* Define to 1 if you can safely include both <sys/time.h> and <time.h>. */
#ifndef LIBVNCSERVER_TIME_WITH_SYS_TIME
#define LIBVNCSERVER_TIME_WITH_SYS_TIME 1
#endif

/* Version number of package */
#ifndef LIBVNCSERVER_VERSION
#define LIBVNCSERVER_VERSION "0.9.10"
#endif

/* LibVNCServer major version */
#ifndef LIBVNCSERVER_VERSION_MAJOR
#define LIBVNCSERVER_VERSION_MAJOR 0
#endif

/* LibVNCServer minor version */
#ifndef LIBVNCSERVER_VERSION_MINOR
#define LIBVNCSERVER_VERSION_MINOR 9
#endif

/* LibVNCServer patchlevel */
#ifndef LIBVNCSERVER_VERSION_PATCHLEVEL
#define LIBVNCSERVER_VERSION_PATCHLEVEL 10
#endif

/* Enable support for libgcrypt in libvncclient */
#ifndef LIBVNCSERVER_WITH_CLIENT_GCRYPT
#define LIBVNCSERVER_WITH_CLIENT_GCRYPT 1
#endif

/* Disable TightVNCFileTransfer protocol */
/* #undef WITH_TIGHTVNC_FILETRANSFER */

/* Disable WebSockets support */
#ifndef LIBVNCSERVER_WITH_WEBSOCKETS
#define LIBVNCSERVER_WITH_WEBSOCKETS 1
#endif

/* Define to empty if `const' does not conform to ANSI C. */
/* #undef const */

/* Define to `__inline__' or `__inline' if that's what the C compiler
   calls it, or to nothing if 'inline' is not supported under any name.  */
#ifndef __cplusplus
/* #undef inline */
#endif

/* Define to `int' if <sys/types.h> does not define. */
/* #undef pid_t */

/* Define to `unsigned int' if <sys/types.h> does not define. */
/* #undef size_t */

/* Substitute for socklen_t */
/* #undef socklen_t */

/* Define as `fork' if `vfork' does not work. */
/* #undef vfork */
 
/* once: _RFB_RFBCONFIG_H */
#endif
