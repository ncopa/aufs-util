
# Copyright (C) 2005-2015 Junjiro R. Okajima
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

HOSTCC ?= cc
override CPPFLAGS += -D_GNU_SOURCE
override CPPFLAGS += -I./libau
override CPPFLAGS += -DAUFHSM_CMD=\"/usr/bin/aufhsm\"
override CFLAGS += -O -Wall
INSTALL ?= install
Install = ${INSTALL} -o root -g root -p
ManDir = /usr/share/man

#
# MountCmd: full path for mount(8)
# UmountCmd: full path for umount(8)
#
MountCmd=/bin/mount
UmountCmd=/bin/umount
override CPPFLAGS += -DMOUNT_CMD=\"${MountCmd}\"
override CPPFLAGS += -DUMOUNT_CMD=\"${UmountCmd}\"

#
# BuildFHSM: specify building FHSM tools
#
BuildFHSM = no
ifeq (${BuildFHSM},yes)
override CPPFLAGS += -DAUFHSM
LibUtilObj = mng_fhsm.o
define MakeFHSM
	${MAKE} -C fhsm ${1}
endef
else
define MakeFHSM
	# empty
endef
endif

Cmd = aubusy auchk aubrsync
Man = aufs.5
Etc = etc_default_aufs
Bin = auibusy aumvdown auplink mount.aufs umount.aufs #auctl
BinObj = $(addsuffix .o, ${Bin})
LibUtil = libautil.a
LibUtilObj += perror.o proc_mnt.o br.o plink.o mtab.o
LibUtilHdr = au_util.h

# suppress 'eval' for ${v}
$(foreach v, CPPFLAGS CFLAGS INSTALL Install ManDir LibUtilHdr, \
	$(eval MAKE += ${v}="$${${v}}"))

all: ver_test ${Man} ${Bin} ${Etc}
	${MAKE} -C libau $@
	ln -sf ./libau/libau*.so .
	$(call MakeFHSM, $@)

clean:
	${RM} ${Man} ${Bin} ${Etc} ${LibUtil} libau.so* *~
	${RM} ${BinObj} ${LibUtilObj}
	${MAKE} -C libau $@
	$(call MakeFHSM, $@)

ver_test: ver
	./ver

${Bin}: override LDFLAGS += -static -s
${Bin}: LDLIBS = -L. -lautil
${BinObj}: %.o: %.c ${LibUtilHdr} ${LibUtil}

${LibUtilObj}: %.o: %.c ${LibUtilHdr}
#${LibUtil}: ${LibUtil}(${LibUtilObj})
${LibUtil}: $(foreach o, ${LibUtilObj}, ${LibUtil}(${o}))
.NOTPARALLEL: ${LibUtil}

etc_default_aufs: c2sh aufs.shlib
	${RM} $@
	echo '# aufs variables for shell scripts' > $@
	./c2sh >> $@
	echo >> $@
	sed -e '0,/^$$/d' aufs.shlib >> $@

aufs.5: aufs.in.5 c2tmac
	${RM} $@
	./c2tmac > $@
	awk '{ \
		gsub(/\140[^\047]*\047/, "\\[oq]&\\[cq]"); \
		gsub(/\\\[oq\]\140/, "\\[oq]"); \
		gsub(/\047\\\[cq\]/, "\\[cq]"); \
		gsub(/\047/, "\\[aq]"); \
		print; \
	}' aufs.in.5 >> $@
	chmod a-w $@

c2sh c2tmac ver: CC = ${HOSTCC}
.INTERMEDIATE: c2sh c2tmac ver

install_sbin: File = auibusy aumvdown auplink mount.aufs umount.aufs
install_sbin: Tgt = ${DESTDIR}/sbin
install_ubin: File = aubusy auchk aubrsync #auctl
install_ubin: Tgt = ${DESTDIR}/usr/bin
install_sbin install_ubin: ${File}
	${INSTALL} -d ${Tgt}
	${Install} -m 755 ${File} ${Tgt}
install_etc: File = etc_default_aufs
install_etc: Tgt = ${DESTDIR}/etc/default/aufs
install_etc: ${File}
	${INSTALL} -d $(dir ${Tgt})
	${Install} -m 644 -T ${File} ${Tgt}
install_man5: File = aufs.5
install_man5: Tgt = ${DESTDIR}${ManDir}/man5
install_man8: File = aumvdown.8
install_man8: Tgt = ${DESTDIR}${ManDir}/man8
install_man5 install_man8: ${File}
	${INSTALL} -d ${Tgt}
	${Install} -m 644 ${File} ${Tgt}
install_man: install_man5 install_man8

install: install_man install_sbin install_ubin install_etc
	${MAKE} -C libau $@
	$(call MakeFHSM, $@)

-include priv.mk
