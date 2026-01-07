/*
 * Copyright NXP 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ndp/ndp_sched.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

static void ndp_stage_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	struct ndp_stage *st = (struct ndp_stage *)p1;
	struct ndp_stage_ctx *ctx = &st->ctx;

	if (st->ops != NULL && st->ops->start != NULL) {
		(void)st->ops->start(ctx);
	}

	atomic_set(&st->running, 1);

	struct k_poll_event events[CONFIG_NDP_MAX_PORTS + 1];
	uint8_t event_count = 0U;

	k_poll_signal_init(&st->stop_sig);
	events[event_count++] = (struct k_poll_event)K_POLL_EVENT_INITIALIZER(
		K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &st->stop_sig);

	for (uint8_t i = 0; i < st->num_inputs && i < CONFIG_NDP_MAX_PORTS; i++) {
		if (st->in[i] == NULL) {
			continue;
		}
		(void)ndp_channel_poll_event(st->in[i], &events[event_count++]);
	}

	while (atomic_get(&st->running) != 0) {
		if (st->num_inputs == 0U && st->ops != NULL && st->ops->run != NULL) {
			(void)st->ops->run(ctx);
			if (!K_TIMEOUT_EQ(st->source_period, K_NO_WAIT) && !K_TIMEOUT_EQ(st->source_period, K_FOREVER)) {
				k_sleep(st->source_period);
			} else {
				k_yield();
			}
			continue;
		}

		int rc = k_poll(events, event_count, K_FOREVER);
		if (rc != 0) {
			continue;
		}

		if (events[0].state == K_POLL_STATE_SIGNALED) {
			break;
		}

		uint8_t msgq_event_index = 1U;
		for (uint8_t in_port = 0; in_port < st->num_inputs && in_port < CONFIG_NDP_MAX_PORTS; in_port++) {
			if (st->in[in_port] == NULL) {
				continue;
			}

			struct k_poll_event *ev = &events[msgq_event_index++];
			if (ev->state != K_POLL_STATE_MSGQ_DATA_AVAILABLE) {
				continue;
			}

			struct ndp_msg msg;
			while (ndp_channel_recv(st->in[in_port], &msg, K_NO_WAIT) == 0) {
				if (st->ops != NULL && st->ops->process != NULL) {
					(void)st->ops->process(ctx, &msg, in_port);
				}
			}

			/* If this input was semaphore-driven, consume any pending counts
			 * so k_poll doesn't wake spuriously.
			 */
			if (st->in[in_port]->backend == NDP_CH_BACKEND_AMP_SHM_RING &&
			    st->in[in_port]->u.amp.notify_sem != NULL) {
				while (k_sem_take(st->in[in_port]->u.amp.notify_sem, K_NO_WAIT) == 0) {
					/* drain */
				}
			}
		}

		for (uint8_t i = 0; i < event_count; i++) {
			events[i].state = K_POLL_STATE_NOT_READY;
		}
	}

	if (st->ops != NULL && st->ops->stop != NULL) {
		(void)st->ops->stop(ctx);
	}

	atomic_set(&st->running, 0);
}

int ndp_sched_start_stage(struct ndp_stage *st)
{
	if (st == NULL || st->ops == NULL) {
		return -EINVAL;
	}
	if (st->stack_area == NULL || st->stack_size == 0U) {
		return -EINVAL;
	}

	st->tid = k_thread_create(&st->thread,
				 st->stack_area, st->stack_size,
				 ndp_stage_thread,
				 st, NULL, NULL,
				 st->prio,
				 0,
				 K_NO_WAIT);
	if (st->tid == NULL) {
		return -EIO;
	}

	k_thread_name_set(st->tid, st->name ? st->name : "ndp_stage");
	return 0;
}

int ndp_sched_stop_stage(struct ndp_stage *st)
{
	if (st == NULL) {
		return -EINVAL;
	}

	atomic_set(&st->running, 0);
	k_poll_signal_raise(&st->stop_sig, 1);
	return 0;
}
