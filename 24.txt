import socket, json, threading, serial, time, RPi.GPIO as GPIO, statistics
from evdev import InputDevice, ecodes

# --- CONFIGURATION ---
UDP_IP_ROBOT2 = "192.168.1.116"
UDP_PORT = 1234
SERIAL_PORT = "/dev/ttyACM0"
PATH_MANETTE = "/dev/input/event4"

WINDOW_SIZE = 7  # Taille de la fenêtre pour la stabilité du dBm

# --- ÉTAT GLOBAL ---
robot_actif = 1
states = {"RT": 0, "LT": 0, "RB": 0, "LB": 0}
current_cmd = "STOP"
ancres_filtrees = {}
ancres_history = {}
kalman_filters = {}

# --- CLASSE KALMAN (Pour la stabilité) ---
class KalmanFilter:
    def __init__(self, q=0.125, r=8.0, p=1.0):
        self.q, self.r, self.p = q, r, p
        self.x = 0.0
        self.initialized = False

    def update(self, measurement):
        if not self.initialized:
            self.x = float(measurement)
            self.initialized = True
        self.p = self.p + self.q
        k = self.p / (self.p + self.r)
        self.x = self.x + k * (measurement - self.x)
        self.p = (1 - k) * self.p
        return self.x

# --- CONFIG GPIO (ROBOT 1 PWM) ---
G_AV, G_RE, D_AV, D_RE = 17, 27, 22, 23
GPIO.setmode(GPIO.BCM)
GPIO.setwarnings(False)
pins = [G_AV, G_RE, D_AV, D_RE]
pwm_pins = {}
for p in pins:
    GPIO.setup(p, GPIO.OUT)
    pwm_pins[p] = GPIO.PWM(p, 1000)
    pwm_pins[p].start(0)

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

# --- LOGIQUE MOTEURS ---
def send_udp(cmd, mot_g, mot_d, v):
    try:
        msg = json.dumps({"cmd": cmd, "v": v, "m_g": mot_g, "m_d": mot_d})
        sock.sendto(msg.encode(), (UDP_IP_ROBOT2, UDP_PORT))
    except: pass

def update_all():
    global robot_actif, current_cmd
    mot_g, mot_d = 0, 0
    if states["LT"] > 50: mot_g = -1 if states["LB"] == 1 else 1
    if states["RT"] > 50: mot_d = -1 if states["RB"] == 1 else 1

    cmd_label = "STOP"
    if mot_g == 1 and mot_d == 1: cmd_label = "AVANCE"
    elif mot_g == -1 and mot_d == -1: cmd_label = "RECULE"
    elif mot_g == 1 and mot_d == 0: cmd_label = "MOT_GAUCHE_AV"
    elif mot_g == 0 and mot_d == 1: cmd_label = "MOT_DROIT_AV"
    elif mot_g == -1 and mot_d == 0: cmd_label = "MOT_GAUCHE_REC"
    elif mot_g == 0 and mot_d == -1: cmd_label = "MOT_DROIT_REC"
    elif mot_g == 1 and mot_d == -1: cmd_label = "PIVOT_DROITE"
    elif mot_g == -1 and mot_d == 1: cmd_label = "PIVOT_GAUCHE"
    current_cmd = cmd_label

    v_max_raw = max(states["LT"], states["RT"])
    duty_g = (states["LT"] / 1023.0) * 100
    duty_d = (states["RT"] / 1023.0) * 100

    if robot_actif == 1:
        if mot_g == 1: pwm_pins[G_AV].ChangeDutyCycle(duty_g); pwm_pins[G_RE].ChangeDutyCycle(0)
        elif mot_g == -1: pwm_pins[G_AV].ChangeDutyCycle(0); pwm_pins[G_RE].ChangeDutyCycle(duty_g)
        else: pwm_pins[G_AV].ChangeDutyCycle(0); pwm_pins[G_RE].ChangeDutyCycle(0)

        if mot_d == 1: pwm_pins[D_AV].ChangeDutyCycle(duty_d); pwm_pins[D_RE].ChangeDutyCycle(0)
        elif mot_d == -1: pwm_pins[D_AV].ChangeDutyCycle(0); pwm_pins[D_RE].ChangeDutyCycle(duty_d)
        else: pwm_pins[D_AV].ChangeDutyCycle(0); pwm_pins[D_RE].ChangeDutyCycle(0)
        send_udp("STOP", 0, 0, 0)
    else:
        for p in pins: pwm_pins[p].ChangeDutyCycle(0)
        send_udp(cmd_label, mot_g, mot_d, v_max_raw)

# --- LECTURE SÉRIE ---
def serial_handshake():
    global ancres_filtrees, ancres_history
    try:
        ser = serial.Serial(SERIAL_PORT, 115200, timeout=0.1)
        time.sleep(2)
        ser.write(b"CONNECT\n")
        while True:
            if ser.in_waiting > 0:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if "Anchor" in line and "," in line:
                    try:
                        name, rssi_raw = line.split(",")
                        rssi_val = float(rssi_raw)
                        if name not in kalman_filters: kalman_filters[name] = KalmanFilter()
                        val_kalman = kalman_filters[name].update(rssi_val)
                        if name not in ancres_history: ancres_history[name] = []
                        ancres_history[name].append(val_kalman)
                        if len(ancres_history[name]) > WINDOW_SIZE: ancres_history[name].pop(0)
                        ancres_filtrees[name] = statistics.median(ancres_history[name])
                    except: continue
    except: pass

# --- AFFICHAGE (UNIQUEMENT dBm) ---

def display_thread():
    """ Rafraîchit l'écran proprement sans spammer """
    while True:
        # 1. On prépare les données des ancres (dBm uniquement)
        display_gps = " | ".join([f"{n}: {int(v)}dBm" for n, v in sorted(ancres_filtrees.items())])

        # 2. Construction de la ligne
        # \r = Retour au début de la ligne
        # \033[K = Code spécial (ANSI) qui efface tout ce qui reste sur la ligne en cours
        output = f"\r\033[K[ROBOT {robot_actif}] {current_cmd:<15} | V:{max(states['RT'], states['LT']):>4} | {display_gps}"

        # 3. On imprime d'un coup
        print(output, end="", flush=True)

        # Pause de 0.1s pour ne pas saturer le processeur
        time.sleep(0.1)


def controller_loop(gamepad):
    for event in gamepad.read_loop():
        changed = False
        if event.type == ecodes.EV_KEY:
            if event.code == 307 and event.value == 1:
                global robot_actif
                robot_actif = 2 if robot_actif == 1 else 1
                changed = True
            if event.code == 311: states["RB"] = event.value; changed = True
            if event.code == 310: states["LB"] = event.value; changed = True
        elif event.type == ecodes.EV_ABS:
            if event.code == 5: states["RT"] = event.value; changed = True
            if event.code == 2: states["LT"] = event.value; changed = True
        if changed: update_all()

# --- RUN ---
try:
    gamepad = InputDevice(PATH_MANETTE)
    threading.Thread(target=serial_handshake, daemon=True).start()
    threading.Thread(target=display_thread, daemon=True).start()
    controller_loop(gamepad)
finally:
    for p in pins: pwm_pins[p].stop()
    GPIO.cleanup()