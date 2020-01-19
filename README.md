# OpenHAB_ESP
A lightweight, simplied OpenHab 2 server for the ESP Platform

At this moment only the Basic UI is supported and very limited OpenHab 2 functionality.

## Why running OpenHab 2 on an ESP8266 ? ##
Today you can integrate ESP devices with OpenHab using MQTT or similar mechanisms.  This works really well but it always requires a true OpenHab (integration) server, such as a PC-based server or a Raspberry PI.
While this works well in a home environment, it might not always work in a remote setting.

Would it not be nice if you can leverage the OpenHab clients on your mobile phone to interface directly with a tiny ESP8266 device ?

## Business architecture ##

The business goals of this project are:
- to figure out how a (massive) server based environment can be made to work on a tiny device with only 80KB of RAM
- to find out whether the OpenHab 2 architecture is flexible and modular enough to be simplified to run on a small device and still deliver useful functionality
- to make the OpenHab ESP server work with the standard mobile phone clients by implementing a subset of the OpenHab 2 REST API
- leverage OpenHab icons
- support initially the Basic UI and perhaps later the PanelUI
- focus on __local functions__: _things_ provided by the ESP, such as sensor data or ESP location which translate to __Items__ and __States__
- provide a minimal set or __external things__ such an NTP server for providing current date and time  
- provide an open source code base for others to contribute to (or fork and start your own variant)
- to be able to run the OpenHab 2.4 demo sitemap (or slightly simplified version thereof that fits within the memory footprint)
- within ristrictions of the platform, make it as easy as possible to leverage sitmaps/items from regular OpenHab 2

The goals of this project do __NOT__ include:
- replicating full OpenHab 2 capabilities
- implement everything the same ways as in regular OpenHab
- support integration server features such as channels, things, etc.

## Functional architecture ##

The architecture of the OpenHab ESP server is based on three functional components:
- a REST-based interface which is a subset of the OpenHab 2 REST API
  REST calls that are needed for the mobile applications are implemented such as sitemaps, items and event subscriptions.
	The architecture allows for all OpenHab 2 REST functions to be added in a straightforward way
- a HTTP 1.1 WEB server to serve configuration files and icons, as  required by the mobile applications
- a socket interface to provide an event bus based on the Server Sent Events (SSE) RFC standard for real time updates to item states, sitemaps and (sitemap) pages

## Technical architecture ##

The ESP8266 platform is an extremely capable and stable platform.  For a few Euros/Dollars one has 4MB of flash/program memory, support for hundreds of libraries, excellent WiFi connectivity, a (primitive) flash based filesystem, a modern GNU C++ v11 compiler and outstanding IDE's such as Arduino and Platform IO.

It's a developers paradise with just __one__ major challenge: there is only limited amount of dynamic data space available - 4KB stack limit and a total of just 80KB of RAM, roughly half of which is already taken by the platform and foundation libraries. 

The technical architecture is based on following elements:
- source code written in C++ version 11 which is the highest version supported by the Espressif 8266 toolset.<br>
	This allows support for std::bind, auto variables/functions, templates and many other advanced functions
- [Platform IO](https://github.com/platformio) on top of Visual Studio Code is being used as default IDE, but the Arduino IDE can be used as well
- The [Arduino ESP8266 library](https://github.com/esp8266/Arduino) is the platform being used from [staging](https://github.com/platformio/platform-espressif8266.git#feature/stage).  All required code changes have been pulled and accepted into the master branch. 
Key functions leveraged from this wonderful library are: ESP8266WebServer, SPIFFS, Ticker, ESP8266mDNS, Base64 and ESP8266HTTPClient
- [ArduinoJSON](https://arduinojson.org/) is an outstanding library for embedded platforms that handles all JSON parsing and handling.  The author has been extremely helpful in supporting this project, even to the extend of adding support for parsing of very large strings with unknown lenghth at compile time.  You need version 6.14.0 (or later).
- A seperate state engine is used to keep the state of the items, since ArduinoJson is not build for handling state.
- Other libraries used: ESP8266TrueRandom for generating the subscription UUID for the event bus
- Lots of effort has been spent to run on top of the [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer), including rewriting parts of that code, but despite these efforts the end result remained unstable in addition to consuming (too) much memory. 
- Where possible (templated) classes and object oriented programming is being used e.g. the OpenHab server is implemented as a class
- Due to extreme memory pressure, the use of Strings is being limited (char pointers are being used instead) to local functions and most memory intensive operations are being buffered using stream based approaches.<br>The pressure on memory also explains why the code is not always as readable as desired and bypasses C++ best practices in several places.

## Implementation architecture ##

All source code is in the _src_ directory, with all include files in the _include_ directory.
The _platformio.ini_ file contains the library depencies as well as the compilation options.   
The _data/conf_ folder is the equivalent of OpenHab's _conf_ folder and is used to upload the configuration data to the ESP flash under the form of a SPIFFS file system.

Right now there is one very minor patch required to the Arduino ESP8266 library (ESP8266WebServer section) to support Server Sent Events.  Hopefully this won't be needed in the future as I have requested an enhancement and posted a PR.  Hence, for now, you need to use the staging version and modify the code.
At this time you also need to use the staging function of ArduinoJson v6 to get things working (_JsonDocument.shrinkToFit() call)

You can enable/disable extra debug information by enabling/disabling _OpenHABDebug_ in _include/OpenHab.h_
You can also enable debug log information for the ESP8266 Arduino Library (e.g. -D DEBUG_ESP_PORT=Serial -D DEBUG_ESP_HTTP_CLIENT enables debug logging for the HTTP_Client library) 

Working with OpenHab ESP happens in __3 phases__ :
1. The desired configuration (sitemap, items, icons) is created on a regular OpenHab 2 server and the UI tested with either the web based Based UI or mobile application
2. The OpenHab ESP code is then compiled and executed to extract and transform the created configuration data (sitemap, items etc.) in a JSON format for the ESP platform.  You will need to manually copy data generated by this generation run into the _/data_ directory, and also need copy all needed icons.  All icon types upported by OpenHab 2 (svg, png, jpg) are also supported with OpenHab ESP.  Use the PlatformIO _Upload File System Image_ extension to upload the configuration data to the ESP SPIFFS based flash file system. 
3. The OpenHab ESP code is compiled to execute as a OpenHab 2 runtime server.  You will need to add your own code to set the states of OpenHab items based on the type of sensors you ar using or type of application you are trying to integrate with OpenHab. 

To invoke the two different execution forms of the code, set or unset the __OPENHAB_GEN_CONFIG__ compiler flag
* With __OPENHAB_GEN_CONFIG__ defined, the code will behave as OpenHab ESP configuration generator, extracting sitemap and item info from a regular OpenHab 2 server.
* With __OPENHAB_GEN_CONFIG__ undefined, the code will behave as a regular OpenHab runtime server, supporting your sensors, application and serving the user interface via REST API's to OpenHab based mobile applications.

## Getting started ##   

1. Download or clone the repository
1. Install VSCode and PlatformIO extension (Linux/Windows (portable))
1. Copy _include/secrets_example.h_ to _include/secrets.h_ and customize it to support your WiFi SSID's, ESP name, passwords and MAC addresses. You will find more information embedded in the file
1. Enable __OPENHAB_GEN_CONFIG__ flag in _include/OpenHab.h_ (line 10) or alternatively in _platformio.ini_ and provide the _name/IP address_ and _port_of your regular OpenHab 2 server and the _sitemap name_ you want to use on the ESP server with the _src/main.cpp_ file.
1. Compile and run
1. Copy the generated data into _main.cpp_ and the _/data_ folder as outlined by the configuration generation run
1. Disable __OPENHAB_GEN_CONFIG__ flag in _platformio.ini_.  Add your code and rules to set or modify the state of all Items that correspond with your sensors or applicatiopn use case (see below).  This can be done in the _src/main.cpp_ file or in seperate (include) files as desired.  
1. Compile the code and upload.  It's OK to run but it will fail since we have not uploaded the file system data.
1. Use the PlatformIO _Upload File System Image_ extension to upload the configuration data to the ESP SPIFFS based flash file system. Reset the ESP. Now the OpenHab server should be running.
1. Disable debugging output in _main.cpp_ by uncommenting _#define OpenHABDebug_ when all is running fine.

## Integrating devices controlled by the ESP ##   

The interface between the OpenHab server and logical/physical devices controlled by your ESP happens through 2 sets of functions:
1. __Callback__ when an item changes state
The function _handleStateChange()_ gets called whenever the user changes the state of an Item via the UI (e.g. Android application). In this way it's possible to invoke changes to the physical controls behind the item to reflect the changed state (e.g. drive pin HIGH).

1. __Set state__ to reflect hardware changes in the user interface
There are 3 flavors of _setState()_ that can be used to update the state of an Item based on a change in the hardware (e.g. a sensor)
A few examples: _setState("CurrentDateTime", "2020-01-19T15:40:41")_, _setState(0, "2020-01-19T15:40:41")_ (using item index, 0 = "CurrentDateTime" in table _items_), _setState("Temperature_FF_Bath", 22.6)_ (using floating point value for numeric type items)

## Current limitations and known issues ##

- Only one sitemap is supported by default (mainly due to memory issues), although the code structures are pre-enabled to support multiple sitemaps (e.g. if you have many small ones)
- Transformed states in native languages are pre-enabled but not yet fully implemented
- The full demo sitemaps and items can not be run at this time.  They are just a little too large to fit in memory, but a slightly reduced subset "demos" is being provided
- Channels are not supported
- Currently there is no provided _rule engine_. A few options are being evaluated.
- Chart data is not yet supported
- Current DataTime data is supported, albeit in a bit of a simplistic way: an item named _CurrentDate_ will reflect the current date and time based on an NTP server if available or the ESP's own reference time.  You will need to modify the timezone and NTP server in the code.  Room for improvement...
- Documentation and user code examples are lacking
- _Things_ are not supported in config file form.  There is an embedded NTP time "Thing"
- Only the Android based OpenHab Mobile Client has been tested (the Beta release)
   
