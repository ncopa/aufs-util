/*
 * aufs sample -- ULOOP driver
 *
 * Copyright (C) 2005-2010 Junjiro Okajima
 *
 * This program, aufs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/file.h>
#include <linux/uloop.h>
#include <linux/version.h>
#include <linux/wait.h>

/* ---------------------------------------------------------------------- */

/* in struct loop_device */
#define private_data key_data

struct ulo_queue {
	spinlock_t		spin;
	struct list_head	head;
	wait_queue_head_t	wq;
};

struct ulo_qelem {
	struct list_head	list;
	union ulo_ctl 		ctl;
};

enum {UloQ_READY, UloQ_RCVREQ, UloQ_SNDRES, UloQ_Last};
struct ulo_dev {
	struct ulo_queue	queue[UloQ_Last];

	struct file		*bmp;
	struct mutex		bmpmtx;
	unsigned long		bmpidx;
	unsigned long		*bmpbuf;
	unsigned long long	bmpsz;
};

static struct kmem_cache *ulo_cache;

#define UloMsg(level, fmt, args...) \
	printk(level ULOOP_NAME ":%s[%d]:%s:%d: " fmt, \
		current->comm, current->pid, __func__, __LINE__, ##args)
#define UloErr(fmt, args...)	UloMsg(KERN_ERR, fmt, ##args)

#if 1
#define UloDebugOn(c)		BUG_ON(!!(c))
#define UloDbg(fmt, args...)	UloMsg(KERN_DEBUG, fmt, ##args)
#define UloDbgErr(e)		if (e) UloDbg("err %d\n", e)
#define UloDbg1(fmt, args...) do { \
	static unsigned char c; \
	if (!c++) \
		UloDbg(fmt, ##args); \
} while (0);
#else
#define UloDebugOn(c)		do {} while(0)
#define UloDbg(fmt, args...)	do {} while(0)
#define UloDbgErr(e)		do {} while(0)
#define UloDbg1(fmt, args...)	do {} while(0)
#endif

/* ---------------------------------------------------------------------- */

static void ulo_append(struct ulo_dev *dev, int qindex, struct ulo_qelem *qelem)
{
	struct ulo_queue *queue;
	spinlock_t *spin;

	queue = dev->queue + qindex;
	spin = &queue->spin;
	spin_lock(spin);
	list_add_tail(&qelem->list, &queue->head);
	spin_unlock(spin);
#if 1
	if (1 || qindex == UloQ_READY)
		wake_up(&queue->wq);
#if 0
	else
		wake_up_all(&queue->wq);
#endif
#else
	wake_up_all(&queue->wq);
#endif
}

static int ulo_queue_lock_nonempty(struct ulo_queue *queue)
//__acquires(QueueSpin)
{
	int empty;

	spin_lock(&queue->spin);
	empty = list_empty(&queue->head);
	if (empty)
		spin_unlock(&queue->spin);
	return !empty;
}

static struct ulo_qelem *ulo_wait(struct ulo_dev *dev, int qindex)
//__releases(QueueSpin)
{
	struct ulo_qelem *qelem;
	struct ulo_queue *queue;
	int err;
	spinlock_t *spin;

	qelem = NULL;
	queue = dev->queue + qindex;
	spin = &queue->spin;
	while (!qelem) {
		err = wait_event_interruptible
			(queue->wq, ulo_queue_lock_nonempty(queue));
		if (unlikely(err)) {
			qelem = ERR_PTR(err);
			break;
		}

		qelem = list_entry(queue->head.next, struct ulo_qelem, list);
		list_del(&qelem->list);
		spin_unlock(spin);
	}

	return qelem;
}

/* ---------------------------------------------------------------------- */

static int ulo_write(struct file *file, void *buf, size_t sz, loff_t *ppos)
{
	int err;
	ssize_t ret;
	mm_segment_t oldfs;

	err = 0;
	oldfs = get_fs();
	set_fs(KERNEL_DS);
	ret = vfs_write(file, (const char __user *)buf, sz, ppos);
	set_fs(oldfs);
	if (unlikely(ret != sz)) {
		err = ret;
		if (ret <= sz) {
			UloDbg("ret %ld, sz %lu\n",
			       (long)ret, (unsigned long)sz);
			err = -EIO;
		}
	}

	return err;
}

static int ulo_bmp_pindex(struct ulo_dev *dev, unsigned long pindex)
{
	int err, e2;
	loff_t pos;
	struct file *bmp;
	void *bmpbuf;

	UloDebugOn(!mutex_is_locked(&dev->bmpmtx));

	if (dev->bmpidx == pindex)
		return 0;
	err = -EIO;
	if (unlikely(dev->bmpsz / PAGE_SIZE < pindex))
		goto out;

	bmp = dev->bmp;
	bmpbuf = dev->bmpbuf;
	pos = dev->bmpidx;
	pos *= PAGE_SIZE;
	err = ulo_write(bmp, bmpbuf, PAGE_SIZE, &pos);
	if (unlikely(err))
		goto out;
	UloDebugOn(dev->bmpsz != i_size_read(bmp->f_dentry->d_inode));

	pos = pindex;
	pos *= PAGE_SIZE;
	UloDebugOn(dev->bmpsz < pos + PAGE_SIZE);
	err = kernel_read(bmp, pos, bmpbuf, PAGE_SIZE);
	if (unlikely(err != PAGE_SIZE)) {
		if (0 <= err) {
			UloDbg("%d\n", err);
			err = -EIO;
		}

		/* restore */
		e2 = kernel_read(bmp, pos - PAGE_SIZE, bmpbuf, PAGE_SIZE);
		if (e2 != PAGE_SIZE) {
			UloDbg("%d\n", e2);
			err = -EIO;
		}
		goto out;
	}

	err = 0;
	dev->bmpidx = pindex;
out:
	UloDbgErr(err);
	return err;
}

/* ---------------------------------------------------------------------- */

static int ulo_xfer_begin(struct ulo_dev *dev, unsigned long long start,
			  int size)
{
	int err;
	struct ulo_qelem *qelem;
	struct ulo_queue *queue;
	struct ulo_ctl_ready ready;
	spinlock_t *spin;

	BUILD_BUG_ON(sizeof(qelem->ctl.rcvreq.start) != sizeof(start)
		     || sizeof(qelem->ctl.rcvreq.size) != sizeof(size));
	//UloDbg("start %Lu, size %d\n", start, size);

	qelem = ulo_wait(dev, UloQ_READY);
	err = PTR_ERR(qelem);
	if (IS_ERR(qelem))
		goto out;

	ready = qelem->ctl.ready;
	qelem->ctl.rcvreq.start = start;
	qelem->ctl.rcvreq.size = size;
	queue = dev->queue + UloQ_RCVREQ;
	spin = &queue->spin;
	spin_lock(spin);
	list_add_tail(&qelem->list, &queue->head);
	spin_unlock(spin);

	/* wake up the user process */
	err = kill_pid(ready.pid, ready.signum, 0);
 out:
	UloDbgErr(err);
	return err;
}

static int ulo_xfer_end(struct ulo_dev *dev,
			unsigned long long start, int size,
			unsigned long pindex, int bit)
{
	int err;
	struct ulo_qelem *qelem;
	union ulo_ctl ctl;
	struct mutex *mtx;

	BUILD_BUG_ON(sizeof(ctl.sndres.start) != sizeof(start)
		     || sizeof(ctl.sndres.size) != sizeof(size));

#if 0
	UloDbg("start %Lu, size %d, pindex %lu, bit %d\n",
	       start, size, pindex, bit);
#endif
	while (1) {
		qelem = ulo_wait(dev, UloQ_SNDRES);
		if (IS_ERR(qelem))
			return PTR_ERR(qelem);

		if (qelem->ctl.sndres.start == start
		    && qelem->ctl.sndres.size == size)
			break;
		/* this is not what I want. return it */
		ulo_append(dev, UloQ_SNDRES, qelem);
	}

	/* set bitmap */
	mtx = &dev->bmpmtx;
	mutex_lock(mtx);
	err = ulo_bmp_pindex(dev, pindex);
	if (!err)
		set_bit(bit, dev->bmpbuf);
	mutex_unlock(mtx);

	kmem_cache_free(ulo_cache, qelem);

	UloDbgErr(err);
	return err;
}

static int uloop_xfer(struct loop_device *lo, int cmd,
		      struct page *raw_page, unsigned int raw_off,
		      struct page *loop_page, unsigned int loop_off,
		      int size, sector_t real_block)
{
	int err, set, bit, sz;
	struct ulo_dev *dev;
	char *raw_buf, *loop_buf;
	loff_t pos, loff;
	unsigned long pindex;
	struct mutex *mtx;
	const unsigned long bmp_page_bytes = PAGE_SIZE * BITS_PER_BYTE;

#if 0
	UloDbg("raw_off %u, loop_off %u, sz %d, real_block %lu\n",
	       raw_off, loop_off, size, real_block);
#endif

#if 0
	err = -EACCES;
	if (unlikely(cmd != READ))
		goto out;
#endif
	err = -ESRCH;
	dev = lo->private_data;
	if (unlikely(!dev || !dev->bmp))
		goto out;

	pos = real_block;
	//pos *= KERNEL_SECTOR_SIZE;
	pos *= 512;
	//pos += loop_off;
#if 0
	UloDbg("pos %Lu, raw_off %u, loop_off %u, sz %d, real_block %lu\n",
	       pos, raw_off, loop_off, size, real_block);
#endif

	// todo: optimize (or make intelligent) this loop
	err = 0;
	sz = size;
	mtx = &dev->bmpmtx;
	while (sz > 0) {
		/* test bitmap */
		set = 1;
		loff = pos / PAGE_SIZE;
		pindex = loff / bmp_page_bytes;
		bit = loff % bmp_page_bytes;
		//Dbg("pindex %lu, bit %d\n", pindex, bit);
		mutex_lock(mtx);
		err = ulo_bmp_pindex(dev, pindex);
		if (!err)
			set = test_bit(bit, dev->bmpbuf);
		mutex_unlock(mtx);
		if (unlikely(err))
			goto out;

		/* xfer by userspace */
		if (!set) {
			err = ulo_xfer_begin(dev, pos, PAGE_SIZE);
			if (!err)
				err = ulo_xfer_end(dev, pos, PAGE_SIZE, pindex,
						   bit);
			if (unlikely(err))
				goto out;
		}

		sz -= PAGE_SIZE;
		pos += PAGE_SIZE;
	}

	/* satisfy the request */
	if (!err) {
		raw_buf = kmap_atomic(raw_page, KM_USER0) + raw_off;
		loop_buf = kmap_atomic(loop_page, KM_USER1) + loop_off;
		memcpy(loop_buf, raw_buf, size);
		kunmap_atomic(raw_buf, KM_USER0);
		kunmap_atomic(loop_buf, KM_USER1);
		cond_resched();
	}
 out:
	UloDbgErr(err);
	return err;
}

/* ---------------------------------------------------------------------- */

static int ulo_ctl_setbmp(struct ulo_dev *dev, union ulo_ctl __user *uarg)
{
	struct file *bmp;
	const mode_t rw = (FMODE_READ | FMODE_WRITE);
	int err;
	union ulo_ctl ctl;

	if (unlikely(dev->bmp))
		return -EBUSY;
	if (unlikely((copy_from_user(&ctl, uarg, sizeof(ctl)))))
		return -EFAULT;

	bmp = fget(ctl.setbmp.fd);
	err = -EINVAL;
	if (unlikely(!bmp || IS_ERR(bmp) || ctl.setbmp.pagesize != PAGE_SIZE))
		goto out;
	err = -EBADF;
	if (unlikely((bmp->f_mode & rw) != rw))
		goto out;
	dev->bmpsz = i_size_read(bmp->f_dentry->d_inode);
	if (unlikely(!dev->bmpsz || dev->bmpsz % PAGE_SIZE))
		goto out;
	err = -ENOMEM;
	dev->bmpbuf = (void *)__get_free_page(GFP_KERNEL);
	if (unlikely(!dev->bmpbuf))
		goto out;

	dev->bmp = bmp;
	dev->bmpidx = 0;
	err = kernel_read(dev->bmp, 0, (void *)dev->bmpbuf, PAGE_SIZE);
	if (err == PAGE_SIZE)
		return 0; /* success */

	/* error */
	if (0 <= err)
		err = -EIO;
	dev->bmp = NULL;
	free_page((unsigned long)dev->bmpbuf);
	dev->bmpbuf = NULL;

 out:
	fput(bmp);
	return err;
}

static int ulo_ctl_queue(struct ulo_dev *dev, int qindex,
			 union ulo_ctl __user *uarg)
{
	struct ulo_qelem *qelem;

	/* this element will be freed by ulo_xfer_{start,end}() */
	qelem = kmem_cache_alloc(ulo_cache, GFP_KERNEL);
	if (IS_ERR(qelem))
		return PTR_ERR(qelem);
	if (unlikely(copy_from_user(&qelem->ctl, uarg, sizeof(*uarg)))) {
		kmem_cache_free(ulo_cache, qelem);
		return -EFAULT;
	}
	if (qindex == UloQ_READY)
		qelem->ctl.ready.pid = task_pid(current);

	ulo_append(dev, qindex, qelem);
	return 0;
}

static int ulo_ctl_rcvreq(struct ulo_dev *dev, union ulo_ctl __user *uarg)
{
	struct ulo_queue *queue;
	struct ulo_qelem *qelem;
	spinlock_t *spin;

	//Dbg("rcvreq\n");
	queue = dev->queue + UloQ_RCVREQ;
	qelem = NULL;
	spin = &queue->spin;
	spin_lock(spin);
	if (!list_empty(&queue->head)) {
		qelem = list_entry(queue->head.next, struct ulo_qelem, list);
		list_del(&qelem->list);
	}
	spin_unlock(spin);
	if (unlikely(!qelem))
		return -ENXIO;

	if (!copy_to_user(uarg, &qelem->ctl, sizeof(*uarg))) {
		kmem_cache_free(ulo_cache, qelem);
		return 0;
	}

	/* error */
	ulo_append(dev, UloQ_RCVREQ, qelem);
	return -EFAULT;
}

static int uloop_ioctl(struct loop_device *loop, int cmd, unsigned long _uarg)
{
	int err;
	union ulo_ctl __user *uarg;
	struct ulo_dev *dev;

	uarg = (__user void *)_uarg;
	dev = loop->private_data;
	switch (cmd) {
	case ULOCTL_SETBMP:
		err = ulo_ctl_setbmp(dev, uarg);
		break;
	case ULOCTL_READY:
		err = ulo_ctl_queue(dev, UloQ_READY, uarg);
		break;
	case ULOCTL_RCVREQ:
		err = ulo_ctl_rcvreq(dev, uarg);
		break;
	case ULOCTL_SNDRES:
		err = ulo_ctl_queue(dev, UloQ_SNDRES, uarg);
		break;
	default:
		err = -EINVAL;
	}
	return err;
}

/* ---------------------------------------------------------------------- */

/*
 * ioctl LOOP_SET_STATUS and LOOP_CLR_FD
 */
static int uloop_release(struct loop_device *loop)
{
	int err, i;
	struct ulo_dev *dev;
	struct ulo_queue *queue;
	struct ulo_qelem *qelem, *tmp;
	loff_t pos;

	dev = loop->private_data;
	if (dev->bmpbuf) {
		pos = dev->bmpidx;
		pos *= PAGE_SIZE;
		err = ulo_write(dev->bmp, dev->bmpbuf, PAGE_SIZE, &pos);
		free_page((unsigned long)dev->bmpbuf);
		if (unlikely(err))
			UloErr("bitmap write failed (%d), ignored\n", err);
	}
	if (dev->bmp)
		fput(dev->bmp);

	for (i = 0; i < UloQ_Last; i++) {
		queue = dev->queue + i;
		spin_lock(&queue->spin);
		list_for_each_entry_safe(qelem, tmp, &queue->head, list) {
			list_del(&qelem->list);
			kmem_cache_free(ulo_cache, qelem);
		}
		spin_unlock(&queue->spin);
	}
	kfree(dev);
	loop->private_data = NULL;
	return 0;
}

/* ioctl LOOP_SET_STATUS */
static int uloop_dev_init(struct loop_device *loop,
			  const struct loop_info64 *info)
{
	struct file *file;
	struct ulo_dev *dev;
	int i;
	struct ulo_queue *queue;

	file = loop->lo_backing_file;
	if (unlikely(!file
		     || i_size_read(file->f_dentry->d_inode) % PAGE_SIZE))
		return -EBADF;

	loop->private_data = dev = kmalloc(sizeof(*dev), GFP_KERNEL);
	if (unlikely(!dev))
		return -ENOMEM;

	for (i = 0; i < UloQ_Last; i++) {
		queue = dev->queue + i;
		spin_lock_init(&queue->spin);
		INIT_LIST_HEAD(&queue->head);
		init_waitqueue_head(&queue->wq);
	}
	dev->bmp = NULL;
	mutex_init(&dev->bmpmtx);
	dev->bmpbuf = NULL;
	return 0;
}

/* ---------------------------------------------------------------------- */

static struct loop_func_table uloop_ops = {
	.number		= LOOP_FILTER_ULOOP,
	.release	= uloop_release,
	.init		= uloop_dev_init,
	.transfer	= uloop_xfer,
	.ioctl		= uloop_ioctl,
	//.owner	= THIS_MODULE
};

static int __init uloop_mod_init(void)
{
	int err;

	ulo_cache = kmem_cache_create(ULOOP_NAME, sizeof(struct ulo_qelem), 0,
				      SLAB_RECLAIM_ACCOUNT,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 23)
				      NULL,
#endif
				      NULL);
	if (!ulo_cache)
		return -ENOMEM;

	err = loop_register_transfer(&uloop_ops);
	if (!err)
		printk(KERN_INFO ULOOP_NAME " " ULOOP_VERSION "\n");
	else
		kmem_cache_destroy(ulo_cache);
	return err;
}

static void __exit uloop_mod_exit(void)
{
	loop_unregister_transfer(uloop_ops.number);
	kmem_cache_destroy(ulo_cache);
}

module_init(uloop_mod_init);
module_exit(uloop_mod_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Junjiro Okajima");
MODULE_DESCRIPTION(ULOOP_NAME " -- Userspace Loopback Block Device");
MODULE_VERSION(ULOOP_VERSION);
