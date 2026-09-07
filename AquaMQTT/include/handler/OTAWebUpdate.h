#ifndef AQUAMQTT_OTAWEBUPDATE_H
#define AQUAMQTT_OTAWEBUPDATE_H

#include <WebServer.h>

namespace aquamqtt
{

/**
 * Serves a small web page at http://<device-ip>/update which accepts a firmware (.bin) upload from a
 * browser, as an alternative to network OTA via PlatformIO/Arduino IDE or a USB/serial flash. See
 * config::ENABLE_OTA_WEBUPDATE.
 */
class OTAWebUpdateHandler
{
public:
    OTAWebUpdateHandler();

    virtual ~OTAWebUpdateHandler() = default;

    void setup();

    void loop();

private:
    void handleUpdatePage();

    void handleUpdateUpload();

    void handleUpdateResult();

    WebServer mServer;

    bool mUpdateStarted;
    bool mUpdateAborted;
};

}  // namespace aquamqtt

#endif  // AQUAMQTT_OTAWEBUPDATE_H
