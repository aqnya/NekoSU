// SPDX-License-Identifier: GPL-3.0-or-later
#include "klog.h"
#include <linux/eventfd.h>
#include <linux/spinlock.h>
#include <linux/version.h>
#include "fd/fd.h"

static struct eventfd_ctx *event_ctx;
static DEFINE_SPINLOCK(event_ctx_lock);

int nksu_bind_eventfd(int fd)
{
	struct eventfd_ctx *ctx = eventfd_ctx_fdget(fd);
	struct eventfd_ctx *old;
	unsigned long flags;

	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	spin_lock_irqsave(&event_ctx_lock, flags);
	old = event_ctx;
	event_ctx = ctx;
	spin_unlock_irqrestore(&event_ctx_lock, flags);

	if (old)
		eventfd_ctx_put(old);
	return 0;
}

void fmac_notify_user(void)
{
	unsigned long flags;

	spin_lock_irqsave(&event_ctx_lock, flags);
	if (!event_ctx)
		goto out;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0)
	eventfd_signal(event_ctx);
#else
	eventfd_signal(event_ctx, 1);
#endif
out:
	spin_unlock_irqrestore(&event_ctx_lock, flags);
}

void fmac_eventfd_exit(void)
{
	struct eventfd_ctx *ctx;
	unsigned long flags;

	spin_lock_irqsave(&event_ctx_lock, flags);
	ctx = event_ctx;
	event_ctx = NULL;
	spin_unlock_irqrestore(&event_ctx_lock, flags);

	if (ctx)
		eventfd_ctx_put(ctx);
}
