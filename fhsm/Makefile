
# Copyright (C) 2011-2015 Junjiro R. Okajima
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

UsrBin = aufhsm
UsrLib = aufhsm-list aufhsmd
Man5 = aufs_fhsm.5
Man8 = $(addsuffix .8, ${UsrBin} ${UsrLib})
Man = ${Man5} ${Man8}
LibFhsm = libfhsm.a
LibFhsmObj = event.o fhsm.o list.o log.o msg.o mvdown.o shm.o
LibFhsmHdr = comm.h daemon.h linux-list.h log.h
Tgt = ${UsrBin} aufhsmd
TgtObj = $(addsuffix .o, ${Tgt})

all: ${Tgt}

clean:
	${RM} ${Tgt} ${TgtObj} ${LibFhsm} ${LibFhsmObj} *~

install: install_ubin install_ulib install_man

########################################

override CPPFLAGS += -DAUFHSM_LIST_CMD=\"/usr/lib/aufhsm-list\"
override CPPFLAGS += -DAUFHSMD_CMD=\"/usr/lib/aufhsmd\"
${LibFhsmObj}: %.o: %.c ${LibFhsmHdr} $(addprefix ../, ${LibUtilHdr})
${LibFhsm}: $(foreach o, ${LibFhsmObj}, ${LibFhsm}(${o}))
.NOTPARALLEL: ${LibFhsm}

${TgtObj}: %.o: %.c ${LibFhsmHdr} $(addprefix ../, ${LibUtilHdr}) ${LibFhsm}
#${Tgt}: override LDFLAGS += -static
${Tgt}: override LDFLAGS += -s
${Tgt}: override LDLIBS += -L. -lfhsm -L.. -lautil -lrt

########################################

install_ubin: File = ${UsrBin}
install_ubin: Tgt = ${DESTDIR}/usr/bin
install_ulib: File = ${UsrLib}
install_ulib: Tgt = ${DESTDIR}/usr/lib
install_ubin install_ulib: ${File}
	${INSTALL} -d ${Tgt}
	${Install} -m 755 ${File} ${Tgt}
install_man5: File = ${Man5}
install_man5: Tgt = ${DESTDIR}${ManDir}/man5
install_man8: File = ${Man8}
install_man8: Tgt = ${DESTDIR}${ManDir}/man8
install_man5 install_man8: ${File}
	${INSTALL} -d ${Tgt}
	${Install} -m 644 ${File} ${Tgt}
install_man: install_man5 install_man8

-include priv.mk
