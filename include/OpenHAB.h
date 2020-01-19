// Copyright Ewald Comhaire 2016-2020
// MIT License
//
// Arduino Micro OpenHAB2.0 library
// If you like this project, please add a star!
#ifndef OpenHABInclude
#define OpenHABInclude
#pragma once
#ifndef OpenHABDebug
#define OpenHABDebug
#endif

#define OPENHAB_GEN_CONFIG    // Comment to swicth to run-time mode
#define SSE_MAX_CHANNELS 8

// Avoid warning: always_inline function might not be inlinable [-Wattributes]
#ifdef _MSC_VER  // Visual Studio
#define FORCE_INLINE  // __forceinline causes C4714 when returning std::string
#define NO_INLINE __declspec(noinline)
#define DEPRECATED(msg) __declspec(deprecated(msg))
#elif defined(__GNUC__)  // GCC or Clang
#define FORCE_INLINE __attribute__((always_inline))
#define NO_INLINE __attribute__((noinline))
#pragma GCC diagnostic ignored "-Wattributes"
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 5)
#define DEPRECATED(msg) __attribute__((deprecated(msg)))
#else
#define DEPRECATED(msg) __attribute__((deprecated))
#endif
#else  // Other compilers
#define FORCE_INLINE
#define NO_INLINE
#define DEPRECATED(msg)
#endif

extern "C" {
#include "c_types.h"
}

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <time.h>
#include <sys/time.h>
#include <coredecls.h>       
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
//#include <ESPAsyncTCP.h>
#define DEBUG_ESP_PORT Serial
#define SSESOCKETS_SAVE_RAM
//#include <SSESocketsServer.h>
#include "ESP8266TrueRandom.h"
#include <ESP8266WiFiType.h>
#include <ESP8266WiFiSTA.h>
#include <ESP8266WiFiScan.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266WiFiGeneric.h>
#include <ESP8266WiFiAP.h>
#include "Ticker.h"
#include <TZ.h>
//#include <WiFiUdp.h>
//#include <WiFiClientSecure.h>
#include "BufferedPrint.h"
#include <FS.h>   // Include the SPIFFS library
#include "base64.h"
#include "libb64/cdecode.h"
#include <ArduinoJson.h>

// Global user defined settings in main.cpp
extern const PROGMEM char *allowedMAC[];

#ifdef OPENHAB_GEN_CONFIG
#include <ESP8266HTTPClient.h>
//	extern const PROGMEM char *OPENHAB_SERVER;
//	extern const PROGMEM char *SITEMAP;
//	extern const PROGMEM int LISTEN_PORT;
#endif

#define strdup_P(s) strndup_P(s, sizeof(s))
#define strndup_P(s, size) strcat_P((char *)malloc(size + 1), s)
#define isValidItemIndex(i) (i != 255)

// Global parameters for runtime
enum ContentType : uint8 {IMAGE_SVG = 0, IMAGE_PNG, IMAGE_JPEG, TEXT_PLAIN, TEXT_HTML, TEXT_CSS, APP_JAVASCRIPT, APP_JSON};
#define IMAGE_DEFAULT IMAGE_SVG       // Default image type

static const char* ContentTypeStr[] PROGMEM = {"image/svg+xml", "image/png", "image/jpeg", "text/plain", "text/htm", "text/css", "application/javascript", "application/json"};
static const char* ContentTypeExt[] PROGMEM = {".svg", ".png", ".jpeg", "", ".htm", ".css", ".js", ".json"};
static const char* ContentTypeArg[] PROGMEM = {"SVG", "PNG", "JPG"};
static const char* HTTPMethodStr[] PROGMEM = { "HTTP_ANY", "HTTP_GET", "HTTP_HEAD", "HTTP_POST", "HTTP_PUT", "HTTP_PATCH", "HTTP_DELETE", "HTTP_OPTIONS" };

inline const char* PROGMEM getContentTypeStr(ContentType contentType) { return ContentTypeStr[contentType]; }
inline const char* PROGMEM getContentTypeExt(ContentType contentType) { return ContentTypeExt[contentType]; }

/*enum ItemType {ItemUnknown = 0, ItemCall = 1, ItemColor = 2, ItemContact = 4, ItemDateTime = 8, ItemDimmer = 16, ItemGroup = 32, ItemLocation = 64, ItemNumber = 128,\
	ItemRollerShutter = 256, ItemString = 512, ItemSwitch = 1024}; */
enum ItemType : uint16 {ItemNull = 0, ItemCall, ItemColor, ItemContact, ItemDateTime, ItemDimmer, ItemGroup, ItemLocation, ItemNumber, ItemNumber_Angle,
	ItemRollerShutter, ItemString, ItemSwitch};
static const char *ItemTypeString[] PROGMEM = {"ItemNull", "ItemCall", "ItemColor", "ItemContact", "ItemDateTime", "ItemDimmer", "ItemGroup", "ItemLocation", "ItemNumber", "ItemNumber_Angle",
	"ItemRollerShutter", "ItemString", "ItemSwitch"};
static const char *ItemTypeStr[] PROGMEM = {"", "Call", "Color", "Contact", "DateTime", "Dimmer", "Group", "Location", "Number", "Number:Angle",
	"Rollershutter", "String", "Switch"};
enum GroupFunction : uint8 {FunctionNone = 0, FunctionAVG, FunctionOR};
#define isNumber(x) ((x == ItemNumber) || (x == ItemDimmer))

#ifdef OpenHABDebug
template <typename T, typename U> static inline void DbgPrint(T x, U y) {Serial.print(x); Serial.print(y);}
template <typename T, typename U> static inline void DbgPrintln(T x, U y) {Serial.print(x); Serial.println(y);}
template <typename T> static inline void DbgPrintln(T x) {Serial.println(x);}
template <typename T> static inline void DbgPrint(T x) {Serial.print(x);}
ICACHE_FLASH_ATTR static inline void DbgPrintf(const char *format, ...) {
  char sbuf[256];			// For debug lines
  va_list varArgs;          	// For variable number of params
  va_start(varArgs, format);	// Prepare parameters
  vsnprintf(sbuf, sizeof(sbuf), format, varArgs);	// Format the message
  va_end(varArgs);            // End of using parameters
  Serial.print(sbuf);
}
#else	// OpenHABDebug
#define DbgPrint(x)			{}
#define DbgPrintln(x,...)	{}
#define DbgPrintf(const char *format, ... ) {}
#endif	// OpenHABDebug

/* 
// Minimal class to replace std::vector
template<typename Data>
class Vector {
  size_t _size; // Stores no. of actually stored objects
  size_t _capacity; // Stores allocated capacity
  Data *_data; // Stores data
  public:
    Vector() : _size(0), _capacity(0), _data(0) {}; // Default constructor
    Vector(Vector const &orig) : _size(orig._size), _capacity(orig._capacity), _data(0) { _data = (Data *)malloc(_capacity*sizeof(Data)); memcpy(_data, orig._data, _size*sizeof(Data)); }; // Copy constuctor
    ~Vector() { free(_data); }; // Destructor
    Vector &operator=(Vector const &orig) { free(_data); _size = orig._size; _capacity = orig._capacity; _data = (Data *)malloc(_capacity*sizeof(Data)); memcpy(_data, orig._data, _size*sizeof(Data)); return *this; }; // Needed for memory management
    //void push_back(Data const &x) { if (_capacity == _size) resize(); _data[_size++] = x; }; // Adds new value. If needed, allocates more space
    Data *push_back(Data const &x) { if (_capacity == _size) resize(); memcpy((void *)&_data[_size], (const void *)&x, sizeof(Data)); return (Data *)&_data[_size++]; }; // Adds new value. If needed, allocates more space
    size_t size() const { return _size; }; // Size getter
    Data const &operator[](size_t idx) const { return _data[idx]; }; // Const getter
    Data &operator[](size_t idx) { return _data[idx]; }; // Changeable getter
  private:
    void resize() { _capacity = _capacity ? _capacity * 2 : 1; Data *newdata = (Data *)malloc(_capacity*sizeof(Data)); memcpy(newdata, _data, _size * sizeof(Data)); free(_data); _data = newdata; };// Allocates double the old space
};
*/

static const char* connectionStatusStr[] PROGMEM = { "Idle status", "Network not available", "", "Connected", "Wrong password", "", "Disconnected" };
static const char* PROGMEM connectionStatus(uint16_t status) {
    return (status < (sizeof(connectionStatusStr) / sizeof(char *))) ? connectionStatusStr[status] : PSTR("Unknown");
}

#define PTM(w) Serial.print(" " #w "="); Serial.print(tm->tm_##w);

ICACHE_FLASH_ATTR static void wifiEventHandler(System_Event_t *event) { 
  //System_Event_t* event = reinterpret_cast<System_Event_t*>(arg);
  //DbgPrintln("wifiEventHandler - event:", event->event);
  switch (event->event) {  // A client connected with the SoftAP
    case WIFI_EVENT_STAMODE_CONNECTED:        break;
    case WIFI_EVENT_STAMODE_DISCONNECTED:     
      DbgPrintf("[WiFi] %d, Disconnected - Status %d, %s\n", event, WiFi.status(), connectionStatus(WiFi.status()));      
      break;
    case WIFI_EVENT_STAMODE_AUTHMODE_CHANGE:  break;
  
    // The ESP8266 connected to another AP and got an IP address
    case WIFI_EVENT_STAMODE_GOT_IP: {
      DbgPrintln(F("OpenHab::wifiEventHandler - WIFI_EVENT_STAMODE_GOT_IP"));
      Event_StaMode_Got_IP_t gotIP = event->event_info.got_ip;
      //gotIP.mask; gotIP.gw
      DbgPrintln(F("ESP IP Address on Router AP: "), IPAddress(gotIP.ip.addr).toString());
      //OpenHabServer.SetIP(IPAddress(gotIP.ip.addr));
      break;
    }
    //case WIFI_EVENT_SOFTAPMODE_STADISCONNECTED:
    case WIFI_EVENT_SOFTAPMODE_STACONNECTED: {
      DbgPrintln(F("OpenHab::wifiEventHandler - WIFI_EVENT_SOFTAPMODE_STACONNECTED"));
      char macStr[20];
      snprintf(macStr, 20, MACSTR, MAC2STR(event->event_info.sta_connected.mac));
      DbgPrintln(F("ESP AP Client MAC address: "), macStr);
      for (int i = 0; allowedMAC[i]; i++) {
        if (strcmp(macStr, allowedMAC[i]) == 0) return;
      }
      // This MAC address is not allowed to connect
      DbgPrintln(F("Illegal MAC address, shutting down AP"));
      WiFi.softAPdisconnect(false);
      delay(1000);
      break;
    }
  }
}

class OpenHab {
public:
	struct Page {
		const char *pageId;
		const JsonVariant obj;
		Page *next;
	};
	struct ItemReference {
		const JsonVariant obj;			// Alternative object pointing to the same item
		const JsonVariant widgetObj;
#ifdef OPENHAB_GEN_CONFIG
		ItemReference *next;
#endif
	};
	struct Group;						// Advance declaration
	struct Item {
		const char *name;				// Ptr to item name (ArduinoJson object)
		ItemType type;					// Item type
		uint8_t referenceCount;			// 
		uint8_t groupIdx;				// For groups, index of Group data (groups)
		const char *state;				// Initial state of item
		const char* labelFormat;		// Label in C/C++ sprintf format
#ifdef OPENHAB_GEN_CONFIG
		Group *group;
		Item *next;
#endif
		//ItemType type : 15;	bool labelIsFormatted : 1;
	};
#ifndef OPENHAB_GEN_CONFIG
	struct ItemState {
		//const char *link;				
		char *state;					// Current state of item
		JsonVariant obj;				// Item object
		JsonVariant widgetObj;			// Widget object (which contains item)
		const ItemReference *references;// Pointer to a list of references to the same item
		//Page *page;
		const char *pageId;
	};
#endif
	
	struct Sitemap {
		const char *name;
		DynamicJsonDocument jsonDoc;
		Page *pageList;
		//Sitemap *next;
	};
	struct ItemList {
		Item* item;
		ItemList *next;		
	};

	struct Group {
#ifdef OPENHAB_GEN_CONFIG
		const char *name;
		Item *item;
#endif
		ItemType type;					// Type of all group member
		GroupFunction function;			// AVG or OR function
		uint8_t *items;					// Index List of all group member items
		uint8_t itemCount;				// Number of member items
#ifdef OPENHAB_GEN_CONFIG
		ItemList *itemList;
		Group *next;
#endif
	};

	struct Subscription {
		uint32_t clientIP;
		WiFiClient client;
		String uuidStr;
		const char *sitemap;
		const char *pageId;
		Ticker keepAliveTimer;
	};
	
	typedef std::function<void(uint8_t)> state_callback_function_t;

	OpenHab(const int port = 80);
	bool Init(const char *ssid, const char *APssid, const char* passphrase, const char* allowedMAC[] = {},
		const char *local_ip = "", const char *gateway = "", const char *subnet = "");
	void HandleClient();
	void stateChangeCallback(state_callback_function_t callback);

#ifdef OPENHAB_GEN_CONFIG
    void GenConfig(const char *OpenHabServer, const int port, const char *sitemap);
	void GetSitemap(const char *sitemap, String uriBase);
	void GenSitemap(const JsonVariant prototype, Sitemap *sitemap, size_t uriBaseLen);

#else
	//Item getItem(uint8_t itemIdx);
	void StartServer();
#endif

protected:
	ESP8266WebServer _server;
	ESP8266WebServer _SSEserver;
	const int _port;
	IPAddress _espIP;
	BufferedPrint<WiFiClient, 256> _print;
	Ticker _currentDateTimer;
	Sitemap *_sitemapList;
	Item *_itemList;

	Group *_groupList;			
	state_callback_function_t _callback_function;
	Subscription _subscription[SSE_MAX_CHANNELS];
	uint8_t _subscriptionCount;
	struct tm *_currentDateTime;
	const char **_allowedMAC;

	char *getCurrentDateTime();

#ifdef OPENHAB_GEN_CONFIG
	Group *newGroup(const char *name, Item *item, JsonObject obj, ItemType type);
	Group *addItemToGroup(const char *name, Item *item, JsonObject obj);
	Group *getGroup(const char *name);
	Item *getItem(const char *name);	
#else
	void SSEHandler(Subscription *subscription);
	void SSEBroadcastPageChange(ItemState &itemState);
	void SSEKeepAlive(Subscription *subscription);
	uint8_t getItemIdx(const char *name);
	void updateItem(const JsonVariant itemObj, const JsonVariant widgetObj, const char *pageId);
	float functionAVG(uint8_t *groupItems, uint8_t count);
	const char *functionOR(uint8_t groupIdx, uint8_t itemIdx, int *numItems);
	void setGroupState(const char *groupName);
	void setState(const char *itemName, const char *state, bool updateGroup);
	void setState(uint8_t itemIdx, const char *state, bool updateGroup = true);
	void setState(uint8_t itemIdx, float f, uint8 precision = 6, bool updateGroup = true);
	//void setState(uint8_t itemIdx, int i, bool updateGroup = true);
	void updateLabel(uint8_t itemIdx, char *state, ItemType type, int numItems = -1);
#endif
	//Item *getItem(const char *name);

	void SendJson(JsonVariant obj);
	void SendFile(String fname, ContentType contentType = APP_JSON, const char *pre = nullptr, const char *post  = nullptr);
	void GetSitemapsFromFS();

	void registerLinkHandlers(const JsonObject obj, const char *pageId, Sitemap *sitemap);
	void handleSitemaps(Sitemap *sitemapList);
	void handleAll(), handleSubscribe();
	void handleSitemap(const char *uri);
	void handleIcon(const char *uri);
	void handleNotFound(ESP8266WebServer &server);
	void handleSSEAll();
	void handleItem(const char *uri);
	//DynamicJsonDocument getJsonDocFromFile(File f); // DeserializationError& error);
	DynamicJsonDocument getJsonDocFromFile(String fileName); // DeserializationError& error);
	JsonVariant getNestedMember(const JsonVariant prototype, const char *key);
	//JsonVariant cloneInPlace(JsonBuffer& jb, JsonVariant prototype);
	//ICACHE_FLASH_ATTR JsonObject containsNestedKey(const JsonObject obj, const char* key);
	ItemType getItemType(const char *itemTypeStr) {
		unsigned int middle, first = 0, last = sizeof(ItemTypeStr)/sizeof(ItemTypeStr[0]) - 1;
		do {
			middle = (first + last) >> 1;
			int cmp = strcmp_P(ItemTypeStr[middle], itemTypeStr);
			if (cmp == 0) return (ItemType) middle; //(1 << --middle);
			else if (cmp < 0) first = middle + 1;
			else last = middle - 1;
		} while (first <= last);
		return ItemNull;
	};
	
	#ifdef OpenHABDebug
	ICACHE_FLASH_ATTR void DbgPrintRequest(ESP8266WebServer &server, String str) {
		String message = F("\nDbgPrintRequest - URI: ");
		message += server.uri(); message += F(" - METHOD: ");
		message += HTTPMethodStr[server.method()];	message += F("  - ARGUMENTS: ");
		for (uint8_t i = 0; i < server.args(); i++) {
			message += server.argName(i); message += F(" = ");
			message += server.arg(i); message += F("; ");
		}
		DbgPrintln(str, message);
	}
	#else
	#define DbgPrintRequest(x,y)	{}
	#endif
};

OpenHab::Item getItem(uint8_t itemIdx);

#endif // OpenHABInclude