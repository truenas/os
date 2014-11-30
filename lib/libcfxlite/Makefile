.include <bsd.init.mk>
LIB=	cfxlite


BASE_INCLUDE= -I${.CURDIR}/../../apsl/include
BASE_INCLUDE+=  -I${.CURDIR}/../../include -I${.CURDIR}/../../sys -I${.CURDIR}
BASE_INCLUDE+= -I/usr/local/include -include config.h
CFLAGS= ${BASE_INCLUDE} 	${DEFINES} -fblocks -DCF_BUILDING_CF=1 -D__APPLE_CC__=1 -DDEPLOYMENT_TARGET_FREEBSD=1


SRCS= CFApplicationPreferences.c CFArray.c CFBase.c CFBasicHash.c CFBigNumber.c
SRCS+= CFBuiltinConverters.c CFCalendar.c CFCharacterSet.c CFData.c CFDate.c 
SRCS+= CFDateFormatter.c CFDictionary.c CFError.c CFFileUtilities.c CFLocale.c 
SRCS+= CFLocaleIdentifier.c CFLocaleKeys.c CFNumber.c CFNumberFormatter.c CFPlatform.c
SRCS+= CFPlatformConverters.c CFPreferences.c CFRuntime.c CFSet.c CFStringEncodings.c 
SRCS+= CFStringUtilities.c CFTimeZone.c CFUnicodeDecomposition.c 
SRCS+= CFUnicodePrecomposition.c CFUniChar.c CFSortFunctions.c CFString.c
SRCS+= CFStringEncodingConverter.c CFStringEncodingDatabase.c CFStringScanner.c CFURL.c CFURLAccess.c CFUtilities.c
CLEANFILES+= *~ cscope.*

.include <bsd.lib.mk>
