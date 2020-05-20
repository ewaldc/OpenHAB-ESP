## SPIFFS versus LittleFS
While SPIFFS is being phased out in favor of LittleFS, there are some advantages associated with SPIFFS.
First, the smallest file allocation is only 256 bytes while it is 4K for LittleFS.
Second, SPIFFS support greater than 32 bytes for filenames, even though it requires recompilation

## 32 versus 64 byte filename support
If the longest file name (= full path name) in the _data_ folder is longer than 32 bytes (but shorter than 64) than you need to enable 64 byte file names.

## Enable 32 or 64 byte filename support
Copy the appropriate Windows 64-bit executable to _%USERPROFILE%\.platformio\packages\tool-mkspiffs_.
Rename the original _mkspiffs.exe_

Modify _platform.ini_ in the following section:
[extra]
build_flags = -D SPIFFS_OBJ_NAME_LEN=_n_
where n = _32_ or _64_

__The default is set to 64 bytes.__
