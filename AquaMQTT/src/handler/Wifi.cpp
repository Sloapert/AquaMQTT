#include "handler/Wifi.h"

#include "config/Configuration.h"

namespace aquamqtt
{

bool WifiHandler::mConnectedToWifiWithValidIpAddress = false;

WifiHandler::WifiHandler() : mLastCheck(0)
{
}

void WifiHandler::setup()
{
    WiFiClass::mode(WIFI_STA);

    // we don't trust the auto reconnect routine, as it seems there are edge cases where it does not work
    WiFi.setAutoReconnect(false);

    // we trust the wifi callbacks to determine if we are properly connected or disconnected
    WiFi.onEvent(wifiCallback);

    // On the very first boot (i.e. no wifi network has ever been associated with this device, neither
    // through the captive portal below nor a previous firmware version), seed the wifi credentials from
    // Configuration.h so existing installs keep working without having to go through the portal. Once a
    // network has been associated (via this fallback or the portal), it is persisted by the esp32 and this
    // is skipped on subsequent boots.
    if (WiFi.SSID().length() == 0 && strlen(config::ssid) > 0)
    {
        WiFi.begin(config::ssid, config::psk);
    }

    // Run the config portal in the background instead of blocking setup(). If no wifi network is reachable,
    // this starts an access point named after config::networkName so a phone or laptop can connect to it
    // and enter wifi credentials through a web page served at 192.168.4.1. The DHW serial relay tasks are
    // independent of wifi and keep controlling/monitoring the heatpump normally while the portal is open.
    mWifiManager.setConfigPortalBlocking(false);
    mWifiManager.setHostname(config::networkName);
    mWifiManager.autoConnect(config::networkName);

    // perform the next wifi check in config::WIFI_RECONNECT_CYCLE_S
    mLastCheck = millis();
}

void WifiHandler::loop()
{
    // drives the non-blocking config portal while it is active, no-op otherwise
    mWifiManager.process();

    if ((millis() - mLastCheck) >= (config::WIFI_RECONNECT_CYCLE_S * 1000))
    {
        mLastCheck = millis();

        // we don't trust WiFi.isConnected() or WiFi.status() == WL_CONNECTED, since it is suspected to be unreliable
        // don't fight the config portal for the radio while the user is entering wifi credentials
        if (!mConnectedToWifiWithValidIpAddress && !mWifiManager.getConfigPortalActive())
        {
            Serial.println("[wifi] attempting reconnect");
            WiFi.disconnect();
            WiFi.reconnect();
        }
    }
}

void WifiHandler::wifiCallback(WiFiEvent_t event)
{
    Serial.print("[wifi] event: ");
    Serial.println(WiFi.eventName(event));

    switch (event)
    {
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        case ARDUINO_EVENT_WIFI_STA_LOST_IP:
            // if we lost connection or ip address, we wil enforce a reconnect within the next cycle
            mConnectedToWifiWithValidIpAddress = false;
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        case ARDUINO_EVENT_WIFI_STA_GOT_IP6:
            // if we got connection and therefore a valid ip address we have a valid connection
            Serial.print("[wifi] ip address: ");
            Serial.println(WiFi.localIP().toString().c_str());
            mConnectedToWifiWithValidIpAddress = true;
            break;
        default:
            break;
    }
}

}  // namespace aquamqtt
