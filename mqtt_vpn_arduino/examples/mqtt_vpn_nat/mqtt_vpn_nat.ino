/*
 *   This sketch is a demo for the MQTT_VPN
 *   Edit the "..." lines in the config for your environment
 */

#include <ESP8266WiFi.h>
#include <lwip/napt.h>
#include <lwip/dns.h>
#include <ESP8266WebServer.h>
#include <mqttif.h>

#define DEBUG

// WiFi settings
const char* ssid     = "...";
const char* password = "...";

// VPN settings
char* broker = "...";
char* vpn_password = "secret";
int broker_port = 1883;
IPAddress mqtt_vpn_addr(10,0,1,2); // address from our side of the tunnel

// Broker settings
char* broker_username = "...";
char* broker_password = "...";
char* broker_topic_prefix = "...";

struct mqtt_if_data *my_if;

/*
 *   The default implementation is limited to 8 hosts to NAT for...
 */
#define NAPT 8
#define NAPT_PORT 10

ESP8266WebServer server(80);

void handleConfig()
{
  String message = "My config is:\n\n";
  message += WiFi.localIP() + " / ";
  message += WiFi.subnetMask() +"\n";
  message += "GW: " + WiFi.gatewayIP();
  message += "\n";
  server.send(200, "text/html", message);
}

/*
 *   This routine converts a string to an IPAddress
 *   it returns true if the string really contains and adress
 *   and false if this is not the case
 */
static bool str2IP(String str, IPAddress* addr)
{
  uint8_t digit4[4];
  char * item = strtok(&str[0], ".");
  uint8_t i=0;
  while (!(item == NULL || i==4)) {
    digit4[i] = atoi(item);
    item = strtok(NULL, ".");
    ++i;
  }

  // if we have exactly 4 integers when the string is all consumed
  if (item == NULL && i==4)
  {
    *addr = IPAddress(digit4[0],digit4[1],digit4[2],digit4[3]);
    return true;
  } else {
    return false;
  }
}

/*
 *   This routine simply check the parameter for a "dest=x" argument
 *   and add the argument as a new IPAddress to NAT for
 *   We also might wants to add a remove counterpart but it's not
 *   currently implemented in the mqttif code
 */
void handleAddNAT()
{
  /*
   *   The client requests the page :
   *     http://.../add?argname1=arg1&argname2=arg2&...
   *   so we have to search for a dest=XXX.XXX.XXX.XXX parameter
   *   We stop when the first "dest=x" occurs
   */
  int i = 0;
  while (!(i == server.args() || server.argName(i)=="dest"))
  {
    ++i;
  }
  String message = "<html><body>";
  int return_code = 500;
  if(i!=server.args()) // if one is found
  {
    /*
     *   The following address must point to another machine
     *   which will also be fully accessible, via the vpn tunnel,
     *   by the host on the other side of the tunnel.
     *   We will NAT every packets destinated to this address
     *   on our local wifi network with the ip_napt_enable call.
     *   This should be set consistently with WiFi.localIP()
     *   We currently do not check anything... FIXME:check this
     */
    IPAddress mqtt_vpn_target_addr;
    if (str2IP(server.arg(i), &mqtt_vpn_target_addr))
    {
      message += "<p>Adding: " + server.arg(i) + "</p>\n";
      mqtt_if_add_reading_topic(my_if,  mqtt_vpn_target_addr);
      return_code = 200;
    }
  }
  message += "</body></html>\n";
  server.send(return_code, "text/html", message);
}

void handleNotFound()
{
  String message = "<html><body><h1>File Not Found</h1>\n";
  message += "<p>URI: " + server.uri() + "</p>\n";
  message += "<p>Method: " + \
             (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "</p>\n";
  message += "<p>Arguments: " + server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "</p>\n";
  }
  message += "</body></html>\n";
  server.send(404, "text/html", message);
}

void setup()
{
#ifdef DEBUG
  Serial.begin(115200);
  delay(10);

  // We start by connecting to a WiFi network

  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
#endif

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
#ifdef DEBUG
    Serial.print(".");
#endif
  }

#ifdef DEBUG
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
#endif

  /* The magic is here:
     This sets up a new IP interface that is connected to the central MQTT broker */
  my_if = mqtt_if_init(broker, broker_username, broker_password, broker_port, broker_topic_prefix, vpn_password, mqtt_vpn_addr, IPAddress(255, 255, 255, 0), IPAddress(0, 0, 0, 0));

#ifdef DEBUG
  Serial.printf("Heap before: %d\r\n", ESP.getFreeHeap());
#endif
  err_t ret = ip_napt_init(NAPT, NAPT_PORT);
#ifdef DEBUG
  Serial.printf("ip_napt_init(%d,%d): ret=%d (OK=%d)\r\n", NAPT, NAPT_PORT, (int)ret, (int)ERR_OK);
#endif
  if (ret == ERR_OK) {
    ret = ip_napt_enable(mqtt_vpn_addr, 1);
#ifdef DEBUG
    Serial.printf("ip_napt_enable_no(my_if): ret=%d (OK=%d)\r\n", (int)ret, (int)ERR_OK);
    if (ret == ERR_OK) {
      Serial.printf("WiFi Network MQTT_VPN is now NATed behind '%s'\r\n", ssid);
    }
#endif
  }

#ifdef DEBUG
  Serial.printf("Heap after napt init: %d\r\n", ESP.getFreeHeap());
  if (ret != ERR_OK) {
    Serial.printf("NAPT initialization failed\r\n");
  }
#endif

  server.on("/", []() {
    server.send(200, "text/html", "<html><body><h1>It works!</h1>\n\
<p>This is the default web page for this server.</p>\n\
<p>The web server software is running but no content has been added, yet.</p>\n\
</body></html>\n");
  });
  server.on("/config", handleConfig);
  server.on("/add", handleAddNAT);
  server.onNotFound(handleNotFound);

  server.begin();
#ifdef DEBUG
  Serial.println("HTTP server started");
#endif
}

void loop()
{
  server.handleClient();
}
