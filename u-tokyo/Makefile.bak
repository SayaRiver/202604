
CC = gcc
CFLAGS = -I .
LIBS = -lz -lsctp -lnats -lm -pthread

nfapi_proxy: nfapi_proxy.c sample_agent.c
	$(CC) nfapi_proxy.c sample_agent.c $(CFLAGS) ./libnp.a -o $@ $(LIBS)

sample_nats: sample_nats.c
	$(CC) sample_nats.c $(CFLAGS) -o $@ -lnats -lm -pthread

dump_nats: dump_nats.c
	$(CC) dump_nats.c $(CFLAGS) -o $@ -lnats -pthread

clean:
	rm -f nfapi_proxy sample_nats dump_nats
