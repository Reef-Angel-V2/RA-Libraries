/*
 * RA_Wiznet5100.h
 *
 *  Created on: Sep 11, 2013
 *      Author: Benjamin
 */

#ifndef RA_WIZNET5100_H_
#define RA_WIZNET5100_H_

#ifdef ETH_WIZ5100
#include <SPI.h>
#include <Ethernet.h>
#include <EthernetDHCP.h>
#include <RA_Wifi.h>
#include <avr/wdt.h>
#include <PubSubClient.h>
#include <RA_CustomSettings.h>

// Static variables for Ethernet configuration
static EthernetServer NetServer(STARPORT);
static byte NetMac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
static IPAddress NetIP(192,168,1,200);

static EthernetClient NetClient;       // General-purpose client
static EthernetClient PortalClient;    // Client for portal operations
static EthernetClient ethClient;       // Client for MQTT communication
static PubSubClient MQTTClient(MQTTServer, MQTTPORT, MQTTSubCallback, ethClient);

// Flags and timeout settings
#define PORTAL_TIMEOUT  10000  // 10 seconds
#define RETRY_COUNT     3      // Number of retry attempts for connections
static boolean PortalWaiting;
static boolean FirmwareWaiting;

class RA_Wiznet5100 : public RA_Wifi
{
public:
    RA_Wiznet5100();
    void Init();                  // Initialize Ethernet and DHCP
    void ReceiveData();           // Handle incoming data
    void ProcessEthernet();       // Process Ethernet requests
    void Update();                // Main update loop
    void Cloud();                 // Handle cloud operations
    void CloudPublish(char* message); // Publish to the MQTT cloud
    boolean IsPortalConnected();  // Check if PortalClient is connected
    boolean IsMQTTConnected();    // Check if MQTTClient is connected

    void PortalConnect();         // Connect to the portal
    void FirmwareConnect();       // Connect to the firmware update server

    // Flags and data for portal and firmware updates
    boolean PortalConnection;
    boolean PortalDataReceived;
    boolean FirmwareConnection;
    unsigned long PortalTimeOut;
    unsigned long downloadsize;
    unsigned long lheader;
    boolean payload_ready;
    boolean downloading;
    boolean goodheader = false;
    boolean FoundIP;

private:
    unsigned long MQTTReconnectmillis; // Track last MQTT reconnect attempt
    unsigned long MQTTSendmillis;      // Track last MQTT publish attempt
    File firwareFile;                  // File for firmware updates
    byte sd_buffer[32];                // Buffer for firmware writes
    byte sd_index;                 // Index for firmware buffer

protected:
	size_t write(uint8_t c);
	size_t write(unsigned long n);
	size_t write(long n);
	size_t write(unsigned int n);
	size_t write(int n);
};

#endif  // ETH_WIZ5100
#endif /* RA_WIZNET5100_H_ */
