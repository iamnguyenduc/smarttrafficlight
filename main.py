import cv2
import threading
import urllib.request
import numpy as np
import time
from ultralytics import YOLO
import cvzone
import serial
import json  # Added for JSON saving

# Set up the COM port for serial communication (adjust as needed)
ser = serial.Serial('COM4', 115200, timeout=1)
time.sleep(2)

# Load the YOLOv8 model
model = YOLO("../Yolo-Weights/yolov8m.pt")
vehicle_classes = ["car", "truck", "bus", "motorcycle"]
last_send_time = 0
last_command = None  # Để theo dõi lệnh cuối cùng gửi
send_interval = 1  # Send daqta every 1 second

# VideoStream class to handle video input from a webcam or URL
class VideoStream:
    def __init__(self, src=0, name="Stream"):
        self.src = src
        self.name = name
        self.frame = None
        self.stopped = False
        self.is_url = isinstance(src, str) and src.startswith('http')

    def start(self):
        threading.Thread(target=self.update, args=(), daemon=True).start()
        return self

    def update(self):
        if self.is_url:
            self.update_from_url()
        else:
            self.update_from_webcam()

    def update_from_webcam(self):
        cap = cv2.VideoCapture(self.src)
        if not cap.isOpened():
            print(f"Warning: Cannot open camera {self.src}")
            self.stopped = True
            return

        cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1920)
        cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 1080)

        while not self.stopped:
            ret, frame = cap.read()
            if not ret:
                print(f"Warning: Cannot read frame from camera {self.src}")
                break
            self.frame = frame

        cap.release()

    def update_from_url(self):
        while not self.stopped:
            try:
                img_resp = urllib.request.urlopen(self.src)
                img_np = np.array(bytearray(img_resp.read()), dtype=np.uint8)
                self.frame = cv2.imdecode(img_np, cv2.IMREAD_COLOR)
                if self.frame is None:
                    print(f"Error: No frame received from {self.src}")
            except Exception as e:
                print(f"Error reading from URL {self.src}: {str(e)}")
                time.sleep(1)

    def read(self):
        return self.frame

    def stop(self):
        self.stopped = True

def process_region(region, vehicle_count):
    # Process detection in a specific region
    result = model(region, stream=True)

    for r in result:
        boxes = r.boxes
        for box in boxes:
            x1, y1, x2, y2 = map(int, box.xyxy[0])
            conf = box.conf[0].item()
            cls = int(box.cls[0])
            currentClass = model.names[cls]

            if currentClass in vehicle_classes and conf > 0.3:
                vehicle_count += 1
                w, h = x2 - x1, y2 - y1
                x1, y1 = max(0, x1), max(0, y1)

                # Draw bounding box and label
                cvzone.cornerRect(region, (x1, y1, w, h), l=9, rt=5)
                cvzone.putTextRect(region, f'{currentClass}', (max(0, x1), max(35, y1)), scale=2, thickness=1, offset=3)
    return vehicle_count

def process_frame(frame, rois, vehicle_count):
    if frame is None or frame.size == 0:
        print("Invalid frame!")
        return vehicle_count

    for r in rois:
        cv2.rectangle(frame, (r[0], r[1]), (r[0] + r[2], r[1] + r[3]), (0, 255, 0), 2)
        region = frame[r[1]:r[1] + r[3], r[0]:r[0] + r[2]]
        vehicle_count = process_region(region, vehicle_count)

    return vehicle_count

# Function to save vehicle counts to a JSON file
def save_counts_to_file(counts):
    with open("vehicle_counts.json", "w") as f:
        json.dump(counts, f)

# Initialize and start video streams
webcam_stream = VideoStream(1, "Webcam").start()
webcam2_stream = VideoStream(2, "Webcam2").start()

cv2.namedWindow("Webcam", cv2.WINDOW_NORMAL)
cv2.namedWindow("Webcam2", cv2.WINDOW_NORMAL)

# Define ROIs
roi_webcam_1 = (1200, 140, 900, 300)  # right lane
roi_webcam_2 = (0, 400, 700, 380)  # left lane
roi_webcam2_1 = (1200, 150, 900, 300)
roi_webcam2_2 = (0, 450, 700, 300)

while True:
    lane_4 = 0
    lane_2 = 0
    lane_3 = 0
    lane_1 = 0

    # Process frames from the first webcam
    webcam_frame = webcam_stream.read()
    if webcam_frame is not None:
        lane_4 = process_frame(webcam_frame, [roi_webcam_1], lane_4)
        lane_2 = process_frame(webcam_frame, [roi_webcam_2], lane_2)
        cvzone.putTextRect(webcam_frame, f'Vehicles ROI 1: {lane_4}', (1200, 50), scale=1, thickness=2, offset=5)
        cvzone.putTextRect(webcam_frame, f'Vehicles ROI 2: {lane_2}', (1200, 100), scale=1, thickness=2, offset=5)
        cv2.imshow("Webcam", webcam_frame)

    # Process frames from the second webcam
    webcam2_frame = webcam2_stream.read()
    if webcam2_frame is not None:
        lane_3 = process_frame(webcam2_frame, [roi_webcam2_1], lane_3)
        lane_1 = process_frame(webcam2_frame, [roi_webcam2_2], lane_1)
        cvzone.putTextRect(webcam2_frame, f'Vehicles ROI 1: {lane_3}', (30, 50), scale=1, thickness=2, offset=5)
        cvzone.putTextRect(webcam2_frame, f'Vehicles ROI 2: {lane_1}', (30, 100), scale=1, thickness=2, offset=5)
        cv2.imshow("Webcam2", webcam2_frame)

    # Send data to ESP32
    land2 = lane_4 + lane_2
    land1 = lane_3 + lane_1
    current_time = time.time()
    if current_time - last_send_time >= send_interval:
        ser.write(f"{land1},{land2}\n".encode())
        print(f"Sent to Arduino: {land1},{land2}")
        last_send_time = current_time
    # ----- ĐỌC LỆNH TỪ WEBAPP VÀ GỬI XUỐNG ESP32 -----
    try:
        with open("esp_command.json", "r") as f:
            cmd_data = json.load(f)
            command = cmd_data.get("command", "").strip()

            # Gửi nếu có lệnh mới khác lệnh cũ
            # Trong bridge_serial.py hoặc đoạn tương tự
            if command and command != last_command:
                ser.write((f"CMD:{command}\n").encode())
                print("📤 Sent COMMAND to ESP32:", command)
                last_command = command

                # Sau khi gửi xong, xóa nội dung file
                with open("esp_command.json", "w") as f:
                    json.dump({"command": ""}, f)

    except Exception as e:
        print("Error reading esp_command.json:", e)
    # Save the counts to the JSON file
    counts = {
        "lane_4": lane_4,
        "lane_2": lane_2,
        "lane_3": lane_3,
        "lane_1": lane_1
    }
    save_counts_to_file(counts)

    # Receive and print data from ESP32
    if ser.in_waiting > 0:
        try:
            response = ser.readline().decode('utf-8').rstrip()
            if response:
                print(f"Received from ESP32/Arduino: {response}")
        except UnicodeDecodeError as e:
            print(f"Error decoding: {e}")
            raw_data = ser.readline()
            print(f"Raw data: {raw_data}")

    if cv2.waitKey(1) & 0xFF == ord('q'):
        break

webcam_stream.stop()
webcam2_stream.stop()
cv2.destroyAllWindows()
