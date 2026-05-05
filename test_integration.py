import socket
import time
import subprocess

def test_flow():
    print("--- Iniciando Prueba de Integración ---")
    
    # 1. Registrar a dos usuarios (simulando protocolo de red)
    # Nota: Aquí usamos sockets directos para verificar que el servidor responde
    def send_op(op, data_list):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect(("127.0.0.1", 8888))
            # Enviar Operación (256 bytes)
            s.sendall(op.ljust(256, '\0').encode())
            # Enviar campos
            for item in data_list:
                s.sendall(item.ljust(256, '\0').encode())
            # Recibir código de respuesta (1 byte)
            res = s.recv(1)
            return int.from_bytes(res, "big")

    print("[1] Registrando a 'Alice'...")
    if send_op("REGISTER", ["Alice"]) == 0: print("OK")
    
    print("[2] Registrando a 'Bob'...")
    if send_op("REGISTER", ["Bob"]) == 0: print("OK")

    print("[3] Conectando a 'Alice' (Puerto 9001)...")
    # Alice se conecta indicando que escucha en el 9001
    send_op("CONNECT", ["Alice", "9001"])

    print("[4] Alice envía mensaje a Bob (que está desconectado)...")
    # Bob está desconectado, el mensaje debería guardarse como pendiente
    send_op("SEND", ["Alice", "Bob", "Hola Bob, veras esto al entrar"])

    print("[5] Conectando a 'Bob' (Puerto 9002)...")
    # Al conectar Bob, el servidor debería reenviarle el mensaje pendiente
    send_op("CONNECT", ["Bob", "9002"])
    
    print("\n--- Prueba básica finalizada. Revisa la consola del servidor. ---")

if __name__ == "__main__":
    test_flow()