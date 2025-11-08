# ESP32-Nightscout-TFT-Graph
![image](https://github.com/Arakon/ESP32-Nightscout-TFT-Graph/blob/main/graph1.jpg)


ESP32-C3 based low cost desktop display for Nightscout Glucose

Display supported:

1.69" 240x280:
https://www.aliexpress.com/item/1005008136222795.html

ESP32-C3:
https://www.aliexpress.com/item/1005006599448997.html

[Wiring diagram](https://github.com/Arakon/ESP32-Nightscout-TFT-Graph/blob/main/wiring-C3.png)

If you want to flash the [binary](https://github.com/Arakon/ESP32-Nightscout-TFT-Graph/blob/main/ESP32-C3-Nightscout-TFT-Graph.bin) directly, get [esptool](https://github.com/espressif/esptool/releases/download/v4.10.0/esptool-v4.10.0-windows-amd64.zip), extract it, copy the ESP32-C3-Nightscout-TFT-Graph.bin to the same folder, then open a command prompt, navigate to the folder you extracted it to, and run:

`esptool --port COMxx write_flash 0x0 ESP32-C3-Nightscout-TFT-Graph.bin`

Replace "COMxx" with your COM port number of the ESP32-C3 (i.e. COM5)

Use Arduino IDE 2.3.5 or 2.3.6 to compile.

**Required libraries to install: LovyanGFX 1.2.7, ArduinoJSON 7.4.1, WiFiManger 2.0.17, ESP_MultiResetDetector 1.3.2**

Newer versions may work, but these are known working.

**It's critical that you install/downgrade the ESP32 Board version to _2.0.14_!**

Versions 3+ will not fit the available memory and also cause compile errors with LGFX, while versions >2.0.14 can cause a reset loop on the C3.

As board type, select the **LOLIN C3 Mini** if you are using the above ESP32-C3 SuperMini.

STL files are provided for the display, put the display in first so it is held by the hooks (note the indent, this is where the bottom of the display with the flex cable goes), put the plate on the back, then glue the ESP32-C3 into the provided space (thick doublesided tape recommended). Attach the plate on all 4 sides with a bit of hotglue. Finally, pop the lid on, noting the direction so that the button is above the ESP32's reset button.
If you need to get back in there for any reason, isopropyl alcohol will instantly release hotglue.
![image](https://github.com/Arakon/ESP32-Nightscout-TFT-Graph/blob/main/graph2.jpg)
![image](https://github.com/Arakon/ESP32-Nightscout-TFT-Graph/blob/main/graph3.jpg)

Once the device starts for the first time, connect your WiFi (cellphone or PC) to the access point "Nightscout-TFT" that becomes available. Enter the password shown on the screen, then visit http://192.168.4.1 in your webbrowser.
You can set up your nightscout instance (add the _/pebble_ at the end like the example! You also need a [token with viewing rights](https://nightscout.github.io/nightscout/security/#create-a-token).) and your home WiFi there.

There is some oddity with the MultiResetDetector library that causes it to sometimes enter config mode when plugged in. Unplugging and plugging back in fixes this usually.

**First** run the Setup tab, with your Nightscout data, High/Low/Critical values and background light strength.

**Afterwards** you will have to reconnect to the access point again and this time, set up the WiFi connection.

Once everything is connected, you should get a full display. If it hangs, try unplugging, waiting for a moment, and plugging it back in.
If you ever want to change any of the options, quickly **tripleclick** the Reset button within 5 seconds on the ESP32 to force config mode. Wait for the yellow text before pressing the button each time. This will require you to connect to the Nightscout-TFT access point again with the password displayed on the screen.

The graph will populate as values arrive, it takes 100 minutes/20 updates to fully fill it.

Some status info is available on the serial monitor via USB (115200 baud).

Partially based on [Gluci-Clock](https://github.com/Frederic1000/gluci-clock/) and my own [Nightscout-TFT](https://github.com/Arakon/Nightscout-TFT).

