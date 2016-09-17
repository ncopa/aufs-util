# Copyright (C) 2005-2016 Junjiro R. Okajima
#
# This program, aufs is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301	 USA

LibSoMajor = 2
LibSoMinor = 9
LibSo = libau.so
LibSoObj = libau.o \
	rdu_lib.o rdu.o \
	pathconf.o
LibSoHdr = libau.h rdu.h

all: ${LibSo}

ifeq (${Glibc},yes)
LibSoObj += rdu64.o

# this is unnecessary on 64bit system?
rdu64.c: rdu.c
	ln -sf $< $@
rdu64.o: override CPPFLAGS += -DRdu64
.INTERMEDIATE.: rdu64.c
else
CPPFLAGS += -I../${ExtlibPath}
LibSoObj += error_at_line.o
LibSoHdr += ../${ExtlibPath}/error_at_line.h

error_at_line.c: %: ../${ExtlibPath}/%
	ln -sf $< $@
.INTERMEDIATE.: error_at_line.c error_at_line.h
endif

${LibSoObj}: override CPPFLAGS += -DNDEBUG -D_REENTRANT -I.
${LibSoObj}: override CFLAGS += -fPIC
${LibSoObj}: %.o: %.c ${LibSoHdr}
${LibSo}: ${LibSo}.${LibSoMajor}
	ln -sf $< $@
${LibSo}.${LibSoMajor}: ${LibSo}.${LibSoMajor}.${LibSoMinor}
	ln -sf $< $@
${LibSo}.${LibSoMajor}.${LibSoMinor}: override LDFLAGS += -s
${LibSo}.${LibSoMajor}.${LibSoMinor}: override LDLIBS += -ldl -lpthread
${LibSo}.${LibSoMajor}.${LibSoMinor}: ${LibSoObj}
	${CC} --shared -Wl,-soname,${LibSo}.${LibSoMajor} ${LDFLAGS} \
		-o $@ $^ ${LDLIBS}

install_ulib: File = ${LibSo}.${LibSoMajor}.${LibSoMinor}
install_ulib: Tgt = ${DESTDIR}${LibAuDir}
install_ulib: ${File}
	$(warning LibAuDir=${LibAuDir})
	${INSTALL} -dv ${Tgt}
	${Install} -m 644 ${File} ${Tgt}
	# -m 755
	ln -sf ${File} ${Tgt}/${LibSo}.${LibSoMajor}
	ln -sf ${LibSo}.${LibSoMajor} ${Tgt}/${LibSo}
install: install_ulib

clean:
	${RM} ${LibSo} ${LibSo}.* ${LibSoObj} *~
	for i in rdu64.c error_at_line.[ch]; \
	do test -L $${i} && ${RM} $${i} || :; \
	done

-include priv.mk
