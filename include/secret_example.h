// The followng section is just to keep IntelliSense happy...
#ifndef PROGMEM
  // The following two macros cause a parameter to be enclosed in quotes
  // by the preopressor (i.e. for concatenating ints to strings)
  #define __STRINGIZE_NX(A) #A
  #define __STRINGIZE(A) __STRINGIZE_NX(A)
  // Since __section__ is supposed to be only use for global variables,
  // there could be conflicts when a static/inlined function has them in the
  // same file as a non-static PROGMEM object.
  // Ref: https://gcc.gnu.org/onlinedocs/gcc-3.2/gcc/Variable-Attributes.html
  // Place each progmem object into its own named section, avoiding conflicts
  #define PROGMEM __attribute__((section( "\".irom.text." __FILE__ "." __STRINGIZE(__LINE__) "."  __STRINGIZE(__COUNTER__) "\"")))
#endif

static const PROGMEM char *softAPssid = "myESP";
static const PROGMEM char *ssid = "myRouter";
static const PROGMEM char *passphrase = "bXlQYXNzd29yZA===E";  //myPassword base64 encoded
// Limit access to a controlled list of MAC addresses in SoftAP mode
const PROGMEM char *allowedMAC[] = {"00:00:00:00:00:00", "00:00:00:00:00:01", ""}; // list of allowed MAC address, terminated by empty string
//const PROGMEM char *allowedMAC[] = {""}; // all MAC addresses allowed