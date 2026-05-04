CC = gcc
CFLAGS = -Wall -g -pthread -Wno-incompatible-function-pointer-types -Wno-incompatible-pointer-types -Wno-deprecated-non-prototype -Wno-pointer-sign
RPCGEN = rpcgen

all: server rpc_server

# Regla para generar archivos RPC a partir del .x
log_rpc_clnt.c log_rpc_svc.c log_rpc.h log_rpc_xdr.c: log_rpc.x
	$(RPCGEN) -C log_rpc.x

# Compilar el servidor de mensajería principal
server: servidor_mensajeria.c servidor_gestor.c log_rpc_clnt.c log_rpc_xdr.c log_rpc.h
	$(CC) $(CFLAGS) -o server servidor_mensajeria.c servidor_gestor.c log_rpc_clnt.c log_rpc_xdr.c

# Compilar el servidor RPC de logs
rpc_server: log_server.c log_rpc_svc.c log_rpc_xdr.c log_rpc.h
	$(CC) $(CFLAGS) -o rpc_server log_server.c log_rpc_svc.c log_rpc_xdr.c

clean:
	rm -f server rpc_server log_rpc_clnt.* log_rpc_svc.* log_rpc.h log_rpc_xdr.*