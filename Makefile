CC = gcc
# Añadimos -I/usr/include/tirpc para que encuentre rpc/rpc.h
CFLAGS = -Wall -g -pthread -I/usr/include/tirpc
# Añadimos -ltirpc para enlazar la librería
LDLIBS = -ltirpc -lpthread
RPCGEN = rpcgen

all: server rpc_server

log_rpc.h log_rpc_clnt.c log_rpc_svc.c log_rpc_xdr.c: log_rpc.x
	$(RPCGEN) -C log_rpc.x

server: servidor_mensajeria.c servidor_gestor.c log_rpc_clnt.c log_rpc_xdr.c
	$(CC) $(CFLAGS) -o server $^ $(LDLIBS)

rpc_server: log_server.c log_rpc_svc.c log_rpc_xdr.c
	$(CC) $(CFLAGS) -o rpc_server $^ $(LDLIBS)

clean:
	rm -f server rpc_server log_rpc_clnt.* log_rpc_svc.* log_rpc.h log_rpc_xdr.*