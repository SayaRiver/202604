
CC = gcc
CFLAGS = -I .
LIBS = -lz -lsctp -lnats -lm -pthread

nfapi_proxy: nfapi_proxy.c sample_agent.c
	$(CC) nfapi_proxy.c sample_agent.c $(CFLAGS) ./libnp.a -o $@ $(LIBS)

sample_nats: sample_nats.c
	$(CC) sample_nats.c $(CFLAGS) ./libnp.a -o $@ $(LIBS)

clean:
	rm -f nfapi_proxy sample_nats
