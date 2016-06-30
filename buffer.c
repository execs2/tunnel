#include "tunnel.h"
#include "buffer.h"

#include <assert.h>

struct ring_buffer*
	alloc_ring_buffer(int len) {
	struct ring_buffer* rb = (struct ring_buffer*)malloc(sizeof(*rb));
	memset(rb, 0, sizeof(*rb));

	rb->data_ptr = (char *)malloc(len);
	rb->max_len = len;

	return rb;
}

void
free_ring_buffer(struct ring_buffer* rb) {
	free(rb->data_ptr);
	free(rb);
}

char*
get_ring_buffer_write_ptr(struct ring_buffer* rb, int* max_len) {
	int read_pos = rb->read_pos;
	if (rb->write_pos < read_pos) {
		read_pos -= 2 * rb->max_len;
	}

	assert(rb->write_pos >= read_pos);
	if (rb->write_pos - read_pos == rb->max_len) {
		return NULL;
	}

	int w_idx = rb->write_pos % rb->max_len;
	int r_idx = read_pos % rb->max_len;

	if (w_idx >= r_idx) {
		*max_len = rb->max_len - w_idx;
	}else {
		*max_len = r_idx - w_idx;
	}

	return rb->data_ptr + w_idx;
}

void
move_ring_buffer_write_pos(struct ring_buffer*rb, int len) {
	int tmp = rb->write_pos + len;
	if (tmp > 3 * rb->max_len && rb->write_pos > rb->read_pos) {
		tmp -= 2 * rb->max_len;
	}

	rb->write_pos = tmp;
}

char*
get_ring_buffer_read_ptr(struct ring_buffer* rb, int* read_len) {
	int write_pos = rb->write_pos;
	int read_pos = rb->read_pos;

	if (write_pos < read_pos) {
		read_pos -= 2 * rb->max_len;
	}

	assert(write_pos >= read_pos);
	if (read_pos == write_pos) {
		return NULL;
	}

	int w_idx = write_pos % rb->max_len;
	int r_idx = read_pos % rb->max_len;
	if (w_idx > r_idx) {
		*read_len = w_idx - r_idx;
	}else {
		*read_len = rb->max_len - r_idx;
	}

	return rb->data_ptr + read_pos % rb->max_len;
}

void
move_ring_buffer_read_pos(struct ring_buffer*rb, int len) {
	int read_pos = rb->read_pos;
	if (rb->write_pos < read_pos) {
		read_pos -= 2 * rb->max_len;
	}

	rb->read_pos += len;
}

int
is_ring_buffer_empty(struct ring_buffer* rb) {
	int write_pos = rb->write_pos;
	int read_pos = rb->read_pos;

	if (write_pos < read_pos) {
		read_pos -= 2 * rb->max_len;
	}

	assert(write_pos >= read_pos);
	if (read_pos == write_pos) {
		return 1;
	}

	return 0;
}

void
reset_ring_buffer(struct ring_buffer* rb) {
	rb->read_pos = 0;
	rb->write_pos = 0;
}
