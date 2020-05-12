#include "OpenHAB.h"
// Following two variables are defined in main.h and included in main.cpp
extern const uint8_t itemCount;
extern const uint8_t groupCount;
extern OpenHab::Item items[];
extern OpenHab::ItemReference *itemRef[];
//extern OpenHab::ItemState itemStates[];
extern char *itemStates[];
extern OpenHab::Group groups[];

//OpenHab::OpenHab(Item *items, unsigned short itemCount, const int port) : 
OpenHab::OpenHab(const int port) : 
	_port(port),
	_sitemapList(nullptr),
	_itemList(nullptr),
	_groupList(nullptr),
	_server(port),
	_print(_server.client()),
	_callback_function(nullptr),
	_subscriptionCount(0)
	{}

void OpenHab::stateChangeCallback (state_callback_function_t callback) {
    _callback_function = std::move(callback);
}

OpenHab::Item getItem(uint8_t itemIdx) {
	static OpenHab::Item item;
	memcpy_P(&item, &items[itemIdx], sizeof(OpenHab::Item));
	return item;
}

uint8_t OpenHab::getItemIdx(const char *name) {
	uint8_t middle, first = 0, last = itemCount;
	do {
		middle = (first + last) >> 1;
		int cmp = strcmp(items[middle].name, name);
		if (cmp == 0) return middle;
		else if (cmp < 0) first = middle + 1;
		else last = middle - 1;
	} while (first <= last);
	return (uint8_t) 255;		
};

const char *OpenHab::functionOR(uint8_t groupIdx, uint8_t itemIdx, uint8_t *numItems) {
	int count = 0;
	DbgPrint(F(" - functionOR"));
	//ItemState &itemState = itemStates[itemIdx];
	//char *state = itemStates[itemIdx];
	//OpenHab::Item item = getItem(itemIdx);			// Create a Item copy from flash	
	JsonObject f = itemRef[itemIdx][0].obj[F("function")];
	JsonArray fparams = f[F("params")];
	const char *fvalue = fparams[0];
	Group &group = groups[groupIdx];
	DbgPrint(F(" - value: "), fvalue); DbgPrint(F(" - itemCount: "), group.itemCount);
	uint8_t *groupItems = group.items;
	for (uint8_t n = 0; n < group.itemCount; n++) {
		uint8_t itemIdx = groupItems[n];
		char *state = itemStates[itemIdx];
		DbgPrint(F(" - name: "), items[itemIdx].name);	DbgPrint(F(" - state: "), state);		
		if (state && strcmp(fvalue, state) == 0) count++;
	}
	DbgPrintln(F(" - final count: "), count);
	*numItems = count;
	return (char *)(count > 0) ? fvalue : fparams[1].as<const char *>();
}

FORCE_INLINE float OpenHab::functionAVG(uint8_t *groupItems, uint8_t count) {
	DbgPrint(F(" - functionAVG"));
	float avg = 0;
	for (uint8_t n = 0; n < count; n++)
		avg += strtof(items[groupItems[n]].state, nullptr);
	DbgPrintln(F(" - avg: "), (count) ? avg / count : (float)0.0);
	return (count) ? avg / count : (float)0.0;
}

static size_t snprintftime(char *state, char *label, const char *labelFormat, int count = 0) {
	static char buf[40];    
	tm tmb;
	strptime(state, "%Y-%m-%dT%H:%M:%S", &tmb);
	return (count == 0) ? strftime(buf, 40, labelFormat, &tmb) : strftime(label, count, labelFormat, &tmb);
}		
FORCE_INLINE static size_t snprintftime(float state, char *label, const char *labelFormat, int count = 0) { return 0; }
FORCE_INLINE static size_t snprintftime(int state, char *label, const char *labelFormat, int count = 0) { return 0; }

// Set label of all references to given item
inline void setReferenceLabel (const OpenHab::ItemReference *refList, uint8_t count, const char *label) {
	for (uint8_t n = 1; n < count; n++) 
		refList[n].widgetObj[F("label")] = label;
}

template <typename T> static void sprf(T state, uint8_t itemIdx) {
	//static char _prevLabel[128];
	DbgPrint(F(" --> sprf "));	
	OpenHab::Item item = getItem(itemIdx);			// Create a Item copy from flash
	OpenHab::ItemReference *itemref = itemRef[itemIdx];
	//OpenHab::ItemState &itemState = itemStates[itemIdx];
	const char *labelFormat = item.labelFormat; // formatted label
	JsonVariant labelObj = itemref[0].widgetObj[F("label")];
	char *label = (char *)(labelObj.as<const char *>());  //pointer to last formatted label or nullptr initially
	//size_t len = (label) ? strlen(label) : 0;
	//if (len == 0) label = nullptr;
	//if (len) strncpy(_oldLabel, label, len); 
	//_oldLabel[len] = 0; // save a copy of old label, assuming < 128 characters
	DbgPrint(F(" - previous Label: "), label);
	size_t count = (item.type == ItemDateTime) ? snprintftime(state, label, labelFormat) : snprintf(NULL, 0, labelFormat, state);
	label = (char *) realloc((void *)label, ++count);
	(item.type == ItemDateTime) ? snprintftime(state, label, labelFormat, count) : sprintf(label, labelFormat, state);
	labelObj.set((const char *)label); // write back pointer to updated label
	if (item.refCount > 0) 
		setReferenceLabel(itemref, item.refCount, label);
	DbgPrintln(F(" - new label: "), label);
}

void OpenHab::updateLabel(uint8_t itemIdx, char *state, ItemType type, int numItems) {
	DbgPrint(F(" - updateLabel"));	DbgPrint(F(" - type: "), ItemTypeStr[type]);
	DbgPrint(F(" - numItems: "), numItems);	DbgPrint(F(" - state: "), state);
	switch (type) {
		case ItemContact:
		case ItemString:
		case ItemSwitch:
		case ItemDateTime:
			sprf(state, itemIdx); break;
		case ItemDimmer:
		case ItemNumber:
		case ItemNumber_Angle:
			sprf(strtof((state) ? state : "0", NULL), itemIdx); break; // (float)atof(item->state)
		case ItemGroup:
			(numItems == -1) ? sprf(atoi(state), itemIdx) : sprf(numItems, itemIdx); break;
		default:;
	}

	//if (_subscriptionCount) SSEBroadcastItemChange(itemStates[itemIdx]);
}

FORCE_INLINE void OpenHab::setGroupState(const char *groupName) {
	DbgPrintln(F(" - setGroupState: "), groupName);
	uint8_t itemIdx = getItemIdx(groupName);
	if (! isValidItemIndex(itemIdx)) return;	// Invalid item
	Item item = getItem(itemIdx);		// Create a Item copy from flash
	if (item.type != ItemGroup) return; // Item is not a group
	//ItemState &itemState = itemStates[itemIdx];
	char *state = itemStates[itemIdx];
	Group &group = groups[item.groupIdx];
	if (group.function == FunctionAVG) {
		setState(itemIdx, functionAVG(group.items, group.itemCount), false);
		updateLabel(itemIdx, state, group.type);
	} else if (group.function == FunctionOR) {
		uint8_t count;
		char buf[4];	// holds uint8_t
		setState(itemIdx, functionOR(item.groupIdx, itemIdx, &count), false);
		sprintf_P(buf, PSTR("%d"), count);
		updateLabel(itemIdx, buf, item.type, count);
	}
	if (_subscriptionCount) SSEBroadcastItemChange(state, "", itemIdx);
}

void OpenHab::setCurrentDateTime(uint8_t itemIdx, char *state) {
	char prevState[24];
	strcpy(prevState, state);
	time_t now = time(nullptr); // Time since epoch
	const tm* tm = localtime(&now); // Converted to localtime
	strftime (state, 22 ,"%FT%T", tm);  // Converted to an ISO date string
	DbgPrintln(F("setCurrentDateTime: "), state);
	auto setStateBind = std::bind(static_cast<void(OpenHab::*)(uint8_t, char*)>
		(&OpenHab::setCurrentDateTime), this, itemIdx, state);
	if (!_currentDateTimer.active())
		_currentDateTimer.attach(60.0, setStateBind);  // Refresh time every minute (could be optimized in the future)
	if (_subscriptionCount) SSEBroadcastItemChange(state, prevState, itemIdx);
}

// Function to set and eventually propagate (e.g. label, group state) the state of an item 
void OpenHab::setState(uint8_t itemIdx, const char *state, bool updateGroup) {
	char prevState[64], intState[4];
	auto handleUpDownState = [&](const char *state, size_t value) {
		if (strcmp_P(state, PSTR("UP")) == 0) value = (value >= 90) ? 100 : value + 10;
		else if (strcmp_P(state, PSTR("DOWN")) == 0) value = (value <= 10) ? 0 : value - 10;
		else value = atoi(state);
		sprintf_P(intState, PSTR("%d"), value);
		DbgPrint(F(" - setRollerShutterState value: "), intState);
		return intState;
	};

	DbgPrint(F(" - setState itemIdx: "), itemIdx);
	DbgPrint(F(" - requested state: "), state);
	//if (!state) return;							// NULL states can crash setState
	//ItemState &itemState = itemStates[itemIdx];	// Get current (previous) state
	Item item = getItem(itemIdx);				// Get Item copy from flash, reading direcly from flash causes misaligned read crashes
	ItemReference *itemref = itemRef[itemIdx];

	char *itemState = itemStates[itemIdx];	// Get current (previous) state
	size_t len = (itemState) ? strlen(itemState) + 1: 0;  // Length of old state in order to make a copy
	prevState[0] = '\0';
	strncpy(prevState, itemState, min(len, (size_t)sizeof(prevState)));
	DbgPrint(F(" - previous state: "), prevState);
	if ((item.type == ItemRollerShutter) || (item.type == ItemColor)) state = handleUpDownState(state, atoi(prevState));
	bool isCurrentDateTime = ((item.type == ItemDateTime) && !strncmp_P(item.name, PSTR("Current"), 7)); // item name reflecting current Date/Time?

	len = (isCurrentDateTime) ? 24 : strlen(state) + 1;
	void *p = realloc((void *) itemState, len);	// New state might require more space
	if (isCurrentDateTime) setCurrentDateTime(itemIdx, (char *) p);	// This is a request to supply the current date and time
	else if (state) memcpy(p, state, len);							// Replicate the new state
	DbgPrint(" - new state: ", (char *) p);
	itemState = (char *) p;

	itemref[0].obj[F("state")] = (const char *) p;	// Update base reference
	for (uint8_t n = 1; n <= item.refCount; n++) { 	// Update all other references, if present
		DbgPrint(F(" - updating reference: "), item.name);
		DbgPrint(F(" with state: "), (const char *)p );
		itemref[n].obj[F("state")] = (const char *) p;
	}

	DbgPrintln("");
	JsonArray groupNames = itemref[0].obj[F("groupNames")];
	for (JsonVariant groupName : groupNames)
		setGroupState(groupName);

	if ((item.type != ItemGroup) && item.labelFormat) updateLabel(itemIdx, (char *)p, item.type);  // finally update the parent label reflecting the new state
	if (_subscriptionCount) SSEBroadcastItemChange((char *) p, prevState, itemIdx);
}

// Function to set and eventually propagate (e.g. label, group state) the state of an item with numeric state (e.g. type Number)
FORCE_INLINE void OpenHab::setState(uint8_t itemIdx, float f, bool updateGroup, uint8 precision) {
    char fs[14];  // Used to convert float to string
	DbgPrintln(F(" - setState float: "), f);
	sprintf_P(fs, "%.*f", precision, f);
	setState(itemIdx, fs, updateGroup);
}

void OpenHab::setState(const char *itemName, const char *state, bool updateGroup) {\
	uint8_t itemIdx = getItemIdx(itemName);
	if (isValidItemIndex(itemIdx))
		return setState(itemIdx, state, updateGroup);
	Serial.printf_P(PSTR("PANIC! Invalid item %s\n"), itemName);
}

// Adds an Item to the itemList and set its initial state as well as any label deduced from that state
// If the item is a group, add the group
// If the itme is member of a group, add the group when not yet existing and include the item as member of the group
FORCE_INLINE void OpenHab::updateItem(const JsonVariant itemObj, const JsonVariant widgetObj, const char*pageId) {
	const char *name = itemObj[F("name")];
	DbgPrint(F("item name: "), name); 
	
	uint8_t itemIdx = getItemIdx(name);
	if (! isValidItemIndex(itemIdx)) {
		Serial.printf_P(PSTR("PANIC: item %s not found!!\n"), name);
		return;
	}
	// At this point we have an item, but we need to update it
	Item item = getItem(itemIdx);			// Create a Item copy from flash
	ItemType itemType = item.type;
	ItemReference *itemref = itemRef[itemIdx];
	const char* state = item.state;			// Get initial state from flash
	itemStates[itemIdx] = (state) ? strdup(state) : nullptr;		// and duplicate (no need for _P)
	JsonVariant labelObj = widgetObj[F("label")];
	const char *label = labelObj; 
	if (item.labelFormat) labelObj.set(nullptr);		// Initialize formatted label to nullstr
	DbgPrint(F(" - itemIdx: "), itemIdx); DbgPrint(F(" - type: "), itemType);	DbgPrint(F(" - state: "), item.state); 
	DbgPrint(F(" - label: "), label); DbgPrint(F(" - pageId: "), pageId); DbgPrint(F(" - refCount: "), item.refCount);

	uint8_t refCount = item.refCount + 1;
	if (!itemref)
		itemRef[itemIdx] = itemref = (ItemReference *)calloc(refCount, sizeof(ItemReference));
	for (uint8_t n = 0; n < refCount; n++) {
		if (!itemref[n].pageId) { 	// first empty entry
			DbgPrint(F(" - reference slot: "), n);
			const ItemReference ref = { pageId, itemObj, widgetObj };
			memcpy((void *)&(itemref[n]), (void *)&ref, sizeof(ItemReference));
			break;
		}
	}
	DbgPrintln(F(""));
}

void OpenHab::registerLinkHandlers(const JsonObject obj, const char *pageId, Sitemap *sitemap) {
	static Page *lastPage = nullptr;
	for (const JsonPair pair : obj) {
		const char *key = pair.key().c_str();
		if (!strcmp_P(key, PSTR("linkedPage")) || !strcmp_P(key, PSTR("homepage"))) { // Page
			JsonObject pageObj = pair.value().as<JsonObject>();
			pageId = pageObj[F("id")];
			DbgPrintln(F("page: "), pageId);
			Page *page = new Page {pageId, (JsonVariant) pageObj, nullptr};
			if (lastPage) lastPage->next = page;
			else { // Homepage, set sitemap name
				sitemap->pageList = page;
				sitemap->name = pageId;
			}
			lastPage = page;
		} else if (!strcmp_P(key, PSTR("item"))) // Items
			updateItem(pair.value().as<JsonObject>(), obj, pageId);

		registerLinkHandlers(pair.value().as<JsonObject>(), pageId, sitemap); //, pair.key);
		for (auto arr : pair.value().as<JsonArray>()) registerLinkHandlers(arr.as<JsonObject>(), pageId, sitemap); //, pair.key);
	}
}

DynamicJsonDocument OpenHab::getJsonDocFromFile(String fileName) {
	DbgPrint(F("getJsonDocFromFile: file name: "), fileName);
	DbgPrint(F(" - free heap memory @entry: "), ESP.getFreeHeap());
	File f = SPIFFS.open(fileName, "r");
	DynamicJsonDocument doc(ESP.getMaxFreeBlockSize() - 4096);
	//DbgPrintln(F(" - allocated: "), doc.capacity());
	//DbgPrintln(F("after allocation of Json doc: "));
	DeserializationError err = deserializeJson(doc, f, DeserializationOption::NestingLimit(20));
	//DbgPrintln(F(" - deserializeJson: "), doc.memoryUsage());
	if (err == DeserializationError::Ok) doc.shrinkToFit();
	else DbgPrintln(F("deserializeJson failed "), err.c_str());
	f.close();
	DbgPrintln(F(" - free heap memory @exit: "), ESP.getFreeHeap());
	return doc;
}

bool OpenHab::Init(const char *ssid, const char *APssid, const char* passphrase, const char*allowedMAC[],
	const char *local_ip, const char *gateway, const char *subnet) {
 	DbgPrintln(F("InitServer"));
	IPAddress espIP;

	// Setup WiFi network
 	WiFi.persistent(false);	// Disables storing the SSID and pasword by SDK
	WiFi.setSleepMode(WIFI_LIGHT_SLEEP); //WIFI_NONE_SLEEP);
	WiFi.setOutputPower(17);        // 10dBm == 10mW, 14dBm = 25mW, 17dBm = 50mW, 20dBm = 100mW
	WiFi.setAutoConnect(false);	// Disables the auto connect of the SDK
  	WiFi.softAPdisconnect(true);
	WiFi.enableAP(false);
  	WiFi.mode(WIFI_STA);
	WiFi.disconnect();
	WiFi.hostname(APssid);
	delay(1000); 

	/*// Don't save WiFi configuration in flash - optional
	  int32_t  channel (void)
	  WiFiSleepType_t  getSleepMode ()
	  bool  setPhyMode (WiFiPhyMode_t mode)
	  WiFiPhyMode_t  getPhyMode ()
	  void  setOutputPower (float dBm)
	  WiFiMode_t  getMode ()
	  bool  forceSleepBegin (uint32 sleepUs = 0)
	  bool  forceSleepWake ()
	  int  hostByName (const char *aHostname, IPAddress & aResult)

	DbgPrintln("Scan start ... ");
  	int n = WiFi.scanNetworks();
  	DbgPrintln("network(s) found: ", n);
  	for (int i = 0; i < n; i++) DbgPrintln("-- ", WiFi.SSID(i));
	*/	
	int len = strlen(passphrase);
	char *_passphrase = strdup(passphrase);
	for (int i = 0, j = 1; i < len; i += 2, j += 2) {
		std::swap(_passphrase[i], _passphrase[j]);
	}
	char *pwdDecoded = (char *)calloc(len + 1, sizeof(char));
	base64_decode_chars(_passphrase, len - 1, pwdDecoded);
	delete _passphrase;
	DbgPrintln(F("passcode: "), pwdDecoded);

	if (ssid) { // We have an SSID, connect to this AP
		if (!WiFi.enableSTA(true)) { //enable STA failed
			DbgPrintln(F("could not enable station mode"));
		} else { 
			wifi_station_set_hostname(APssid);
			WiFi.begin(ssid, pwdDecoded);
			//while (WiFi.status() != WL_CONNECTED) delay(500);
			if (WiFi.waitForConnectResult() == WL_CONNECTED) {
				DbgPrint(F("Connected to AP "), ssid);
				DbgPrint(F(" with MAC:"), WiFi.macAddress());
				DbgPrintln(F("and name:"), WiFi.hostname());
				_habPanelWeb = PSTR("http://");
				_habPanelWeb += WiFi.gatewayIP().toString();
				_habPanelWeb += PSTR(":");
				_habPanelWeb += _port;
				WiFi.gatewayIP().toString();
				_espIP = WiFi.localIP();
				configTime(TZ_Europe_Brussels, "0.be.pool.ntp.org", "1.be.pool.ntp.org"); // If on AP, get time
				//configTime(TZ_Etc_GMTm7, "pool.ntp.org"); // If on AP, get time
				int count = 100;
				while((time(nullptr) <= 100000) && count--) delay(100);
				if (count < 0) Serial.println(PSTR("Time server not reachable"));
				else Serial.println(F("Time from time server"));
				goto out;
			}
		}
	}
	// Fallback to SoftAP if no connection to AP
	DbgPrintln(F("Falling back to soft AP due to failed connection to AP "), ssid);
  	WiFi.mode(WIFI_AP);
  	WiFi.enableSTA(false);
	WiFi.disconnect(true);
	if (APssid) { // We  stat
		if (!WiFi.enableAP(true)) { //enabling of Soft AP failed
			DbgPrintln(F("could not create Access Point"));
			return false;
		}
		IPAddress local_IP(192,168,4,1);
    	IPAddress gateway(192,168,4,1);
    	IPAddress subnet(255,255,255,0);

		wifi_station_set_hostname(APssid);
		if (!WiFi.softAPConfig(local_IP, gateway, subnet))
		DbgPrintln(F("could not configure Access Point"));
		WiFi.softAP(APssid, pwdDecoded, 13, false, 4);
    	MDNS.begin(APssid);
		wifi_station_set_hostname(APssid);
    	WiFi.hostname(APssid);
		DbgPrintln(F("Soft AP name"), WiFi.hostname());
		if (local_ip) //Set local AP IP
			WiFi.softAPConfig(local_IP, gateway, subnet);
		_espIP = WiFi.softAPIP();
	}
out:
	delete pwdDecoded;
	DbgPrintln(F("IP address: "), _espIP.toString());
	delay(100);
	//wifi_set_event_handler_cb(wifiEventHandler);

	SPIFFS.begin();
	DbgPrintln(F("Loading sitemap"));
	GetSitemapsFromFS();

	DbgPrintln(F("Initializing states and labels"));	// Second pass set state
	for(uint8_t n = 0; n < itemCount; n++) {
		DbgPrint(F("init state: "), n);
		DbgPrintln(F(" - name : "), items[n].name);
		setState(n, items[n].state);
	}

	DbgPrintln(F("Exit OpenHab::InitServer"));
	return true;
}

void OpenHab::HandleClient() {
	_server.handleClient();
}

// Start the web server, handle all requests in a single function to save memory
void OpenHab::StartServer() { //
	_server.on(F("/"), std::bind(&OpenHab::handleAll, this));
	_server.onNotFound(std::bind(&OpenHab::handleAll, this));
	_server.begin();
}

void OpenHab::GetSitemapsFromFS() {
	DbgPrintln(F("GetSitemapsFromFS"));
	DbgPrintln(F("free heap memory: "), ESP.getFreeHeap());
	Dir dir = SPIFFS.openDir(F("/conf/sitemaps"));
	DbgPrintln(F("isDirectory"), dir.isDirectory());
	while (dir.next()) {
		String fileName = dir.fileName(); 
		DbgPrintln(F("GetSitemapsFromFS - file"), fileName);
		_sitemapList = new Sitemap{nullptr, getJsonDocFromFile(fileName), nullptr}; //, _sitemapList
		registerLinkHandlers(_sitemapList->jsonDoc.as<JsonObject>(), "", _sitemapList);
		break; //At this moment, allow only one sitemap
	}
	DbgPrintln(F("free heap memory @exit: "), ESP.getFreeHeap());
}

void OpenHab::SendJson(JsonVariant obj) {
	int size = measureJson(obj);
	DbgPrintln(F("SendJson "), size);	
	_server.setContentLength(size);
	_server.send_P(200, getContentTypeStr(APP_JSON), NULL);
	_print.client(_server.client());
	serializeJson(obj, _print);
	_print.stop(_server.client());
 	//DbgPrintln(F(" - free heap memory @exit: "), ESP.getFreeHeap());
}

void OpenHab::SendFile(String fname, ContentType contentType, const char *pre, const char *post) {
	char buf[536];
	bool isHabPanel = fname.startsWith(F("/habpanel/"));
	bool isStatic = fname.startsWith(F("/static/"));
	String location = _habPanelWeb + fname;
	if (isHabPanel) fname = fname.c_str() + sizeof("habpanel");

	DbgPrintln(F("SendFile "), fname);
	if (SPIFFS.exists(fname)) {
		File f = SPIFFS.open(fname, "r");
		size_t sz = f.size();
		//DbgPrintln(F("file size "), sz);
		//_server.sendHeader(F("Access-Control-Allow-Origin"), _habPanelWeb, true);
		_server.setContentLength((pre || post) ? CONTENT_LENGTH_UNKNOWN : sz);
		//DbgPrintln(F("file type "), getContentTypeStr(contentType));
		if (contentType == AUTO) {
			String ext = fname.substring(fname.lastIndexOf('.'));
			contentType = getContentType(ext.c_str());
			DbgPrintln(F("AUTO extension:"), ext.c_str());			
		}
	 	_server.send_P(200, getContentTypeStr(contentType), PSTR(""));
		//DbgPrintln(F("before pre test"));
		if (pre) _server.sendContent_P(pre);
		while (sz > 0) {
			size_t len = std::min((size_t) sizeof(buf) - 1, sz);
			//DbgPrintln(F("len "), len);
			len = f.readBytes(buf, len);
			//DbgPrintln(F("len from readbytes "), len);
			_server.sendContent_P((const char*)buf, len);
			//DbgPrintln(F("back from sendcontent"));
			sz -= len; 
		}
		if (post) {
			_server.sendContent_P(post);
			_server.sendContent_P(PSTR(""));
		}
		f.close();
	} else if (isHabPanel || isStatic) {
		/*
		WiFiClient client;
		HTTPClient httpClient;  //Declare an object of class HTTPClient
		httpClient.begin(client, location);
		fname = fname.substring(fname.lastIndexOf('/'));
		//DbgPrintln(F("short fname: "), fname);		
		int httpCode = httpClient.GET();                  //Send the request
		DbgPrintln(F(" - http code: "), httpCode);
		if (httpCode == 200) { //Check the returning code
			File wfile = SPIFFS.open(fname, "w");
			httpClient.writeToStream(&wfile);
			wfile.close();
			httpClient.end();		//Close connection
			SendFile(fname, AUTO);
			SPIFFS.remove(fname);
		} else {
			httpClient.end();		//Close connection
			return handleNotFound();
		}
		*/
		DbgPrint(F("Redirecting to: "), location);
		String origin = F("http://");
		origin += _espIP.toString();
		origin += ":";
		origin += _port;
		if (contentType == AUTO) {
			String ext = fname.substring(fname.lastIndexOf('.'));
			DbgPrintln(F("AUTO extension:"), ext.c_str());			
			contentType = getContentType(ext.c_str());
		}
		DbgPrintln(F(" - with contentType: "), getContentTypeStr(contentType));
		_server.sendHeader(F("Access-Control-Allow-Origin"), PSTR("*"), true);
		_server.sendHeader(F("Location"), location, true);
	 	_server.send_P(302, getContentTypeStr(contentType), PSTR(""));
	} else DbgPrintln(F("File not found"));
}

ICACHE_FLASH_ATTR void OpenHab::handleNotFound() {
	DbgPrintRequest(F("handleNotFound"));
	_server.send(404, F("text/plain"), _server.responseCodeToString(404));
}

// While the data structures allow for multiple sitemaps (sitemap list), only one is supported for now
ICACHE_FLASH_ATTR void OpenHab::handleSitemaps(Sitemap *sitemap) {
	DbgPrintln(F("handleSiteMaps"));
	String fileName = F("/conf/sitemaps/");
	fileName += sitemap->name; 
	SendFile(fileName, APP_JSON, PSTR("["), PSTR("]"));
}

void OpenHab::handleSitemap(const char *uri) {
	//DbgPrintRequest(F("handleSitemap"));
	while (uri && *uri++ != '/');
	DbgPrintln(F("handleSitemap - "), uri);

	Sitemap *sitemap = _sitemapList;
		Page *page = sitemap->pageList;
		while (page) {
			//DbgPrintln(F("page: "), page->pageId);
			if (strcmp(page->pageId, uri) == 0) 
				return SendJson(page->obj);
			page = page->next;
		}
	handleNotFound(); 
}

void OpenHab::handleItem(const char *uri) {
	uint8_t itemIdx = getItemIdx(uri);
	if (!isValidItemIndex(itemIdx)) return handleNotFound();
	DbgPrint(F("handleItem - "));
	if (_server.method() == HTTP_GET) return SendJson(itemRef[itemIdx][0].obj);
	else if (_server.method() == HTTP_POST) {  // Item has changed
		DbgPrintRequest(F("handleItem - POST request: "));
		//if (_server.hasArg(F("plain"))) { //Check if body received as plain argument
			//const char *state = _server.arg(F("plain")).c_str();
			const char *state = _server.arg(0).c_str();
			setState(itemIdx, state);		// change state and labels
			DbgPrint(F(" - new state:"), state);
			//_callback_function(itemIdx);	// call back user code
		//}
		_server.send_P(200, PSTR("text/plain"), NULL);
	}
	DbgPrintln(F(" - free heap memory @exit: "), ESP.getFreeHeap());
}

void OpenHab::handleIcon(const char *uri) {
	auto getIconType = [&](const char *type) {
		for (uint8_t n = 0; n < 4; n++)
			if (strcmp_P(type, ContentTypeArg[n]) == 0) return (ContentType) n;
		return (ContentType) 4;
	};

	char fileName[32];
	//DbgPrintRequest(_server, F("icon"));
	String s = _server.arg(F("state")); s.toLowerCase();
	const char *state = s.c_str();
	const char *format = _server.arg(F("anyFormat")).c_str();
	bool anyformat = (strlen(format)) ? (strcmp_P(format, PSTR("true")) == 0) : true;
	ContentType type = (anyformat) ? IMAGE_DEFAULT : getIconType(_server.arg(F("format")).c_str());
	const char *ext = getContentTypeExt(type);
	sprintf_P(fileName, PSTR("%s-%s%s"), uri, state, ext);
	//DbgPrintln(F("type: "), type);
	if (SPIFFS.exists(fileName)) return SendFile(fileName, type);
	unsigned short value = (atoi(state) / 10) * 10; // round to a multiple of 10
	sprintf_P(fileName, PSTR("%s-%d%s"), uri, value, ext);
	if (SPIFFS.exists(fileName)) return SendFile(fileName, type);
	sprintf_P(fileName, PSTR("%s%s"), uri, ext);
	SendFile(fileName, type);
}

void OpenHab::handleSubscribe() {
	byte uuid[16];
	char buf[96];
	uint8_t channel;

	DbgPrint(F("HandleSubscribe"));
	//DbgPrintRequest(F("subscribe: "));
	File f = SPIFFS.open(F("/conf/subscribe"), "r");
	StaticJsonDocument<450> jsonDoc;	// Allocate on the stack to prevent heap fragmentation
	DeserializationError err = deserializeJson(jsonDoc, f);
	f.close();
	if (err) { 
		DbgPrintln(F(" - deserializeJson failed "), err.c_str());
		return;
	}
	IPAddress clientIP = _server.client().remoteIP();
	sprintf_P(buf, PSTR("http://%s:%d/rest/sitemaps/events/"), _espIP.toString().c_str(), _port);
	ESP8266TrueRandom.uuid(uuid);

	//Does this IP address already have a subscription. If so, reuse channel
	//for (channel = 0; channel < SSE_MAX_CHANNELS; channel++)
	//	if (_subscription[channel].clientIP == (uint32_t) clientIP)	goto initSubscription;

	// Allocate a new channel, if available
	if (_subscriptionCount == (SSE_MAX_CHANNELS - 1))
		return handleNotFound();  // We ran out of channels
	++_subscriptionCount;
	for (channel = 0; channel < SSE_MAX_CHANNELS; channel++) // Find first free slot
		if (!_subscription[channel].clientIP) break;

	_subscription[channel] = {(uint32_t) clientIP, _server.client(), ESP8266TrueRandom.uuidToString(uuid), nullptr, nullptr, Ticker()};
	strcat(buf, _subscription[channel].uuidStr.c_str());
	DbgPrint(F(" - allocated channel "), channel);	
	DbgPrintln(F(" on uri "), buf);	

	JsonObject context = jsonDoc.as<JsonObject>()[F("context")];
	JsonArray location = context[F("headers")][F("Location")];
	location[0] = (const char *) buf;
	SendJson(jsonDoc.as<JsonVariant>());
	jsonDoc.clear();
	DbgPrintln(F(" - free heap memory @handlesubscribe exit: "), ESP.getFreeHeap());
}

void OpenHab::SSEdisconnect(WiFiClient client, Subscription &subscription) {
	Serial.print(PSTR(" - client no longer connected, remove subscription for page: "));
	Serial.println(subscription.pageId);
	client.flush();
	client.stop();
	subscription.clientIP = 0;
	subscription.keepAliveTimer.detach();
	_subscriptionCount--;
}

void OpenHab::SSEBroadcastItemChange(const char *state, const char *prevState, uint8_t itemIdx) {
	Item item = getItem(itemIdx);			// Create a Item copy from flash
	ItemReference *itemref = itemRef[itemIdx];
	//DbgPrint(F("SSEBroadcastItemChange for item: "), item.name);
	//DbgPrintln(F(" - with (new) state: "), state);
	for (uint8_t channel = 0; channel < SSE_MAX_CHANNELS; channel++) {
		//DbgPrintln(F("channel: "), channel);
		if (!_subscription[channel].clientIP) continue;	// Skip unallocated channels
	    WiFiClient client = _subscription[channel].client;
    	String IPaddrstr = IPAddress(_subscription[channel].clientIP).toString();
    	if (client.connected()) {	// Only broadcast if client still connected
			if (_subscription[channel].pageId) {	// Basic UI
				uint8_t refCount = item.refCount;
				for (uint8_t n = 0; n <= refCount; n++)
					if (!strcmp(_subscription[channel].pageId, itemref[n].pageId)) { // finds a matching pageId
						String itemStr;
						DbgPrint(F("Broadcast Basic UI page change to client IP: "), IPaddrstr.c_str());
						DbgPrint(F(" on channel: "), channel);
						DbgPrintln(F(" with pageid: "), itemref[n].pageId);
						//serializeJson(itemRef[n].obj, Serial); Serial.println("");

						serializeJson(itemref[n].obj, itemStr);
						int offset = itemStr.indexOf(F("\"state\":"));
						//DbgPrintln(F("item: "), itemStr.substring(offset));
						client.printf_P(PSTR("event: event\ndata: {\"widgetId\":\"%s\",\"label\":\"%s\",\"visibility\":true,\"item\":{%s,\"sitemapName\":\"%s\",\"pageId\":\"%s\"}\n\n"),
							itemref[n].widgetObj[F("widgetId")].as<const char *>(), itemref[n].widgetObj[F("label")].as<const char *>(), 
							itemStr.substring(offset).c_str(), _subscription[channel].sitemap, _subscription[channel].pageId);
						/*
						Serial.printf_P(PSTR("event: event\ndata: {\"widgetId\":\"%s\",\"label\":\"%s\",\"visibility\":true,\"item\":{%s,\"sitemapName\":\"%s\",\"pageId\":\"%s\"}\n\n"), \
							itemRef[n].widgetObj[F("widgetId")].as<const char *>(), itemRef[n].widgetObj[F("label")].as<const char *>(), \
							itemStr.substring(offset).c_str(), _subscription[channel].sitemap, _subscription[channel].pageId);
						*/
					}
			} else { // HabPanel UI
				const char *typeStr = itemref[0].obj[F("type")];
				DbgPrint(F("Broadcast HabPanel UI page change to client IP: "), IPaddrstr.c_str());
				client.printf_P(PSTR("event: message\ndata: {\"topic\":\"smarthome/items/%s/statechanged\",\"payload\":\"{\\\"type\\\":\\\"%s\\\",\\\"value\\\":\\\"%s\\\",\\\"oldType\\\":\\\"%s\\\",\\\"oldValue\\\":\\\"%s\\\"}\",\"type\":\"ItemStateChangedEvent\"}\n\n"),
					itemref[0].obj[F("name")].as<const char *>(), typeStr,
					itemref[0].widgetObj[F("label")].as<const char *>(), typeStr, prevState);
				/*
				Serial.printf_P(PSTR("event: message\ndata: {\"topic\":\"smarthome/items/%s/statechanged\",\"payload\":\"{\"type\":\"%s\",\"value\":\"%s\",\"oldType\":\"%s\",\"oldValue\":\"%s\"}\",\"type\":\"ItemStateChangedEvent\"}\n\n"), \
					itemref[0].widgetObj[F("widgetId")].as<const char *>(), typeStr, \
					itemref[0].widgetObj[F("label")].as<const char *>(), typeStr, prevState);
				*/
			}
		} else SSEdisconnect(client, _subscription[channel]);
	}
}

void OpenHab::SSEKeepAlive(Subscription &subscription) {
	DbgPrint(F("SSEKeepAlive for IP: "), subscription.clientIP);
    WiFiClient client = subscription.client;  
	if (client.connected()) {
		if (subscription.pageId) {
			DbgPrintln(F(" - client is still connected - pageId: "), subscription.pageId);
			client.printf_P(PSTR("event: event\ndata: { \"TYPE\":\"ALIVE\",\"sitemapName\":\"%s\",\"pageId\":\"%s\"}\n\n"), subscription.sitemap, subscription.pageId);
		} 
	} else SSEdisconnect(client, subscription);
}

void OpenHab::handleSSEAll(const char *uri, bool isHabPanel) {
	DbgPrintRequest(F("handleSSEAll"));
	if (strncmp_P(uri, PSTR("subscribe"), 9) == 0)
	return handleSubscribe();	//this is a sitemap event registration

	DbgPrint(PSTR("Event Stream Listener request: "), uri);
	WiFiClient client = _server.client();
	client.setNoDelay(true);
	IPAddress clientIP = client.remoteIP();
	String IPaddrstr = IPAddress(clientIP).toString();
	//DbgPrintln(F("clientIP from SSEserver: "), IPaddrstr.c_str());	
	//DbgPrintRequest(F("SSEHandler"));
	uint8_t channel;
	for (channel = 0; channel < SSE_MAX_CHANNELS; channel++)
		if (isHabPanel) {
			if (!_subscription[channel].clientIP) break;	// First free slot
		} else {	// Look for pre-registered slots
			if ((_subscription[channel].clientIP == (uint32_t) clientIP)) { //&&
				//DbgPrintln(F("Matching IP for channel: "), channel);				 
				//DbgPrintln(F("_subscription uuidstr: "), _subscription[channel].uuidStr.c_str());				 
				//DbgPrintln(F("uuid comparison: "), (_subscription[channel].uuidStr == uri));				 
				if (strncmp_P(_subscription[channel].uuidStr.c_str(), uri, 36) == 0) break;
			}
		}
	if (channel == SSE_MAX_CHANNELS) {	// IP address and UUID did not match, reject this client
   		DbgPrintln(F(" - No free slots or unregistered client attempts to listen\n"));
	   	return handleNotFound();
 	}
	DbgPrintln(F(" - reserving channel: "), channel);	
	_subscription[channel].client = client; // update client

	if (isHabPanel) {
		_subscriptionCount++;
		_subscription[channel] = {(uint32_t) clientIP, _server.client(), PSTR(""), nullptr, nullptr, Ticker()};
	} else {
		_subscription[channel].sitemap =  (strcmp((_server.arg(0)).c_str(), _sitemapList->name)) ? nullptr : _sitemapList->name;
		DbgPrint(F("Sitemap: "), _subscription[channel].sitemap);	
		const char *pageId = (_server.arg(1)).c_str();
		Page *page = _sitemapList->pageList;
		while (page && (strcmp(page->pageId, pageId))) page = page->next;
		_subscription[channel].pageId = (page) ? page->pageId : nullptr;
		DbgPrintln(F(" - pageid: "), _subscription[channel].pageId);
	}

	_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
	_server.sendContent_P(PSTR("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream;\r\nConnection: keep-alive\r\nCache-Control: no-cache\r\nAccess-Control-Allow-Origin: *\r\n\r\n"));
	auto keepaliveBind = std::bind(static_cast<void(OpenHab::*)(Subscription &)>(&OpenHab::SSEKeepAlive), this, _subscription[channel]);
	_subscription[channel].keepAliveTimer.attach_scheduled((float)120.0, keepaliveBind);  // Refresh time every minute (could be optimized in the future)
}

// ESP webserver does not support wildcards, process image/icon requests here
void OpenHab::handleAll() {
	const char* uri = _server.uri().c_str();
	DbgPrint(F("OpenHab::handleAll: "), uri);
	IPAddress clientIP = _server.client().remoteIP();
	String IPaddrstr = IPAddress(clientIP).toString();
	DbgPrint(F(" - from IP address: "), IPaddrstr);
	DbgPrintln(F(" - free heap memory @entry: "), ESP.getFreeHeap());
	if (strncmp_P(uri, PSTR("/habpanel/"), 10) == 0) return SendFile(uri, AUTO);
	if (strncmp_P(uri, PSTR("/icon/"), 6) == 0)	return handleIcon(uri);
	if (strncmp_P(uri, PSTR("/rest/items/"), 12) == 0) return handleItem(uri + 12);
	if (strncmp_P(uri, PSTR("/rest/sitemaps/events/"), 22) == 0) return handleSSEAll(uri + sizeof("/rest/sitemaps/events"));
	if (strncmp_P(uri, PSTR("/rest/sitemaps/"), 15) == 0) return handleSitemap(uri + sizeof("/rest/sitemaps"));
	if (strcmp_P(uri, PSTR("/rest/sitemaps")) == 0)	return handleSitemaps(_sitemapList);
	if (strcmp_P(uri, PSTR("/rest/events")) == 0) return handleSSEAll(uri, true);
	if (strcmp_P(uri, PSTR("/chart")) == 0)	return SendFile(strcat_P((char *)uri, getContentTypeExt(IMAGE_PNG)), IMAGE_PNG);
	if (strncmp_P(uri, PSTR("/static/"), 8) == 0) return SendFile(uri, AUTO);
	if (strcmp_P(uri, PSTR("/rest")) == 0)	return SendFile(F("/conf/rest"));
	if (strcmp_P(uri, PSTR("/rest/items")) == 0) return SendFile(uri);
	if (strcmp_P(uri, PSTR("/rest/services")) == 0)	return SendFile(uri);
	if (strcmp_P(uri, PSTR("/rest/services/org.eclipse.smarthome.i18n/config")) == 0) return SendFile(F("/conf/i18nconfig"));
	if (strcmp_P(uri, PSTR("/rest/services/org.openhab.habpanel/config")) == 0) return SendFile(F("/conf/habpanelconfig"));
	if (strcmp_P(uri, PSTR("/rest/services/org.openhab.core.i18n/config")) == 0) return _server.send_P(200, PSTR("application/json"), PSTR("{}"));
	if (strcmp_P(uri, PSTR("/rest/links")) == 0) return _server.send_P(200, PSTR("application/json"), PSTR("[]"));
	if (strcmp_P(uri, PSTR("/rest/bindings")) == 0)	return _server.send_P(200, PSTR("application/json"), PSTR("[]"));
	handleNotFound();
}
