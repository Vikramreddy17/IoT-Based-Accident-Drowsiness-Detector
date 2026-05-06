import cv2
import mediapipe as mp
import numpy as np
import math
import time
import serial

# ================== FUNCTIONS ==================
def eye_aspect_ratio(eye):
    A = math.dist(eye[1], eye[5])
    B = math.dist(eye[2], eye[4])
    C = math.dist(eye[0], eye[3])
    return (A + B) / (2.0 * C)

def mouth_aspect_ratio(mouth):
    A = math.dist(mouth[1], mouth[5])
    B = math.dist(mouth[2], mouth[4])
    C = math.dist(mouth[0], mouth[3])
    return (A + B) / (2.0 * C)

def separator(frame, x1, x2, y):
    cv2.line(frame, (x1, y), (x2, y), (90, 90, 90), 1)

# ================== MEDIAPIPE ==================
mp_face_mesh = mp.solutions.face_mesh
face_mesh = mp_face_mesh.FaceMesh(
    max_num_faces=1,
    refine_landmarks=True,
    min_detection_confidence=0.5,
    min_tracking_confidence=0.5
)

cap = cv2.VideoCapture(0)

LEFT_EYE_IDX = [33, 160, 158, 133, 153, 144]
LEFT_IRIS_IDX = [468, 469, 470, 471]
MOUTH_IDX = [61, 81, 13, 311, 291, 402]

# ================== PARAMETERS ==================
EYE_CLOSED_THRESH = 0.20
YAWN_THRESH = 0.6
MAX_YAWNS = 3
YAWN_FRAMES_REQUIRED = 15

yawn_frames = 0
yawn_count = 0
pitch = yaw = 0
baseline_ear = None
recalib_msg_time = 0

# ================== GSM SERIAL ==================
try:
    arduino = serial.Serial('COM8', 9600, timeout=1)
    time.sleep(2)
    print("SIM900A Connected on COM8")
except:
    arduino = None
    print("SIM900A NOT connected")

last_sms_time = 0
SMS_COOLDOWN = 60  # seconds

# ================== UI CONSTANTS ==================
FONT_TITLE = cv2.FONT_HERSHEY_COMPLEX
FONT_LABEL = cv2.FONT_HERSHEY_SIMPLEX
FONT_VALUE = cv2.FONT_HERSHEY_COMPLEX_SMALL

WHITE = (220,220,220)
GREEN = (0,255,0)
YELLOW = (0,215,255)
RED = (0,0,255)
GRAY = (150,150,150)
DARK = (45,45,45)
BAR_BG = (80,80,80)

# ================== MAIN LOOP ==================
while True:
    ret, frame = cap.read()
    if not ret:
        break

    frame = cv2.flip(frame, 1)
    h, w, _ = frame.shape

    rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    results = face_mesh.process(rgb)

    ear = mar = 0
    gaze_h = gaze_v = 0
    x1 = y1 = x2 = y2 = 0

    if results.multi_face_landmarks:
        for face_landmarks in results.multi_face_landmarks:

            xs = [int(lm.x * w) for lm in face_landmarks.landmark]
            ys = [int(lm.y * h) for lm in face_landmarks.landmark]
            x1, x2 = min(xs), max(xs)
            y1, y2 = min(ys), max(ys)

            cv2.rectangle(frame, (x1, y1), (x2, y2), GREEN, 2)
            cv2.putText(frame, "MONITORED", (x1, y1-10),
                        FONT_LABEL, 0.55, GREEN, 2)

            left_eye = [(int(face_landmarks.landmark[i].x * w),
                         int(face_landmarks.landmark[i].y * h)) for i in LEFT_EYE_IDX]
            ear = eye_aspect_ratio(left_eye)
            if baseline_ear is None:
                baseline_ear = ear

            iris = [(face_landmarks.landmark[i].x * w,
                     face_landmarks.landmark[i].y * h) for i in LEFT_IRIS_IDX]
            iris_x, iris_y = np.mean(iris, axis=0)
            eye_x, eye_y = np.mean(left_eye, axis=0)
            gaze_h = int(iris_x - eye_x)
            gaze_v = int(iris_y - eye_y)

            mouth = [(int(face_landmarks.landmark[i].x * w),
                      int(face_landmarks.landmark[i].y * h)) for i in MOUTH_IDX]
            mar = mouth_aspect_ratio(mouth)

            if mar > YAWN_THRESH:
                yawn_frames += 1
            else:
                if yawn_frames >= YAWN_FRAMES_REQUIRED:
                    yawn_count += 1
                yawn_frames = 0
            yawn_count = min(yawn_count, MAX_YAWNS)

            try:
                image_points = np.array([
                    (face_landmarks.landmark[1].x * w, face_landmarks.landmark[1].y * h),
                    (face_landmarks.landmark[152].x * w, face_landmarks.landmark[152].y * h),
                    (face_landmarks.landmark[33].x * w, face_landmarks.landmark[33].y * h),
                    (face_landmarks.landmark[263].x * w, face_landmarks.landmark[263].y * h),
                    (face_landmarks.landmark[61].x * w, face_landmarks.landmark[61].y * h),
                    (face_landmarks.landmark[291].x * w, face_landmarks.landmark[291].y * h)
                ], dtype="double")

                model_points = np.array([
                    (0,0,0),(0,-330,-65),(-225,170,-135),
                    (225,170,-135),(-150,-150,-125),(150,-150,-125)
                ])

                cam_matrix = np.array([[w,0,w/2],[0,w,h/2],[0,0,1]])
                success, rot_vec, _ = cv2.solvePnP(
                    model_points, image_points, cam_matrix, np.zeros((4,1))
                )
                if success:
                    rmat, _ = cv2.Rodrigues(rot_vec)
                    angles, _, _, _, _, _ = cv2.RQDecomp3x3(rmat)
                    pitch = int(angles[0] * 360)
                    yaw = int(angles[1] * 360)
            except:
                pass

    face_gray = gray[y1:y2, x1:x2]
    brightness = int(np.mean(face_gray)) if face_gray.size > 0 else int(np.mean(gray))
    mode = "Day Mode" if brightness > 95 else "Night Mode"

    drowsy = int(
        (30 if ear < EYE_CLOSED_THRESH else 0) +
        (yawn_count / MAX_YAWNS) * 40 +
        (20 if abs(pitch) > 10 else 0)
    )
    drowsy = min(drowsy, 100)

    # ================== GSM ALERT ==================
    if arduino and drowsy >= 40:
        if time.time() - last_sms_time > SMS_COOLDOWN:
            try:
                arduino.write(b"ALERT\n")
                last_sms_time = time.time()
                print("SMS ALERT SENT")
            except:
                pass

    status = "ALERT" if drowsy >= 40 else "SAFE"
    status_color = RED if status == "ALERT" else GREEN

    # ================== UI ==================
    overlay = frame.copy()
    cv2.rectangle(overlay, (0,0), (w,45), (30,30,30), -1)
    cv2.rectangle(overlay, (20,75), (260,275), DARK, -1)
    cv2.rectangle(overlay, (w-260,75), (w-20,385), DARK, -1)
    frame = cv2.addWeighted(overlay, 0.85, frame, 0.15, 0)

    cv2.putText(frame, "DRIVER DROWSINESS DETECTION",
                (20,32), FONT_TITLE, 0.75, WHITE, 2)
    cv2.circle(frame, (w-80,22), 7, GREEN, -1)
    cv2.putText(frame, "ACTIVE", (w-65,28),
                FONT_LABEL, 0.6, GREEN, 2)

    # -------- METRICS --------
    xL, yL, line = 30, 105, 25
    cv2.putText(frame, "METRICS", (xL, yL), FONT_LABEL, 0.55, GRAY, 1)
    cv2.putText(frame, "Eye Openness", (xL, yL+line), FONT_LABEL, 0.5, WHITE, 1)
    cv2.putText(frame, f"{ear:.2f}", (xL, yL+line*2), FONT_VALUE, 0.75, YELLOW, 1)
    cv2.putText(frame, "Yawn Detection", (xL, yL+line*3), FONT_LABEL, 0.5, WHITE, 1)
    cv2.putText(frame, f"{mar:.2f}", (xL, yL+line*4), FONT_VALUE, 0.75, YELLOW, 1)
    cv2.putText(frame, f"Yawns: {yawn_count}/3", (xL, yL+line*5), FONT_LABEL, 0.5, GRAY, 1)

    bar_y = yL + line*6
    cv2.rectangle(frame, (xL, bar_y), (xL+200, bar_y+7), BAR_BG, -1)
    cv2.rectangle(frame, (xL, bar_y),
                  (xL + int((yawn_count/3)*200), bar_y+7), GREEN, -1)

    # -------- SYSTEM INFO --------
    xR, yR = w-245, 105
    cv2.putText(frame, "SYSTEM INFO", (xR, yR), FONT_LABEL, 0.55, GRAY, 1)
    cv2.putText(frame, "Drowsiness Level", (xR, yR+line), FONT_LABEL, 0.5, WHITE, 1)
    cv2.putText(frame, f"{drowsy}%", (xR, yR+line*2), FONT_VALUE, 0.75, GREEN, 1)
    cv2.putText(frame, status, (xR+75, yR+line*2), FONT_LABEL, 0.5, status_color, 1)

    bar2_y = yR + line*3
    cv2.rectangle(frame, (xR, bar2_y), (xR+200, bar2_y+7), BAR_BG, -1)
    cv2.rectangle(frame, (xR, bar2_y),
                  (xR + int((drowsy/100)*200), bar2_y+7), GREEN, -1)

    separator(frame, xR, w-30, bar2_y+15)

    cv2.putText(frame, f"Mode: {mode}", (xR, bar2_y+line*5),
                FONT_LABEL, 0.5, WHITE, 1)
    cv2.putText(frame, f"Brightness: {brightness}", (xR, bar2_y+line*6),
                FONT_LABEL, 0.5, GREEN, 1)
    cv2.putText(frame, "Faces: 1", (xR, bar2_y+line*7),
                FONT_LABEL, 0.5, GREEN, 1)

    key = cv2.waitKey(1) & 0xFF
    if key == 27:
        break
    elif key == ord('c'):
        baseline_ear = ear
        yawn_count = 0
        yawn_frames = 0
        recalib_msg_time = time.time()

    cv2.imshow("Driver Drowsiness Detection", frame)

cap.release()
cv2.destroyAllWindows()
