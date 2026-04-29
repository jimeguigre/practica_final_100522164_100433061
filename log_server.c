#include <stdio.h>
#include "log_rpc.h"

int *log_operacion_1_svc(struct log_data *input, struct svc_req *rqstp) {
    static int result = 0;

    printf("%s\n", input->usuario); // Imprime Nombre_usuario [cite: 193]
    
    if (strlen(input->fichero) > 0) {
        // Si hay fichero, es SENDATTACH [cite: 208, 210]
        printf("%s %s\n", input->operacion, input->fichero);
    } else {
        printf("%s\n", input->operacion); // Imprime OPERACION [cite: 194]
    }

    return &result;
}