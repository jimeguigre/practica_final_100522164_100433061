struct log_data {
    string usuario<256>;
    string operacion<256>;
    string fichero<256>; /* Solo se usa para SENDATTACH [cite: 208, 213] */
};

program LOG_PROG {
    version LOG_VERS {
        int LOG_OPERACION(struct log_data) = 1;
    } = 1;
} = 0x20000001;