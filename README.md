# iot-water-motor
An automated solution using IOT based ESP8266 (ESP01) WiFi Chip to Turn ON/OFF Water Motor.

1. The WiFi chip triggers an API call to Turn ON the WiFi-based Smart plug as per its schedule.
2. As the Water tank starts filling up and the water tank reaches its full capacity, the float-based switch automatically Turns ON due to water level. This switch ON of the float switch is detected by ESP8266 chip (called external interrupt) and the Chip immediately triggers an API call to Turn OFF the WiFi Smart Plug to Turn OFF water motor.

For more details, navigate to https://www.anirudhsethi.in/blog/tech/the-water-problem/

www.anirudhsethi.in
