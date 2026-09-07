#ifndef AQUAMQTT_STATUSPAGE_H
#define AQUAMQTT_STATUSPAGE_H

#include <ArduinoJson.h>
#include <WebServer.h>

#include "message/MessageConstants.h"

namespace aquamqtt
{

/**
 * Registers a status page at http://<device-ip>/ showing the latest known boiler information and
 * settings (water temperature, operation mode, energy counters, AquaMQTT overrides, wifi/system info,
 * ...), and the same data as JSON at http://<device-ip>/status.json which the page polls periodically.
 * Routes are registered on a WebServer instance owned and driven (begin()/handleClient()) elsewhere, so
 * this can share one web server/port with other pages.
 */
class StatusPageHandler
{
public:
    explicit StatusPageHandler(WebServer& server);

    virtual ~StatusPageHandler() = default;

    void setup();

private:
    void handleStatusPage();

    void handleStatusJson();

    void addSystemStatus(JsonObject& target) const;

    void addOverrideStatus(JsonObject& target) const;

    void addMainStatus(JsonObject& target) const;

    void addHmiStatus(JsonObject& target) const;

    void addEnergyStatus(JsonObject& target) const;

    void addExtraStatus(JsonObject& target) const;

    void addErrorStatus(JsonObject& target) const;

    WebServer& mServer;

    // scratch buffer used to read the latest frames from DHWState; mutable since reading a frame is a
    // logically-const operation from the perspective of the add*Status() methods that use it
    mutable uint8_t mTransferBuffer[message::HEATPUMP_MAX_FRAME_LENGTH];
};

}  // namespace aquamqtt

#endif  // AQUAMQTT_STATUSPAGE_H
