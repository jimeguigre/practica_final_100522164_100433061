#include <stdio.h>
#include "log_rpc.h"

int *log_operacion_1_svc(struct log_data *input, struct svc_req *rqstp) {
    static int result = 0;
    printf("[LOG RPC] Usuario: %s | Operación: %s | Fichero: %s\n", 
            input->usuario, input->operacion, input->fichero);
    return &result;
}