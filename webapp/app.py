"""
PIR Motion Detection Dashboard
Port: 5001
Real-time motion detection alerts from LoRaWAN PIR nodes
"""

from flask import Flask, render_template_string, request, redirect
import paho.mqtt.client as mqtt
import json
import threading
from datetime import datetime

app = Flask(__name__)

pir_events = []
motion_status = {"status": "CLEAR", "last_seen": "Never", "location": "Unknown"}
alert_active = True

# Application ID — update with your ChirpStack application ID !
PIR_APP_ID = "bad015a4-81af-4792-8557-a64655bf08a1"

# Device location mapping — update with your locations !
DEVICE_LOCATIONS = {
    "PIR-node-1": "Main Gate JKUAT",
    "PIR-node-2": "Back Gate",
    "PIR-node-3": "Field A",
}

HTML = """
<!DOCTYPE html>
<html>
<head>
    <title>🚨 PIR Motion Dashboard</title>
    <meta http-equiv="refresh" content="5">
    <style>
        body { font-family: Arial; margin: 20px; background: #1a1a2e; color: white; }
        h1 { color: #e94560; }
        .status-box { padding: 30px; border-radius: 10px; text-align: center; font-size: 40px; font-weight: bold; margin: 20px 0; }
        .detected { background: #e94560; animation: blink 0.5s infinite; }
        .clear { background: #0f3460; }
        @keyframes blink { 0%{opacity:1;} 50%{opacity:0.3;} 100%{opacity:1;} }
        .btn-stop { background: #ff6b6b; color: white; border: none; padding: 15px 30px; font-size: 18px; border-radius: 5px; cursor: pointer; margin: 10px; }
        .btn-enable { background: #4ecca3; color: black; border: none; padding: 15px 30px; font-size: 18px; border-radius: 5px; cursor: pointer; margin: 10px; }
        table { width: 100%; border-collapse: collapse; background: #16213e; }
        th { background: #0f3460; color: white; padding: 12px; text-align: left; }
        td { padding: 10px; border-bottom: 1px solid #0f3460; }
        .detected-row { color: #e94560; font-weight: bold; }
        .clear-row { color: #4ecca3; }
        .location { font-size: 20px; color: #4ecca3; margin: 10px 0; }
    </style>
    <script>
        function playAlarm() {
            var ctx = new (window.AudioContext || window.webkitAudioContext)();
            for (var i = 0; i < 3; i++) {
                (function(i) {
                    setTimeout(function() {
                        var osc = ctx.createOscillator();
                        var gain = ctx.createGain();
                        osc.connect(gain);
                        gain.connect(ctx.destination);
                        osc.type = 'square';
                        osc.frequency.value = 880;
                        gain.gain.value = 0.5;
                        osc.start(ctx.currentTime);
                        osc.stop(ctx.currentTime + 0.3);
                    }, i * 400);
                })(i);
            }
        }
        window.onload = function() {
            var status = "{{ motion_status.status }}";
            var alert_on = "{{ alert_active }}";
            if (status === "DETECTED" && alert_on === "True") {
                playAlarm();
            }
        }
    </script>
</head>
<body>
    <h1>🚨 PIR Motion Detection Dashboard</h1>
    <p>Auto-refreshes every 5 seconds</p>

    <div class="status-box {{ 'detected' if motion_status.status == 'DETECTED' else 'clear' }}">
        {% if motion_status.status == "DETECTED" %}
            🚨 MOTION DETECTED !
        {% else %}
            ✅ CLEAR — No Motion
        {% endif %}
    </div>

    <p class="location">📍 Location : {{ motion_status.location }}</p>
    <p>Last seen : {{ motion_status.last_seen }}</p>

    <form method="POST" action="/stop_alert" style="display:inline">
        <button class="btn-stop" type="submit">🔕 Stop Alert</button>
    </form>
    <form method="POST" action="/enable_alert" style="display:inline">
        <button class="btn-enable" type="submit">🔔 Enable Alert</button>
    </form>
    <p>Alert sound : {{ "🔔 ON" if alert_active else "🔕 OFF" }}</p>

    <h2>Recent Events</h2>
    <table>
        <tr>
            <th>Time</th>
            <th>Device</th>
            <th>Location</th>
            <th>Status</th>
            <th>RSSI</th>
        </tr>
        {% for event in events %}
        <tr class="{{ 'detected-row' if event.motion == 'DETECTED' else 'clear-row' }}">
            <td>{{ event.time }}</td>
            <td>{{ event.device }}</td>
            <td>{{ event.location }}</td>
            <td>{{ event.motion }}</td>
            <td>{{ event.rssi }} dBm</td>
        </tr>
        {% endfor %}
    </table>
</body>
</html>
"""

def on_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload)
        dev_name = payload.get("deviceInfo", {}).get("deviceName", "unknown")
        location = DEVICE_LOCATIONS.get(dev_name, "Unknown location")
        obj = payload.get("object", {})
        motion = obj.get("motion", "CLEAR")
        rssi = payload.get("rxInfo", [{}])[0].get("rssi", "N/A")
        now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

        motion_status["status"] = motion
        motion_status["last_seen"] = now
        motion_status["location"] = location

        pir_events.insert(0, {
            "time": now,
            "device": dev_name,
            "location": location,
            "motion": motion,
            "rssi": rssi
        })

        if len(pir_events) > 20:
            pir_events.pop()

    except Exception as e:
        print(f"Error: {e}")

def start_mqtt():
    client = mqtt.Client()
    client.on_message = on_message
    client.connect("localhost", 1883)
    client.subscribe(f"application/{PIR_APP_ID}/device/+/event/up")
    client.loop_forever()

@app.route('/')
def index():
    return render_template_string(HTML,
        motion_status=motion_status,
        events=pir_events,
        alert_active=alert_active)

@app.route('/stop_alert', methods=['POST'])
def stop_alert():
    global alert_active
    alert_active = False
    motion_status["status"] = "CLEAR"
    return redirect('/')

@app.route('/enable_alert', methods=['POST'])
def enable_alert():
    global alert_active
    alert_active = True
    return redirect('/')

if __name__ == '__main__':
    t = threading.Thread(target=start_mqtt)
    t.daemon = True
    t.start()
    app.run(host='0.0.0.0', port=5001, debug=False)
