
Brsync -- synchronize files between two aufs branches
J. R. Okajima

Let's assume a system such like this,
- aufs with only two branches
- one lower readonly branch on SSD
- one upper read/write branch on tmpfs
- using the system for a while, usage of the tmpfs grows
- you may want to reduce usage of tmpfs and regain the system main
  memory
- usage of SSD never change since it is readonly, even if you remove
  some larger files in aufs
- you also may want to reduce usage of SSD
- yes, it is ASUS EeePC. :-)

In this case, I'd recommend you to try aubrsync script in aufs2-util.git
tree. It executes rsync(1) between the two branches.


SYNTAX
----------------------------------------------------------------------
aubrsync Options move | move_with_wh | copy \
        mntpnt src_branch dst_branch [ options for rsync ]

generic form:
aubrsync [ -w | --wh ] [ -i | --inotify ] Options \
        mntpnt cmd [ parameters for cmd ]

Options:
        [ -n | --dry_run ]
        [ -q | --quiet ]
----------------------------------------------------------------------

SIMPLE EXAMPLES
----------------------------------------------------------------------
1.
# mount -t aufs -o br:/rw:/ro none /u
# aubrsync copy /u /rw /ro

The script executes rsync(1) and,
- remove the whiteout-ed files in /ro
- COPY the non-whiteouted files in /rw to /ro

2.
# mount -t aufs -o br:/rw:/ro none /u
# aubrsync move /u /rw /ro

This is similar to above except COPY.
The operation 'move' removes the non-whiteouted files in /rw by rsync(1).
After rsync(1), the script finds all whiteouts in /rw and removes them
too.
After this aubrsync, /rw will be almost empty.

For the operation 'move_with_wh', see the sample for 'shwh.'
----------------------------------------------------------------------

EXAMPLES IN DETAIL
----------------------------------------------------------------------
The dst_branch must be mounted as writable.
During the operation, the mntpnt is set readonly.
If you are opening a file for writing on the writable branch,
you need to close the file before invoking this script.
The -w or --wh option requires CONFIG_AUFS_SHWH enabled.
The -i or --inotify option requires CONFIG_AUFS_HINOTIFY enabled.

'copy' is a shortcut for
        aubrsync mntpnt \
        rsync --exclude=lost+found -aHSx --devices --specials --delete-before mntpnt/ dst_branch
'move' is a shortcut for
        aubrsync mntpnt \
        "rsync --exclude=lost+found -aHSx --devices --specials --delete-before \
        mntpnt/ dst_branch && \
        find src_branch -xdev -depth \( \
		\( ! -type d \
			\( -name .wh..wh..opq \
			-o ! -name .wh..wh.\* \) \) \
		-o \( -type d \
			! -name .wh..wh.\* \
			! -wholename src_branch \
			! -wholename src_branch/lost+found \) \
		\) -print0 |\
        xargs -r0 rm -fr"
        Note: in most cases, you will need '-i' option, and
              find(1) is invoked by aubrsync only when rsync(1)
              succeded.
'move_with_wh' is a simple variation of 'move' which moves
whiteouts separately before the actual 'move'.

examples:
- Copy and reflect all the modification (modifed files, newly
  created and removed ones) in the upper branch to the lower
  branch. This operation is for aufs which has only 2 branches,
  and mainly for a system shutdown script.
  All files on the upper branch remain.

  $ sudo aubrsync copy /your/aufs /your/upper_branch /your/lower_branch

- Like above (2 branches), move and reflect all modifications
  from upper to lower. Almost all files on the upper branch will
  be removed. You can still use this aufs after the
  operation. But the inode number may be changed. If your
  application which depends upon the inode number was running at
  that time, it may not work correctly.

  $ sudo aubrsync move /your/aufs /your/upper_branch /your/lower_branch
----------------------------------------------------------------------

NOTE
----------------------------------------------------------------------
Since aubrsync handles the aufs branch directly (bypassing aufs), you
need special care. One recomendation is to execute in the system
shutdown script. It will keep the source aufs branch from modifying, and
you can copy/move files in safe.
Otherwise you need to enable CONFIG_AUFS_HINOTIFY and specify -i
option to aubrsync. The -i option remounts aufs with udba=inotify
internaly and executes 'syncing'. Although even if you use -i, other
processes in your system may modify the files in aufs. If it happens,
the copied/moved files to the lower branch may be obsoleted.
----------------------------------------------------------------------

Some tips for ASUS EeePC users.
----------------------------------------------------------------------
o log files
- Generally the log files are unnecessary to be stacked by aufs.
- Exclude them by mounting tmpfs at /var/log (like /tmp). Recreating
  directoies may be necessary. Customizing /etc/syslogd.conf is good
  too.
- If you want to keep them even after reboot, forget about this
  approach.

o xino files
- The xino files should not put in SSD since it is written
  frequently. While I am not sure how it damges the life of SSD, I'd
  suggest you to put them in tmpfs.

o ~/.xsession-errors
  Currently it grows unconditionally, and I'd like to sugget you to
  remove it just before starting-up the X server. You may want to keep
  the old contents of the file, in this case it is better to rename it
  to .xsession-errors.old or something.
  I am afraid this file can be one of the disk space pressure.
----------------------------------------------------------------------
