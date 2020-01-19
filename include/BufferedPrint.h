#pragma once
/// This is intended for ESP8266 Arduino stack and ArduinoJSON 5 or 6
 
//#include <ESP8266WebServer.h>
#include <type_traits>
#include <WiFiClient.h>
#include <Print.h>
 
template <typename T, size_t BUFFER_SIZE = 12>
class BufferedPrint : public Print {
  public:
    ICACHE_FLASH_ATTR BufferedPrint(T client): _client(client), _length(0) {}
    ICACHE_FLASH_ATTR ~BufferedPrint() {
#ifdef DEBUG_ESP_PORT
      // Note: This is manual expansion of assertion macro
      if (_length != 0) {
        DEBUG_ESP_PORT.printf("\nassertion failed at " __FILE__ ":%d: " "_length == 0" "\n", __LINE__);
        // Note: abort() causes stack dump and restart of the ESP
        abort();
      }
#endif
    }
 
    ICACHE_FLASH_ATTR void client(T client) { _client = client; }
 
    ICACHE_FLASH_ATTR virtual size_t write(const uint8_t* s, size_t n) override {
      size_t capacity = BUFFER_SIZE - _length;
      if (capacity > n) {
        memcpy ((void *)&_buffer[_length], (const void *)s, n);
        _length += n;
      } else {
        memcpy ((void *)&_buffer[_length], (const void *)s, capacity);
        _flush();
        _length = n - capacity;
        memcpy ((void *)&_buffer[0], s + capacity, _length);
      }
      return n;
    }
     
    ICACHE_FLASH_ATTR virtual size_t write(uint8_t c) override {
      _buffer[_length++] = c;
      if (_length == BUFFER_SIZE) _flush();
      return 1;
    }
 
    ICACHE_FLASH_ATTR void flush() {
      if (_length != 0) _flush(_length);
    }
 
    ICACHE_FLASH_ATTR void stop(WiFiClient client) {
      flush();
      client.stop();
    }

    ICACHE_FLASH_ATTR void stop(HardwareSerial client) {
      flush();
      client.println();
    }
 
 
  private:
    ICACHE_FLASH_ATTR void _flush(size_t size = BUFFER_SIZE) {
      while (!_client.availableForWrite()) yield();
      _client.write((const uint8_t*)_buffer, size);
      _length = 0;
    }
 
    T _client;
    uint8_t _buffer[BUFFER_SIZE];
    size_t _length;
};