
Maintain Aufs Branches Using SHWH mode
Junjiro R. Okajima


Originally aufs hides the whiteout totally and users cannot see/handle
them in aufs. One user, Michael Towers suggested a new aufs mount option
which makes the whiteouts visible.
When I read his idea, to be honest, I was confused. Visible whiteout??

Discussing about his idea by many mails, I could understand what he
wants and I implemented 'shwh' option. After he confirmed that was
exactly what he wanted, he sent a document "EXAMPLE USAGE OF THE 'shwh'
OPTION" to aufs-users ML.
<http://sourceforge.net/mailarchive/forum.php?thread_name=47D64998.50607%40web.de&forum_name=aufs-users>
With this option and his sample, you can merge aufs branches containing
whiteouts, and create a new squashfs image.
Here is a little modified version. If you want to check his original
version, see the url above.

----------------------------------------------------------------------
 ########################################################
 # EXAMPLE USAGE OF THE 'shwh' OPTION ------ 2008.03.11 #
 #					 Michael Towers #
 #			     slightly modified by sfjro #
 ########################################################

 The show-whiteout ('shwh') option (CONFIG_AUFS_SHWH is required)
 can be used to merge aufs branches
 containing whiteouts.

 This example is based on the usage in larch-5.2
 (http://larch.berlios.de), a live USB-stick construction kit, based on
 Arch Linux.
 The live system has an aufs root mount comprising three layers:
 Bottom: 'system', squashfs (underlying base system), read-only
 Middle: 'mods', squashfs, read-only
 Top: 'overlay', ram (tmpfs), read-write

 The top layer is loaded at boot from a tar-lzo archive, which can also
 be saved at shutdown, to preserve the changes made to the system during
 the session.

 When larger changes have been made, or smaller changes have accumulated,
 the tar-lzo archive will have reached a size where loading and saving it
 take an appreciable time. At this point, it would be nice to be able to
 merge the two overlay branches ('mods' and 'overlay') and rewrite the
 'mods' squashfs, clearing the top layer and thus restoring save and load
 speed.

 This merging is simplified by the use of another aufs mount, of just the
 two overlay branches using the new 'shwh' option.
 In larch, access to the individual branches of the root aufs is made
 possible by using 'mount -o bind' in the initramfs. The tmpfs is made
 available at /.livesys, containing mount points /.livesys/mods and
 /.livesys/overlay for the two overlay branches. The new, merging aufs
 mount will be at /.livesys/merge_union and it can be prepared using the
 command:

 # mount -t aufs \
 -o ro,shwh,br:/.livesys/overlay=ro+wh:/.livesys/mods=rr+wh \
 aufs /.livesys/merge_union

 Note that the aufs mount must be 'ro'. A merged view of the two overlay
 branches is then available at /.livesys/merge_union, and the new feature
 is that the whiteouts (.wh..wh..opq, etc.) are visible!

 [[[ Remounting is also possible, e.g.

 # mount -t aufs -o ro,remount,shwh,br:b1=ro+wh:b2=ro+wh aufs mp

 Making the whiteouts vanish again is also possible:

 # mount -o remount,noshwh mp
 ]]]

 It is now possible to save the combined contents of the two overlay
 branches to a new squashfs, e.g.:

 # mksquashfs /.livesys/merge_union /path/to/newmods.squash

 This new squashfs archive can be stored on the boot device and the
 initramfs will use it to replace the old one at the next boot.

 [[[ A new tar-lzo overlay must of course also be built, e.g. (retaining
 as root directory 'overlay'):

 # tar -cf - -C /path/to overlay | lzop > /path/to/newoverlay.tar.lzo
 ]]]

 Share and Enjoy!
 mt
----------------------------------------------------------------------

You may also want trying aubrsync utility in aufs2-util.git tree.
For example, here is a sample script to create a new ext2fs image as a
middle layer in live aufs.

----------------------------------------
#!/bin/sh

AufsMntpnt=/aufs
tmp=/tmp/$$
. /etc/default/aufs

# initial state, you can change it anything you like.
sudo mount -t aufs -o br:/rw:/ro none $AufsMntpnt

# body
sudo mount -o remount,ro,shwh $AufsMntpnt
cd $AufsMntpnt
sudo mount -o remount,rw ../ro
dd if=/dev/zero of=$tmp.img bs=4k count=1k
mkfs -t ext2 -F -q $tmp.img
mkdir $tmp.br
sudo mount -o rw,loop $tmp.img $tmp.br
sudo mount -vo remount,ro,ins:1:$tmp.br=ro+wh $AufsMntpnt
sudo aubrsync _move $AufsMntpnt ../rw ../ro \
	"--remove-source-files \
	--exclude=$AUFS_WH_BASE --exclude=$AUFS_WH_PLINKDIR --exclude=$AUFS_WH_ORPHDIR \
	../rw/ $tmp.br;
	mount -o remount,ro $tmp.br"
sudo mount -o bind $tmp.br ../ro
sudo umount ../ro
sudo mount -o remount,del:$tmp.br $AufsMntpnt
sudo umount $tmp.br
----------------------------------------
