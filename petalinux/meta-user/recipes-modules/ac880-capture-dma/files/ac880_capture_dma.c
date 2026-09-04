// SPDX-License-Identifier: GPL-2.0

#include <linux/cdev.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "ac880_capture_dma_uapi.h"

#define AC880_DMA_NAME       "ac880_capture_dma"
#define AC880_NUM_PERIODS    AC880_DMA_NUM_PERIODS
#define AC880_PERIOD_BYTES   AC880_DMA_PERIOD_BYTES
#define AC880_BUFFER_BYTES   AC880_DMA_BUFFER_BYTES

struct ac880_dma_dev {
	struct device *dev;
	struct dma_chan *chan;
	void *cpu_addr;
	dma_addr_t dma_addr;
	struct dma_async_tx_descriptor *desc;
	dma_cookie_t cookie;
	struct mutex op_lock;
	spinlock_t state_lock;
	wait_queue_head_t waitq;
	u64 period_seq;
	unsigned int period_head;
	bool running;
	dev_t devno;
	struct cdev cdev;
	struct class *class;
	struct device *char_dev;
};

struct ac880_dma_file {
	struct ac880_dma_dev *d;
	u64 seen_seq;
};

static void ac880_dma_fill_info(struct ac880_dma_dev *d,
				struct ac880_dma_info *info)
{
	unsigned long flags;

	memset(info, 0, sizeof(*info));
	info->version = AC880_DMA_UAPI_VERSION;
	info->period_bytes = AC880_PERIOD_BYTES;
	info->num_periods = AC880_NUM_PERIODS;
	info->buffer_bytes = AC880_BUFFER_BYTES;
	info->sample_bytes = 1;
	spin_lock_irqsave(&d->state_lock, flags);
	info->period_seq = d->period_seq;
	info->completed_index =
		(d->period_head + AC880_NUM_PERIODS - 1) % AC880_NUM_PERIODS;
	info->running = d->running;
	spin_unlock_irqrestore(&d->state_lock, flags);
}

static void ac880_dma_period_done(void *arg)
{
	struct ac880_dma_dev *d = arg;
	unsigned long flags;

	spin_lock_irqsave(&d->state_lock, flags);
	d->period_head = (d->period_head + 1) % AC880_NUM_PERIODS;
	d->period_seq++;
	spin_unlock_irqrestore(&d->state_lock, flags);
	wake_up_interruptible(&d->waitq);
}

static int ac880_dma_start_locked(struct ac880_dma_dev *d)
{
	unsigned long flags;

	spin_lock_irqsave(&d->state_lock, flags);
	if (d->running) {
		spin_unlock_irqrestore(&d->state_lock, flags);
		return 0;
	}
	spin_unlock_irqrestore(&d->state_lock, flags);

	d->desc = dmaengine_prep_dma_cyclic(d->chan, d->dma_addr,
			AC880_BUFFER_BYTES, AC880_PERIOD_BYTES, DMA_DEV_TO_MEM,
			DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!d->desc)
		return -EIO;
	d->desc->callback = ac880_dma_period_done;
	d->desc->callback_param = d;
	d->cookie = dmaengine_submit(d->desc);
	if (dma_submit_error(d->cookie)) {
		d->desc = NULL;
		return -EIO;
	}
	dma_async_issue_pending(d->chan);

	spin_lock_irqsave(&d->state_lock, flags);
	d->running = true;
	spin_unlock_irqrestore(&d->state_lock, flags);
	return 0;
}

static void ac880_dma_stop_locked(struct ac880_dma_dev *d)
{
	unsigned long flags;

	dmaengine_terminate_sync(d->chan);
	spin_lock_irqsave(&d->state_lock, flags);
	d->running = false;
	d->desc = NULL;
	spin_unlock_irqrestore(&d->state_lock, flags);
	wake_up_interruptible(&d->waitq);
}

static int ac880_dma_open(struct inode *inode, struct file *file)
{
	struct ac880_dma_dev *d = container_of(inode->i_cdev,
			struct ac880_dma_dev, cdev);
	struct ac880_dma_file *af;
	int ret;

	af = kzalloc(sizeof(*af), GFP_KERNEL);
	if (!af)
		return -ENOMEM;
	af->d = d;
	af->seen_seq = READ_ONCE(d->period_seq);
	file->private_data = af;

	mutex_lock(&d->op_lock);
	ret = ac880_dma_start_locked(d);
	mutex_unlock(&d->op_lock);
	if (ret) {
		kfree(af);
		return ret;
	}
	return 0;
}

static int ac880_dma_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	return 0;
}

static ssize_t ac880_dma_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	struct ac880_dma_file *af = file->private_data;
	struct ac880_dma_dev *d = af->d;
	unsigned long flags;
	u64 seq;
	unsigned int index;
	int ret;

	if (count < AC880_PERIOD_BYTES)
		return -EINVAL;
	ret = wait_event_interruptible(d->waitq,
			READ_ONCE(d->period_seq) != af->seen_seq ||
			!READ_ONCE(d->running));
	if (ret)
		return ret;
	if (!READ_ONCE(d->running) && READ_ONCE(d->period_seq) == af->seen_seq)
		return -EPIPE;

	spin_lock_irqsave(&d->state_lock, flags);
	seq = d->period_seq;
	index = (d->period_head + AC880_NUM_PERIODS - 1) % AC880_NUM_PERIODS;
	spin_unlock_irqrestore(&d->state_lock, flags);

	if (copy_to_user(buf, (u8 *)d->cpu_addr + index * AC880_PERIOD_BYTES,
			AC880_PERIOD_BYTES))
		return -EFAULT;
	af->seen_seq = seq;
	return AC880_PERIOD_BYTES;
}

static __poll_t ac880_dma_poll(struct file *file, poll_table *wait)
{
	struct ac880_dma_file *af = file->private_data;
	struct ac880_dma_dev *d = af->d;
	__poll_t mask = 0;

	poll_wait(file, &d->waitq, wait);
	if (READ_ONCE(d->period_seq) != af->seen_seq)
		mask |= EPOLLIN | EPOLLRDNORM;
	if (!READ_ONCE(d->running))
		mask |= EPOLLHUP;
	return mask;
}

static long ac880_dma_ioctl(struct file *file, unsigned int cmd,
			unsigned long arg)
{
	struct ac880_dma_file *af = file->private_data;
	struct ac880_dma_dev *d = af->d;
	u64 seq;
	struct ac880_dma_info info;
	struct ac880_dma_wait wait_info;
	int ret = 0;

	switch (cmd) {
	case AC880_DMA_IOC_START:
		mutex_lock(&d->op_lock);
		ret = ac880_dma_start_locked(d);
		mutex_unlock(&d->op_lock);
		break;
	case AC880_DMA_IOC_STOP:
		mutex_lock(&d->op_lock);
		ac880_dma_stop_locked(d);
		mutex_unlock(&d->op_lock);
		break;
	case AC880_DMA_IOC_GET_SEQ:
		seq = READ_ONCE(d->period_seq);
		if (copy_to_user((void __user *)arg, &seq, sizeof(seq)))
			ret = -EFAULT;
		break;
	case AC880_DMA_IOC_GET_INFO:
		ac880_dma_fill_info(d, &info);
		if (copy_to_user((void __user *)arg, &info, sizeof(info)))
			ret = -EFAULT;
		break;
	case AC880_DMA_IOC_WAIT_SEQ:
		if (copy_from_user(&wait_info, (void __user *)arg,
				   sizeof(wait_info))) {
			ret = -EFAULT;
			break;
		}
		ret = wait_event_interruptible(d->waitq,
			READ_ONCE(d->period_seq) != wait_info.last_seq ||
			!READ_ONCE(d->running));
		if (ret)
			break;
		if (!READ_ONCE(d->running) &&
		    READ_ONCE(d->period_seq) == wait_info.last_seq) {
			ret = -EPIPE;
			break;
		}
		ac880_dma_fill_info(d, &wait_info.info);
		if (copy_to_user((void __user *)arg, &wait_info,
				 sizeof(wait_info)))
			ret = -EFAULT;
		break;
	default:
		ret = -ENOTTY;
		break;
	}
	return ret;
}

static int ac880_dma_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct ac880_dma_file *af = file->private_data;
	struct ac880_dma_dev *d = af->d;
	unsigned long size = vma->vm_end - vma->vm_start;

	if (vma->vm_pgoff != 0 || size == 0 || size > AC880_BUFFER_BYTES)
		return -EINVAL;
	vm_flags_set(vma, VM_DONTEXPAND | VM_DONTDUMP);
	return dma_mmap_coherent(d->dev, vma, d->cpu_addr, d->dma_addr,
				 AC880_BUFFER_BYTES);
}

static const struct file_operations ac880_dma_fops = {
	.owner = THIS_MODULE,
	.open = ac880_dma_open,
	.release = ac880_dma_release,
	.read = ac880_dma_read,
	.poll = ac880_dma_poll,
	.mmap = ac880_dma_mmap,
	.unlocked_ioctl = ac880_dma_ioctl,
	.llseek = noop_llseek,
};

static int ac880_dma_probe(struct platform_device *pdev)
{
	struct ac880_dma_dev *d;
	int ret;

	d = devm_kzalloc(&pdev->dev, sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;
	d->dev = &pdev->dev;
	mutex_init(&d->op_lock);
	spin_lock_init(&d->state_lock);
	init_waitqueue_head(&d->waitq);
	platform_set_drvdata(pdev, d);

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;
	d->chan = dma_request_chan(&pdev->dev, "rx");
	if (IS_ERR(d->chan))
		return dev_err_probe(&pdev->dev, PTR_ERR(d->chan),
				"failed to request rx DMA channel\n");
	d->cpu_addr = dma_alloc_coherent(&pdev->dev, AC880_BUFFER_BYTES,
			&d->dma_addr, GFP_KERNEL);
	if (!d->cpu_addr) {
		ret = -ENOMEM;
		goto err_chan;
	}

	ret = alloc_chrdev_region(&d->devno, 0, 1, AC880_DMA_NAME);
	if (ret)
		goto err_buf;
	cdev_init(&d->cdev, &ac880_dma_fops);
	d->cdev.owner = THIS_MODULE;
	ret = cdev_add(&d->cdev, d->devno, 1);
	if (ret)
		goto err_chrdev;
	d->class = class_create(AC880_DMA_NAME);
	if (IS_ERR(d->class)) {
		ret = PTR_ERR(d->class);
		goto err_cdev;
	}
	d->char_dev = device_create(d->class, &pdev->dev, d->devno, d,
			AC880_DMA_NAME);
	if (IS_ERR(d->char_dev)) {
		ret = PTR_ERR(d->char_dev);
		goto err_class;
	}

	dev_info(&pdev->dev, "S2MM cyclic DMA ready: %u periods x %u bytes\n",
		AC880_NUM_PERIODS, AC880_PERIOD_BYTES);
	return 0;

err_class:
	class_destroy(d->class);
err_cdev:
	cdev_del(&d->cdev);
err_chrdev:
	unregister_chrdev_region(d->devno, 1);
err_buf:
	dma_free_coherent(&pdev->dev, AC880_BUFFER_BYTES, d->cpu_addr,
			d->dma_addr);
err_chan:
	dma_release_channel(d->chan);
	return ret;
}

static void ac880_dma_remove(struct platform_device *pdev)
{
	struct ac880_dma_dev *d = platform_get_drvdata(pdev);

	mutex_lock(&d->op_lock);
	ac880_dma_stop_locked(d);
	mutex_unlock(&d->op_lock);
	device_destroy(d->class, d->devno);
	class_destroy(d->class);
	cdev_del(&d->cdev);
	unregister_chrdev_region(d->devno, 1);
	dma_free_coherent(&pdev->dev, AC880_BUFFER_BYTES, d->cpu_addr,
			d->dma_addr);
	dma_release_channel(d->chan);
}

static const struct of_device_id ac880_dma_of_match[] = {
	{ .compatible = "ac880,acm108-capture-dma" },
	{ }
};
MODULE_DEVICE_TABLE(of, ac880_dma_of_match);

static struct platform_driver ac880_dma_driver = {
	.probe = ac880_dma_probe,
	.remove = ac880_dma_remove,
	.driver = {
		.name = AC880_DMA_NAME,
		.of_match_table = ac880_dma_of_match,
	},
};
module_platform_driver(ac880_dma_driver);

MODULE_DESCRIPTION("AC880 ACM108 AXI DMA S2MM capture client");
MODULE_AUTHOR("AC880 project");
MODULE_LICENSE("GPL");
