#ifndef	BUFFER_H
#define BUFFER_H

struct ring_buffer
{
	char* data_ptr;
	int max_len;
	volatile int read_pos;
	volatile int write_pos;
};

struct ring_buffer* alloc_ring_buffer(int len);
void free_ring_buffer(struct ring_buffer* rb);
char* get_ring_buffer_write_ptr(struct ring_buffer* rb, int* max_len);
void move_ring_buffer_write_pos(struct ring_buffer*rb, int len);
char* get_ring_buffer_read_ptr(struct ring_buffer* rb, int* read_len);
void move_ring_buffer_read_pos(struct ring_buffer*rb, int len);
int is_ring_buffer_empty(struct ring_buffer* rb);
void reset_ring_buffer(struct ring_buffer* rb);
#endif
