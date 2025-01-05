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
#include <PubSubClient.h>
#include <RA_Wifi.h>
#include <avr/wdt.h>
#include <RA_CustomSettings.h>


// Static variables for Ethernet configuration
static EthernetServer NetServer(STARPORT);
static byte NetMac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
static IPAddress NetIP(192, 168, 1, 200);

static EthernetClient NetClient;    // General-purpose client
static EthernetClient PortalClient; // Client for portal operations
static EthernetClient ethClient;    // Client for MQTT communication
static PubSubClient  MQTTClient(MQTTServer, MQTTPORT, MQTTSubCallback, ethClient);

// Flags and timeout settings
#define PORTAL_TIMEOUT   10000    // 10 seconds
#define READ_LOOP_MAX_MS 5000UL

static boolean PortalWaiting;
static boolean FirmwareWaiting;

class RA_Wiznet5100 : public RA_Wifi
{
public:
    RA_Wiznet5100();
    void Init();                // Initialize Ethernet and DHCP
    void Update();              // Main update loop

    // Handling incoming data
    void ReceiveData();         // Handle incoming data from NetServer
    void ProcessEthernet();     // Process data from NetClient

    // Portal / Firmware
    void PortalConnect();       // Connect to PortalServer
    void FirmwareConnect();     // Connect to WebWizardServer
    boolean IsPortalConnected(); // checks connections status
    void ForceCheckFirmware(); // Call to check for firmware
    void partialReadPortalClient();
    void checkPortalFirmwareStatus();


    // Cloud (MQTT) operations
    void Cloud();
    void CloudPublish(char* message);
    boolean IsMQTTConnected();

    

    // Flags and data for portal/firmware
    boolean PortalConnection;
    boolean PortalDataReceived;
    boolean FirmwareConnection;
    boolean payload_ready;
    boolean downloading;
    boolean goodheader;
    boolean FoundIP;
  

    unsigned long PortalTimeOut;
    unsigned long downloadsize;
    unsigned long lheader;

private:
    void publishParams();

    // Timers
    unsigned long MQTTReconnectmillis;
    unsigned long MQTTSendmillis;

    // File for firmware updates
    File firwareFile;

    // Buffers
    byte sd_buffer[32];
    byte sd_index;

    // Track the last time we saw "activity"
    unsigned long lastActivityMillis;

protected:
    size_t write(uint8_t c);
};

#endif  // ETH_WIZ5100
#endif /* RA_WIZNET5100_H_ */
