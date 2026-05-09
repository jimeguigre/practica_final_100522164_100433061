from flask import Flask, request, jsonify

app = Flask(__name__)

@app.route('/normalize', methods=['POST'])
def normalize():
    # Obtiene el mensaje del JSON recibido
    data = request.get_json()
    msg = data.get('message', '')
    # Elimina espacios repetidos: separa por espacios y vuelve a unir con uno solo
    normalized_msg = " ".join(msg.split())
    return jsonify({"normalized_message": normalized_msg})

if __name__ == '__main__':
    # Se despliega en la máquina local del cliente 
    app.run(port=5001)