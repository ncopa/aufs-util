# Copyright (C) 2005-2011 Junjiro R. Okajima
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
LibSoMinor = 7
LibSo = libau.so
LibSoObj = libau.o \
	rdu_lib.o rdu.o rdu64.o \
	pathconf.o
LibSoHdr = libau.h rdu.h

all: ${LibSo}

# this is unnecessary on 64bit system?
rdu64.c: rdu.c
	ln -sf $< $@
rdu64.o: CFLAGS += -DRdu64
.INTERMEDIATE.: rdu64.c

${LibSoObj}: CFLAGS += -fPIC -DNDEBUG -D_REENTRANT -I.
${LibSoObj}: %.o: %.c ${LibSoHdr}
${LibSo}: ${LibSo}.${LibSoMajor}
	ln -sf $< $@
${LibSo}.${LibSoMajor}: ${LibSo}.${LibSoMajor}.${LibSoMinor}
	ln -sf $< $@
${LibSo}.${LibSoMajor}.${LibSoMinor}: LDLIBS += -ldl -lpthread
${LibSo}.${LibSoMajor}.${LibSoMinor}: ${LibSoObj}
	${CC} --shared -Wl,-soname,${LibSo}.${LibSoMajor} ${LDFLAGS} \
		-o $@ $^ ${LDLIBS}

install_ulib: File = ${LibSo}.${LibSoMajor}.${LibSoMinor}
install_ulib: Tgt = ${DESTDIR}/usr/lib
install_ulib: ${File}
	install -d ${Tgt}
	${Install} -m 644 -s ${File} ${Tgt}
	# -m 6755
	ln -sf ${File} ${Tgt}/${LibSo}.${LibSoMajor}
	ln -sf ${LibSo}.${LibSoMajor} ${Tgt}/${LibSo}

clean:
	${RM} ${LibSo} ${LibSo}.* ${LibSoObj} *~
	test -L rdu64.c && ${RM} rdu64.c || :
