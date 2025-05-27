from flask import Flask, jsonify, render_template, request
import json
import os
#abc
app = Flask(__name__)

# Route: Giao diện chính
@app.route('/')
def index():
    return render_template('index.html')

# Route: Lấy dữ liệu số lượng xe
@app.route('/vehicle_counts')
def vehicle_counts():
    try:
        with open("vehicle_counts.json", "r") as f:
            counts = json.load(f)
    except (FileNotFoundError, json.JSONDecodeError):
        counts = {
            "lane_4": 0,
            "lane_2": 0,
            "lane_3": 0,
            "lane_1": 0
        }
    return jsonify(counts)

# Route: Nhận lệnh từ WebApp và lưu vào esp_command.json
@app.route('/send_command', methods=['POST'])
def send_command():
    data = request.get_json()
    if "command" in data:
        try:
            with open("esp_command.json", "w") as f:
                json.dump(data, f)
            return jsonify({"status": "OK"})
        except Exception as e:
            return jsonify({"status": "error", "message": f"Lỗi ghi file: {str(e)}"})
    else:
        return jsonify({"status": "error", "message": "Không có lệnh nào được gửi"})

# Chạy Flask server
if __name__ == '__main__':
    app.run(debug=True)
