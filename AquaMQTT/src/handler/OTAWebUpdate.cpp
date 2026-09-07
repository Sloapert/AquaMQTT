#include "handler/OTAWebUpdate.h"

#include <Update.h>
#include <esp_task_wdt.h>

#include "config/Configuration.h"

namespace aquamqtt
{

namespace
{
constexpr char UPDATE_PAGE[] =
        "<!DOCTYPE html><html><head><title>AquaMQTT Firmware Update</title></head><body>"
        "<h3>AquaMQTT Firmware Update</h3>"
        "<p>Select a firmware .bin file built for this device and upload it. The device reboots "
        "automatically once the update has been applied successfully.</p>"
        "<form method='POST' action='/update' enctype='multipart/form-data'>"
        "<input type='file' name='firmware' accept='.bin'>"
        "<input type='submit' value='Upload'>"
        "</form></body></html>";

// the first byte of a valid esp32 application image is always this magic byte
constexpr uint8_t ESP32_APP_IMAGE_MAGIC_BYTE = 0xE9;
}  // namespace

OTAWebUpdateHandler::OTAWebUpdateHandler() : mServer(80), mUpdateStarted(false), mUpdateAborted(false)
{
}

void OTAWebUpdateHandler::setup()
{
    if (!config::ENABLE_OTA_WEBUPDATE)
    {
        return;
    }

    mServer.on("/update", HTTP_GET, [this]() { handleUpdatePage(); });

    mServer.on(
            "/update",
            HTTP_POST,
            [this]() { handleUpdateResult(); },
            [this]() { handleUpdateUpload(); });

    mServer.begin();
}

void OTAWebUpdateHandler::loop()
{
    if (!config::ENABLE_OTA_WEBUPDATE)
    {
        return;
    }

    mServer.handleClient();
}

void OTAWebUpdateHandler::handleUpdatePage()
{
    mServer.send(200, "text/html", UPDATE_PAGE);
}

void OTAWebUpdateHandler::handleUpdateUpload()
{
    // long-running uploads must not trip the watchdog of the task driving this loop
    esp_task_wdt_reset();

    HTTPUpload& upload = mServer.upload();

    if (upload.status == UPLOAD_FILE_START)
    {
        mUpdateStarted = false;
        mUpdateAborted = false;

        Serial.print("[ota-web] receiving update: ");
        Serial.println(upload.filename);

        // basic sanity check: only accept files that look like a firmware binary, not just any upload
        if (!upload.filename.endsWith(".bin"))
        {
            Serial.println("[ota-web] rejected: filename does not end with .bin");
            mUpdateAborted = true;
            return;
        }

        if (!Update.begin(UPDATE_SIZE_UNKNOWN))
        {
            Serial.println("[ota-web] failed to start update, is there enough free space?");
            mUpdateAborted = true;
            return;
        }

        mUpdateStarted = true;
    }
    else if (upload.status == UPLOAD_FILE_WRITE)
    {
        if (!mUpdateStarted || mUpdateAborted)
        {
            return;
        }

        // on the very first chunk, also verify the esp32 application image header before writing anything,
        // instead of only relying on the uploaded filename
        if (upload.totalSize == upload.currentSize && upload.currentSize > 0
            && upload.buf[0] != ESP32_APP_IMAGE_MAGIC_BYTE)
        {
            Serial.println("[ota-web] rejected: not a valid esp32 firmware image");
            Update.abort();
            mUpdateAborted = true;
            return;
        }

        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
        {
            Serial.println("[ota-web] write failed");
            Update.abort();
            mUpdateAborted = true;
        }
    }
    else if (upload.status == UPLOAD_FILE_END)
    {
        if (mUpdateStarted && !mUpdateAborted && Update.end(true))
        {
            Serial.printf("[ota-web] update successful: %u bytes\n", upload.totalSize);
        }
        else
        {
            Serial.println("[ota-web] update failed or was rejected");
            mUpdateAborted = true;
        }
    }
    else if (upload.status == UPLOAD_FILE_ABORTED)
    {
        Update.abort();
        mUpdateAborted = true;
    }
}

void OTAWebUpdateHandler::handleUpdateResult()
{
    if (mUpdateStarted && !mUpdateAborted && !Update.hasError())
    {
        mServer.send(200, "text/plain", "Update successful, rebooting...");
        mServer.client().stop();
        delay(500);
        ESP.restart();
    }
    else
    {
        mServer.send(400, "text/plain", "Update failed or was rejected, see serial log for details");
    }
}

}  // namespace aquamqtt
