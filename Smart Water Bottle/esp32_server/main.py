from fastapi import FastAPI
from fastapi.responses import HTMLResponse, JSONResponse
from pydantic import BaseModel
import sqlite3
from datetime import datetime

app = FastAPI()


conn = sqlite3.connect("drink_events.db", check_same_thread=False)
cursor = conn.cursor()

cursor.execute("""
CREATE TABLE IF NOT EXISTS drink_logs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    amountDrank REAL,
    rtcTime TEXT,
    serverTimestamp TEXT,
    drinkNumber INTEGER
)
""")
conn.commit()

class DrinkEvent(BaseModel):
    amountDrank: float
    rtcTime: str
    drinkNumber: int


@app.post("/log_drink")
async def log_drink(event: DrinkEvent):
    serverTimestamp = datetime.now().isoformat()

    cursor.execute(
        "INSERT INTO drink_logs (amountDrank, rtcTime, serverTimestamp, drinkNumber) VALUES (?, ?, ?, ?)",
        (event.amountDrank, event.rtcTime, serverTimestamp, event.drinkNumber)
    )
    conn.commit()

    print("RECEIVED:", event.json())
    return {"status": "logged", "drink": event.dict()}


@app.get("/api/drinks")
async def get_drinks():
    cursor.execute("SELECT amountDrank, serverTimestamp, drinkNumber FROM drink_logs ORDER BY id ASC")
    rows = cursor.fetchall()

    data = []
    for amt, ts, num in rows:
        data.append({
            "amount": amt,
            "timestamp": ts,
            "drinkNumber": num
        })
    return JSONResponse(data)


@app.get("/dashboard", response_class=HTMLResponse)
async def dashboard():
    html = """
    <!DOCTYPE html>
    <html>
    <head>
        <title>Hydration Dashboard</title>
        <style>
            body {
                font-family: Arial, sans-serif;
                padding: 20px;
                background: #f4f4f4;
            }
            h1 { color: #0077cc; }
            #stats {
                margin-bottom: 20px;
                font-size: 18px;
                background: white;
                padding: 10px 15px;
                border-radius: 8px;
                display: inline-block;
            }
            canvas {
                background: white;
                padding: 15px;
                border-radius: 10px;
            }
        </style>
    </head>
    <body>
        <h1> Hydration Dashboard</h1>
        <div id="stats">Loading...</div>
        <br><br>
        <canvas id="chart" width="600" height="300"></canvas>

        <!-- Chart.js CDN -->
        <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>

        <script>
            async function fetchData() {
                const res = await fetch('/api/drinks');
                const data = await res.json();

                const amounts = data.map(d => d.amount);
                const times = data.map(d => new Date(d.timestamp).toLocaleTimeString());
                const total = amounts.reduce((a,b) => a+b, 0);

                document.getElementById("stats").innerHTML =
                    "Total Drank Today: <b>" + total.toFixed(1) + " Fl oz</b><br>" +
                    "Drinks Logged: <b>" + amounts.length + "</b>";

                updateChart(times, amounts);
            }

            let chart;

            function updateChart(labels, values) {
                if (chart) chart.destroy();

                const ctx = document.getElementById('chart').getContext('2d');

                chart = new Chart(ctx, {
                    type: 'bar',
                    data: {
                        labels: labels,
                        datasets: [{
                            label: 'Drink Amount (Fl oz)',
                            data: values,
                            backgroundColor: 'rgba(0, 123, 255, 0.5)',
                            borderColor: 'rgba(0, 123, 255, 1)',
                            borderWidth: 1
                        }]
                    },
                    options: {
                        scales: {
                            y: { beginAtZero: true }
                        }
                    }
                });
            }

            fetchData();
            setInterval(fetchData, 5000); // auto-refresh every 5s
        </script>
    </body>
    </html>
    """
    return html


@app.get("/")
def root():
    return {"message": "Hydration server running!"}
