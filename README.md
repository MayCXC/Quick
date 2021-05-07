# Quick
meshnet using [esp-mdf](https://github.com/espressif/esp-mdf)

backend using [Elixir gen_tcp](https://github.com/elixir-lang/elixir)

frontend using [Eclipse Paho](https://www.eclipse.org/paho/index.php?page=clients/js/index.php)

the meshnet will pub/sub to a MQTT server like [Mosquitto](https://mosquitto.org/), here is a basic config that connects it to the frontend:

```
#/etc/mosquitto/mosquitto.conf
port 1883
protocol mqtt
listener 8083
protocol websockets
```

# esp-idf breaks when you clone this repo!
ninja will get stuck in an endless build loop because it uses timestamps to find dirty targets, but apparently not with sub 24-hour resolution. Be sure to run `find . -exec touch -d "2 days ago" {} +` after `git clone https://github.com/mayhd3/Quick.git` and before `idf.py build`.
