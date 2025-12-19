CC = gcc
CFLAGS = -Wall -Wextra -O2 -I..
LDFLAGS = 

TESTS = test_ncq test_identify test_ioctl test_port_reset test_port_start_stop test_read_dma

all: $(TESTS)

test_ncq: test_ncq.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_identify: test_identify.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_ioctl: test_ioctl.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_port_reset: test_port_reset.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_port_start_stop: test_port_start_stop.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_read_dma: test_read_dma.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TESTS) *.o

.PHONY: all clean
