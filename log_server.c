#include <stdio.h>
#include "log_rpc.h"

int *log_operacion_1_svc(struct log_data *input, struct svc_req *rqstp) {
    static int result = 0;

    printf("%s\n", input->usuario); // Imprime Nombre_usuario 
    
    if (strlen(input->fichero) > 0) {
        // Si hay fichero, es SENDATTACH 
        printf("%s %s\n", input->operacion, input->fichero);
    } else {
        printf("%s\n", input->operacion); // Imprime OPERACION 
    }

    return &result;
}