
Aufs for VMware Appliance
Junjiro R. Okajima

o Introduction
----------------------------------------------------------------------
A recent trend for VMware and Xen tends to consume much disk
space. For instance, a typical VMware appliance is distributed as a
8GB disk image and you need such large disk space for each
virtualization even if the filesystem image in it actually consumes
less than 1GB.
Additionally every virtual machine has the same files mostly. When you
construct three virtual servers from one VMware appliance, you will
need 3 x 8GB plus alpha disk spaces. More and more virtual servers,
more and more disks are needed. The "plus alpha" means the part of
differences between servers.
But the actual necessary files are single sharable system files, 1GB
for example, and the part of differences.

With AUFS you can share the common part of virtual servers, and stores
the server specific part individually.
The basic approach is such like this.
- extract the actual filesystem image from VMware appliance which we
  call it "common system image"
- construct an NFS-exportable aufs with an empty writable branch and
  the readonly "common system image"
- boot a virtual server with PXE and nfsroot, mount the exported aufs
  as its root filesystem
- so you need two or three server softwares but they can live in a
  single host
  + NFS server
  + TFTP server
  + DHCP server (optional)

Finally you can save much of disk space,
	3 x (8GB + diff) > 8GB + 3 x diff

In this document, I will describe the sample steps to build such
environment.


o VMware appliance
----------------------------------------------------------------------
  Here is an assumption.
  + you already have VMware environment.
    you can get and start any VMware appliance, and know how to
    customize it.

  In this sample, we use debian appliance where you can get
	http://mirror.o-line.net/vmware/debian-4.0r1-netinst.7z

- unpack it
$ 7z x debian-4.0r1-netinst.7z
	:::
Extracting  debian-4.0r1-netinst/debian-4.0r1-netinst-f001.vmdk
Extracting  debian-4.0r1-netinst/debian-4.0r1-netinst-f002.vmdk
Extracting  debian-4.0r1-netinst/debian-4.0r1-netinst-f003.vmdk
Extracting  debian-4.0r1-netinst/debian-4.0r1-netinst-f004.vmdk
Extracting  debian-4.0r1-netinst/debian-4.0r1-netinst-f005.vmdk
Extracting  debian-4.0r1-netinst/debian-4.0r1-netinst.vmdk
Extracting  debian-4.0r1-netinst/debian-4.0r1-netinst.nvram
Extracting  debian-4.0r1-netinst/vmware-0.log
Extracting  debian-4.0r1-netinst/vmware.log
Extracting  debian-4.0r1-netinst/debian-4.0r1-netinst.vmx
Extracting  debian-4.0r1-netinst/debian-4.0r1-netinst.vmxf
Extracting  debian-4.0r1-netinst/debian-4.0r1-netinst.vmsd
Extracting  debian-4.0r1-netinst
	:::

- check the disk partitions
$ file debian-4.0r1-netinst-f00*.vmdk
debian-4.0r1-netinst-f001.vmdk: x86 boot sector; partition 1: ID=0x83, active, starthead 1, startsector 63, 15952482 sectors; partition 2: ID=0x5, starthead 0, startsector 15952545, 819315 sectors, code offset 0x48
debian-4.0r1-netinst-f002.vmdk: data
debian-4.0r1-netinst-f003.vmdk: data
debian-4.0r1-netinst-f004.vmdk: data
debian-4.0r1-netinst-f005.vmdk: data

  It shows that there are two disk partitions, one is from 63 sector
  and has 15952482 sectors, the other is from 15952545 sector and has
  819315 sectors. The latter is swap space, so let's ignore it here.

- extract the filesystem image
$ cat debian-4.0r1-netinst-f00?.vmdk | dd ibs=512 skip=63 count=15952482 obs=32m of=etch.ext3
$ fsck.ext3 -nf etch.ext3
$ sudo mount -o ro,loop etch.ext3 /mnt
$ df /mnt

  Now you get the ext3 filesystem image which is stored in the VMware
  appliance. In this sample, we create a brand new ext2 fs-image and
  duplicate the system files since it will be an unchanged readonly fs
  and ext3's journaling feature is unnecessary.

$ dd if=/dev/zero of=etch.ext2 bs=1M count=1k
$ mkfs -t ext2 etch.ext2
$ sudo mount -o loop ./etch.ext2 /tmp/w
$ sudo chown 0:0 /tmp/w
$ sudo rsync -aqHSEx --numeric-ids /mnt/* /tmp/w
$ df /tmp/w

  Let's make sure that our files are all fine.

$ cd /mnt
$ sudo find . -printf '%M %n %U %G %s %t %p %l\n' |
> sort -k 11 |
> awk '
	/^l/ {print $1, $2, $3, $4, $5, $11, $12; next}
	/^d/ {print $1, $2, $3, $4, $6, $7, $8, $9, $10, $11, $12; next}
	{print}'
 >| /tmp/l1
$ cd /tmp/w
$ sudo find . -printf '%M %n %U %G %s %t %p %l\n' | sort -k 11 >| /tmp/l2
$ diff -u /tmp/l[12]
$ sudo umount /mnt

  Now we have the base system under /tmp/w.
  Next let's customize the common part.

- customize the common part
  In order to disable the root and swap partition on the local (virtual)
  disk, edit fstab. The file will be such like this.

$ sudo vi /tmp/w/etc/fstab
proc            /proc           proc    defaults        0       0
#/dev/sda1       /               ext3    defaults,errors=remount-ro 0       1
#/dev/sda5       none            swap    sw              0       0
/dev/hdc        /media/cdrom0   udf,iso9660 user,noauto     0       0
/dev/fd0        /media/floppy0  auto    rw,user,noauto  0       0

  In this sample, we use 192.168.1.2 as local DNS server.

$ sudo vi /tmp/w/etc/resolv.conf
nameserver 192.168.1.2

  In this debian VMware appliance, the server to retrieve the packages
  is registered in /etc/apt/sources.list, and it still has CD-ROM
  entry. Let's delete it. Of course, you can customize the package
  server as you like.

$ sudo vi /tmp/w/etc/apt/sources.list
#deb cdrom:[Debian GNU/Linux 4.0 r1 _Etch_ - Official i386 NETINST Binary-1 20070820-20:21]/ etch contrib main

deb http://debian.lcs.mit.edu/debian/ etch main
deb-src http://debian.lcs.mit.edu/debian/ etch main

deb http://security.debian.org/ etch/updates main contrib
deb-src http://security.debian.org/ etch/updates main contrib

  If you don't live within the timezone of EST, it is better to change
  the timezone too.

$ sudo chroot /tmp/w tzconfig

  Now we finished the customization of the "common system
  image". Unmount it and keep the filesystem image.

$ sudo umount /tmp/w


o AUFS on NFS server
----------------------------------------------------------------------
  In this sample, I will not describe the generic NFS server
  issues. Refer to the other documents if you need.
  Building and loading the aufs module is easy, but you should enable
  CONFIG_AUFS_EXPORT and some other configurations. It purely depends
  on your kernel version, and is described in other documents in
  aufs. Please refer them too.
  Here is a sample step to mount and export aufs.

$ mkdir /tmp/ro /tmp/rw /tmp/u
$ sudo mount -o ro,loop etch.ext2 /tmp/ro
$ sudo mount -t aufs -o br=/tmp/rw:/tmp/ro=rr none /tmp/u
$ sudo exportfs -i -o rw,async,no_subtree_check,no_root_squash,fsid=999 \*:/tmp/u

  Please refer to the other manual if you want to know how to use
  exportfs command. In this sample, I just inform you some important
  things.
- export it with remote root user can write
- specify fsid since aufs has no own real disk device


o New VMware appliance
----------------------------------------------------------------------
  In this sample, we create a new VMware appliance which doesn't have
  any local disk. So it will never swap-out. Of course you can create
  local disk and use it as swap area if you want.
  Here we just create a new virtual server by VMware Server Console. I
  hope you know it already and I won't describe here.
  But this sample uses its MAC address in next section to set-up the
  TFTP/DHCP server.


o TFTP/DHCP server
----------------------------------------------------------------------
  This part is entirely depends upon your environment. On my test
  environment, I used dnsmasq and tftpd-hpa tools. Here is what I did.

- /etc/dnsmasq.conf
# assign 192.168.1.99 for a vmware debian
dhcp-range=192.168.1.99,192.168.1.99,12h
dhcp-host=00:1a:2b:3c:4d:5e,192.168.1.99
# boot it with pxe
dhcp-boot=pxelinux.0,jrodns,192.168.1.2
# gateway
dhcp-option=3,192.168.1.1
# dns server
dhcp-option=6,192.168.1.2

  The MAC address here is created by VMware Server Console. You may
  not need it in your environment.
  Now restart dnsmasq daemon.

  Fortunately, the VMware appliance we use here supports nfsroot and
  we just need to pass some parameters to boot it. Also you don't need
  to use MAC address as a filename of configuration. You can freely
  customize your TFTP server. The important thing is booting the
  VMware appliance with nfsroot.

- tftpd-hpa
$ cp /usr/lib/syslinux/pxelinux.0 /var/lib/tftpboot
$ mkdir /var/lib/tftpboot/pxelinux.cfg
$ vi 01-00-1a-2b-3c-4d-5e
default linux
#prompt 1
timeout 600

label linux
	kernel auware/etch/vmlinuz-2.6.18-5-686
	append initrd=auware/etch/initrd.img-2.6.18-5-686 root=/dev/nfs rootfstype=nfs nfsroot=192.168.1.102:/tmp/u,v3 ip=dhcp noresume

- copy the kernel and initramfs image from the vmware appliance to
  /var/lib/tftpboot/auware
$ mkdir -p /var/lib/tftpboot/auware/etch
$ cp -ip /tmp/ro/boot/vmlinuz-2.6.18-5-686 \
  /tmp/ro/boot/initrd.img-2.6.18-5-686 \
  /var/lib/tftpboot/auware/etch

  Of course you don't need to use DHCP if you don't want. In this
  case, you will change the kernel parameters too.


----------------------------------------------------------------------

Now our preparations are all completed.
Boot your virtual server from VMware Server Console. It will boot with
nfsroot and the all modification will be put under /tmp/w on NFS
server.

Enjoy!
