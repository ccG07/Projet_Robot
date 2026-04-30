import socket
import json
import threading
import serial
import time
from evdev import InputDevice, ecodes, list_devices

# --- CONFIGURATION ---
ROBOT_1_IP = "127.0.0.1"    # Pi_Robot_1 (Local)
ROBOT_2_IP = "192.168.4.1"  # Peak_Robot_2 (Access Point)
UDP_PORT = 1234
SERIAL_PORT = "/dev/ttyGPS"

# Calibration ajustée selon tes dernières valeurs
ANCHOR_CALIBRATION = {
    "Anchor_1": -80,
    "Anchor_2": -76,
    "Anchor_3": -82,
    "Anchor_4": -80,
    "DEFAULT": -67
}
N_FACTOR = 2.4
WINDOW_SIZE = 5

# --- VARIABLES D'ÉTAT ---
current_target = ROBOT_1_IP
target_name = "PI_ROBOT_1"
control_mode = "SINGLE"
rssi_filtered = {}
rssi_history = {}
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

# Stockage des valeurs analogiques pour calcul en temps réel
# RT/LT : 0 à 1023 | Stick X : 0 à 65535 (Milieu ~32768)
analog_states = {"RT": 0, "LT": 0, "X": 32768}
DEADZONE_TRIGGER = 50
DEADZONE_STICK = 8000

#DEADZONE_TRIGGER = 35000

# --- LOGIQUE FILTRAGE (KALMAN) ---
kalman_filters = {}

class KalmanFilter:
    def __init__(self, q=0.125, r=8.0, p=1.0):
        self.q = q
        self.r = r
        self.p = p
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

def calculate_average(values):
    if not values: return 0
    return sum(values) / len(values)

def map_range(x, in_min, in_max, out_min, out_max):
    """Convertit une plage de valeur (ex: 0-1023 vers 0-255)"""
    return int((x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min)

# --- LOGIQUE GPS ---

def calculate_dist(rssi, anchor_name):
    p0 = ANCHOR_CALIBRATION.get(anchor_name, ANCHOR_CALIBRATION["DEFAULT"])
    return round(10 ** ((p0 - rssi) / (10 * N_FACTOR)), 2)

def check_safety(intended_cmd):
    rssi_actuel = rssi_filtered.get("Anchor_3", -100)
    dist_actuelle = calculate_dist(rssi_actuel, "Anchor_3")
    if dist_actuelle < 0.5 and intended_cmd == "AVANCE":
        print(f"\r[SÉCURITÉ] BLOQUÉ ! ({dist_actuelle}m)      ", end="")
        return "STOP"
    return intended_cmd

def gps_listener():
    global rssi_filtered, rssi_history
    try:
        ser = serial.Serial(SERIAL_PORT, 115200, timeout=1)
        time.sleep(2)
        # --- ENVOI DU HANDSHAKE ---#
        ser.write(b"CONNECT\n")
        print(f"[*] Tentative de poignée de main sur {SERIAL_PORT}...")

        while True:
            if ser.in_waiting > 0:
                line = ser.readline().decode('utf-8', errors='ignore').strip()

                # Vérification de la réponse de l'ESP32
                if "ACK_PI" in line:
                    print("\n[CONFIRMATION] ESP32 connecté et prêt ! ")
                    continue

                if "," in line:
                    try:
                        parts = line.split(",")
                        if len(parts) != 2: continue
                        name, rssi_raw = parts
                        if not name.startswith("Anchor_"): continue
                        rssi_val = int(rssi_raw)

                        if name not in kalman_filters:
                            kalman_filters[name] = KalmanFilter()
                        val_kalman = kalman_filters[name].update(rssi_val)

                        if name not in rssi_history:
                            rssi_history[name] = []
                        rssi_history[name].append(val_kalman)
                        if len(rssi_history[name]) > WINDOW_SIZE:
                            rssi_history[name].pop(0)

                        rssi_filtered[name] = calculate_average(rssi_history[name])
                    except ValueError:
                        continue

            if rssi_filtered:
                display_str = f"\r[MODE: {control_mode} | CIBLE: {target_name}] | "
                for ancre, rssi in rssi_filtered.items():
                    dist = calculate_dist(rssi, ancre)
                    display_str += f"{ancre}: {dist}m ({int(rssi)}dBm) | "
                print(display_str + "          ", end="")
    except Exception as e:
        print(f"\n[!] Erreur Fatale GPS: {e}")

# --- LOGIQUE MOTEURS ---

def send_cmd(cmd, ip_target, v=255):
    msg = json.dumps({"cmd": cmd, "v": v})
    try:
        sock.sendto(msg.encode(), (ip_target, UDP_PORT))
    except:
        pass


# --- NOUVELLES PLAGES POUR MANETTE XBOX BLUETOOTH ---
# On passe sur une plage 0-65535 car c'est ce que ta manette envoie

# --- CONFIGURATION ANALOGIQUE ---
# On part sur une base 0-1023. Si ta manette est en 65k, le map_range s'en occupe.

def map_range(x, in_min, in_max, out_min, out_max):
    """Convertit proprement et limite les valeurs entre 0 et 255"""
    if in_min == in_max: return out_min
    val = int((x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min)
    return max(0, min(255, val))

def controller_loop(gamepad):
    global current_target, target_name, control_mode
    print(f"\n[*] Configuration validée ! RT: Avancer | LT: Reculer")

    for event in gamepad.read_loop():
        # 1. BOUTONS (EV_KEY)
        if event.type == ecodes.EV_KEY:
            if event.value == 1: # À l'appui
                if event.code == ecodes.BTN_START:
                    control_mode = "SYNC" if control_mode == "SINGLE" else "SINGLE"
                    print(f"\n[MODE] {control_mode}")
                elif control_mode == "SINGLE":
                    if event.code == ecodes.BTN_TL: # LB
                        current_target, target_name = ROBOT_1_IP, "PI_ROBOT_1"
                    elif event.code == ecodes.BTN_TR: # RB
                        current_target, target_name = ROBOT_2_IP, "PEAK_ROBOT_2"
            continue # Ignore le reste des touches (RS_B, LS_B, etc.)

        # 2. ANALOGIQUE (EV_ABS)
        elif event.type == ecodes.EV_ABS:
            # On ignore le stick droit (codes 2 et 5) qui créait des interférences
            if event.code in [2, 5]:
                continue

            # MAPPING DES AXES (Adapté à tes logs)
            if event.code == 0:  # Stick Gauche Horizontal
                analog_states["X"] = event.value
            elif event.code == 10: # Gâchette Droite
                analog_states["RT"] = event.value
            elif event.code == 9:  # Gâchette Gauche
                analog_states["LT"] = event.value
            else:
                continue

            cmd = "STOP"
            v = 0

            # Calcul des vitesses (0-1023 -> 0-255)
            val_rt = map_range(analog_states["RT"], 0, 1023, 0, 255)
            val_lt = map_range(analog_states["LT"], 0, 1023, 0, 255)

            # --- LOGIQUE DE MOUVEMENT (RT=AVANCE / LT=RECULE) ---

            # 1. Priorité Direction (Stick Gauche)
            if abs(analog_states["X"] - 32768) > 8000:
                if analog_states["X"] < 32768 - 8000: cmd = "GAUCHE"
                else: cmd = "DROITE"
                # Vitesse : celle de la gâchette active, sinon 160
                v = max(val_rt, val_lt)
                if v < 40: v = 160

            # 2. Avancer (RT)
            elif val_rt > 40:
                cmd = check_safety("AVANCE")
                v = val_rt

            # 3. Reculer (LT)
            elif val_lt > 40:
                cmd = "RECULER"
                v = val_lt

            # 4. Envoi de la commande
            if cmd == "STOP":
                send_cmd("STOP", current_target, 0)
                if control_mode == "SYNC":
                    other = ROBOT_2_IP if current_target == ROBOT_1_IP else ROBOT_1_IP
                    send_cmd("STOP", other, 0)
            else:
                send_cmd(cmd, current_target, v)
                if control_mode == "SYNC":
                    other = ROBOT_2_IP if current_target == ROBOT_1_IP else ROBOT_1_IP
                    send_cmd(cmd, other, v)
# --- LANCEMENT ---#
devices = [InputDevice(path) for path in list_devices()]
gamepad = next((d for d in devices if "Xbox" in d.name), None)

if gamepad:
    threading.Thread(target=gps_listener, daemon=True).start()
    controller_loop(gamepad)
else:
    print("ERREUR: Manette non trouvée.")
