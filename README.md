# GGA hardware handler

This is a daemon I wrote to for a handheld gaming system I'm making based on a
Raspberry Pi 5. It reads from an INA219 battery gauge and an
[Adafruit Arcade Bonnet](https://www.adafruit.com/product/3422), keeps track of
battery level, and simulates keyboard presses.

Battery percentage is output to `/run/bat/capacity`, and the charging status of
either "Charging" or "Discharging" is output to `/run/bat/status`. Battery
percentage is estimated base on the battery voltage when the daemon starts, and
then updated based on integrating the current usage over time.

You can change which simulated keys are pressed by editing the `KEYCODES`
array in `main.c`. 

To build and enable on system boot:
```
sudo make install
sudo systemctl enable GGA.service
```
