# Iono Pi Max driver kernel module

Raspberry Pi OS Kernel module for [Iono Pi Max](https://www.sferalabs.cc/iono-pi-max/) - the industrial controller based on the Raspberry Pi Compute Module.

Usage examples, from the shell:

Close a relay:

    $ echo 1 > /sys/class/ionopimax/digital_out/o1
    
Read the voltage on AV1:

    $ cat /sys/class/ionopimax/analog_in/av1
    
Or using Python:

    f = open('/sys/class/ionopimax/digital_out/o1', 'w')
    f.write('1')
    f.close()
    print('Relay O1 closed')

    f = open('/sys/class/ionopimax/analog_in/av1', 'r')
    val = f.read().strip()
    f.close()
    print('AV1: ' + val)

## Compile and Install

Make sure your system is updated:

    sudo apt update
    sudo apt upgrade
    sudo reboot
    
After reboot, install git and the Raspberry Pi kernel headers:

    sudo apt install git raspberrypi-kernel-headers

Clone this repo:

    git clone --depth 1 https://github.com/sfera-labs/iono-pi-max-kernel-module.git

Make and install:

    cd iono-pi-max-kernel-module
    make
    sudo make install
    
Compile the Device Tree and install it:

    dtc -@ -Hepapr -I dts -O dtb -o ionopimax.dtbo ionopimax.dts
    sudo cp ionopimax.dtbo /boot/overlays/

Add to `/boot/config.txt` the following line:

    dtoverlay=ionopimax

The `99-ionopimax-serial.rules` udev rule makes sure the USB dev connected to Iono's RS-485 interface is always available under `/dev/ionopimax-serial`:

    sudo cp 99-ionopimax-serial.rules /etc/udev/rules.d/

Optionally, to be able to use the `/sys/class/ionopimax/` files not as super user, create a new group "ionopimax" and set it as the module owner group by adding an udev rule:

    sudo groupadd ionopimax
    sudo cp 99-ionopimax.rules /etc/udev/rules.d/

and add your user to the group, e.g. for user "pi":

    sudo usermod -a -G ionopimax pi

To enable SocketCAN support for the CAN FD/CAN 2.0 controller (MCP2518FD), add to `/boot/config.txt` the following line:

    dtoverlay=mcp251xfd,spi0-0,interrupt=28

Reboot:

    sudo reboot

## Usage

After loading the module, you will find all the available devices under the directory `/sys/class/ionopimax/`.

The following paragraphs list all the possible devices (directories) and files coresponding to Iono Pi Max's features. 

You can write to and/or read these files to configure, monitor and control your Iono Pi Max. The kernel module will take care of performing the corresponding GPIO or I2C operations. I2C transactions are automatically repeated in case of error and CRC validation is used when supported by the installed firmware (>= 1.4).

Files written in _italic_ are configuration parameters. Those marked with * are not persistent, i.e. their values are reset to default after a power cycle. To change the default values use the `/mcu/config` file (see below).    
Configuration parameters not marked with * are permanently saved each time they are changed, so that their value is retained across power cycles or MCU resets.   
This allows to have a different configuration during the boot up phase, even after an abrupt shutdown. For instance, you may want a short watchdog timeout while your application is running, but it needs to be reset to a longer timeout when a power cycle occurs so that Iono Pi Max has the time to boot and restart your application handling the watchdog heartbeat.

### Button - `/sys/class/ionopimax/button/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|status|R|0|Button released|
|status|R|1|Button pressed|

### Buzzer - `/sys/class/ionopimax/buzzer/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|status|R/W|0|Buzzer off|
|status|R/W|1|Buzzer on|
|status|W|F|Flip buzzer's state|
|beep|W|&lt;t&gt;|Buzzer on for &lt;t&gt; ms|
|beep|W|&lt;t_on&gt; &lt;t_off&gt; &lt;rep&gt;|Buzzer beep &lt;rep&gt; times with &lt;t_on&gt;/&lt;t_off&gt; ms periods. E.g. "200 50 3"|

### LED - `/sys/class/ionopimax/led/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|l&lt;n&gt;_r|R/W|&lt;val&gt;|Value (0 - 255) of red channel of RGB LED number &lt;n&gt; (1 - 5)|
|l&lt;n&gt;_g|R/W|&lt;val&gt;|Value (0 - 255) of green channel of RGB LED number &lt;n&gt; (1 - 5)|
|l&lt;n&gt;_b|R/W|&lt;val&gt;|Value (0 - 255) of blue channel of RGB LED number &lt;n&gt; (1 - 5)|
|l&lt;n&gt;_br|R/W|&lt;val&gt;|Brightness value (0 - 255) of RGB LED number &lt;n&gt; (1 - 5)|

### Digital Inputs - `/sys/class/ionopimax/digital_in/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|di&lt;n&gt;|R|0|Digital input (DI) &lt;n&gt; (1 - 4) low|
|di&lt;n&gt;|R|1|Digital input (DI) &lt;n&gt; (1 - 4) high|

For each digital input, we also expose: 
* the debounced state
* 2 debounce times in ms ("on" for high state and "off" for low state) with default value of 50ms
* 2 state counters ("on" for high state and "off" for low state)

The debounce times for each DI has been splitted in "on" and "off" in order to make the debounce feature more versatile and suited for particular application needs (e.g. if we consider digital input 1, and set its debounce "on" time to 50ms and its debounce "off" time to 0ms, we just created a delay-on type control for digital input 1 with delay-on time equal to 50ms).    
Change in value of a debounce time automatically resets both counters.    
The debounce state of each digital input at system start is UNDEFINED (-1), because if the signal on the specific channel cannot remain stable for a period of time greater than the ones defined as debounce "on" and "off" times, we are not able to provide a valid result. 

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|di*N*_deb|R|1|Digital input *N* debounced value high|
|di*N*_deb|R|0|Digital input *N* debounced value low|
|di*N*_deb|R|-1|Digital input *N* debounced value undefined|
|di*N*_deb_on_ms|RW|val|Minimum stable time in ms to trigger change of the debounced value of digital input *N* to high state. Default value=50|
|di*N*_deb_off_ms|RW|val|Minimum stable time in ms to trigger change of the debounced value of digital input *N* to low state. Default value=50|
|di*N*_deb_on_cnt|R|val| Number of times with the debounced value of the digital input *N* in high state. Rolls back to 0 after 4294967295|
|di*N*_deb_off_cnt|R|val|Number of times with the debounced value of the digital input *N* in low state. Rolls back to 0 after 4294967295|

### Digital Outputs - `/sys/class/ionopimax/digital_out/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|_pdc_*|R/W|0|Pulldown current disabled|
|_pdc_*|R/W|1|Pulldown current enabled (factory default)|
|o&lt;n&gt;|R/W|0|Relay (O) &lt;n&gt; (1 - 4) open|
|o&lt;n&gt;|R/W|1|Relay (O) &lt;n&gt; (1 - 4) closed|
|o&lt;n&gt;|R|F|Relay (O) &lt;n&gt; (1 - 4) fault while open|
|o&lt;n&gt;|R|S|Relay (O) &lt;n&gt; (1 - 4) fault while closed|
|oc&lt;n&gt;|R/W|0|Open collector (OC) &lt;n&gt; (1 - 4) open|
|oc&lt;n&gt;|R/W|1|Open collector (OC) &lt;n&gt; (1 - 4) closed|
|oc&lt;n&gt;|R|F|Open collector (OC) &lt;n&gt; (1 - 4) fault open|
|oc&lt;n&gt;|R|S|Open collector (OC) &lt;n&gt; (1 - 4) short circuit|

### Digital I/O DTx - `/sys/class/ionopimax/digital_io/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|dt&lt;n&gt;_mode|R/W|x|DT &lt;n&gt; (1 - 4) line not controlled by kernel module|
|dt&lt;n&gt;_mode|R/W|in|DT &lt;n&gt; (1 - 4) line set as input|
|dt&lt;n&gt;_mode|R/W|out|DT &lt;n&gt; (1 - 4) line set as output|
|dt&lt;n&gt;|R(/W)|0|DT &lt;n&gt; (1 - 4) line low. Writable only in output mode|
|dt&lt;n&gt;|R(/W)|1|DT &lt;n&gt; (1 - 4) line high. Writable only in output mode|

### Analog Inputs - `/sys/class/ionopimax/analog_in/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|_enabled_*|R/W|0|Analog converter disabled (power off)|
|_enabled_*|R/W|1|Analog converter enabled (factory default)|
|_hsf_*|R/W|0|High speed filter for AV/AI inputs disabled (factory default)|
|_hsf_*|R/W|1|High speed filter for AV/AI inputs enabled|
|*av&lt;n&gt;_mode**|R/W|0|AV &lt;n&gt; (1 - 4) input disabled (FW >= 1.3)|
|*av&lt;n&gt;_mode**|R/W|U|AV &lt;n&gt; (1 - 4) unipolar mode (range 0V - +20V) (factory default)|
|*av&lt;n&gt;_mode**|R/W|B|AV &lt;n&gt; (1 - 4) bipolar mode (range -10V - +10V)|
|av&lt;n&gt;|R|&lt;val&gt;|AV &lt;n&gt; (1 - 4) voltage value in mV/100|
|*ai&lt;n&gt;_mode**|R/W|0|AI &lt;n&gt; (1 - 4) input disabled (FW >= 1.3)|
|*ai&lt;n&gt;_mode**|R/W|U|AI &lt;n&gt; (1 - 4) unipolar mode (range 0mA - +20mA) (factory default)|
|*ai&lt;n&gt;_mode**|R/W|B|AI &lt;n&gt; (1 - 4) bipolar mode (range -10mA - +10mA)|
|ai&lt;n&gt;|R|&lt;val&gt;|AI &lt;n&gt; (1 - 4) current value in &micro;A|
|*at&lt;n&gt;_mode**|R/W|0|AT &lt;n&gt; (1 - 2) disabled (factory default)|
|*at&lt;n&gt;_mode**|R/W|1|AT &lt;n&gt; (1 - 2) enabled as PT100 sensor input|
|*at&lt;n&gt;_mode**|R/W|2|AT &lt;n&gt; (1 - 2) enabled as PT1000 sensor input|
|at&lt;n&gt;|R|&lt;val&gt;|AT &lt;n&gt; (1 - 2) temperature value in &deg;C/100|

### Analog Outputs - `/sys/class/ionopimax/analog_out/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|*ao&lt;n&gt;_enabled**|R/W|0|Analog output (AO) &lt;n&gt; disabled (factory default)|
|*ao&lt;n&gt;_enabled**|R/W|1|Analog output (AO) &lt;n&gt; enabled|
|*ao&lt;n&gt;_mode**|R/W|I|Analog output (AO) &lt;n&gt; current mode (factory default)|
|*ao&lt;n&gt;_mode**|R/W|V|Analog output (AO) &lt;n&gt; voltage mode|
|*ao&lt;n&gt;**|R/W|&lt;val&gt;|Analog output (AO) &lt;n&gt; value, in mV (voltage mode) or &micro;A (current mode)|
|ao&lt;n&gt;_err|R|&lt;err&gt;|Analog output (AO) &lt;n&gt; errors register value. Bit 0 (LSB) set to 1 indicates an over-temperature error, bit 1 a load error, and bit 2 (MSB) a common mode error|

### Power Outputs - `/sys/class/ionopimax/power_out/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|_vso_enabled_*|R/W|0|VSO output disabled|
|_vso_enabled_*|R/W|1|VSO output enabled (factory default)|
|_vso_*|R/W|&lt;val&gt;|VSO voltage value in mV (11500 - 24500) (factory default: 12000)|
|vso_mon_v|R|&lt;val&gt;|Actual voltage measured on VSO, in mV|
|vso_mon_i|R|&lt;val&gt;|Current drain measured on VSO, in mA|
|_5vo_enabled_*|R/W|0|5VO output disabled|
|_5vo_enabled_*|R/W|1|5VO output enabled|

### Wiegand - `/sys/class/ionopimax/wiegand/`

You can use the DT lines as Wiegand interfaces for keypads or card readers. You can connect up to two Wiegand devices using DT1/DT2 respctively for the D0/D1 lines of the first device (w1) and DT3/DT4 for D0/D1 of the second device (w2).

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|w&lt;N&gt;_enabled|R/W|0|Wiegand interface w&lt;N&gt; disabled|
|w&lt;N&gt;_enabled|R/W|1|Wiegand interface w&lt;N&gt; enabled|
|w&lt;N&gt;_data|R|&lt;ts&gt; &lt;bits&gt; &lt;data&gt;|Latest data read from wiegand interface w&lt;N&gt;. The first number (&lt;ts&gt;) represents an internal timestamp of the received data, it shall be used only to discern newly available data from the previous one. &lt;bits&gt; reports the number of bits received (max 64). &lt;data&gt; is the sequence of bits received represnted as unsigned integer|

The following properties can be used to improve noise detection and filtering. The w&lt;N&gt;_noise property reports the latest event and is reset to 0 after being read.

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|w&lt;N&gt;_pulse_width_max|R/W|&lt;val&gt;|Maximum bit pulse width accepted, in &micro;s|
|w&lt;N&gt;_pulse_width_min|R/W|&lt;val&gt;|Minimum bit pulse width accepted, in &micro;s|
|w&lt;N&gt;_pulse_itvl_max|R/W|&lt;val&gt;|Maximum interval between pulses accepted, in &micro;s|
|w&lt;N&gt;_pulse_itvl_min|R/W|&lt;val&gt;|Minimum interval between pulses accepted, in &micro;s|
|w&lt;N&gt;_noise|R|0|No noise|
|w&lt;N&gt;_noise|R|10|Fast pulses on lines|
|w&lt;N&gt;_noise|R|11|Pulses interval too short|
|w&lt;N&gt;_noise|R|12/13|Concurrent movement on both D0/D1 lines|
|w&lt;N&gt;_noise|R|14|Pulse too short|
|w&lt;N&gt;_noise|R|15|Pulse too long|

### Watchdog - `/sys/class/ionopimax/watchdog/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|enabled|R/W|0|Watchdog disabled|
|enabled|R/W|1|Watchdog enabled|
|enabled|W|F|Flip watchdog enabled state|
|expired|R|0|Watchdog timeout not expired|
|expired|R|1|Watchdog timeout expired|
|heartbeat|W|0|Set watchdog heartbeat line low|
|heartbeat|W|1|Set watchdog heartbeat line high|
|heartbeat|W|F|Flip watchdog heartbeat state|
|_enable_mode_*|R/W|D|Watchdog normally disabled (factory default)|
|_enable_mode_*|R/W|A|Watchdog always enabled (ignores /enabled value)|
|_timeout_*|R/W|&lt;t&gt;|Watchdog heartbeat timeout, in seconds (1 - 65535). Factory default: 60|
|_down_delay_*|R/W|&lt;t&gt;|Forced shutdown delay from the moment the timeout is expired and the shutdown cycle has not been enabled, in seconds (1 - 65535). Factory default: 60|
|_sd_switch_*|R/W|&lt;n&gt;|Switch boot from SDA/SDB after &lt;n&gt; consecutive watchdog resets, if no heartbeat is detected. A value of n > 1 can be used with /enable_mode set to A only; if /enable_mode is set to D, then /sd_switch is set automatically to 1|
|_sd_switch_*|R/W|0|SD switch on watchdog reset disabled (factory default)|

### Power - `/sys/class/ionopimax/power/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|down_enabled|R/W|0|Delayed shutdown cycle disabled|
|down_enabled|R/W|1|Delayed shutdown cycle enabled|
|_down_delay_*|R/W|&lt;t&gt;|Shutdown delay from the moment it is enabled, in seconds (1 - 65535). Factory default: 60|
|_off_time_*|R/W|&lt;t&gt;|Duration of power-off, in seconds (1 - 65535). Factory default: 5|
|_up_delay_*|R/W|&lt;t&gt;|Power-up delay after main power is restored, in seconds (0 - 65535). Factory default: 0|
|_down_enable_mode_*|R/W|I|Immediate (factory default): when shutdown is enabled, Iono will immediately initiate the power-cycle, i.e. wait for the time specified in /down_delay and then power off the Pi board for the time specified in /off_time|
|_down_enable_mode_*|R/W|A|Arm: enabling shutdown will arm the shutdown procedure, but will not start the power-cycle until the shutdown enable line goes low again (i.e. shutdown disabled or Raspberry Pi switched off). After the line goes low, Iono will initiate the power-cycle|
|_up_mode_*|R/W|A|Always: if shutdown is enabled when the main power is not present, only the Raspberry Pi is turned off, and the power is always restored after the power-off time, even if running on battery, with no main power present|
|_up_mode_*|R/W|M|Main power (factory default): if shutdown is enabled when the main power is not present, Iono is fully powered down after the shutdown wait time, and powered up again only when the main power is restored|
|_sd_switch_|R/W|1|Switch boot from SDA/SDB at every power-cycle|
|_sd_switch_|R/W|0|SD switch at power-cycle disabled (factory default)|

### UPS - `/sys/class/ionopimax/ups/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|_enabled_*|R/W|0|UPS disabled|
|_enabled_*|R/W|1|UPS enabled (factory default)|
|battery|R|0|Running on main power|
|battery|R|1|Running on battery power|
|status|R|0|UPS status: idle|
|status|R|1|UPS status: detecting battery|
|status|R|2|UPS status: battery disconnected|
|status|R|4|UPS status: charging battery|
|status|R|5|UPS status: battery charged|
|status|R|6|UPS status: battery in use|
|status|R|8|UPS status: battery overvoltage error|
|status|R|9|UPS status: battery undervoltage error|
|status|R|10|UPS status: charger damaged|
|status|R|11|UPS status: unstable|
|battery_charge|R|&lt;n&gt;|Estimated battery charge percentage|
|charger_mon_v|R|&lt;val&gt;|Voltage measured on battery charger output, in mV|
|charger_mon_i|R|&lt;val&gt;|Current drain measured on battery charger output, in mA|
|_battery_capacity_*|R/W|&lt;c&gt;|Battery capacity in mAh (100 - 60000). Writable only while UPS disabled. Factory default: 800|
|_battery_v_*|R/W|&lt;n&gt;|Voltage rating of the battery in mV. Accepted values: 12000 or 24000. factory default: 12000|
|_battery_i_max_*|R/W|&lt;c&gt;|Maximum charging current allowed. If set to zero, the value is derived from the battery capacity. Writable only while UPS disabled. Factory default: 0 |
|_power_delay_*|R/W|&lt;t&gt;|UPS automatic power-cycle timeout, in seconds (0 - 65535). Iono will automatically initiate a delayed power-cycle (just like when /power/down_enabled is set to 1) if the main power source is not available for the number of seconds set. A value of 0 (factory default) disables the automatic power-cycle|

### Power Supply Input - `/sys/class/ionopimax/power_in/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|mon_v|R|&lt;val&gt;|Voltage measured on power supply input, in mV|
|mon_i|R|&lt;val&gt;|Current drain measured on power supply input, in mA|

### SD - `/sys/class/ionopimax/sd/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|_sdx_enabled_*|R/W|1|SDX bus enabled (factory default)|
|_sdx_enabled_*|R/W|0|SDX bus disabled|
|sdx_enabled|R/W|2|SDX bus disabled, reset to enabled upon power cycle (FW ver. >= 1.2)|
|_sd1_enabled_*|R/W|1|SD1 bus enabled|
|_sd1_enabled_*|R/W|0|SD1 bus disabled (factory default)|
|sd1_enabled|R/W|2|SD1 bus enabled, reset to disabled upon power cycle (FW ver. >= 1.2)|
|_sdx_default_|R/W|A|At power-up, SDX bus routed to SDA and SD1 bus to SDB by default (factory default)|
|_sdx_default_|R/W|B|At power-up, SDX bus routed to SDB and SD1 bus to SDA, by default|
|_sdx_routing_|R/W|A|SDX bus routed to SDA and SD1 bus to SDB (factory default)|
|_sdx_routing_|R/W|B|SDX bus routed to SDB and SD1 bus to SDA|

### USB - `/sys/class/ionopimax/usb/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|usb&lt;n&gt;_enabled|R/W|0|USB &lt;n&gt; (1 or 2) disabled (power off)|
|usb&lt;n&gt;_enabled|R/W|1|USB &lt;n&gt; (1 or 2) enabled|
|usb&lt;n&gt;_err|R|0|USB &lt;n&gt; (1 or 2) OK|
|usb&lt;n&gt;_err|R|1|USB &lt;n&gt; (1 or 2) fault|

### Fan - `/sys/class/ionopimax/fan/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|_always_on_*|R/W|0|The fan will activate automatically based on system state (factory default)|
|_always_on_*|R/W|1|Fan always active|
|status|R|0|Fan inactive|
|status|R|1|Fan active|

### System Temperature - `/sys/class/ionopimax/sys_temp/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|top|R|&lt;val&gt;|Temperature value from sensor on the top side of the bottom board, in &deg;C/100|
|bottom|R|&lt;val&gt;|Temperature value from sensor on the bottom side of the bottom board, in &deg;C/100|

### Expansion Bus - `/sys/class/ionopimax/expbus/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|_enabled_*|R/W|0|Expansion bus disabled (factory default)|
|_enabled_*|R/W|1|Expansion bus enabled|
|aux|R|0|Expansion bus auxiliary line low|
|aux|R|1|Expansion bus auxiliary line high|
|_5vx_*|R/W|0|Expansion bus 5V power off (factory default)|
|_5vx_*|R/W|1|Expansion bus 5V power on|

### System state - `/sys/class/ionopimax/sys_state/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|status_all|R|&lt;val&gt;|System state register value. Bitmap of all the following status flags from fan_status (bit 0 - LSB) to rs485_err (bit 13)|
|fan_status|R|0|Fan inactive|
|fan_status|R|1|Fan active|
|5vo_prot|R|0|5VO output OK|
|5vo_prot|R|1|5VO output protection enabled. Output temporarily disabled|
|5vx_prot|R|0|Expansion bus 5V line OK|
|5vx_prot|R|1|Expansion bus 5V line protection enabled. Output temporarily disabled|
|expbus_aux|R|0|Expansion bus auxiliary line low|
|expbus_aux|R|1|Expansion bus auxiliary line high|
|vso_prot|R|0|VSO output OK|
|vso_prot|R|1|VSO output protection enabled. Output temporarily disabled|
|ao&lt;n&gt;_prot|R|0|AO &lt;n&gt; output OK|
|ao&lt;n&gt;_prot|R|1|AO &lt;n&gt; output protection enabled. AO &lt;n&gt; temporarily disabled|
|vso_err|R|0|VSO control OK|
|vso_err|R|1|VSO control failure|
|ad4112_err|R|0|Analog converter for analog inputs OK|
|ad4112_err|R|1|Analog converter for analog inputs failure|
|ups_err|R|0|UPS control OK|
|ups_err|R|1|UPS control failure|
|led_err|R|0|LEDs control OK|
|led_err|R|1|LEDs control failure|
|sys_temp_err|R|0|System temperature probes OK|
|sys_temp_err|R|1|System temperature probes failure|
|rs232_err|R|0|RS-232 interface OK|
|rs232_err|R|1|RS-232 interface failure|
|rs485_err|R|0|RS-485 interface OK|
|rs485_err|R|1|RS-485 interface failure|

### MCU - `/sys/class/ionopimax/mcu/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|config|W|S|Save the current configuration as default to be retained across power cycles|
|config|W|R|Restore the original factory configuration and default values|
|fw_version|R|&lt;m&gt;.&lt;n&gt;|Read the firmware version, &lt;m&gt; is the major version number, &lt;n&gt; is the minor version number E.g. "1.0"|

### Secure Element - `/sys/class/ionopimax/sec_elem/`

|File|R/W|Value|Description|
|----|:---:|:-:|-----------|
|serial_num|R|9 1-byte HEX values|Secure element serial number|

### CAN

Check that the SocketCAN interface is correctly enabled by running:

    ifconfig -a
    
you should see an interface named `can0`:

    can0: flags=128<NOARP>  mtu 16
            unspec 00-00-00-00-00-00-00-00-00-00-00-00-00-00-00-00  txqueuelen 10  (UNSPEC)
            RX packets 0  bytes 0 (0.0 B)
            RX errors 0  dropped 0  overruns 0  frame 0
            TX packets 0  bytes 0 (0.0 B)
            TX errors 0  dropped 0 overruns 0  carrier 0  collisions 0
            device interrupt 199 

Initialize `can0` specifying communication parameters and protocol options, e.g.:

    sudo ip link set can0 up type can bitrate 1000000 dbitrate 8000000 restart-ms 1000 berr-reporting on fd on
    sudo ifconfig can0 txqueuelen 65536

Dump data from the bus:

    candump can0

Generate random traffic:

    cangen can0 -mv

Refer to the [SocketCAN documentation](https://www.kernel.org/doc/Documentation/networking/can.txt) for further details.
