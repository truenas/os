#!/bin/sh

set -e

CURDIR=$(pwd)
ECHO=${ECHO:=echo}

tmpfile=$(mktemp osreldate.XXXXXXXX)
trap "rm -f $tmpfile" EXIT

${ECHO} creating osreldate.h from newvers.sh

export PARAMFILE="${PARAM_H:=$CURDIR/../sys/sys/param.h}"
. "${NEWVERS_SH:=$CURDIR/../sys/conf/newvers.sh}"
cat > $tmpfile <<EOF
$COPYRIGHT
#ifdef _KERNEL
#error "<osreldate.h> cannot be used in the kernel, use <sys/param.h>"
#else
#undef __FreeBSD_version
#define __FreeBSD_version $RELDATE
#endif
EOF
mv $tmpfile osreldate.h
