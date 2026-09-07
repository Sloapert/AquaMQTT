#ifndef AQUAMQTT_OTAWEBUPDATE_H
#define AQUAMQTT_OTAWEBUPDATE_H

#include <WebServer.h>

namespace aquamqtt
{

/**
 * Registers a page at http://<device-ip>/update which accepts a firmware (.bin) upload from a browser,
 * as an alternative to network OTA via PlatformIO/Arduino IDE or a USB/serial flash. See
 * config::ENABLE_OTA_WEBUPDATE. Routes are registered on a WebServer instance owned and driven
 * (begin()/handleClient()) elsewhere, so this can share one web server/port with other pages.
 */
class OTAWebUpdateHandler
{
public:
    explicit OTAWebUpdateHandler(WebServer& server);

    virtual ~OTAWebUpdateHandler() = default;

    void setup();

private:
    void handleUpdatePage();

    void handleUpdateUpload();

    void handleUpdateResult();

    WebServer& mServer;

    bool mUpdateStarted;
    bool mUpdateAborted;
};

}  // namespace aquamqtt

#endif  // AQUAMQTT_OTAWEBUPDATE_H
