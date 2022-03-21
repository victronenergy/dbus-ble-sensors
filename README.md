# dbus-ble-sensors

A daemon, part of [Venus](https://github.com/victronenergy/venus/), that reads
BLE (Bluetooth Low Energy) sensor advertisements and publishes it on dbus.  Currently supported sensors are:
- [RuuviTag](https://ruuvi.com/ruuvitag/) temperature sensors
- [RuuviTag Pro](https://ruuvi.com/ruuvitag-pro/) rugged and IP certified temperature sensors
- Mopeka Pro LPG tank sensors for [Steel](https://www.mopeka.com/product/mopeka-check-pro-lp-sensor/) or [Aluminum](https://www.mopeka.com/product/mopeka-check-pro-lp-senor-without-magnet/) tanks 
- [Mopeka Pro water tank sensors](https://www.mopeka.com/product/mopeka-water-sensor-bottom-mount/)

## using

Generally speaking, you should set up your sensors in the manufacturer's app before trying to add them to your Victron installation.  Doing so will let you make sure your sensors are positioned properly and sending data as expected.  

In order for sensors to show up in Venus, you first need to turn on Bluetooth in `Settings` -> `Bluetooth`.  Next, enable support for Bluetooth Sensors in `Settings` -> `I/O` -> `Bluetooth sensors`.  Once that's enabled, any of your sensors that are broadcasting BLE advertisements should start showing up on the `Bluetooth sensors` screen within a few minutes.  Once you see them on that screen, `Enable` each individual sensor to start using it.  You should be able to go back out to the `Device List` to see the metrics being reported by the sensor.  You may also need to do some minor, device-specific setup in `Device List` -> [Your Sensor] -> `Setup`.  You can also set a custom name for that sensor in `Device List` -> [Your Sensor] -> `Device` -> `Custom Name`.

For Ruuvi sensors, you can set the Type, which indicates whether the sensor is being used for Battery temperatures, Fridge temperatures, or Generic temperatures.

For Mopeka sensors, you'll need to set the following:
- The `Volume Units` for the tank
- The `Capacity` of the tank in those Units
- The `Fluid Type` contained in the tank (Fuel, Fresh Water, Grey Water, etc.)
- The `Sensor Value When Full` which is the fluid depth in mm when the tank is full

| :warning:        | Reporting for Mopeka sensors won't be accurate until `Sensor Value When Full` is set.  The easiest way to know what to set `Sensor Value When Full` to is to fill the tank completely and use the value shown at `Device List` -> [Your Sensor] -> `Tank Level in MM` as your 'Full' value. |
|---------------|:------------------------| 

## building

This is a [velib](https://github.com/victronenergy/velib/) project.

Besides on velib, it also depends on:

- dbus libs + headers
- libevent libs + headers

For cross-compiling for a Venus device, see
[here](https://www.victronenergy.com/live/open_source:ccgx:setup_development_environment).
And then especially the section about velib projects.

After cloning, and getting the velib submodule, run `make` to build the daemon.  If all goes well, you should have a new dbus-ble-sensors artifact in ./obj


## dbus paths

Tank:

```
com.victronenergy.tank


/Level              0 to 100%
/Remaining          m3
/Status             0=Ok; 1=Disconnected; 2=Short circuited; 3=Reverse polarity; 4=Unknown
/Capacity           m3
/FluidType          0=Fuel; 1=Fresh water; 2=Waste water; 3=Live well; 4=Oil; 5=Black water (sewage)
/Standard           0=European; 1=USA

Note that the FluidType enumeration is kept in sync with NMEA2000 definitions.

Mopeka-specific:
/BatteryVoltage     2.2 to 2.9 volts, usually
/Confidence         1 to 3; The level of confidence the Mopeka sensor has in its measurement
/LevelInMM          The tank level in millimeters
/MopekaSensorTypeId 3=Mopeka Pro LGP sensors; 5=Mopeka Pro water sensors
/RawValueFull       The tank level (mm) when the tank is full
/SyncButton         0 or 1; Whether the Mopeka sensor is in 'Sync' mode
/Temperature        Sensor temperature in degrees Celcius
```

Temperature:

```
com.victronenergy.temperature

/Temperature        degrees Celcius
/Status             0=Ok; 1=Disconnected; 2=Short circuited; 3=Reverse polarity; 4=Unknown
/TemperatureType    0=battery; 1=fridge; 2=generic

Ruuvi-specific:
/AccelX             X acceleration in G           
/AccelY             Y acceleration in G
/AccelZ             Z acceleration in G
/BatteryVoltage     2.2 to 2.9 volts, usually
/Humidity           Percent humidity
/Pressure           Pressure in hPa
/SeqNo              An incrementing ID for each BLE advertisement from this sensor
/TxPower            dBm
```
