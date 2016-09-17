
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

LibUtil = libautil.a
LibUtilObj += perror.o proc_mnt.o br.o plink.o mtab.o
LibUtilHdr = au_util.h

TopDir = ${CURDIR}
# don't use -q for fgrep here since it exits when the string is found,
# and it causes the broken pipe error.
define test_glibc
	$(shell ${1} ${CPPFLAGS} -I ${TopDir}/extlib/non-glibc -E -P -dM ${2} |\
		fgrep -w __GNU_LIBRARY__ > /dev/null && \
		echo yes || \
		echo no)
endef
$(eval Glibc=$(call test_glibc, ${CC}, ver.c))
#$(warning Glibc=${Glibc})

ifneq (${CC},${HOSTCC})
	ifeq (${LibAuDir},)
$(warning Warning: CC is set, but LibAuDir.)
		LibAuDir = $(shell ldconfig -p | \
			fgrep libc. | \
			head -n 1 | \
			cut -f2 -d'>' | \
			xargs -r dirname)
		ifeq (${LibAuDir},)
			LibAuDir = /usr/lib
		endif
	endif
endif

ExtlibPath = extlib/glibc
ExtlibObj = au_nftw.o
ifeq (${Glibc},no)
ExtlibPath = extlib/non-glibc
ExtlibObj += au_decode_mntpnt.o error_at_line.o
LibUtilHdr += ${ExtlibPath}/error_at_line.h
override CPPFLAGS += -I${CURDIR}/${ExtlibPath}
endif
LibUtilObj += ${ExtlibObj}

Cmd = aubusy auchk aubrsync
Man = aufs.5
Etc = etc_default_aufs
Bin = auibusy aumvdown auplink mount.aufs umount.aufs #auctl
BinObj = $(addsuffix .o, ${Bin})

ifeq (${Glibc},no)
AuplinkFtwCmd=/sbin/auplink_ftw
override CPPFLAGS += -DAUPLINK_FTW_CMD=\"${AuplinkFtwCmd}\"
Cmd += auplink_ftw
endif

# suppress 'eval' for ${v}
$(foreach v, CC CPPFLAGS CFLAGS INSTALL Install ManDir TopDir LibUtilHdr \
	Glibc LibAuDir ExtlibPath, \
	$(eval MAKE += ${v}="$${${v}}"))

all: ver_test ${Man} ${Bin} ${Etc}
	${MAKE} -C libau $@
	ln -sf ./libau/libau*.so .
	$(call MakeFHSM, $@)

clean:
	${RM} ${Man} ${Bin} ${Etc} ${LibUtil} libau.so* *~
	${RM} ${BinObj} ${LibUtilObj}
	for i in ${ExtlibSrc}; \
	do test -L $${i} && ${RM} $${i} || :; \
	done
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
ExtlibSrc = $(patsubst %.o,%.c, ${ExtlibObj})
${ExtlibSrc}: %: ${ExtlibPath}/%
	ln -sf $< $@
.INTERMEDIATE: ${ExtlibSrc}
${ExtlibObj}: CPPFLAGS += -I${CURDIR}

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
ifeq (${Glibc},no)
install_sbin: File += auplink_ftw
endif
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

install_ulib:
	${MAKE} -C libau $@

install: install_man install_sbin install_ubin install_etc install_ulib
	$(call MakeFHSM, $@)

-include priv.mk
