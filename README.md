# Quick
* meshnet using [esp-mdf](https://github.com/espressif/esp-mdf)
* backend using [elixir](https://github.com/elixir-lang/elixir)
* frontend using [Eclipse Paho](https://www.eclipse.org/paho/index.php?page=clients/js/index.php)

## Setup
Install [esp-mdf](https://github.com/espressif/esp-mdf) and the [Espressif IDF](https://marketplace.visualstudio.com/items?itemName=espressif.esp-idf-extension) VS Code extension. Ignore the "Welcome to the extension setup" page, instead open the command pallette and select `ESP-IDF: Configure Paths` > `IDF_PATH` > `${env:HOME}/esp/esp-mdf/esp-idf/`, this avoids downloading `esp-idf` twice.

To set up the MQTT connection, open a terminal in the workspace and type `. ~/esp/esp-mdf/export.sh`, and then `idf.py menuconfig` > `Example Configuration` > `MQTT broker URL` > `mqtt://0.0.0.0:1883`, replacing `0.0.0.0` with the address of your MQTT broker. You can also set the network SSID and password from this menu, the defaults are `QuickTest` and `Testing123`. Finally, you can run a broker like [Mosquitto](https://mosquitto.org/) on the same network, here is a basic config that connects it to the frontend:

```
#/etc/mosquitto/mosquitto.conf
port 1883
protocol mqtt
listener 8083
protocol websockets
```
 Each node will publish bluetooth information and network topology changes to the broker:

`mosquitto_sub -h localhost -t "mesh/+/toCloud"`

`mosquitto_sub -h localhost -t "mesh/+/topo"`

## `idf.py build` might fail if your clone of this repo is too young!
`ninja` will get stuck in an endless build loop because it uses timestamps to find dirty targets, but apparently not with sub 24-hour resolution. Be sure to run `find . -exec touch -d "2 days ago" {} +` after `git clone` and before `idf.py build`.

Flashing and monitoring might fail if serial ports are busy, try `sudo systemctl stop serial-getty@USB0.service` if you get `device reports readiness to read but returned no data` errors.

The backend uses [gen_tcp](https://elixir-lang.org/getting-started/mix-otp/task-and-gen-tcp.html) and [Plug.Cowboy](https://github.com/elixir-plug/plug_cowboy), start it with `mix run` and open `http://127.0.0.1:8080/panel.html`. The backend does not need to be running to see MQTT messages.

The frontend can be opened in a browser as-is.

## Demos
Parts [1](https://drive.google.com/file/d/1RUB25v0KZ8gKf_x0r_MXZNGIfBAjof6k/view?usp=sharing) and [2](https://drive.google.com/file/d/17djVrHJW8_8Fp9lnTQ7tsjYYEkwgC1B4/view?usp=sharing).

## Todos
* Less redundant message formats, right now the nodes publish nested JSONs that can include their MAC address twice.
* Better multithreading than FreeRTOS tasks, ideally bluetooth scan results should be added to a shared queue from one thread and published to MQTT from another.
* Cool interactive canvas visuals for the frontend. 
