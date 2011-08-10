
ULOOP -- Loopback block device in userspace
(and a sample for HTTP and generic block device)
Junjiro Okajima


0. Introduction
As you know, there is a Loopback block device in Linux, /dev/loop,
which enables you to mount a fs-image local file.
Also it can adopt a userspace program, such as cryptloop.
This sample ULOOP driver makes it generic, and enables to adopt any
userspace program.
You can give an empty or non-existing file to /dev/loop backend.
When a process reads from /dev/loop, this dirver wakes a user process
up and passes the I/O transaction to it. A user process makes the
required block ready and tells the driver. Then the driver completes
the I/O transaction.
Also there is sample scripts or usage for diskless nodes working with
aufs. This driver may work with it well.
The name is unrelated to YouTube. :-)


1. sample for HTTP
Simple 'make' will build ./drivers/block/uloop.ko and ./ulohttp.
Ulohttp application behaves like losetup(8). Additionally, ulohttp is
an actual daemon which handles I/O request.
Here is a syntax.

ulohttp [-b bitmap] [-c cache] device URL

The device is /dev/loopN and the URL is a URL for fs-image file via
HTTP. The http server must support byte range (Range: header).
The bitmap is a new filename or previously specified as the bitmap for
the same URL. Its filesize will be 'the size of the specified fs-image
/ pagesize (usually 4k) / bits in a byte (8)', and round-up to
pagesize.
The cache is a new filename or previously specified as the cache for
the same URL. Its filesize will be 'the size of the specified
fs-image', and round-up to pagesize.
Note that both the bitmap and the cache are re-usable as long as you
don't change the filedata and URL.

When someone reads from the specified /dev/loopN, or accesses a file
on a filesystem after mounting /dev/loopN, ULOOP driver first checks
the corresponding bit in the bitmap file. When the bit is not set,
which means the block is not retrieved yet, it passes the offset and
size of the I/O request to ulohttp daemon.
Ulohttp converts the offset and the size into HTTP GET request with
Range header and send it to the http server.
Retriving the data from the http server, ulohttp stores it to the
cache file, and tells ULOOP driver that the HTTP transfer completes.
Then the ULOOP driver sets the corresponding bit in the bitmap, and
finishes the I/O/request.

In other words, it is equivalent to this operation.
$ wget URL_for_fsimage
$ sudo mount -o loop retrieved_fsimage /mnt
But ULOOP driver and ulohttp retrieves only the data (block) on-demand,
and stores into the cache file. The first access to a block is slow
since it involves HTTP GET, but the next access to the same block is
fast since it is in the local cache file. In this case, the behaviour
is equivalent to the simple /dev/loop device.

o Note
- ulohttp requires libcurl.
- ulohttp doesn't support HTTP PUT or POST, so the device rejects
  WRITE operation.
- ulohttp doesn't have a smart exit routine.
- This sample is "proof-of-concepts", do not expect the maturity level
  too much.
- This driver and the sample is developed and tested on linux-2.6.21.3.
- If you implement other protocols such like nbd/enbd, iscsi, aoe or
  something, instead of http, I guess it will be fantastic. :-)

o Usage
$ make
$ sudo modprobe loop
$ sudo insmod ./drivers/block/uloop.ko
$ dev=/dev/loop7
$ ./ulohttp -b /tmp/b -c /tmp/c $dev http://whatever/you/like
$ sudo mount -o ro $dev /mnt
$ ls /mnt
	:::
$ sudo umount /mnt
$ killall ulohttp
$ sudo losetup -d $dev


2. sample for generic block device
The sample `ulohttp' (above) retrieves data from a remote host via
HTTP, and stores it into a local file as a cache. It means you can
reduce the network traffic and the workload on a remote server.
As you can guess easily, this scheme is also effective to a local disk
device, especially when you want to make your disk and spin down/off
it. Recent flash memory is getting larger and cheaper. You can cache
the whole contents of your harddrive into a file on your flash.
Here is a sample for it, `ulobdev.' The basic usage is very similar to
`ulohttp'. See above.
Of course, it is available for remote block devices too, such as
nbd/enbd, iscsi and aoe.

You should not mount the backend block device as readwrite, since it
modifies the superblock of the filesystem on the block device even if
you don't write anything to it.

Currently this sample supports readonly mode only.
If someone is interested in this approach and sample, I will add some
features which will support read/write mode and write-back to the
harddrive periodically, and discard/re-create the cache file.


3. libuloop API
- int ulo_init(struct ulo_init *init);
  struct ulo_init {
	char *path[ULO_Last];
	int dev_flags;
	unsigned long long size;
  };
  enum {ULO_DEV, ULO_CACHE, ULO_BITMAP, ULO_Last};

  Initializes ULOOP driver. All members in struct ulo_init must be set
  before you call ulo_init().
	+ path[ULO_DEV]
	  pathname of loopback device such as "/dev/loopN".
	+ path[ULO_CACHE]
	  pathname of a cache file. A userspace program stores the
	  real data to this file.
	+ path[ULO_BITMAP]
	  pathname of a bitmap file. The ULOOP driver sets the bit
	  which is corresponding the block number when the block is
	  filled by a userspace program. When the bit is not set,
	  ULOOP driver invokes the userspace program.
	+ dev_flags
	  Flags for open(2) of path[ULO_DEV].
	+ size
	  the size of real data. the ULOOP library set this size to
	  the cache file after creating it internally.

- int ulo_loop(int sig, ulo_cb_t store, void *arg);
  typedef int (*ulo_cb_t)(unsigned long long start, int size, void *arg);

  Waits for a I/O request from ULOOP driver. When a user accesses a
  ULOOP device, ULOOP driver translates the request to the offset in
  the cache file and the requested size, and invokes the user-defined
  callback function which is specified by `store.' The function `store'
  must fill the data in the cache file following the given offset and
  size. You can add an argument `arg' for the callback function.

- extern const struct uloop *uloop;
  struct uloop {
	int fd[ULO_Last];
	int pagesize;
	unsigned long long tgt_size, cache_size;
  };

  A global variable in ULOOP library. Usually you will need
  'ulo_cache_fd` only. See below.
	#define ulo_dev_fd	({ uloop->fd[ULO_DEV]; })
	#define ulo_cache_fd	({ uloop->fd[ULO_CACHE]; })
	#define ulo_bitmap_fd	({ uloop->fd[ULO_BITMAP]; })


Enjoy!
