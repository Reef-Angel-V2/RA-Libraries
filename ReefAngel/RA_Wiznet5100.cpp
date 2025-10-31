/*
 * RA_Wiznet5100.cpp
 *
 *  Created on: Sep 11, 2013
 *      Author: Benjamin
 */

 #include <Globals.h>
 #ifdef ETH_WIZ5100
 
 #include <ReefAngel.h>
 #include "RA_Wiznet5100.h"
 #include <Ethernet.h>
 #include <EthernetDHCP.h>
 #include <RA_Wifi.h>
 #include <avr/wdt.h>
 
 RA_Wiznet5100::RA_Wiznet5100()
 {
     PortalTimeOut   = millis();
     downloading     = false;
     
     // Initialize connection stability tracking
     connectionRetryCount = 0;
     lastConnectionAttempt = 0;
     isReconnecting = false;
     lastDHCPAttempt = 0;
     dhcpRetryCount = 0;
     
    // Initialize internet connectivity tracking
    hasInternet = false;
    lastInternetCheck = 0;
    internetFailureCount = 0;
    internetCheckInProgress = false;
    
    // Initialize non-blocking internet check state
    internetCheckStarted = false;
    internetCheckStartTime = 0;
    testClient = nullptr;
     
    // Initialize DNS resolution tracking
    mqttServerResolved = false;
    portalServerResolved = false;
    firmwareServerResolved = false;
 }
 // Init()
 //  - Initializes Ethernet with DHCP
 //  - Resets flags/timers
 void RA_Wiznet5100::Init()
 {
     // Possibly randomize the last MAC byte if not set
     NetMac[5] = InternalMemory.read(StarMac);
     if (NetMac[5] == 0xFF)
     {
         byte tempmac = random(0xFF);
         InternalMemory.write(StarMac, tempmac);
         NetMac[5] = tempmac;
     }
     
    // Start Ethernet with DHCP
    EthernetDHCP.begin(NetMac, 1);
    
    NetServer.begin();
     FoundIP             = false;
     PortalConnection    = false;
     PortalWaiting       = false;
     PortalDataReceived  = false;
     FirmwareConnection  = false;
     FirmwareWaiting     = false;
     payload_ready   = false;
     MQTTReconnectmillis = millis();
     MQTTSendmillis      = millis();
     downloadsize        = 0;
     sd_index            = 0;
     goodheader          = false;
 
     lastActivityMillis  = millis();
     
     // Reset connection stability tracking
     connectionRetryCount = 0;
     lastConnectionAttempt = millis();
     isReconnecting = false;
     lastDHCPAttempt = millis();
     dhcpRetryCount = 0;
     
     // Reset internet connectivity tracking
     hasInternet = false;
     lastInternetCheck = millis() - INTERNET_CHECK_INTERVAL; // Force immediate internet check on startup
     internetFailureCount = 0;
     internetCheckInProgress = false;
     
    // Reset DNS resolution tracking
    mqttServerResolved = false;
    portalServerResolved = false;
    firmwareServerResolved = false;
 }
 
 // Update()
 //  - Called repeatedly in main loop
 //  - Polls DHCP, checks IP, partial-reads portal data, etc.
void RA_Wiznet5100::Update()
{
    wdt_reset(); // Reset watchdog at start of main loop
    
    EthernetDHCP.poll();
     
     // Check if we have an IP address
     const byte* ipAddr = EthernetDHCP.ipAddress();
     if (ipAddr[0] != 0)
     {
        if (!FoundIP)
        {
            Serial.println(ip_to_str(ipAddr));
            // Reset retry counters on successful IP acquisition
            dhcpRetryCount = 0;
            connectionRetryCount = 0;
            // Reset internet connectivity tracking when we get a new IP
            ResetInternetFailures();
        }
         FoundIP = true;
         isReconnecting = false;
         
        // STEP 1: Check internet connectivity first (most important)
        unsigned long timeSinceLastCheck;
        if (millis() >= lastInternetCheck) {
            timeSinceLastCheck = millis() - lastInternetCheck;
        } else {
            // Handle overflow case
            timeSinceLastCheck = (ULONG_MAX - lastInternetCheck) + millis() + 1;
        }
        
        // Check immediately if we haven't checked yet, or if it's been 60+ seconds
        // OR if we're already checking (continue the check)
        if (timeSinceLastCheck > INTERNET_CHECK_INTERVAL || internetCheckInProgress)
        {
            CheckInternetConnectivity();
        }
         
         // STEP 2: Only proceed with ANY network operations if we have confirmed internet
         if (hasInternet && internetFailureCount == 0)
         {
             // Resolve MQTT server only once when internet is confirmed
             if (!mqttServerResolved)
             {
                 ResolveMQTTServer();
             }
             
             // Handle normal server data
             ReceiveData();
             
             // If we have portal data to read
             if (PortalClient.available() && (PortalConnection || FirmwareConnection))
             {
                 partialReadPortalClient();
             }

             // Check disconnection/timeout of portal or firmware
             checkPortalFirmwareStatus();
         }
         else
         {
             // No internet - stop all network operations gracefully
             if (PortalConnection || FirmwareConnection)
             {
                 PortalConnection = false;
                 FirmwareConnection = false;
                 PortalClient.stop();
                 if (firwareFile) firwareFile.close();
             }
             
             // Reset DNS resolution flags when no internet
             mqttServerResolved = false;
             portalServerResolved = false;
             firmwareServerResolved = false;
         }
     }
     else
     {
         // No IP address - handle DHCP failure gracefully
         if (FoundIP)
         {
             // We lost our IP address
             FoundIP = false;
         }
         
         // Try to recover DHCP with retry logic
         if (millis() - lastDHCPAttempt > DHCP_RETRY_DELAY)
         {
             if (dhcpRetryCount < MAX_CONNECTION_RETRIES)
             {
                 wdt_reset(); // Reset watchdog before DHCP operation
                 EthernetDHCP.begin(NetMac, 1);
                 lastDHCPAttempt = millis();
                 dhcpRetryCount++;
             }
             else
             {
                 // Too many DHCP failures - stop trying to avoid reboot loop
                 dhcpRetryCount = 0;
                 wdt_reset(); // Reset watchdog
                 // Don't call Init() - just stop trying
             }
         }
     }
 }
 // partialReadPortalClient()
 //  - Only read PortalClient data for up to 2s
 //    so we don't block everything else
 void RA_Wiznet5100::partialReadPortalClient()
 {
     unsigned long readStart = millis();
 
     static String headerline = "";
     bool newline = false;
 
     while (PortalClient.available())
     { 
         // If we've been reading more than 2 seconds, break
         if (millis() - readStart > READ_LOOP_MAX_MS)
         {
             //Serial.println(F("Breaking from PortalClient read to avoid blocking too long."));
             break;
         }
         // We have data => system is "active"
         lastActivityMillis = millis();
         wdt_reset();
 
         // If we already saw the HTTP headers, we are reading firmware payload
         if (payload_ready)
         {
             if (PortalClient.available() > 32)
             {
                 // read 32 bytes in a burst
                 for (int i = 0; i < 32; i++)
                 {
                     sd_buffer[i] = PortalClient.read();
                 }
                 if (goodheader) firwareFile.write(sd_buffer, 32);
                 downloadsize += 32;
                 sd_index++;
                 // UI feedback
                 if (sd_index == 32)
                 {
                     sd_index = 0;
                     ReefAngel.Timer[PORTAL_TIMER].Start();
                     PortalTimeOut = millis();
                     ReefAngel.Font.DrawTextP(38, 9, DOWNLOADING);
                     ReefAngel.Font.DrawText((downloadsize * 100UL) / lheader);
                     ReefAngel.Font.DrawText("%");
                 }
             }
             else
             {
                 // read less than 32 bytes
                 char c = PortalClient.read();
                 downloadsize++;
                 if (goodheader) firwareFile.write(c);
             }
         }
         else
         {
             // We are still reading headers
             char c = PortalClient.read();
             downloadsize++;
             headerline += c;
             Serial.write(c);
 
             if (c == '\n')
             {
                 if (headerline.indexOf("200 OK") >= 0)
                     goodheader = true;
 
                 byte sheader = headerline.indexOf("Length");
                 if (sheader == 8)
                 {
                     lheader = headerline.substring(sheader + 8).toInt();
                 }
                 headerline = "";
                 newline = true;
 
                 // Peek next char(s)
                 if (PortalClient.available() > 0)
                 {
                     c = PortalClient.read();
                     headerline += c;
                     Serial.write(c);
 
                     if (c == '\r' && newline)
                     {
                         // Possibly read next char if it exists
                         if (PortalClient.available() > 0)
                         {
                             c = PortalClient.read();
                             Serial.write(c);
                         }
 
                         if (FirmwareConnection)
                         {
                             payload_ready = true;
                             if (lheader > 0) downloading = true;
                         }
                         downloadsize = 0;
                     }
                     else
                     {
                         newline = false;
                     }
                 }
             }
         }
     }
 
     if (PortalConnection) PortalDataReceived = true;
 }
 // checkPortalFirmwareStatus()
 //  - Checks if portal or firmware connection lost, etc.
 void RA_Wiznet5100::checkPortalFirmwareStatus()
 {
     // If server disconnected, but we had PortalConnection
     if (!PortalClient.connected() && PortalConnection)
     {
         PortalConnection = false;
         PortalClient.stop();
         
         if (!PortalDataReceived)
         {
             // Instead of full Init(), try to reconnect gracefully
             if (connectionRetryCount < MAX_CONNECTION_RETRIES && 
                 millis() - lastConnectionAttempt > CONNECTION_RETRY_DELAY)
             {
                 connectionRetryCount++;
                 lastConnectionAttempt = millis();
                 wdt_reset(); // Reset watchdog before connection attempt
                 PortalConnect();
                 return;
             }
             else if (connectionRetryCount >= MAX_CONNECTION_RETRIES)
             {
                 connectionRetryCount = 0;
                 // Don't call Init() - just stop trying to avoid reboot loop
                 PortalConnection = false;
                 FirmwareConnection = false;
                 PortalClient.stop();
                 return;
             }
         }
         
         PortalDataReceived = false;
         FirmwareConnection = true;
         PortalWaiting = false;
         FirmwareWaiting = false;
         downloadsize = 0;
        lheader = 0;
        payload_ready = false;
        goodheader = false;
        FirmwareConnect();
     }
     // If server disconnected, but we had FirmwareConnection
     else if (!PortalClient.connected() && FirmwareConnection)
     {
         Serial.print(F("Firmware data: "));
         Serial.println(downloadsize);
         Serial.print(F("Expected: "));
         Serial.println(lheader);
         Serial.println(F("Firmware disconnected"));
         FirmwareConnection = false;
         PortalWaiting = false;
         FirmwareWaiting = false;
         PortalClient.stop();
         payload_ready = false;
         if (firwareFile) firwareFile.close();
 
         // Check if full firmware downloaded
         if ((lheader == downloadsize) && (downloadsize > 600))
         {
             if (firwareFile) firwareFile.close();
             Serial.println(F("Updating firmware..."));
             InternalMemory.write(RemoteFirmware, 0xf0);
             while (1)
             {
                 //REBOOT
             }
         }
         else
         {
             // remove partial file
             if (SD.exists("FIRMWARE.BIN")) SD.remove("FIRMWARE.BIN");
         }
         downloadsize = 0;
         lheader = 0;
     }
     // If request timed out - handle more gracefully
     else if (PortalClient.connected() && (PortalConnection || FirmwareConnection) && 
              (millis() - PortalTimeOut > PORTAL_TIMEOUT))
     {
         PortalConnection = false;
         FirmwareConnection = false;
         PortalClient.stop();
         
         // Don't immediately reinitialize - try to reconnect first
         if (connectionRetryCount < MAX_CONNECTION_RETRIES && 
             millis() - lastConnectionAttempt > CONNECTION_RETRY_DELAY)
         {
             connectionRetryCount++;
             lastConnectionAttempt = millis();
             wdt_reset(); // Reset watchdog before connection attempt
             PortalConnect();
             return;
         }
         
         // Only reset if we've exhausted retries
         if (!PortalDataReceived)   
         {
             // Don't call Init() - just stop trying to avoid reboot loop
         }
         
         PortalDataReceived = false;
         PortalWaiting = false;
         FirmwareWaiting = false;
         downloadsize = 0;
         lheader = 0;
         payload_ready = false;
         if (firwareFile) firwareFile.close();
         if (SD.exists("FIRMWARE.BIN")) SD.remove("FIRMWARE.BIN");
     }
     // If we want firmware, are connected, but not waiting yet
     else if (IsPortalConnected() && FirmwareConnection && !PortalConnection && !FirmwareWaiting)
     {
         payload_ready = false;
         lheader = 0;
         FirmwareWaiting = true;
         firwareFile = SD.open("FIRMWARE.BIN", O_WRITE | O_CREAT | O_TRUNC);
         if (!firwareFile)
         {
             // Could not create firmware file
         }
         PortalClient.print("GET /firmwareupdate?u=");
         PortalClient.print(CLOUD_USERNAME);
         PortalClient.println(" HTTP/1.1");
         PortalClient.println("Host: forum.reefangel.com");
         PortalClient.println("Connection: close");
         PortalClient.println();
     }
 }
 // ReceiveData()
 //  - Check NetServer for new client, handle data
 void RA_Wiznet5100::ReceiveData()
 {
     if (FoundIP)
     {
         NetClient = NetServer.available();
         if (NetClient)
         {
             while (NetClient.connected())
             {
                 // We're active
                 lastActivityMillis = millis();
                 wdt_reset();
                 if (NetClient.available() > 0)
                 {
                     ProcessEthernet();
                 }
             }
         }
     }
 }
 //Firmware Connection Only
 void RA_Wiznet5100::ForceCheckFirmware()
 {
     if(FirmwareConnection)
     {
         //Serial.println(F("Already Checking for firmware."));
         return;
     }
     else
         PortalConnection = false;
         PortalClient.stop();
         PortalDataReceived = false;
         PortalWaiting = false;
         FirmwareWaiting = false;
         downloadsize = 0;
        lheader = 0;
        payload_ready = false;
        goodheader = false;
        FirmwareConnection = true;
        FirmwareConnect();
 }
 // ProcessEthernet()
 //  - Reads from NetClient until done
 void RA_Wiznet5100::ProcessEthernet()
 {
     bIncoming = true;
     timeout = millis();
     while (bIncoming)
     {
         if (millis() - timeout > 100)
         {
             bIncoming = false;
             NetClient.stop();
         }
         if (NetClient.available() > 0)
         {
             // Activity
             lastActivityMillis = millis();
             wdt_reset();
 
             PushBuffer(NetClient.read());
             timeout = millis();
             if (reqtype > 0 && reqtype < 128)
             {
                 bIncoming = false;
                 while (NetClient.available())
                 {
                     wdt_reset();
                     NetClient.read();
                 }
             }
         }
     }
     wdt_reset();
     ProcessHTTP();
 
     NetClient.stop();
     m_pushbackindex = 0;
 }
 // Portal / Firmware Connect
void RA_Wiznet5100::PortalConnect()
{
    int result;
    
    // Check if we already have the resolved IP
    if (!portalServerResolved) {
        // Try DNS resolution first, then connect with resolved IP
        DNSClient dns;
        IPAddress resolvedIP;
        
        // Use Google DNS (8.8.8.8) for reliable DNS resolution
        IPAddress dnsServer = IPAddress(8, 8, 8, 8);
        
        dns.begin(dnsServer);
        
        // Add timeout protection for DNS resolution
        unsigned long dnsStartTime = millis();
        int dnsResult = dns.getHostByName(PortalServerHost, resolvedIP);
        
        // Validate DNS result and timeout
        if (dnsResult == 1 && resolvedIP[0] != 0 && (millis() - dnsStartTime < DNS_RESOLUTION_TIMEOUT)) {
            // Validate IP address (not 0.0.0.0 or 255.255.255.255)
            if (resolvedIP != IPAddress(0, 0, 0, 0) && resolvedIP != IPAddress(255, 255, 255, 255)) {
                portalServerIP = resolvedIP;
                portalServerResolved = true;
            } else {
                portalServerResolved = false;
            }
        } else {
            portalServerResolved = false;
        }
    }
    
    // Connect using cached IP or hostname
    if (portalServerResolved) {
        result = PortalClient.noblockconnect(portalServerIP, 3000);
    } else {
        result = PortalClient.noblockconnect(PortalServerHost, 3000);
    }
    
    PortalTimeOut = millis();
}
void RA_Wiznet5100::FirmwareConnect()
{
    int result;
    
    // Check if we already have the resolved IP
    if (!firmwareServerResolved) {
        // Try DNS resolution first, then connect with resolved IP
        DNSClient dns;
        IPAddress resolvedIP;
        
        // Use Google DNS (8.8.8.8) for reliable DNS resolution
        IPAddress dnsServer = IPAddress(8, 8, 8, 8);
        
        dns.begin(dnsServer);
        
        // Add timeout protection for DNS resolution
        unsigned long dnsStartTime = millis();
        int dnsResult = dns.getHostByName(WebWizardServerHost, resolvedIP);
        
        // Validate DNS result and timeout
        if (dnsResult == 1 && resolvedIP[0] != 0 && (millis() - dnsStartTime < DNS_RESOLUTION_TIMEOUT)) {
            // Validate IP address (not 0.0.0.0 or 255.255.255.255)
            if (resolvedIP != IPAddress(0, 0, 0, 0) && resolvedIP != IPAddress(255, 255, 255, 255)) {
                firmwareServerIP = resolvedIP;
                firmwareServerResolved = true;
            } else {
                firmwareServerResolved = false;
            }
        } else {
            firmwareServerResolved = false;
        }
    }
    
    // Connect using cached IP or hostname
    if (firmwareServerResolved) {
        result = PortalClient.noblockconnect(firmwareServerIP, 3000);
    } else {
        result = PortalClient.noblockconnect(WebWizardServerHost, 3000);
    }
    
    PortalTimeOut = millis();
}
 boolean RA_Wiznet5100::IsPortalConnected()
 {
    // wdt_reset();
     // 0x17 indicates connected for RA's checkconnect
     return (PortalClient.checkconnect() == 0x17);
 }
 // IsMQTTConnected()
 boolean RA_Wiznet5100::IsMQTTConnected()
 {
     return MQTTClient.connected();
 }
 // Cloud Functions for Mqtt
void RA_Wiznet5100::Cloud() {
    wdt_reset(); // Reset watchdog at start of cloud operations
    
    // Only proceed with MQTT operations if we have confirmed internet connectivity
    if (FoundIP && hasInternet && internetFailureCount == 0) {
         if (payload_ready) {
             // Firmware payload is active, prioritize it
             partialReadPortalClient();
         } else {
             // Handle MQTT normally
             Portal(CLOUD_USERNAME);
             MQTTClient.loop();
 
             // Improved MQTT reconnection with exponential backoff
             if (millis() - MQTTReconnectmillis > 10000) {
                 if (!MQTTClient.connected()) {
                     char sub_buffer[sizeof(CLOUD_USERNAME) + 6];
                     MQTTReconnectmillis = millis();
                     Serial.println(F("MQTT Connecting..."));
                     sprintf(sub_buffer, "RA-%s", CLOUD_USERNAME);
 
                     if (MQTTClient.connect(sub_buffer, CLOUD_USERNAME, CLOUD_PASSWORD)) {
                         sprintf(sub_buffer, "%s/in/#", CLOUD_USERNAME);
                         Serial.println(F("MQTT succeeded"));
                         MQTTClient.subscribe(sub_buffer);
                         // Reset retry counter on successful connection
                         connectionRetryCount = 0;
                     } else {
                         Serial.println(F("MQTT failed"));
                         MQTTClient.disconnect();
                         // Don't increment portal retry counter for MQTT failures
                     }
                 }
             }
 
             // Publish changed parameters with error handling
             if (millis() - MQTTSendmillis > 1000 && MQTTClient.connected()) {
                 MQTTSendmillis = millis();
                 publishParams();
             }
         }
     } else {
         // No internet or IP - disconnect MQTT gracefully
         if (MQTTClient.connected()) {
             Serial.println(F("No internet - disconnecting MQTT"));
             MQTTClient.disconnect();
         }
     }
 }
 void RA_Wiznet5100::publishParams() {
     for (byte a = 0; a < NumParamByte; a++) {
         if (*ReefAngel.ParamArrayByte[a] != ReefAngel.OldParamArrayByte[a]) {
             char buffer[15];
             strcpy_P(buffer, (char*)pgm_read_word(&(param_items_byte[a])));
             sprintf(buffer, "%s:%d", buffer, *ReefAngel.ParamArrayByte[a]);
             CloudPublish(buffer);
             ReefAngel.OldParamArrayByte[a] = *ReefAngel.ParamArrayByte[a];
         }
     }
     for (byte a = 0; a < NumParamInt; a++) {
         if (*ReefAngel.ParamArrayInt[a] != ReefAngel.OldParamArrayInt[a]) {
             char buffer[15];
             strcpy_P(buffer, (char*)pgm_read_word(&(param_items_int[a])));
             sprintf(buffer, "%s:%d", buffer, *ReefAngel.ParamArrayInt[a]);
             CloudPublish(buffer);
             ReefAngel.OldParamArrayInt[a] = *ReefAngel.ParamArrayInt[a];
         }
     }
 }
 void RA_Wiznet5100::CloudPublish(char* message)
 {
     if (MQTTClient.connected())
     {
         char pub_buffer[sizeof(CLOUD_USERNAME) + 5];
         sprintf(pub_buffer, "%s/out", CLOUD_USERNAME);
         MQTTClient.publish(pub_buffer, message);
     }
 }
 
 // Connection stability methods
 void RA_Wiznet5100::ResetConnectionRetries()
 {
     connectionRetryCount = 0;
     dhcpRetryCount = 0;
     lastConnectionAttempt = millis();
     lastDHCPAttempt = millis();
     isReconnecting = false;
 }
 
 boolean RA_Wiznet5100::IsConnectionStable()
 {
     // Consider connection stable if we have IP and haven't had recent failures
     return FoundIP && 
            connectionRetryCount < MAX_CONNECTION_RETRIES && 
            dhcpRetryCount < MAX_CONNECTION_RETRIES &&
            !isReconnecting;
 }
 
// Internet connectivity methods
boolean RA_Wiznet5100::CheckInternetConnectivity()
{
    // If we're already checking, continue the check
    if (internetCheckStarted) {
        if (testClient == nullptr) {
            // Cleanup if client was destroyed
            internetCheckStarted = false;
            internetCheckInProgress = false;
            return false;
        }
        
        // Check if we've exceeded the timeout
        if (millis() - internetCheckStartTime > INTERNET_CHECK_TIMEOUT) {
            // Timeout - connection failed
            testClient->stop();
            delete testClient;
            testClient = nullptr;
            internetCheckStarted = false;
            internetCheckInProgress = false;
            hasInternet = false;
            internetFailureCount++;
            return false;
        }
        
        // Check connection status
        int status = testClient->checkconnect();
        
        if (status == 0x17) { // ESTABLISHED - Connected
            testClient->stop();
            delete testClient;
            testClient = nullptr;
            internetCheckStarted = false;
            internetCheckInProgress = false;
            hasInternet = true;
            internetFailureCount = 0;
            return true;
        } else if (status == 0x00 || status == 0x1C) { // CLOSED or CLOSE_WAIT - Connection failed
            testClient->stop();
            delete testClient;
            testClient = nullptr;
            internetCheckStarted = false;
            internetCheckInProgress = false;
            hasInternet = false;
            internetFailureCount++;
            return false;
        }
        
        // Still connecting, check again next time
        return false;
    }
    
    // Start new internet check
    internetCheckInProgress = true;
    lastInternetCheck = millis();
    
    wdt_reset(); // Reset watchdog before internet check
    
    // Create new test client
    testClient = new EthernetClient();
    internetCheckStartTime = millis();
    
    // Try non-blocking connect to Cloudflare DNS (1.1.1.1) on port 443 (HTTPS)
    int connectResult = testClient->noblockconnect(IPAddress(1, 1, 1, 1), 443);
    
    if (connectResult == 1) {
        // Connection initiated successfully
        internetCheckStarted = true;
        return false; // Will complete in next call
    } else {
        // Connection failed immediately
        delete testClient;
        testClient = nullptr;
        internetCheckInProgress = false;
        hasInternet = false;
        internetFailureCount++;
        return false;
    }
}
 
 boolean RA_Wiznet5100::HasInternetConnection()
 {
     return hasInternet;
 }
 
 void RA_Wiznet5100::ResetInternetFailures()
 {
     internetFailureCount = 0;
     hasInternet = false; // Don't assume internet is available - wait for actual check
     lastInternetCheck = millis() - INTERNET_CHECK_INTERVAL; // Force immediate internet check
 }
 
 // DNS resolution methods
 boolean RA_Wiznet5100::ResolveMQTTServer()
 {
     if (!hasInternet) {
         mqttServerResolved = false;
         MQTTClient.setServer(MQTTServerHost, MQTTPORT);
         return false;
     }
     
     wdt_reset(); // Reset watchdog before DNS resolution
     
     // DNS resolution using DNSClient with timeout protection
     DNSClient dns;
     IPAddress resolvedIP;
     
    // Use Google DNS (8.8.8.8) for reliable DNS resolution
    IPAddress dnsServer = IPAddress(8, 8, 8, 8);
     
     dns.begin(dnsServer);
     
     // Add timeout protection for DNS resolution with watchdog resets
     unsigned long dnsStartTime = millis();
     wdt_reset(); // Reset watchdog before DNS call
     
     int result = dns.getHostByName(MQTTServerHost, resolvedIP);
     
     wdt_reset(); // Reset watchdog after DNS call
     
     // If DNS takes too long, abort to prevent blocking
     if (millis() - dnsStartTime > DNS_RESOLUTION_TIMEOUT) { // DNS timeout
         mqttServerResolved = false;
         return false;
     }
     
    if (result == 1 && resolvedIP[0] != 0) {
        // Validate IP address (not 0.0.0.0 or 255.255.255.255)
        if (resolvedIP != IPAddress(0, 0, 0, 0) && resolvedIP != IPAddress(255, 255, 255, 255)) {
            mqttServerIP = resolvedIP;
            mqttServerResolved = true;
            MQTTClient.setServer(mqttServerIP, MQTTPORT);
            return true;
        } else {
            mqttServerResolved = false;
            return false;
        }
    }
     
     // DNS resolution failed
     mqttServerResolved = false;
     wdt_reset(); // Reset watchdog after DNS failure
     return false;
 }
 // Write Overloads
 size_t RA_Wiznet5100::write(uint8_t c)
 {
     if (PortalConnection)
     {
         if (PortalClient.connected())
         {
             return PortalClient.write(c);
         }
         else
         {
             PortalConnection = false;
             return 0;
         }
     }
     else
     {
         if (NetClient.connected())
         {
             return NetClient.write(c);
         }
         else
         {
             return 0;
         }
     }
 }
 #endif // ETH_WIZ5100
 