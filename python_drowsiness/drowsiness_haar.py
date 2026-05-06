import cv2
import serial
import time

# ===== SETTINGS =====
# Windows: change 'COM3' to your port (check Device Manager)
# Linux/Mac: change to '/dev/ttyUSB0' or '/dev/ttyACM0'
SERIAL_PORT      = 'COM7'
BAUD_RATE        = 115200

CLOSED_THRESHOLD = 20    # frames eyes must be closed to trigger drowsy
ALERT_COOLDOWN   = 10    # seconds between repeated alerts

# ===== SERIAL CONNECT =====
ser = None
try:
    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    time.sleep(2)
    print(f"✅ Serial connected on {SERIAL_PORT}")
except Exception as e:
    print(f"⚠️  Serial NOT connected: {e}")
    print("   Running in display-only mode (no ESP32 signal)")

# ===== LOAD HAAR CASCADES =====
face_cascade = cv2.CascadeClassifier(
    cv2.data.haarcascades + 'haarcascade_frontalface_default.xml'
)
eye_cascade = cv2.CascadeClassifier(
    cv2.data.haarcascades + 'haarcascade_eye.xml'
)

# ===== OPEN LAPTOP WEBCAM =====
cap = cv2.VideoCapture(0)
cap.set(cv2.CAP_PROP_FRAME_WIDTH,  640)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)

if not cap.isOpened():
    print("❌ Webcam not found! Check if camera is connected.")
    exit()

# ===== STATE VARIABLES =====
closed_frame_count = 0
last_alert_time    = 0
drowsy_triggered   = False

print("✅ Drowsiness detection running...")
print("   Press Q to quit")
print("-" * 40)

# ===== SEND DROWSY SIGNAL TO ESP32 =====
def send_drowsy():
    global ser
    print("🚨 DROWSY DETECTED — Sending alert to ESP32!")
    if ser and ser.is_open:
        try:
            ser.write(b'DROWSY\n')
            ser.flush()
            print("   ✅ Signal sent to ESP32 via serial")
        except Exception as e:
            print(f"   ❌ Serial error: {e}")
    else:
        print("   ⚠️  Serial not connected — signal not sent")

# ===== MAIN LOOP =====
while True:
    ret, frame = cap.read()
    if not ret:
        print("❌ Frame read error — check webcam")
        break

    frame     = cv2.resize(frame, (640, 480))
    gray      = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    curr_time = time.time()

    # Detect faces
    faces = face_cascade.detectMultiScale(
        gray, scaleFactor=1.3, minNeighbors=5, minSize=(80, 80)
    )

    eyes_open_this_frame = False

    for (fx, fy, fw, fh) in faces:
        # Draw green rectangle around face
        cv2.rectangle(frame, (fx, fy), (fx+fw, fy+fh), (0, 255, 0), 2)
        cv2.putText(frame, "Face", (fx, fy-6),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 1)

        # Only look for eyes in TOP HALF of face
        roi_gray  = gray [fy : fy + fh//2, fx : fx + fw]
        roi_color = frame[fy : fy + fh//2, fx : fx + fw]

        eyes = eye_cascade.detectMultiScale(
            roi_gray, scaleFactor=1.1, minNeighbors=5, minSize=(20, 20)
        )

        if len(eyes) >= 2:
            eyes_open_this_frame = True
            for (ex, ey, ew, eh) in eyes:
                cv2.rectangle(roi_color,
                              (ex, ey), (ex+ew, ey+eh),
                              (255, 150, 0), 2)

    # ===== DROWSINESS LOGIC =====
    if len(faces) > 0:
        if not eyes_open_this_frame:
            closed_frame_count += 1
        else:
            closed_frame_count = 0
            drowsy_triggered   = False
    else:
        closed_frame_count = max(0, closed_frame_count - 1)

    # Trigger alert if eyes closed long enough
    if closed_frame_count >= CLOSED_THRESHOLD and not drowsy_triggered:
        if curr_time - last_alert_time > ALERT_COOLDOWN:
            send_drowsy()
            last_alert_time  = curr_time
            drowsy_triggered = True

    # ===== HUD OVERLAY =====
    cv2.rectangle(frame, (0, 0), (640, 38), (20, 20, 20), -1)

    if len(faces) == 0:
        status_txt = "No Face Detected"
        status_col = (150, 150, 150)
    elif not eyes_open_this_frame:
        if closed_frame_count >= CLOSED_THRESHOLD:
            status_txt = "DROWSY! ALERT SENT"
            status_col = (0, 0, 255)
        else:
            status_txt = f"Eyes Closed [{closed_frame_count}/{CLOSED_THRESHOLD}]"
            status_col = (0, 100, 255)
    else:
        status_txt = "Eyes Open - Safe"
        status_col = (0, 200, 0)

    cv2.putText(frame, f"Driver: {status_txt}",
                (10, 26), cv2.FONT_HERSHEY_SIMPLEX, 0.65, status_col, 2)

    # Serial status top right
    if ser and ser.is_open:
        cv2.putText(frame, "Serial: ON", (490, 26),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.45, (0, 200, 100), 1)
    else:
        cv2.putText(frame, "Serial: OFF", (480, 26),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.45, (0, 100, 200), 1)

    # Eye closed progress bar
    if closed_frame_count > 0:
        bar_fill = int((closed_frame_count / CLOSED_THRESHOLD) * 200)
        bar_fill = min(bar_fill, 200)
        cv2.rectangle(frame, (10, 44), (210, 54), (50, 50, 50), -1)
        bar_col = (0, 200, 0) if bar_fill < 100 else (0, 100, 255) if bar_fill < 180 else (0, 0, 255)
        cv2.rectangle(frame, (10, 44), (10 + bar_fill, 54), bar_col, -1)
        cv2.putText(frame, "Closed Eye Timer", (215, 53),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.38, (150, 150, 255), 1)

    cv2.putText(frame, f"Faces: {len(faces)}", (10, 75),
                cv2.FONT_HERSHEY_SIMPLEX, 0.45, (180, 180, 180), 1)

    cv2.imshow("AI Driver Safety - Drowsiness Detection", frame)

    if cv2.waitKey(1) & 0xFF == ord('q'):
        print("Quit by user.")
        break

# ===== CLEANUP =====
cap.release()
cv2.destroyAllWindows()
if ser and ser.is_open:
    ser.close()
    print("Serial port closed.")
print("Program ended.")
