import RPi.GPIO as GPIO
import sys
import tty
import termios
import time

SERVO_PIN = 18
FREQ      = 50
DUTY_MIN  = 2.5
DUTY_MID  = 7.5
DUTY_MAX  = 12.5
STEP      = 0.1

def duty_to_us(duty):
    return duty / 100 * (1_000_000 / FREQ)

def send_pulse(pwm, duty, cycles=30):
    """Send a burst of pulses then cut signal"""
    pwm.ChangeDutyCycle(duty)
    time.sleep(cycles / FREQ)   # 30 cycles @ 50Hz = 0.6s
    pwm.ChangeDutyCycle(0)      # cut signal, servo holds position

def main():
    GPIO.setmode(GPIO.BCM)
    GPIO.setup(SERVO_PIN, GPIO.OUT)

    pwm = GPIO.PWM(SERVO_PIN, FREQ)
    duty = DUTY_MID
    pwm.start(0)

    # Move to start position
    send_pulse(pwm, duty)

    print("MG90S Neutral Finder (pulse-and-cut)")
    print("─────────────────────────────")
    print("  w / s  →  increase / decrease")
    print("  r      →  reset to 7.5%")
    print("  q      →  quit and save result")
    print("─────────────────────────────")
    print(f"Starting at {duty}% = {duty_to_us(duty):.0f}µs\n")

    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)

    try:
        tty.setraw(fd)
        while True:
            ch = sys.stdin.read(1)
            moved = True

            if ch == 'w':
                duty = min(round(duty + STEP, 2), DUTY_MAX)
            elif ch == 's':
                duty = max(round(duty - STEP, 2), DUTY_MIN)
            elif ch == 'r':
                duty = DUTY_MID
            elif ch == 'q':
                break
            else:
                moved = False

            if moved:
                send_pulse(pwm, duty)
                sys.stdout.write(f"\r  Duty: {duty:.2f}%  |  Pulse: {duty_to_us(duty):.0f}µs   ")
                sys.stdout.flush()

    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old)
        pwm.stop()
        GPIO.cleanup()
        print(f"\n\nNeutral duty cycle : {duty:.2f}%")
        print(f"Neutral pulse width: {duty_to_us(duty):.0f}µs")

if __name__ == "__main__":
    main()