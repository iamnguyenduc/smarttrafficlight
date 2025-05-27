// Lấy dữ liệu số lượng xe
function fetchVehicleCounts() {
    $.getJSON('/vehicle_counts', function(data) {
        $('#count1').text(data.lane_1);
        $('#count2').text(data.lane_2);
        $('#count3').text(data.lane_3);
        $('#count4').text(data.lane_4);
    });
}

// Cập nhật đồng hồ theo múi giờ GMT+7
function updateTime() {
    const now = new Date();
    const options = {
        timeZone: 'Asia/Bangkok',
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit',
        hour12: false
    };
    const formattedTime = new Intl.DateTimeFormat('en-GB', options).format(now);
    document.getElementById('time_display').innerText = `Time (GMT+7): ${formattedTime}`;
}

// Gửi lệnh điều khiển đến Flask → ESP32
function sendCommand(cmd) {
    $.ajax({
        url: '/send_command',
        type: 'POST',
        contentType: 'application/json',
        data: JSON.stringify({ command: cmd }),
        success: function(response) {
            console.log(response.message);  // ✅ log nhẹ
        },
        error: function(xhr) {
            console.error("Lỗi gửi lệnh:", xhr.responseText);
        }
    });
}

// Lặp lại theo chu kỳ
setInterval(updateTime, 1000);
setInterval(fetchVehicleCounts, 1000);
