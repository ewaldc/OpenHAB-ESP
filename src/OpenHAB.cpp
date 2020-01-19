#include "OpenHAB.h"
// Following two variables are defined in main.cpp
extern uint8_t itemCount, groupCount;

#ifndef OPENHAB_GEN_CONFIG
	extern OpenHab::Item items[];
	extern OpenHab::ItemState itemStates[];
	extern OpenHab::Group groups[];
#endif

//OpenHab::OpenHab(Item *items, unsigned short itemCount, const int port) : 
OpenHab::OpenHab(const int port) : 
	_server(port),
	_port(port),
	_print(_server.client()),
	_itemList(nullptr),
	_groupList(nullptr),
	_callback_function(nullptr),
	_subscriptionCount(0)
	{}

#ifndef OPENHAB_GEN_CONFIG
void OpenHab::stateChangeCallback (state_callback_function_t callback) {
        _callback_function = std::move(callback);
}

OpenHab::Item getItem(uint8_t itemIdx) {
		static OpenHab::Item item;
		memcpy_P(&item, &items[itemIdx], sizeof(OpenHab::Item));
		return item;
}

const char *OpenHab::functionOR(uint8_t groupIdx, uint8_t itemIdx, int *numItems) {
	int count = 0;
	DbgPrint(F("functionOR"));
	ItemState &itemState = itemStates[itemIdx];
	JsonObject f = itemState.obj[F("function")];
	JsonArray fparams = f[F("params")];
	const char *fvalue = fparams[0];
	Group &group = groups[groupIdx];
	DbgPrint(F(" - value: "), fvalue); DbgPrint(F(" - itemCount: "), group.itemCount);
	uint8_t *groupItems = group.items;
	for (uint8_t n = 0; n < group.itemCount; n++) {
		uint8_t itemIdx = groupItems[n];
		char *state = itemStates[itemIdx].state;
		DbgPrint(F(" - item name: "), items[itemIdx].name);	DbgPrint(F(" - item state: "), state);		
		if (state && strcmp(fvalue, state) == 0) count++;
	}
	DbgPrintln(F(" - final count: "), count);
	*numItems = count;
	return (char *)(count > 0) ? fvalue : fparams[1].as<const char *>();
}

FORCE_INLINE float OpenHab::functionAVG(uint8_t *groupItems, uint8_t count) {
	DbgPrint(F("functionAVG"));
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
inline void setReferenceLabel (const OpenHab::ItemReference *references, uint8_t count, const char *label) {
	for (uint8_t n = 0; n < count; n++) 
		references[n].widgetObj[F("label")] = label;
}

template <typename T> static void sprf(T state, uint8_t itemIdx) {
	//static char _prevLabel[128];
	DbgPrint(F(" --> sprf "));	
	OpenHab::Item item = getItem(itemIdx);			// Create a Item copy from flash
	OpenHab::ItemState &itemState = itemStates[itemIdx];
	const char *labelFormat = item.labelFormat; // formatted label
	JsonVariant labelObj = itemState.widgetObj[F("label")];
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
	if (item.referenceCount > 0) 
		setReferenceLabel(itemState.references, item.referenceCount, label);
	DbgPrintln(F(" - new label: "), label);
}

void OpenHab::updateLabel(uint8_t itemIdx, char *state, ItemType type, int numItems) {
	DbgPrint(F("updateLabel"));	DbgPrint(F(" - type: "), ItemTypeStr[type]);
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
}

FORCE_INLINE void OpenHab::setGroupState(const char *groupName) {
	DbgPrintln(F("setGroupState: "), groupName);
	uint8_t itemIdx = getItemIdx(groupName);
	if (! isValidItemIndex(itemIdx)) return;	// Invalid item
	Item item = getItem(itemIdx);		// Create a Item copy from flash
	if (item.type != ItemGroup) return; // Item is not a group
	Group &group = groups[item.groupIdx];
	if (group.function == FunctionAVG) {
		setState(itemIdx, functionAVG(group.items, group.itemCount), false);
		updateLabel(itemIdx, itemStates[itemIdx].state, item.type);
	} else if (group.function == FunctionOR) {
		int count;
		setState(itemIdx, functionOR(item.groupIdx, itemIdx, &count), false);
		updateLabel(itemIdx, itemStates[itemIdx].state, item.type, count);
	}
}

// Function to set and eventually propagate (e.g. label, group state) the state of an item 
void OpenHab::setState(uint8_t itemIdx, const char *state, bool updateGroup) {
	static char prevState[64];
	auto setCurrentDateTime = [&](void *p) {
		time_t now = time(nullptr); // Time since epoch
    	const tm* tm = localtime(&now); // Converted to localtime
		strftime ((char *) p, 24 ,"%FT%T", tm);  // Converted to an ISO date string
		auto setStateBind = std::bind(static_cast<void(OpenHab::*)(uint8_t, const char*, bool)>(&OpenHab::setState), this, itemIdx, state, true);
		if (!_currentDateTimer.active())
			_currentDateTimer.attach(60.0, setStateBind);  // Refresh time every minute (could be optimized in the future)
	};

	auto setRollerShutterState = [&](size_t value) {
		static char intState[4];
		//size_t value = atoi(prevState);
		if (strcmp_P(state, "UP") == 0) value = min(value + 10, (size_t)100);
		else if (strcmp_P(state, "DOWN") == 0) value = max(value - 10,  (size_t)0);
		else value = atoi(state);
		sprintf_P(intState, PSTR("%d"), value);
		return intState;
	};

	if (!state) return;							// NULL states can crash setState
	DbgPrint(F("setState"));
	
	// We have a valid state
	ItemState itemState = itemStates[itemIdx];	// Get current (previous) state
	Item item = getItem(itemIdx);				// Get Item copy from flash

	size_t len = (itemState.state) ? strlen(itemState.state) + 1: 0;  // Length of old state
	if (len) strncpy(prevState, itemState.state, min(len, (size_t)sizeof(prevState)));
	DbgPrint(F(" - previous state: "), prevState);

	if (item.type == ItemRollerShutter) 
		state = setRollerShutterState(atoi(prevState));

	bool isCurrentDateTime = ((item.type == ItemDateTime) && !strncmp_P(item.name, PSTR("Current"), 7)); // item name reflecting current Date/Time?
	len = (isCurrentDateTime) ? 24 : strlen(state) + 1;
	void *p = realloc((void *) itemState.state, len);  // New state might require more space
	if (isCurrentDateTime) // This is a request to supply the current date and time
		setCurrentDateTime(p);
	else memcpy(p, state, len);  // Replicate the new state
	DbgPrint(" - new state: ", (char *) p);
	itemState.state = (char *)p;
	itemState.obj[F("state")] = (const char *) p;

	const ItemReference *references = itemState.references;
	for (uint8_t n = 0; n < item.referenceCount; n++) {
		DbgPrint(F(" - updating reference: "), item.name);
		DbgPrint(F(" with state: "), (const char *)p );
		references[n].obj[F("state")] = (const char *) p;
	}

	DbgPrintln(" - updateGroup: ", (updateGroup) ? "true" : "false");
	if (!updateGroup) return;

	JsonArray groupNames = itemState.obj[F("groupNames")];
	for (JsonVariant groupName : groupNames)
		setGroupState(groupName);
	if (item.labelFormat) updateLabel(itemIdx, (char *)p, item.type);  // finally update the parent label reflecting the new state
	if (_subscriptionCount) SSEBroadcastPageChange(itemState);
}

// Function to set and eventually propagate (e.g. label, group state) the state of an item with numeric state (e.g. type Number)
FORCE_INLINE void OpenHab::setState(uint8_t itemIdx, float f, uint8 precision, bool updateGroup) {
    char fs[14];  // Used to convert float to string
	DbgPrintln("setState float: ", f);
	setState(itemIdx, dtostrf(f, precision + 2, precision, fs), updateGroup);
}

/*
FORCE_INLINE bool isFormatted(const char* label) {
	char curr, next = *label++;
	while ((curr = next) != '\0') {
		next = *label++;
		if ((curr == '%') && (next != '%')) return true;
	}
	return false;
}
*/

uint8_t OpenHab::getItemIdx (const char *name) {
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
	DbgPrint(F("item: "), name); 
	
	uint8_t itemIdx = getItemIdx(name);
	if (! isValidItemIndex(itemIdx)) {
		Serial.printf_P(PSTR("PANIC: item %s not found!!\n"), name);
		return;
	}
	// At this point we have an item, but we need to update it
	Item item = getItem(itemIdx);			// Create a Item copy from flash
	ItemType itemType = item.type;
	ItemState &itemState = itemStates[itemIdx];
	const char* state = item.state;			// Get initial state from flash
	itemState.state = (state) ? strdup(state) : nullptr;		// and duplicate (no need for _P)
	itemState.pageId = pageId;
	JsonVariant labelObj = widgetObj[F("label")];
	const char *label = labelObj; 
	if (item.labelFormat) labelObj.set(nullptr);		// Initialize formatted label to nullstr
	DbgPrint(F(" - type: "), itemType);	DbgPrint(F(" - state: "), item.state); DbgPrint(F(" - label: "), label);
	DbgPrint(F(" - pageId: "), pageId);

	uint8_t refCount = item.referenceCount;
	if (refCount > 0) {
		if (!itemState.references) {
			itemState.references = new ItemReference[refCount];
			goto setObject;
		}
		const ItemReference *references = itemState.references;
		for (uint8_t n = 0; n < refCount; n++)
			if (!references[n].obj) { 	// first empty entry
				DbgPrintln(F(" - item reference slot: "), n);
				const ItemReference ref = { itemObj, widgetObj };
				memcpy((void *)&references[--refCount], (void *)&ref, sizeof(ItemReference));
			}
		return;
	}
setObject:
	DbgPrintln(F(""));
	itemState.obj = itemObj;
	itemState.widgetObj = widgetObj;
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
#endif

DynamicJsonDocument OpenHab::getJsonDocFromFile(String fileName) {
	DbgPrint(F("getJsonDocFromFile: file name: "), fileName);
	DbgPrint(F(" - free heap memory @entry: "), ESP.getFreeHeap());
	File f = SPIFFS.open(fileName, "r");
	DynamicJsonDocument doc(ESP.getFreeHeap() - 4096);
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
	wifi_set_event_handler_cb(wifiEventHandler);

	SPIFFS.begin();
#ifndef OPENHAB_GEN_CONFIG
	DbgPrintln(F("Loading sitemap"));
	GetSitemapsFromFS();
	
	DbgPrintln(F("Initializing states and labels"));	// Second pass set state
	for(uint8_t n = 0; n < itemCount; n++) {
		DbgPrint(F("init state: "), n);
		DbgPrintln(F(" - name : "), items[n].name);
		setState(n, itemStates[n].state);
	}
#endif
	DbgPrintln(F("Exit OpenHab::InitServer"));
	return true;
}

void OpenHab::HandleClient() {
	_server.handleClient();
}

#ifdef OPENHAB_GEN_CONFIG
FORCE_INLINE static void convDateTimeJavaToC(char *dateTime) {
	String dateTimeStr = dateTime;
	dateTimeStr.replace("1$t", "");
	strncpy(dateTime, dateTimeStr.c_str(), dateTimeStr.length() + 1);
	DbgPrintln(F("convDateTimeJavaToC: "), dateTime);
}

const char *genFormattedLabel(String label, const char *pattern, const char* state) {
	String formattedLabel;
	formattedLabel = label.substring(0, label.indexOf("[") + 1);
	formattedLabel += pattern;
	formattedLabel += label.substring(label.indexOf("]"));
	if (formattedLabel.indexOf(F("%unit%"))) {
		String s = state;
		String unit = s.substring(s.indexOf(" ") + 1);
		formattedLabel.replace("%unit%", unit);
	}
	DbgPrintln(F(" - new label: "), formattedLabel.c_str());
	return strdup(formattedLabel.c_str());
}

FORCE_INLINE OpenHab::Group *OpenHab::getGroup(const char *name) {
	Group *group = _groupList;
	while (group && (strcmp(group->name, name)))
		group = group->next;
	return group;
}

FORCE_INLINE OpenHab::Item *OpenHab::getItem(const char *name) {
	Item *item = _itemList;
	while (item && (strcmp(item->name, name)))
		item = item->next;
	return item;
}

// type == ItemNone: we just have a group name and item is a group member
// type == ItemGroup: we have a true group, check groupType for a group with dynamic label & state 
OpenHab::Group *OpenHab::newGroup(const char *groupName, Item *item, JsonObject obj, ItemType type) {
	GroupFunction groupFunction = FunctionNone;
	//JsonObject obj = item->obj;
	JsonVariant groupTypeVariant = obj[F("groupType")];  
	ItemType groupType = (groupTypeVariant.isNull()) ? ItemNull : getItemType(groupTypeVariant);
	DbgPrint(F("adding group with name: "), groupName);
	DbgPrintln(F(" - grouptype: "), groupType);
	
	if (type == ItemGroup) { // item refers to a true group item
		JsonObject groupFunctionObj = obj[F("function")];  // if it has a function, it has a dynamic label & state
		if (!groupFunctionObj.isNull()) {
			const char *groupFunctionStr = groupFunctionObj[F("name")];
			if (!strcmp_P(groupFunctionStr, PSTR("AVG")))
				groupFunction = FunctionAVG;
			else if (!strcmp_P(groupFunctionStr, PSTR("OR"))) groupFunction = FunctionOR;
		}
		DbgPrintln(F(" - groupfunction: "), groupFunction);
	}		

	if (Group *group = getGroup(groupName)) { // we might already have a group with this name but without group leader
		if (groupType != ItemNull) { // group with dynamic label/state found
			DbgPrintln(F("Group leader found: "), groupName);
			group->type = groupType; // update type		
			group->function = groupFunction; // update function
		}
		if (type != ItemGroup) { // we are adding an item to an exising group
			group->itemList = new ItemList{item, group->itemList};
			group->itemCount++;
		}
		return group;
	}
	DbgPrintln(F(" - insert new group in list: "), groupName);
	groupCount++;
	return _groupList = new Group {groupName, item, groupType, groupFunction, nullptr, (type == ItemNull) ? (uint8_t)1 : (uint8_t)0, 
		(type == ItemNull) ? new ItemList{item, nullptr} : nullptr, _groupList};
}

FORCE_INLINE OpenHab::Group *OpenHab::addItemToGroup(const char *groupName, Item *item, JsonObject obj) {
	DbgPrint(F("addItemToGroup adding item named: "), item->name);
	DbgPrintln(F(" - to group: "), groupName);
	return newGroup(groupName, item, obj, ItemNull); // create or get group
}

void OpenHab::GenSitemap(const JsonVariant prototype, Sitemap *sitemap, size_t uriBaseLen) {
	if (prototype.is<JsonObject>()) {
		const JsonObject& obj = prototype;
		for (const JsonPair pair : obj) {
			const char *key = pair.key().c_str();
			if (!strcmp_P(key, PSTR("link"))) { // Sitemap
				const char* link = pair.value().as<char*>();
				obj[pair.key()] = link + uriBaseLen;
			} else 
			//DbgPrint(F(" - key: "), key); DbgPrint(F(" - value: "), pair.value().as<const char *>()); DbgPrintln(F(" - free mem: "), ESP.getFreeHeap());
			if (!strcmp_P(key, PSTR("item"))) {// Items
				JsonObject itemObj = pair.value().as<JsonObject>();
				const char *name = itemObj[F("name")];
				const char *state = itemObj[F("state")];
				itemObj[F("state")] = "";	// we have copied state, discard it in JSON
				const char *transformedState = itemObj[F("transformedState")];
				if (transformedState) itemObj[F("transformedState")] = ""; // we have copied transformed state, discard it in JSON
				JsonObject stateObj = itemObj[F("stateDescription")];

				if (Item *item = getItem(name)) {	// Test if item has already been created
					item->referenceCount++;			// Increase reference count
					DbgPrint(F(" - referenceCount"), item->referenceCount);
					if (stateObj) itemObj.remove(F("stateDescription"));	// We have it still in master item
					if (item->labelFormat) obj[F("label")] = ""; // Discard formatted label for widget in Json, we have it in master
				} else {  // New item
					itemCount++;	// Increase global count of items
					const char *label = obj[F("label")];
					ItemType itemType = getItemType(itemObj[F("type")]);
					DbgPrint(F(" - id: "), itemCount);
					DbgPrintln(F(" - name: "), name);

					const char *pattern = nullptr;
					if (stateObj) {
						pattern = (transformedState) ? "%s" : stateObj[F("pattern")].as<const char *>();
						JsonArray options = stateObj[F("options")];
						if (options.size() > 0) {
							for (JsonVariant option : options)
								if (!strcmp(option[F("value")], state))
									state = option[F("label")];
							//DbgPrintln(F("options state: "), state);
						} else itemObj.remove(F("stateDescription")); //unless we have 'options' we no longer need statedesciption
					}
					const char *formattedLabel = nullptr;
					if (pattern) { // we have a formatted label
						formattedLabel = genFormattedLabel(label, pattern, state);
						if (itemType == ItemDateTime)
							convDateTimeJavaToC((char *) formattedLabel); // convert Java label format to C/C++ strftime format
						obj[F("label")] = ""; // Discard formatted label for widget in Json
					}
					Group *group = (itemType == ItemGroup ) ? newGroup(name, nullptr, itemObj, ItemGroup) : nullptr;
					_itemList = new Item{strdup(name), itemType, 0, 255, strcmp_P(state, PSTR("NULL")) ? strdup(state) : nullptr, 
						(formattedLabel) ? strdup(formattedLabel) : nullptr, group, _itemList};

					JsonArray groupNames = itemObj[F("groupNames")];
					if (groupNames.size() > 0)
						for (JsonVariant groupName : groupNames)
							addItemToGroup(groupName, _itemList, itemObj);
				}
			}
			yield();  // avoid Soft WDT reset
			GenSitemap(pair.value().as<JsonObject>(), sitemap, uriBaseLen); //, pair.key);
			for (auto arr : pair.value().as<JsonArray>()) {
				GenSitemap(arr.as<JsonVariant>(), sitemap, uriBaseLen); //, pair.key);
			}
		}
	} else if (prototype.is<JsonArray>()) {
		const JsonArray& arr = prototype;
		for (const auto& elem : arr) GenSitemap(elem.as<JsonVariant>(), sitemap, uriBaseLen); //, parent);
	}
}

void OpenHab::GetSitemap(const char *sitemap, String uriBase) {
	WiFiClient client;
	HTTPClient httpClient;  //Declare an object of class HTTPClient
	String uri = uriBase;
	uri += F("/rest/sitemaps/");
	uri += sitemap;
	httpClient.begin(client, uri);
	DbgPrint(F("GenConfig - uri: "), uri);
	int httpCode = httpClient.GET();                  //Send the request
	DbgPrintln(F(" - http code: "), httpCode);
	if (httpCode > 0) { //Check the returning code
		if (!SPIFFS.begin())
			Serial.println(F("Error mounting the file system"));
		SPIFFS.format();			
		File wfile = SPIFFS.open(F("/conf/sitemap.tmp"), "w");
		httpClient.writeToStream(&wfile);
		DbgPrintln(F(" - http get completed"));
		wfile.close();
	    httpClient.end();		//Close connection
		delay(5000);
	} else httpClient.end();		//Close connection
}

void OpenHab::GenConfig(const char *OpenHabServer, const int port, const char *sitemap) {
	String uriBase = F("http://");
	uriBase += OpenHabServer;
	uriBase += F(":");
	uriBase += port;
	// Get sitemap from full OpenHab 2.X server and write to SPIFFS in a buffered way
	GetSitemap(sitemap, uriBase);
	DbgPrintln(F(" - free mem after GetSitemap: "), ESP.getFreeHeap());
	yield();

	// Read sitemap as JsonObject from file
	const char *fileName = PSTR("/conf/sitemap.tmp");
	_sitemapList = new Sitemap{fileName, getJsonDocFromFile(fileName), nullptr};
	SPIFFS.end();
	DbgPrintln(F(" - free mem after new Sitemap: "), ESP.getFreeHeap());
	yield();

	// Adapt the sitemap to conserve memory
	GenSitemap(_sitemapList->jsonDoc.as<JsonObject>(), _sitemapList, uriBase.length());
	DbgPrintln(F(" - free mem ater GenSitemap: "), ESP.getFreeHeap());
	yield();

	// Print out the generated sitemap Json Object
	Serial.println(F("Copy the following line (JSON string) to data/conf/<your sitemap name> ---------------------------- "));
	BufferedPrint<HardwareSerial, 512> print(Serial);
	serializeJson(_sitemapList->jsonDoc, print);
	print.flush();
	Serial.println();
	Serial.println(F("----------------------------- "));
 	DbgPrintln(F("free heap memory: "), ESP.getFreeHeap());
	yield();
 	
	// Create an array of pointers to list of Items
	DbgPrintln(F("item count: "), itemCount);
	Item **itemList = new Item*[itemCount];
	
	auto getItemIdx = [&](const char *name) {
		uint8_t middle, first = 0, last = itemCount;
		do {
			middle = (first + last) >> 1;
			int cmp = strcmp(itemList[middle]->name, name);
			if (cmp == 0) return middle;
			else if (cmp < 0) first = middle + 1;
			else last = middle - 1;
		} while (first <= last);
		return (uint8_t) 255;		
	};

	Item *itemPtr = _itemList;
	for (unsigned int i = 0; i < itemCount; i++, itemPtr = itemPtr->next) {
		itemList[i] = itemPtr;
	}
 	DbgPrintln(F("free heap memory: "), ESP.getFreeHeap());
	// Bubble sort Items
	unsigned int n = 1;
	while (n < itemCount) {
		if (strcmp(itemList[n]->name, itemList[n - 1]->name) < 0) {
			Item *item = itemList[n];
			itemList[n] = itemList[n - 1];
			itemList[n - 1] = item;
			if (n > 1) n--;
		} else n++;
	}
	yield();

	// Create an array of pointers to list of Groups
	DbgPrintln(F("group count: "), groupCount);
	Group **groupList = new Group*[groupCount];
	groupCount = 0;
	for (n = 0; n < itemCount; n++) {
		if (itemList[n]->type == ItemGroup) {
			groupList[groupCount] = itemList[n]->group;
			itemList[n]->groupIdx = groupCount++;
		}
	}

	// Finally, write out the generated item, itemState and group list
	String items = F("OpenHab::Item items[] PROGMEM = {");
	for (unsigned int i = 0; i < itemCount; ) {
		items += F("{\"");
		items += itemList[i]->name;
		items += F("\", ");
		items += ItemTypeString[itemList[i]->type];
		items += F(", ");
		items += itemList[i]->referenceCount;
		items += F(", ");
		items += itemList[i]->groupIdx;
		items += F(", ");
		const char *state = itemList[i]->state;
		if (state) {	// Non-empty state
			items += F("\"");
			items += state;
			items += F("\", ");
		} else switch (itemList[i]->type ) { // Empty state -> make "" or "0"
			case ItemContact:
			case ItemString:
			case ItemSwitch:
			case ItemGroup:
			case ItemDateTime:
				items += F("\"\", "); break;
			case ItemNumber:
			case ItemDimmer:
				items += F("\"0\", "); break;
				default:;
		}	 
		const char *labelFormat = itemList[i]->labelFormat;
		if (labelFormat) {
			items += F("\"");
			items += labelFormat;
			items += F("\"}");		
		} else items += F("nullptr}");
		if (++i == itemCount) items += "};";
		else items += ", ";
	}

	String itemState = F("OpenHab::ItemState itemStates[");
	itemState += itemCount;
	itemState += "];";

	String groups = F("OpenHab::Group groups[] = {");
	for (unsigned int i = 0; i < groupCount; ) {
		groups += "{";
		//groups += groupList[i]->name;
		//groups += ", ";		
		groups += ItemTypeString[groupList[i]->type];
		groups += F(", (GroupFunction) ");
		groups += groupList[i]->function;
		groups += F(", new uint8_t[");
		groups += groupList[i]->itemCount;
		groups += F("]{");
		ItemList *member = groupList[i]->itemList;
		while (member) {
			groups += (uint8_t) getItemIdx(member->item->name);
			member = member->next;
			if (member) groups += ", ";
		}
		groups += F("}, ");
		groups += groupList[i]->itemCount;
		if (++i == groupCount) groups += F("}};");
		else groups += F("}, ");
	}	
	Serial.println(F("Copy the following lines to src/main.cpp ---------------------------- "));
	Serial.println();
	Serial.printf_P(PSTR("uint8_t itemCount = %d;\n"), itemCount); // max 255 items on ESP8266
	Serial.println(itemState);
	Serial.println(items);
	Serial.printf_P(PSTR("uint8_t groupCount = %d;\n"), groupCount); // max 255 items on ESP8266
	Serial.println(groups);
	Serial.println();
	Serial.println(F("----------------------------- "));
}
#else

// Start the web server, handle all requests in a single function to save memory
void OpenHab::StartServer() { //
	_server.onNotFound(std::bind(&OpenHab::handleAll, this));
	_server.begin();
}

void OpenHab::GetSitemapsFromFS() {
	DbgPrintln(F("GetSitemapsFromFS"));
	DbgPrintln(F("free heap memory: "), ESP.getFreeHeap());
	Dir dir = SPIFFS.openDir(F("/conf/sitemaps"));
	while (dir.next()) {
		String fileName = dir.fileName(); 
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
	DbgPrintln(F("SendFile "), fname);
	char buf[128];
	if (SPIFFS.exists(fname)) {
		File f = SPIFFS.open(fname, "r");
		size_t sz = f.size();
		//DbgPrintln(F("file size "), sz);
		_server.setContentLength((pre || post) ? CONTENT_LENGTH_UNKNOWN : sz);
		//DbgPrintln(F("file type "), getContentTypeStr(contentType));
	 	_server.send_P(200, getContentTypeStr(contentType), PSTR(""));
		//DbgPrintln(F("before pre test"));
		if (pre) _server.sendContent_P(pre);
		while (sz > 0) {
			size_t len = std::min((size_t) sizeof(buf), sz);
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
	ItemState &itemState = itemStates[itemIdx];
	JsonVariant obj = itemState.obj;
	DbgPrint(F("handleItem - "));
	if (_server.method() == HTTP_GET) return SendJson(obj);
	else if (_server.method() == HTTP_POST) {  // Item has changed
		//DbgPrintRequest(_server, "handleItem - POST request: ");
		if (_server.hasArg(F("plain"))) { //Check if body received as plain argument
			const char *state = _server.arg(F("plain")).c_str();
			if (strcmp(state, itemState.state)) { // state (type char *) has changed 
				setState(itemIdx, state);		// change state and labels
				_callback_function(itemIdx);	// call back user code
			}
		}
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
	String s = _server.arg("state"); s.toLowerCase();
	const char *state = s.c_str();
	const char *format = _server.arg("anyFormat").c_str();
	bool anyformat = (strlen(format)) ? (strcmp_P(format, PSTR("true")) == 0) : true;
	ContentType type = (anyformat) ? IMAGE_DEFAULT : getIconType(_server.arg("format").c_str());
	const char *ext = getContentTypeExt(type);
	sprintf_P(fileName, PSTR("%s-%s%s"), uri, state, ext);
	//String fileName = uri; fileName += "-";	fileName += state; fileName += ext;
	//DbgPrintln(F("type: "), type);
	DbgPrintln(F("filename: "), fileName);
	if (SPIFFS.exists(fileName)) return SendFile(fileName, type);
	//unsigned short value = state.toInt() / 10 * 10; // round to a multiple of 10
	unsigned short value = (atoi(state) / 10) * 10; // round to a multiple of 10
	DbgPrintln(F("value: "), value);
	sprintf_P(fileName, PSTR("%s-%d%s"), uri, value, ext);
	DbgPrintln(F("filename: "), fileName);
	//fileName = uri; fileName += "-"; fileName += value; fileName += ext;
	if (SPIFFS.exists(fileName)) return SendFile(fileName, type);
	sprintf_P(fileName, PSTR("%s%s"), uri, ext);
	//fileName = uri;	fileName += ext;
	SendFile(fileName, type);
}

void OpenHab::handleSubscribe() {
	byte uuid[16];
	char buf[96];
	uint8_t channel;

	DbgPrint(F("handleSubscribe"));
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

void OpenHab::SSEBroadcastPageChange(ItemState &itemState) {
	DbgPrintln(F("SSEBroadcastPageChange: "));
	for (uint8_t channel = 0; channel < SSE_MAX_CHANNELS; channel++) {
		//DbgPrintln(F("channel: "), channel);
		if (!_subscription[channel].clientIP) continue;							// Skip unallocated channels
	    WiFiClient client = _subscription[channel].client;
    	String IPaddrstr = IPAddress(_subscription[channel].clientIP).toString();
		//Serial.printf_P(PSTR("checking if channel %d (to client IP %s) is connected\n"), channel, IPaddrstr.c_str());
    	if (client.connected()) {
			//Serial.printf_P(PSTR("checking if page id's match: %s <-> %s\n"), _subscription[channel].pageid, pageid);
			if (strcmp(_subscription[channel].pageId, itemState.pageId)) continue;	// Skip pageId mismatch

			Serial.printf_P(PSTR("broadcast page change to client IP %s on channel %d with pageid %s\n"), IPaddrstr.c_str(), channel, itemState.pageId);
			//serializeJson(obj, Serial); Serial.println("");
			DbgPrintln(F("state: "), itemState.state);
			String itemStr;
			serializeJson(itemState.obj, itemStr);
			int offset = itemStr.indexOf(F(",")) + 1;
			//DbgPrintln(F("item: "), itemStr.substring(offset));

			client.printf_P(PSTR("event: event\ndata: {\"widgetId\":\"%s\",\"label\":\"%s\",\"visibility\":true,\"item\":{%s,\"sitemapName\":\"%s\",\"pageId\":\"%s\"}\n\n"),
				itemState.widgetObj[F("widgetId")].as<const char *>(), itemState.widgetObj[F("label")].as<const char *>(), 
				itemStr.substring(offset).c_str(), _subscription[channel].sitemap, _subscription[channel].pageId);

			Serial.printf_P(PSTR("event: event\ndata: {\"widgetId\":\"%s\",\"label\":\"%s\",\"visibility\":true,\"item\":{%s,{\"sitemapName\":\"%s\",\"pageId\":\"%s\"}\n\n"),
				itemState.widgetObj[F("widgetId")].as<const char *>(), itemState.widgetObj[F("label")].as<const char *>(), 
				itemStr.substring(offset).c_str(), _subscription[channel].sitemap, _subscription[channel].pageId);
			//serializeJson(itemState.obj, client);
		}
	}
}

void OpenHab::SSEKeepAlive(Subscription *subscription) {
	DbgPrintln(F("SSEKeepAlive for IP: "), subscription->clientIP);
    WiFiClient client = subscription->client;  
	if (client.connected()) {
		DbgPrintln(F("Client is still connected - pageId: "), subscription->pageId);
		client.printf_P(PSTR("event: event\ndata: { \"TYPE\":\"ALIVE\",\"sitemapName\":\"%s\",\"pageId\":\"%s\"}\n\n"), subscription->sitemap, subscription->pageId);
	} else {
		Serial.println(F("SSEKeepAlive - client no longer connected, remove subscription"));
		client.flush();
		client.stop();
		subscription->clientIP = 0;
		subscription->keepAliveTimer.detach();
		_subscriptionCount--;
    }
}

void OpenHab::handleSSEAll(const char *uri) {
	if (strcmp_P(uri, PSTR("subscribe")) == 0)
	return handleSubscribe();	//this is a sitemap event registration

	DbgPrint(PSTR("Listener request for uuid "), uri);
	WiFiClient client = _server.client();
	client.setNoDelay(true);
	IPAddress clientIP = client.remoteIP();
	String IPaddrstr = IPAddress(clientIP).toString();
	//DbgPrintln(F("clientIP from SSEserver: "), IPaddrstr.c_str());	
	//DbgPrintRequest(F("SSEHandler"));
	uint8_t channel;
	for (channel = 0; channel < SSE_MAX_CHANNELS; channel++)
		if ((_subscription[channel].clientIP == (uint32_t) clientIP)) { //&&
			//DbgPrintln(F("Matching IP for channel: "), channel);				 
			//DbgPrintln(F("_subscription uuidstr: "), _subscription[channel].uuidStr.c_str());				 
			//DbgPrintln(F("uuid comparison: "), (_subscription[channel].uuidStr == uri));				 
			if (strncmp_P(_subscription[channel].uuidStr.c_str(), uri, 36) == 0) break;
		}
	if (channel == SSE_MAX_CHANNELS) {	// IP address and UUID did not match, reject this client
   		DbgPrintln(F(" - unregistered client tries to listen\n"));
	   	return handleNotFound();
 	}
	DbgPrint(F(" - client IP/UUID match for channel: "), channel);	
	_subscription[channel].client = client; // update client
	_subscription[channel].sitemap =  (strcmp((_server.arg(0)).c_str(), _sitemapList->name)) ? nullptr : _sitemapList->name;
	DbgPrint(F(" - sitemap: "), _subscription[channel].sitemap);	

	const char *pageId = (_server.arg(1)).c_str();
	Page *page = _sitemapList->pageList;
	while (page && (strcmp(page->pageId, pageId))) page = page->next;
	_subscription[channel].pageId = (page) ? page->pageId : nullptr;
	DbgPrintln(F(" - pageid: "), _subscription[channel].pageId);	

	_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
	_server.sendContent_P(PSTR("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream;\r\nConnection: keep-alive\r\nCache-Control: no-cache\r\nAccess-Control-Allow-Origin: *\r\n\r\n"));
	auto keepaliveBind = std::bind(static_cast<void(OpenHab::*)(Subscription *)>(&OpenHab::SSEKeepAlive), this, &(_subscription[channel]));
	_subscription[channel].keepAliveTimer.attach_scheduled((float)120.0, keepaliveBind);  // Refresh time every minute (could be optimized in the future)
}

// ESP webserver does not support wildcards, process image/icon requests here
void OpenHab::handleAll() {
	const char* uri = _server.uri().c_str();
	DbgPrintln(F("OpenHab::handleAll: "), uri);
	DbgPrintln(F("free heap memory @entry: "), ESP.getFreeHeap());
	if (strncmp_P(uri, PSTR("/icon/"), 6) == 0)	return handleIcon(uri);
	if (strncmp_P(uri, PSTR("/rest/items/"), 12) == 0) return handleItem(uri + 12);
	if (strncmp_P(uri, PSTR("/rest/sitemaps/events/"), 22) == 0) return (handleSSEAll(uri + sizeof("/rest/sitemaps/events")));
	if (strncmp_P(uri, PSTR("/rest/sitemaps/"), 15) == 0) return (handleSitemap(uri + sizeof("/rest/sitemaps")));
	if (strcmp_P(uri, PSTR("/rest/sitemaps")) == 0)	return handleSitemaps(_sitemapList);
	if (strcmp_P(uri, PSTR("/chart")) == 0)	return SendFile(strcat_P((char *)uri, getContentTypeExt(IMAGE_PNG)), IMAGE_PNG);
	if (strcmp_P(uri, PSTR("/rest")) == 0)	return SendFile(F("/conf/rest"));
	if (strcmp_P(uri, PSTR("/rest/links")) == 0) return _server.send_P(200, PSTR("application/json"), PSTR("[]"));
	if (strcmp_P(uri, PSTR("/rest/bindings")) == 0)	return _server.send_P(200, PSTR("application/json"), PSTR("[]"));
	if (strcmp_P(uri, PSTR("/rest/services")) == 0)	return SendFile(F("/conf/services.cfg"));
	handleNotFound();
}

#endif