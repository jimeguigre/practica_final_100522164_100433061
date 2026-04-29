from enum import Enum
import argparse
import socket
import threading
import struct

class client:

    # ******************** TYPES *********************
    class RC(Enum):
        OK = 0
        ERROR = 1
        USER_ERROR = 2

    # ****************** ATTRIBUTES ******************
    _server = None
    _port = -1

    # Estado de conexión del cliente
    _connected_user = None       # nombre del usuario conectado actualmente
    _listen_socket = None        # socket de escucha para mensajes entrantes
    _listen_thread = None        # hilo receptor de mensajes
    _listen_port = -1            # puerto de escucha asignado

    # ****************** AUXILIARES ******************

    @staticmethod
    def _send_field(sock, text):
        """Envía un campo de exactamente 256 bytes terminado en \0."""
        encoded = text.encode('utf-8')[:255]  # máximo 255 chars + \0
        field = encoded + b'\0'
        field = field.ljust(256, b'\0')
        sock.sendall(field)

    @staticmethod
    def _recv_field(sock):
        """Recibe un campo de exactamente 256 bytes y devuelve el string sin \0."""
        data = b''
        while len(data) < 256:
            chunk = sock.recv(256 - len(data))
            if not chunk:
                raise ConnectionError("Conexión cerrada por el servidor")
            data += chunk
        return data.split(b'\0', 1)[0].decode('utf-8')

    @staticmethod
    def _find_free_port():
        """Busca un puerto TCP libre en el sistema."""
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.bind(('', 0))
            return s.getsockname()[1]

    @staticmethod
    def _listener_thread_func():
        """
        Hilo receptor: escucha conexiones entrantes del servidor.
        Procesa SEND_MESSAGE (mensaje de otro usuario) y SEND_MESS_ACK (confirmación de entrega).
        """
        listen_sock = client._listen_socket
        listen_sock.listen(10)

        while True:
            try:
                conn, _ = listen_sock.accept()
            except OSError:
                # El socket fue cerrado (DISCONNECT), salimos del hilo
                break

            try:
                op = client._recv_field(conn)

                if op == "SEND_MESSAGE":
                    # Protocolo 8.6: recibir mensaje de otro usuario
                    remitente = client._recv_field(conn)
                    id_msg    = client._recv_field(conn)
                    mensaje   = client._recv_field(conn)
                    print(f"\ns> MESSAGE {id_msg} FROM {remitente}")
                    print(mensaje)
                    print("END")

                elif op == "SEND_MESS_ACK":
                    # Protocolo 8.6: confirmación de entrega al remitente
                    id_msg = client._recv_field(conn)
                    print(f"\nc> SEND MESSAGE {id_msg} OK")

            except Exception:
                pass
            finally:
                conn.close()

    # ******************** METHODS *******************

    @staticmethod
    def register(user):
        """Registra un nuevo usuario en el servidor."""
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))
            client._send_field(s, "REGISTER")
            client._send_field(s, user)
            res = s.recv(1)[0]
            s.close()

            if res == 0:
                print("c> REGISTER OK")
                return client.RC.OK
            elif res == 1:
                print("c> USERNAME IN USE")
                return client.RC.USER_ERROR
            else:
                print("c> REGISTER FAIL")
                return client.RC.ERROR
        except Exception:
            print("c> REGISTER FAIL")
            return client.RC.ERROR

    @staticmethod
    def unregister(user):
        """Da de baja a un usuario del servidor."""
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))
            client._send_field(s, "UNREGISTER")
            client._send_field(s, user)
            res = s.recv(1)[0]
            s.close()

            if res == 0:
                print("c> UNREGISTER OK")
                return client.RC.OK
            elif res == 1:
                print("c> USER DOES NOT EXIST")
                return client.RC.USER_ERROR
            else:
                print("c> UNREGISTER FAIL")
                return client.RC.ERROR
        except Exception:
            print("c> UNREGISTER FAIL")
            return client.RC.ERROR

    @staticmethod
    def connect(user):
        """
        Conecta al usuario al servicio:
        1. Busca puerto libre
        2. Crea hilo receptor
        3. Envía solicitud de conexión al servidor con el puerto
        """
        try:
            # 1. Buscar puerto libre y crear socket de escucha
            port = client._find_free_port()
            listen_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            listen_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            listen_sock.bind(('', port))

            # 2. Crear hilo receptor ANTES de enviar la solicitud al servidor
            client._listen_socket = listen_sock
            client._listen_port = port
            t = threading.Thread(target=client._listener_thread_func, daemon=True)
            client._listen_thread = t
            t.start()

            # 3. Enviar solicitud de conexión al servidor (protocolo 8.3)
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))
            client._send_field(s, "CONNECT")
            client._send_field(s, user)
            client._send_field(s, str(port))   # el puerto se envía como cadena
            res = s.recv(1)[0]
            s.close()

            if res == 0:
                client._connected_user = user
                print("c> CONNECT OK")
                return client.RC.OK
            elif res == 1:
                # Usuario no existe: cerrar hilo
                client._stop_listener()
                print("c> CONNECT FAIL, USER DOES NOT EXIST")
                return client.RC.USER_ERROR
            elif res == 2:
                # Ya conectado: cerrar hilo
                client._stop_listener()
                print("c> USER ALREADY CONNECTED")
                return client.RC.USER_ERROR
            else:
                client._stop_listener()
                print("c> CONNECT FAIL")
                return client.RC.ERROR

        except Exception:
            client._stop_listener()
            print("c> CONNECT FAIL")
            return client.RC.ERROR

    @staticmethod
    def _stop_listener():
        """Para el hilo receptor cerrando el socket de escucha."""
        if client._listen_socket is not None:
            try:
                client._listen_socket.close()
            except Exception:
                pass
            client._listen_socket = None
        client._listen_thread = None
        client._listen_port = -1
        client._connected_user = None

    @staticmethod
    def disconnect(user):
        """Desconecta al usuario y para el hilo receptor."""
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))
            client._send_field(s, "DISCONNECT")
            client._send_field(s, user)
            res = s.recv(1)[0]
            s.close()

            # Siempre paramos el hilo, aunque haya error (según el enunciado)
            client._stop_listener()

            if res == 0:
                print("c> DISCONNECT OK")
                return client.RC.OK
            elif res == 1:
                print("c> DISCONNECT FAIL, USER DOES NOT EXIST")
                return client.RC.USER_ERROR
            elif res == 2:
                print("c> DISCONNECT FAIL, USER NOT CONNECTED")
                return client.RC.USER_ERROR
            else:
                print("c> DISCONNECT FAIL")
                return client.RC.ERROR

        except Exception:
            client._stop_listener()
            print("c> DISCONNECT FAIL")
            return client.RC.ERROR

    @staticmethod
    def send(user, message):
        """
        Envía un mensaje a otro usuario.
        Necesita que haya un usuario conectado (_connected_user).
        """
        if client._connected_user is None:
            print("c> SEND FAIL")
            return client.RC.ERROR

        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))
            client._send_field(s, "SEND")
            client._send_field(s, client._connected_user)  # remitente
            client._send_field(s, user)                    # destinatario
            client._send_field(s, message)                 # mensaje
            res = s.recv(1)[0]

            if res == 0:
                # Recibir el ID asignado al mensaje
                id_msg = client._recv_field(s)
                s.close()
                print(f"c> SEND OK - MESSAGE {id_msg}")
                return client.RC.OK
            elif res == 1:
                s.close()
                print("c> SEND FAIL, USER DOES NOT EXIST")
                return client.RC.USER_ERROR
            else:
                s.close()
                print("c> SEND FAIL")
                return client.RC.ERROR

        except Exception:
            print("c> SEND FAIL")
            return client.RC.ERROR

    @staticmethod
    def users():
        """Solicita la lista de usuarios conectados."""
        if client._connected_user is None:
            print("c> CONNECTED USERS FAIL")
            return client.RC.ERROR

        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))
            client._send_field(s, "USERS")
            client._send_field(s, client._connected_user)  # envía el nombre del usuario
            res = s.recv(1)[0]

            if res == 0:
                # Recibir número de usuarios conectados
                num_str = client._recv_field(s)
                num = int(num_str)
                print(f"c> CONNECTED USERS ({num} users connected) OK")
                # Recibir cada usuario (una cadena por usuario)
                for _ in range(num):
                    user_info = client._recv_field(s)
                    print(f"  {user_info}")
                s.close()
                return client.RC.OK
            elif res == 1:
                s.close()
                print("c> CONNECTED USERS FAIL, USER IS NOT CONNECTED")
                return client.RC.USER_ERROR
            else:
                s.close()
                print("c> CONNECTED USERS FAIL")
                return client.RC.ERROR

        except Exception:
            print("c> CONNECTED USERS FAIL")
            return client.RC.ERROR

    @staticmethod
    def sendAttach(user, file, message):
        """Envía un mensaje con fichero adjunto (Parte 2)."""
        if client._connected_user is None:
            print("c> SENDATTACH FAIL")
            return client.RC.ERROR
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((client._server, client._port))
            client._send_field(s, "SENDATTACH")
            client._send_field(s, client._connected_user)
            client._send_field(s, user)
            client._send_field(s, message)
            client._send_field(s, file)
            res = s.recv(1)[0]

            if res == 0:
                id_msg = client._recv_field(s)
                s.close()
                print(f"c> SENDATTACH OK - MESSAGE {id_msg}")
                return client.RC.OK
            elif res == 1:
                s.close()
                print("c> SEND FAIL, USER DOES NOT EXIST")
                return client.RC.USER_ERROR
            else:
                s.close()
                print("c> SENDATTACH FAIL")
                return client.RC.ERROR
        except Exception:
            print("c> SENDATTACH FAIL")
            return client.RC.ERROR

    # ******************** SHELL ********************

    @staticmethod
    def shell():
        while True:
            try:
                command = input("c> ")
                line = command.split(" ")
                if len(line) > 0:
                    line[0] = line[0].upper()

                    if line[0] == "REGISTER":
                        if len(line) == 2:
                            client.register(line[1])
                        else:
                            print("Syntax error. Usage: REGISTER <userName>")

                    elif line[0] == "UNREGISTER":
                        if len(line) == 2:
                            client.unregister(line[1])
                        else:
                            print("Syntax error. Usage: UNREGISTER <userName>")

                    elif line[0] == "CONNECT":
                        if len(line) == 2:
                            client.connect(line[1])
                        else:
                            print("Syntax error. Usage: CONNECT <userName>")

                    elif line[0] == "DISCONNECT":
                        if len(line) == 2:
                            client.disconnect(line[1])
                        else:
                            print("Syntax error. Usage: DISCONNECT <userName>")

                    elif line[0] == "USERS":
                        if len(line) == 1:
                            client.users()
                        else:
                            print("Syntax error. Usage: USERS")

                    elif line[0] == "SEND":
                        if len(line) >= 3:
                            message = ' '.join(line[2:])
                            client.send(line[1], message)
                        else:
                            print("Syntax error. Usage: SEND <userName> <message>")

                    elif line[0] == "SENDATTACH":
                        if len(line) >= 4:
                            message = ' '.join(line[3:])
                            client.sendAttach(line[1], line[2], message)
                        else:
                            print("Syntax error. Usage: SENDATTACH <userName> <filename> <message>")

                    elif line[0] == "QUIT":
                        if len(line) == 1:
                            break
                        else:
                            print("Syntax error. Use: QUIT")
                    else:
                        print("Error: command " + line[0] + " not valid.")

            except Exception as e:
                print("Exception: " + str(e))

    @staticmethod
    def usage():
        print("Usage: python3 client.py -s <server> -p <port>")

    @staticmethod
    def parseArguments(argv):
        parser = argparse.ArgumentParser()
        parser.add_argument('-s', type=str, required=True, help='Server IP')
        parser.add_argument('-p', type=int, required=True, help='Server Port')
        args = parser.parse_args()

        if args.s is None:
            parser.error("Usage: python3 client.py -s <server> -p <port>")
            return False

        if args.p < 1024 or args.p > 65535:
            parser.error("Error: Port must be in the range 1024 <= port <= 65535")
            return False

        client._server = args.s   # guardar en atributo de clase
        client._port = args.p

        return True

    @staticmethod
    def main(argv):
        if not client.parseArguments(argv):
            client.usage()
            return
        client.shell()
        print("+++ FINISHED +++")


if __name__ == "__main__":
    client.main([])