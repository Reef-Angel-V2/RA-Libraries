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
}

// Update()
//  - Called repeatedly in main loop
//  - Polls DHCP, checks IP, partial-reads portal data, etc.
void RA_Wiznet5100::Update()
{
    EthernetDHCP.poll();
    // Check if we have an IP address
    const byte* ipAddr = EthernetDHCP.ipAddress();
    if (ipAddr[0] != 0)
    {
        if (!FoundIP)
        {
            Serial.println(ip_to_str(ipAddr));
        }
        FoundIP = true;
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
        Serial.println(F("Disconnected"));
        PortalConnection = false;
        PortalClient.stop();
        if (!PortalDataReceived)
        {
          Init();  // Reinitialize network settings
          return;  // Exit the current function call after reinitialization
        }
        PortalDataReceived = false;
        FirmwareConnection = true;
        PortalWaiting = false;
        FirmwareWaiting = false;
        downloadsize = 0;
        lheader = 0;
        payload_ready = false;
        goodheader = false;
        delay(100);
        FirmwareConnect();
        Serial.println(F("Connecting..."));
    }
    // If server disconnected, but we had FirmwareConnection
 else   if (!PortalClient.connected() && FirmwareConnection)
    {
        Serial.print(F("Data: "));
        Serial.println(downloadsize);
        Serial.print(F("Header: "));
        Serial.println(lheader);
        Serial.println(F("Disconnected"));
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
            Serial.println(F("Updating..."));
            InternalMemory.write(RemoteFirmware, 0xf0);
            while (1)
            {
                //REEBOOT
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
    // If request timed out
  else  if (PortalClient.connected() && (PortalConnection || FirmwareConnection) && (millis() - PortalTimeOut > PORTAL_TIMEOUT))
    {
        Serial.println(F("Portal Timeout"));
        Serial.println(downloadsize);
        PortalConnection = false;
        FirmwareConnection = false;
        PortalClient.stop();
        if (!PortalDataReceived)   Init();
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
 else  if (IsPortalConnected() && FirmwareConnection && !PortalConnection && !FirmwareWaiting)
    {
        payload_ready = false;
        lheader = 0;
        FirmwareWaiting = true;
        firwareFile = SD.open("FIRMWARE.BIN", O_WRITE | O_CREAT | O_TRUNC);
        if (!firwareFile)
        {
            Serial.println(F("Could not create file"));
        }
        Serial.println(F("Connected"));
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
    if (!FoundIP)
    {
        //Serial.println(F("No IP address available. Cannot check for firmware."));
        return;
    }
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
        delay(100);
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
    PortalClient.noblockconnect(PortalServer, 3000);
    PortalTimeOut = millis();
}
void RA_Wiznet5100::FirmwareConnect()
{
    PortalClient.noblockconnect(WebWizardServer, 3000);
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
    if (FoundIP) {
        if (payload_ready) {
            // Firmware payload is active, prioritize it
            partialReadPortalClient();
        } else {
            // Handle MQTT normally
            Portal(CLOUD_USERNAME);
            MQTTClient.loop();

            if (millis() - MQTTReconnectmillis > 5000) {
                if (!MQTTClient.connected()) {
                    char sub_buffer[sizeof(CLOUD_USERNAME) + 6];
                    MQTTReconnectmillis = millis();
                    Serial.println(F("MQTT Connecting..."));
                    sprintf(sub_buffer, "RA-%s", CLOUD_USERNAME);

                    if (MQTTClient.connect(sub_buffer, CLOUD_USERNAME, CLOUD_PASSWORD)) {
                        sprintf(sub_buffer, "%s/in/#", CLOUD_USERNAME);
                        Serial.println(F("MQTT succeeded"));
                        MQTTClient.subscribe(sub_buffer);
                    } else {
                        Serial.println(F("MQTT failed"));
                        MQTTClient.disconnect();
                    }
                }
            }

            // Publish changed parameters
            if (millis() - MQTTSendmillis > 1000 && MQTTClient.connected()) {
                MQTTSendmillis = millis();
                publishParams();
            }
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
