# acqspect version
VERSION = 0.1

# Customize below to fit your system

# paths
PREFIX = /usr/local
MANPREFIX = ${PREFIX}/share/man

USBINC=$(shell pkg-config --cflags libusb)
USBLIB=$(shell pkg-config --libs libusb)


# includes and libs
INCS = -I. -I/usr/include ${USBINC}
LIBS = -L/usr/lib ${USBLIB} -lm

# flags
CPPFLAGS = -DVERSION=\"${VERSION}\"
# for big-endian systems, use the one below instead
#CPPFLAGS = -DVERSION=\"${VERSION}\" -DSYSTEM_IS_BIGENDIAN=1
CFLAGS = -std=c99 -pedantic -Wall -Os ${INCS} ${CPPFLAGS}
LDFLAGS = -g ${LIBS}

# compiler and linker
CC = cc
