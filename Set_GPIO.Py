from flask import Flask, render_template, request, jsonify, Response
from flask_cors import CORS
from gpiozero import OutputDevice
import serial
import struct
import threading
import time

app = Flask(__name__)
CORS(app)

# =========================
# GPIO CONFIG
# =========================
test_gpio = OutputDevice(16, initial_value=False)

robot_pins = {
    1: {
        'HAUT': OutputDevice(17, initial_value=False),
        'GAUCHE': OutputDevice(27, initial_value=False),
        'DROITE': OutputDevice(22, initial_value=False),
        'BAS': OutputDevice(23, initial_value=False),
    },
    2: {
        'HAUT': OutputDevice(24, initial_value=False),
        'GAUCHE': OutputDevice(25, initial_value=False),
        'DROITE': OutputDevice(5, initial_value=False),
        'BAS': OutputDevice(6, initial_value=False),
    }
}

# =========================
# CAMERA USB SERIE CONFIG
# =========================
SERIAL_PORT = "/dev/ttyUSB1"
BAUDRATE = 115200
MAGIC = b"IMG0"

latest_frame = None
latest_lock = threading.Lock()


def read_exact(ser, n):
    data = b""
    while len(data) < n:
        chunk = ser.read(n - len(data))
        if not chunk:
            raise TimeoutError("Timeout lecture serie")
        data += chunk
    return data


def find_magic(ser):
    buf = b""
    start = time.time()

    while True:
        b = ser.read(1)
        if not b:
            if time.time() - start > 10:
                raise TimeoutError("Timeout: MAGIC non trouve")
            continue

        buf += b
        if len(buf) > 4:
            buf = buf[-4:]

        if buf == MAGIC:
            return


def serial_worker():
    global latest_frame

    while True:
        try:
            print("Ouverture du port serie...", SERIAL_PORT)
            ser = serial.Serial(
                SERIAL_PORT,
                BAUDRATE,
                timeout=2,
                rtscts=False,
                dsrdtr=False
            )

            ser.setRTS(False)
            ser.setDTR(False)

            time.sleep(4)

            start = time.time()
            while time.time() - start < 2:
                if ser.in_waiting:
                    line = ser.readline().decode(errors="ignore").strip()
                    if line:
                        print("[ESP32]", line)

            while True:
                try:
                    ser.reset_input_buffer()
                    ser.write(b"R")
                    ser.flush()

                    find_magic(ser)

                    raw_len = read_exact(ser, 4)
                    frame_len = struct.unpack("<I", raw_len)[0]

                    if frame_len <= 0 or frame_len > 200000:
                        print("Taille invalide :", frame_len)
                        continue

                    frame = read_exact(ser, frame_len)

                    with latest_lock:
                        latest_frame = frame

                    time.sleep(0.15)

                except Exception as e:
                    print("Erreur reception frame :", e)
                    time.sleep(1)

        except Exception as e:
            print("Erreur ouverture port serie :", e)
            time.sleep(2)


# =========================
# ROUTES FLASK
# =========================

@app.route('/')
def index():
    return render_template('index.html')


@app.route('/frame.jpg')
def frame_jpg():
    with latest_lock:
        if latest_frame is None:
            return Response(status=503)
        data = latest_frame
    return Response(data, mimetype='image/jpeg')


@app.route('/gpio', methods=['POST'])
def gpio_control():
    data = request.get_json(silent=True) or {}
    action = str(data.get('action', '')).lower()

    if action == 'on':
        test_gpio.on()
        return jsonify({'status': 'success', 'state': 'on'})
    elif action == 'off':
        test_gpio.off()
        return jsonify({'status': 'success', 'state': 'off'})

    return jsonify({'status': 'error', 'message': 'invalid action'}), 400


@app.route('/cmd', methods=['POST'])
def cmd_control():
    data = request.get_json(silent=True) or {}

    try:
        robot = int(data.get('robot'))
    except (TypeError, ValueError):
        return jsonify({'status': 'error', 'message': 'robot invalide'}), 400

    direction = str(data.get('direction', '')).upper()
    action = str(data.get('action', '')).lower()

    if robot not in robot_pins:
        return jsonify({'status': 'error', 'message': 'robot invalide'}), 400

    if direction not in robot_pins[robot]:
        return jsonify({'status': 'error', 'message': 'direction invalide'}), 400

    pin = robot_pins[robot][direction]

    if action == 'start':
        pin.on()
        return jsonify({
            'status': 'success',
            'message': f'Robot {robot} {direction} active'
        })

    elif action == 'stop':
        pin.off()
        return jsonify({
            'status': 'success',
            'message': f'Robot {robot} {direction} desactive'
        })

    return jsonify({'status': 'error', 'message': 'action invalide'}), 400


@app.route('/test')
def test():
    return jsonify({'status': 'ok'})


if __name__ == '__main__':
    t = threading.Thread(target=serial_worker, daemon=True)
    t.start()
    app.run(host='0.0.0.0', port=5000, threaded=True)