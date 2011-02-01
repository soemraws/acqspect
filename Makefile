# acqspect - Acquire spectra from an Ocean Optics USB spectrometer
# See LICENSE file for copyright and license details.

include config.mk

SRC = acqspect.c
OBJ = ${SRC:.c=.o}

all: options acqspect

options:
	@echo acqspect build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

.c.o:
	@echo CC $<
	@${CC} -c ${CFLAGS} $<

${OBJ}: config.mk

acqspect: ${OBJ}
	@echo CC -o $@
	@${CC} -o $@ acqspect.o ${LDFLAGS}

clean:
	@echo cleaning
	@rm -f acqspect ${OBJ} acqspect-${VERSION}.tar.gz

dist: clean
	@echo creating dist tarball
	@mkdir -p acqspect-${VERSION}
	@cp -R LICENSE Makefile config.mk README \
		acqspect.1 ${SRC} acqspect-${VERSION}
	@tar -cf acqspect-${VERSION}.tar acqspect-${VERSION}
	@gzip acqspect-${VERSION}.tar
	@rm -rf acqspect-${VERSION}

install: all
	@echo installing executable file to ${DESTDIR}${PREFIX}/bin
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	@cp -f acqspect ${DESTDIR}${PREFIX}/bin
	@chmod 755 ${DESTDIR}${PREFIX}/bin/acqspect
	@echo installing manual page to ${DESTDIR}${MANPREFIX}/man1
	@mkdir -p ${DESTDIR}${MANPREFIX}/man1
	@sed "s/VERSION/${VERSION}/g" < acqspect.1 > ${DESTDIR}${MANPREFIX}/man1/acqspect.1
	@chmod 644 ${DESTDIR}${MANPREFIX}/man1/acqspect.1

uninstall:
	@echo removing executable file from ${DESTDIR}${PREFIX}/bin
	@rm -f ${DESTDIR}${PREFIX}/bin/acqspect
	@echo removing manual page from ${DESTDIR}${MANPREFIX}/man1
	@rm -f ${DESTDIR}${MANPREFIX}/man1/acqspect.1

.PHONY: all options clean dist install uninstall
