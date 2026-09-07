#include "handler/StatusPage.h"

#include <WiFi.h>
#include <message/Factory.h>

#include "config/Configuration.h"
#include "mqtt/MQTTDefinitions.h"
#include "state/DHWState.h"
#include "state/HMIStateProxy.h"
#include "state/MainStateProxy.h"

using namespace aquamqtt::message;
using namespace aquamqtt::mqtt;

namespace aquamqtt
{

namespace
{
constexpr char STATUS_PAGE[] =
        "<!DOCTYPE html><html><head><title>AquaMQTT Status</title>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<meta charset='utf-8'>"
        "<style>"
        ":root{--bg:#f2f5f4;--surface:#fff;--surface-alt:#eef2f0;--border:#dbe1de;--ink:#1a2422;"
        "--muted:#66716d;--accent:#0f6e6a;--accent-soft:#e1f1ef;--good:#2f7d4f;--good-soft:#e1f2e6;"
        "--off-soft:#eceeed}"
        "@media(prefers-color-scheme:dark){:root{--bg:#101614;--surface:#182220;--surface-alt:#1e2926;"
        "--border:#2c3835;--ink:#e9efec;--muted:#93a29c;--accent:#3fc2b7;--accent-soft:#173432;"
        "--good:#5fce8c;--good-soft:#123420;--off-soft:#232e2b}}"
        "*{box-sizing:border-box}"
        "body{font-family:-apple-system,Segoe UI,Roboto,Helvetica,Arial,sans-serif;margin:0;"
        "background:var(--bg);color:var(--ink)}"
        "header{display:flex;justify-content:space-between;align-items:baseline;flex-wrap:wrap;gap:8px;"
        "padding:18px 20px 14px;border-bottom:1px solid var(--border)}"
        "h1{font-size:22px;margin:0}"
        "h2{font-size:14px;text-transform:uppercase;letter-spacing:.04em;color:var(--muted);"
        "margin:0;padding:10px 14px;background:var(--surface-alt);border-bottom:1px solid var(--border)}"
        "#updated{color:var(--muted);font-size:12px}"
        "main{max-width:960px;margin:0 auto;padding:16px 20px 40px}"
        "#sections{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:14px}"
        ".card{background:var(--surface);border:1px solid var(--border);border-radius:10px;overflow:hidden;"
        "box-shadow:0 1px 2px rgba(0,0,0,.05)}"
        ".rows{padding:2px 0}"
        ".row{display:flex;justify-content:space-between;gap:12px;padding:7px 14px;font-size:13.5px;"
        "border-bottom:1px solid var(--border)}"
        ".row:last-child{border-bottom:none}"
        ".k{color:var(--muted)}"
        ".v{font-variant-numeric:tabular-nums;text-align:right;white-space:nowrap}"
        ".pill{font-size:11.5px;padding:2px 8px;border-radius:999px;font-weight:600}"
        ".pill.true{background:var(--good-soft);color:var(--good)}"
        ".pill.false{background:var(--off-soft);color:var(--muted)}"
        "footer{max-width:960px;margin:0 auto;padding:6px 20px 24px;font-size:12.5px;color:var(--muted)}"
        "a{color:var(--accent)}"
        "</style></head><body>"
        "<header><h1>AquaMQTT Status</h1><span id='updated'>loading...</span></header>"
        "<main><div id='sections'></div></main>"
        "<footer><a href='/update'>Firmware update</a></footer>"
        "<script>"
        "function fmt(v){"
        "if(typeof v==='boolean'){"
        "var s=document.createElement('span');s.className='pill '+v;s.textContent=v;return s;"
        "}"
        "var s=document.createElement('span');s.textContent=v;return s;"
        "}"
        "function render(data){"
        "var el=document.getElementById('sections');el.innerHTML='';"
        "for(var section in data){"
        "var card=document.createElement('div');card.className='card';"
        "var h=document.createElement('h2');h.textContent=section;card.appendChild(h);"
        "var rows=document.createElement('div');rows.className='rows';"
        "var body=data[section];"
        "for(var key in body){"
        "var row=document.createElement('div');row.className='row';"
        "var k=document.createElement('span');k.className='k';k.textContent=key;"
        "var v=document.createElement('span');v.className='v';v.appendChild(fmt(body[key]));"
        "row.appendChild(k);row.appendChild(v);rows.appendChild(row);"
        "}"
        "card.appendChild(rows);el.appendChild(card);"
        "}"
        "document.getElementById('updated').textContent='last updated '+new Date().toLocaleTimeString();"
        "}"
        "function refresh(){"
        "fetch('/status.json').then(function(r){return r.json()}).then(render)"
        ".catch(function(e){document.getElementById('updated').textContent='failed to load status: '+e;});"
        "}"
        "refresh();setInterval(refresh,3000);"
        "</script></body></html>";
}  // namespace

StatusPageHandler::StatusPageHandler(WebServer& server) : mServer(server), mTransferBuffer{}
{
}

void StatusPageHandler::setup()
{
    if (!config::ENABLE_STATUS_WEBPAGE)
    {
        return;
    }

    mServer.on("/", HTTP_GET, [this]() { handleStatusPage(); });
    mServer.on("/status.json", HTTP_GET, [this]() { handleStatusJson(); });
}

void StatusPageHandler::handleStatusPage()
{
    mServer.send(200, "text/html", STATUS_PAGE);
}

void StatusPageHandler::handleStatusJson()
{
    JsonDocument doc;

    JsonObject systemStatus = doc["system"].to<JsonObject>();
    addSystemStatus(systemStatus);

    JsonObject overrideStatus = doc["overrides"].to<JsonObject>();
    addOverrideStatus(overrideStatus);

    JsonObject mainStatus = doc["main"].to<JsonObject>();
    addMainStatus(mainStatus);

    JsonObject hmiStatus = doc["hmi"].to<JsonObject>();
    addHmiStatus(hmiStatus);

    JsonObject energyStatus = doc["energy"].to<JsonObject>();
    addEnergyStatus(energyStatus);
    addExtraStatus(energyStatus);

    JsonObject errorStatus = doc["error"].to<JsonObject>();
    addErrorStatus(errorStatus);
    if (errorStatus.size() == 0)
    {
        doc.remove("error");
    }

    String output;
    serializeJson(doc, output);
    mServer.send(200, "application/json", output);
}

void StatusPageHandler::addSystemStatus(JsonObject& target) const
{
    target[STATS_AQUAMQTT_MODE]
            = config::OPERATION_MODE == config::EOperationMode::LISTENER ? ENUM_AQUAMQTT_MODE_LISTENER
                                                                          : ENUM_AQUAMQTT_MODE_MITM;
    target[STATS_AQUAMQTT_PROTOCOL] = protocolVersionStr(DHWState::getInstance().getVersion());
    target[STATS_AQUAMQTT_ADDR]     = WiFi.localIP().toString();
    target[STATS_AQUAMQTT_RSSI]     = WiFi.RSSI();
    target["uptimeSeconds"]         = millis() / 1000;
    target["freeHeapBytes"]         = ESP.getFreeHeap();

    if (config::OPERATION_MODE != config::EOperationMode::LISTENER)
    {
        auto hmiStats  = DHWState::getInstance().getFrameBufferStatistics(FrameBufferChannel::CH_HMI);
        auto mainStats = DHWState::getInstance().getFrameBufferStatistics(FrameBufferChannel::CH_MAIN);
        target["hmiBusMsgHandled"]    = hmiStats.msgHandled;
        target["hmiBusMsgCRCFailed"]  = hmiStats.msgCRCFail;
        target["mainBusMsgHandled"]   = mainStats.msgHandled;
        target["mainBusMsgCRCFailed"] = mainStats.msgCRCFail;
    }
    else
    {
        auto listenerStats = DHWState::getInstance().getFrameBufferStatistics(FrameBufferChannel::CH_LISTENER);
        target["busMsgHandled"]   = listenerStats.msgHandled;
        target["busMsgCRCFailed"] = listenerStats.msgCRCFail;
    }
}

void StatusPageHandler::addOverrideStatus(JsonObject& target) const
{
    target[STATS_AQUAMQTT_OVERRIDE_MODE] = aquamqttOverrideStr(HMIStateProxy::getInstance().getOverrideMode());
    target[STATS_ENABLE_FLAG_PV_HEATPUMP]    = HMIStateProxy::getInstance().isPVModeHeatPumpEnabled();
    target[STATS_ENABLE_FLAG_PV_HEATELEMENT] = HMIStateProxy::getInstance().isPVModeHeatElementEnabled();
}

void StatusPageHandler::addMainStatus(JsonObject& target) const
{
    ProtocolVersion  version      = PROTOCOL_UNKNOWN;
    ProtocolChecksum checksumType = CHECKSUM_TYPE_UNKNOWN;
    const size_t length = MainStateProxy::getInstance().copyFrame(MAIN_MESSAGE_IDENTIFIER, mTransferBuffer, version, checksumType);
    if (length == 0)
    {
        return;
    }

    std::unique_ptr<IMainMessage> message = createMainMessageFromBuffer(version, mTransferBuffer);

    if (message->hasAttr(MAIN_ATTR_FLOAT::WATER_TEMPERATURE))
        target[MAIN_HOT_WATER_TEMP] = message->getAttr(MAIN_ATTR_FLOAT::WATER_TEMPERATURE);
    if (message->hasAttr(MAIN_ATTR_FLOAT::WATER_LOWER_TEMPERATURE))
        target[MAIN_HOT_WATER_TEMP_LOWER] = message->getAttr(MAIN_ATTR_FLOAT::WATER_LOWER_TEMPERATURE);
    if (message->hasAttr(MAIN_ATTR_FLOAT::WATER_UPPER_TEMPERATURE))
        target[MAIN_HOT_WATER_TEMP_UPPER] = message->getAttr(MAIN_ATTR_FLOAT::WATER_UPPER_TEMPERATURE);
    if (message->hasAttr(MAIN_ATTR_FLOAT::AIR_TEMPERATURE))
        target[MAIN_SUPPLY_AIR_TEMP] = message->getAttr(MAIN_ATTR_FLOAT::AIR_TEMPERATURE);
    if (message->hasAttr(MAIN_ATTR_FLOAT::EVAPORATOR_UPPER_TEMPERATURE))
        target[MAIN_EVAPORATOR_AIR_TEMP_UPPER] = message->getAttr(MAIN_ATTR_FLOAT::EVAPORATOR_UPPER_TEMPERATURE);
    if (message->hasAttr(MAIN_ATTR_FLOAT::EVAPORATOR_LOWER_TEMPERATURE))
        target[MAIN_EVAPORATOR_AIR_TEMP_LOWER] = message->getAttr(MAIN_ATTR_FLOAT::EVAPORATOR_LOWER_TEMPERATURE);
    if (message->hasAttr(MAIN_ATTR_FLOAT::COMPRESSOR_OUTLET_TEMPERATURE))
        target[MAIN_COMPRESSOR_OUTLET_TEMP] = message->getAttr(MAIN_ATTR_FLOAT::COMPRESSOR_OUTLET_TEMPERATURE);
    if (message->hasAttr(MAIN_ATTR_FLOAT::FAN_SPEED_PWM))
        target[MAIN_FAN_PWM] = message->getAttr(MAIN_ATTR_FLOAT::FAN_SPEED_PWM);

    if (message->hasAttr(MAIN_ATTR_BOOL::STATE_HEATING_ELEMENT))
        target[MAIN_STATE_HEAT_ELEMENT] = message->getAttr(MAIN_ATTR_BOOL::STATE_HEATING_ELEMENT);
    if (message->hasAttr(MAIN_ATTR_BOOL::STATE_HEATPUMP))
        target[MAIN_STATE_HEATPUMP] = message->getAttr(MAIN_ATTR_BOOL::STATE_HEATPUMP);
    if (message->hasAttr(MAIN_ATTR_BOOL::STATE_BOILER_BACKUP))
        target[MAIN_STATE_EXT_BOILER] = message->getAttr(MAIN_ATTR_BOOL::STATE_BOILER_BACKUP);
    if (message->hasAttr(MAIN_ATTR_BOOL::STATE_FAN))
        target[MAIN_STATE_FAN] = message->getAttr(MAIN_ATTR_BOOL::STATE_FAN);
    if (message->hasAttr(MAIN_ATTR_BOOL::STATE_DEFROST))
        target[MAIN_STATE_DEFROST] = message->getAttr(MAIN_ATTR_BOOL::STATE_DEFROST);
    if (message->hasAttr(MAIN_ATTR_BOOL::STATE_PV))
        target[MAIN_STATE_PV] = message->getAttr(MAIN_ATTR_BOOL::STATE_PV);
    if (message->hasAttr(MAIN_ATTR_BOOL::STATE_SOLAR))
        target[MAIN_STATE_SOLAR] = message->getAttr(MAIN_ATTR_BOOL::STATE_SOLAR);
    if (message->hasAttr(MAIN_ATTR_BOOL::CAPABILITY_HAS_HEAT_EXCHANGER))
        target[MAIN_CAPABILITY_HEAT_EXC] = message->getAttr(MAIN_ATTR_BOOL::CAPABILITY_HAS_HEAT_EXCHANGER);
    if (message->hasAttr(MAIN_ATTR_BOOL::CAPABILITY_HAS_CIRCULATION))
        target[MAIN_CAPABILITY_CIRCULATION] = message->getAttr(MAIN_ATTR_BOOL::CAPABILITY_HAS_CIRCULATION);
    if (message->hasAttr(MAIN_ATTR_BOOL::CAPABILITY_HAS_PV_INPUT))
        target[MAIN_CAPABILITY_PV_INPUT] = message->getAttr(MAIN_ATTR_BOOL::CAPABILITY_HAS_PV_INPUT);
    if (message->hasAttr(MAIN_ATTR_BOOL::CAPABILITY_HAS_COMMUNICATION))
        target[MAIN_CAPABILITY_EXT_COMM] = message->getAttr(MAIN_ATTR_BOOL::CAPABILITY_HAS_COMMUNICATION);
    if (message->hasAttr(MAIN_ATTR_BOOL::CAPABILITY_HAS_ANTI_DRY_HEATING))
        target[MAIN_CAPABILITY_DRY_HEATING] = message->getAttr(MAIN_ATTR_BOOL::CAPABILITY_HAS_ANTI_DRY_HEATING);

    if (message->hasAttr(MAIN_ATTR_U8::ERROR_CODE))
        target[MAIN_ERROR_CODE] = message->getAttr(MAIN_ATTR_U8::ERROR_CODE);
    if (message->hasAttr(MAIN_ATTR_U8::SETTING_FAN_PWM_FIRST))
        target[MAIN_SETTING_PWM_01] = message->getAttr(MAIN_ATTR_U8::SETTING_FAN_PWM_FIRST);
    if (message->hasAttr(MAIN_ATTR_U8::SETTING_FAN_PWM_SECOND))
        target[MAIN_SETTING_PWM_02] = message->getAttr(MAIN_ATTR_U8::SETTING_FAN_PWM_SECOND);
    if (message->hasAttr(MAIN_ATTR_U8::SETTING_FAN_PWM_THIRD))
        target[MAIN_SETTING_PWM_03] = message->getAttr(MAIN_ATTR_U8::SETTING_FAN_PWM_THIRD);
    if (message->hasAttr(MAIN_ATTR_U8::SETTING_MIN_TARGET_WATER_TEMPERATURE))
        target[MAIN_SETTING_MIN_TEMP_TARGET] = message->getAttr(MAIN_ATTR_U8::SETTING_MIN_TARGET_WATER_TEMPERATURE);
    if (message->hasAttr(MAIN_ATTR_U8::SETTING_LEGIONELLA_TARGET_WATER_TEMPERATURE))
        target[MAIN_SETTING_MIN_TEMP_LEGIONELLA]
                = message->getAttr(MAIN_ATTR_U8::SETTING_LEGIONELLA_TARGET_WATER_TEMPERATURE);
    if (message->hasAttr(MAIN_ATTR_U8::SETTING_BRAND))
        target[MAIN_SETTING_BOILER_BRAND] = brandStr(static_cast<MAINBrands>(message->getAttr(MAIN_ATTR_U8::SETTING_BRAND)));
    if (message->hasAttr(MAIN_ATTR_U8::VERSION_CONTROLLER_ASCII))
        target[MAIN_VERSION] = String(static_cast<char>(message->getAttr(MAIN_ATTR_U8::VERSION_CONTROLLER_ASCII)));

    if (message->hasAttr(MAIN_ATTR_U16::SETTING_HEAT_ELEMENT_WATTAGE))
        target[MAIN_SETTING_PWR_HEATELEM] = message->getAttr(MAIN_ATTR_U16::SETTING_HEAT_ELEMENT_WATTAGE);
    if (message->hasAttr(MAIN_ATTR_U16::SETTING_BOILER_CAPACITY))
        target[MAIN_SETTING_BOILER_CAP] = message->getAttr(MAIN_ATTR_U16::SETTING_BOILER_CAPACITY);
}

void StatusPageHandler::addHmiStatus(JsonObject& target) const
{
    ProtocolVersion  version      = PROTOCOL_UNKNOWN;
    ProtocolChecksum checksumType = CHECKSUM_TYPE_UNKNOWN;
    const size_t length = HMIStateProxy::getInstance().copyFrame(HMI_MESSAGE_IDENTIFIER, mTransferBuffer, version, checksumType);
    if (length == 0)
    {
        return;
    }

    std::unique_ptr<IHMIMessage> message = createHmiMessageFromBuffer(version, mTransferBuffer);

    if (message->hasAttr(HMI_ATTR_FLOAT::WATER_TARGET_TEMPERATURE))
        target[HMI_HOT_WATER_TEMP_TARGET] = message->getAttr(HMI_ATTR_FLOAT::WATER_TARGET_TEMPERATURE);
    if (message->hasAttr(HMI_ATTR_U8::OPERATION_MODE))
        target[HMI_OPERATION_MODE] = operationModeStr(static_cast<HMIOperationMode>(message->getAttr(HMI_ATTR_U8::OPERATION_MODE)));
    if (message->hasAttr(HMI_ATTR_U8::OPERATION_TYPE))
        target[HMI_OPERATION_TYPE] = operationTypeStr(static_cast<HMIOperationType>(message->getAttr(HMI_ATTR_U8::OPERATION_TYPE)));
    if (message->hasAttr(HMI_ATTR_U8::STATE_INSTALLATION_MODE))
        target[HMI_INSTALLATION_CONFIG] = installationModeStr(
                static_cast<HMIInstallation>(message->getAttr(HMI_ATTR_U8::STATE_INSTALLATION_MODE)));
    if (message->hasAttr(HMI_ATTR_U8::CONFIG_FAN_EXHAUST))
        target[HMI_FAN_EXHAUST_CONFIG]
                = exhaustModeStr(static_cast<HMIFanExhaust>(message->getAttr(HMI_ATTR_U8::CONFIG_FAN_EXHAUST)));
    if (message->hasAttr(HMI_ATTR_U8::CONFIG_AIRDUCT))
        target[HMI_AIR_DUCT_CONFIG]
                = airDuctConfigStr(static_cast<HMIAirDuctConfig>(message->getAttr(HMI_ATTR_U8::CONFIG_AIRDUCT)));
    if (message->hasAttr(HMI_ATTR_U8::STATE_SETUP))
        target[HMI_SETUP_STATE] = setupStr(static_cast<HMISetup>(message->getAttr(HMI_ATTR_U8::STATE_SETUP)));
    if (message->hasAttr(HMI_ATTR_U8::STATE_TEST))
        target[HMI_TEST_MODE] = testModeStr(static_cast<HMITestMode>(message->getAttr(HMI_ATTR_U8::STATE_TEST)));
    if (message->hasAttr(HMI_ATTR_U8::ANTI_LEGIONELLA_CYCLES))
        target[HMI_LEGIONELLA] = message->getAttr(HMI_ATTR_U8::ANTI_LEGIONELLA_CYCLES);
    if (message->hasAttr(HMI_ATTR_U8::VERSION_HMI_ASCII))
        target[HMI_VERSION] = String(static_cast<char>(message->getAttr(HMI_ATTR_U8::VERSION_HMI_ASCII)));

    if (message->hasAttr(HMI_ATTR_BOOL::EMERGENCY_MODE_ENABLED))
        target[HMI_EMERGENCY_MODE] = message->getAttr(HMI_ATTR_BOOL::EMERGENCY_MODE_ENABLED);
    if (message->hasAttr(HMI_ATTR_BOOL::HEATING_ELEMENT_ALLOWED))
        target[HMI_HEATING_ELEMENT_ENABLED] = message->getAttr(HMI_ATTR_BOOL::HEATING_ELEMENT_ALLOWED);
    if (message->hasAttr(HMI_ATTR_BOOL::PV_INPUT_ALLOWED))
        target[HMM_PV_INPUT_ACTIVATED] = message->getAttr(HMI_ATTR_BOOL::PV_INPUT_ALLOWED);

    if (message->hasAttr(HMI_ATTR_U8::TIME_HOURS) && message->hasAttr(HMI_ATTR_U8::TIME_MINUTES))
    {
        char buf[24];
        snprintf(
                buf,
                sizeof(buf),
                "%02d:%02d",
                message->getAttr(HMI_ATTR_U8::TIME_HOURS),
                message->getAttr(HMI_ATTR_U8::TIME_MINUTES));
        target[HMI_TIME] = String(buf);
    }
    if (message->hasAttr(HMI_ATTR_U8::DATE_DAY) && message->hasAttr(HMI_ATTR_U8::DATE_MONTH)
        && message->hasAttr(HMI_ATTR_U16::DATE_YEAR))
    {
        char buf[24];
        snprintf(
                buf,
                sizeof(buf),
                "%d.%d.%d",
                message->getAttr(HMI_ATTR_U8::DATE_DAY),
                message->getAttr(HMI_ATTR_U8::DATE_MONTH),
                message->getAttr(HMI_ATTR_U16::DATE_YEAR));
        target[HMI_DATE] = String(buf);
    }
    if (message->hasAttr(HMI_ATTR_STR::TIMER_WINDOW_A))
    {
        char buf[24];
        message->getAttr(HMI_ATTR_STR::TIMER_WINDOW_A, buf);
        target[HMI_TIMER_WINDOW_A] = String(buf);
    }
    if (message->hasAttr(HMI_ATTR_STR::TIMER_WINDOW_B))
    {
        char buf[24];
        message->getAttr(HMI_ATTR_STR::TIMER_WINDOW_B, buf);
        target[HMI_TIMER_WINDOW_B] = String(buf);
    }
}

void StatusPageHandler::addEnergyStatus(JsonObject& target) const
{
    ProtocolVersion  version      = PROTOCOL_UNKNOWN;
    ProtocolChecksum checksumType = CHECKSUM_TYPE_UNKNOWN;
    const size_t length
            = MainStateProxy::getInstance().copyFrame(ENERGY_MESSAGE_IDENTIFIER, mTransferBuffer, version, checksumType);
    if (length == 0)
    {
        return;
    }

    std::unique_ptr<IEnergyMessage> message = createEnergyMessageFromBuffer(version, mTransferBuffer);

    if (message->hasAttr(ENERGY_ATTR_U16::POWER_HEATPUMP))
        target[ENERGY_POWER_HEATPUMP] = message->getAttr(ENERGY_ATTR_U16::POWER_HEATPUMP);
    if (message->hasAttr(ENERGY_ATTR_U16::POWER_HEATELEMENT))
        target[ENERGY_POWER_HEAT_ELEMENT] = message->getAttr(ENERGY_ATTR_U16::POWER_HEATELEMENT);
    if (message->hasAttr(ENERGY_ATTR_U16::POWER_TOTAL))
        target[ENERGY_POWER_TOTAL] = message->getAttr(ENERGY_ATTR_U16::POWER_TOTAL);
    if (message->hasAttr(ENERGY_ATTR_U16::WATER_TOTAL))
        target[ENERGY_TOTAL_WATER_PRODUCTION] = message->getAttr(ENERGY_ATTR_U16::WATER_TOTAL);

    if (message->hasAttr(ENERGY_ATTR_U32::TOTAL_HEATPUMP_HOURS))
        target[ENERGY_TOTAL_HEATPUMP_HOURS] = message->getAttr(ENERGY_ATTR_U32::TOTAL_HEATPUMP_HOURS);
    if (message->hasAttr(ENERGY_ATTR_U32::TOTAL_HEATING_ELEMENT_HOURS))
        target[ENERGY_TOTAL_HEATING_ELEM_HOURS] = message->getAttr(ENERGY_ATTR_U32::TOTAL_HEATING_ELEMENT_HOURS);
    if (message->hasAttr(ENERGY_ATTR_U32::TOTAL_HOURS))
        target[ENERGY_TOTAL_HOURS] = message->getAttr(ENERGY_ATTR_U32::TOTAL_HOURS);
    if (message->hasAttr(ENERGY_ATTR_U64::TOTAL_ENERGY))
        target[ENERGY_TOTAL_ENERGY_WH] = message->getAttr(ENERGY_ATTR_U64::TOTAL_ENERGY);

    if (message->hasAttr(ENERGY_ATTR_I8::DIAG_AIR_TEMP_MAX))
        target[ENERGY_DIAG_AIR_TEMP_MAX] = message->getAttr(ENERGY_ATTR_I8::DIAG_AIR_TEMP_MAX);
    if (message->hasAttr(ENERGY_ATTR_I8::DIAG_AIR_TEMP_MIN))
        target[ENERGY_DIAG_AIR_TEMP_MIN] = message->getAttr(ENERGY_ATTR_I8::DIAG_AIR_TEMP_MIN);
    if (message->hasAttr(ENERGY_ATTR_I8::DIAG_EVA_UPPER_AIR_TEMP_MAX))
        target[ENERGY_DIAG_EVA_UPPER_AIR_TEMP_MAX] = message->getAttr(ENERGY_ATTR_I8::DIAG_EVA_UPPER_AIR_TEMP_MAX);
    if (message->hasAttr(ENERGY_ATTR_I8::DIAG_EVA_UPPER_AIR_TEMP_MIN))
        target[ENERGY_DIAG_EVA_UPPER_AIR_TEMP_MIN] = message->getAttr(ENERGY_ATTR_I8::DIAG_EVA_UPPER_AIR_TEMP_MIN);
    if (message->hasAttr(ENERGY_ATTR_I8::DIAG_EVA_LOWER_AIR_TEMP_MAX))
        target[ENERGY_DIAG_EVA_LOWER_AIR_TEMP_MAX] = message->getAttr(ENERGY_ATTR_I8::DIAG_EVA_LOWER_AIR_TEMP_MAX);
    if (message->hasAttr(ENERGY_ATTR_I8::DIAG_EVA_LOWER_AIR_TEMP_MIN))
        target[ENERGY_DIAG_EVA_LOWER_AIR_TEMP_MIN] = message->getAttr(ENERGY_ATTR_I8::DIAG_EVA_LOWER_AIR_TEMP_MIN);
    if (message->hasAttr(ENERGY_ATTR_I8::DIAG_COMPRESSOR_TEMP_MAX))
        target[ENERGY_DIAG_COMPRESSOR_TEMP_MAX] = message->getAttr(ENERGY_ATTR_I8::DIAG_COMPRESSOR_TEMP_MAX);
    if (message->hasAttr(ENERGY_ATTR_I8::DIAG_COMPRESSOR_TEMP_MIN))
        target[ENERGY_DIAG_COMPRESSOR_TEMP_MIN] = message->getAttr(ENERGY_ATTR_I8::DIAG_COMPRESSOR_TEMP_MIN);
    if (message->hasAttr(ENERGY_ATTR_I8::DIAG_WATER_TEMP_MAX))
        target[ENERGY_DIAG_WATER_TEMP_MAX] = message->getAttr(ENERGY_ATTR_I8::DIAG_WATER_TEMP_MAX);
    if (message->hasAttr(ENERGY_ATTR_I8::DIAG_WATER_TEMP_MIN))
        target[ENERGY_DIAG_WATER_TEMP_MIN] = message->getAttr(ENERGY_ATTR_I8::DIAG_WATER_TEMP_MIN);
}

void StatusPageHandler::addExtraStatus(JsonObject& target) const
{
    ProtocolVersion  version      = PROTOCOL_UNKNOWN;
    ProtocolChecksum checksumType = CHECKSUM_TYPE_UNKNOWN;
    const size_t length
            = MainStateProxy::getInstance().copyFrame(EXTRA_MESSAGE_IDENTIFIER, mTransferBuffer, version, checksumType);
    if (length == 0)
    {
        return;
    }

    std::unique_ptr<IExtraMessage> message = createExtraMessageFromBuffer(version, mTransferBuffer);

    if (message->hasAttr(EXTRA_ATTR_U16::EXTRA_POWER_TOTAL))
        target[ENERGY_POWER_TOTAL] = message->getAttr(EXTRA_ATTR_U16::EXTRA_POWER_TOTAL);
    if (message->hasAttr(EXTRA_ATTR_U16::EXTRA_VOLTAGE_GRID))
        target[ENERGY_VOLTAGE_GRID] = message->getAttr(EXTRA_ATTR_U16::EXTRA_VOLTAGE_GRID);
    if (message->hasAttr(EXTRA_ATTR_U32::EXTRA_TOTAL_ENERGY))
        target[ENERGY_TOTAL_ENERGY_WH] = message->getAttr(EXTRA_ATTR_U32::EXTRA_TOTAL_ENERGY);
    if (message->hasAttr(EXTRA_ATTR_FLOAT::EXTRA_AMPERAGE))
        target[ENERGY_AMPERAGE] = message->getAttr(EXTRA_ATTR_FLOAT::EXTRA_AMPERAGE);
}

void StatusPageHandler::addErrorStatus(JsonObject& target) const
{
    ProtocolVersion  version      = PROTOCOL_UNKNOWN;
    ProtocolChecksum checksumType = CHECKSUM_TYPE_UNKNOWN;
    const size_t length
            = MainStateProxy::getInstance().copyFrame(ERROR_MESSAGE_IDENTIFIER, mTransferBuffer, version, checksumType);
    if (length == 0)
    {
        return;
    }

    std::unique_ptr<IErrorMessage> message = createErrorMessageFromBuffer(version, mTransferBuffer);
    if (message->isEmpty())
    {
        return;
    }

    if (message->hasAttr(ERROR_ATTR_U8::ERROR_ERROR_CODE))
        target[MAIN_ERROR_CODE] = message->getAttr(ERROR_ATTR_U8::ERROR_ERROR_CODE);
    if (message->hasAttr(ERROR_ATTR_U8::ERROR_DATE_DAY) && message->hasAttr(ERROR_ATTR_U8::ERROR_DATE_MONTH)
        && message->hasAttr(ERROR_ATTR_U16::ERROR_DATE_YEAR))
    {
        char buf[24];
        snprintf(
                buf,
                sizeof(buf),
                "%d.%d.%d",
                message->getAttr(ERROR_ATTR_U8::ERROR_DATE_DAY),
                message->getAttr(ERROR_ATTR_U8::ERROR_DATE_MONTH),
                message->getAttr(ERROR_ATTR_U16::ERROR_DATE_YEAR));
        target[HMI_DATE] = String(buf);
    }
    if (message->hasAttr(ERROR_ATTR_U8::ERROR_TIME_HOURS) && message->hasAttr(ERROR_ATTR_U8::ERROR_TIME_MINUTES))
    {
        char buf[24];
        snprintf(
                buf,
                sizeof(buf),
                "%02d:%02d",
                message->getAttr(ERROR_ATTR_U8::ERROR_TIME_HOURS),
                message->getAttr(ERROR_ATTR_U8::ERROR_TIME_MINUTES));
        target[HMI_TIME] = String(buf);
    }
    if (message->hasAttr(ERROR_ATTR_FLOAT::ERROR_WATER_TEMPERATURE))
        target[MAIN_HOT_WATER_TEMP] = message->getAttr(ERROR_ATTR_FLOAT::ERROR_WATER_TEMPERATURE);
    if (message->hasAttr(ERROR_ATTR_FLOAT::ERROR_AIR_TEMPERATURE))
        target[MAIN_SUPPLY_AIR_TEMP] = message->getAttr(ERROR_ATTR_FLOAT::ERROR_AIR_TEMPERATURE);
    if (message->hasAttr(ERROR_ATTR_FLOAT::ERROR_EVAPORATOR_LOWER_TEMPERATURE))
        target[MAIN_EVAPORATOR_AIR_TEMP_LOWER] = message->getAttr(ERROR_ATTR_FLOAT::ERROR_EVAPORATOR_LOWER_TEMPERATURE);
    if (message->hasAttr(ERROR_ATTR_FLOAT::ERROR_EVAPORATOR_UPPER_TEMPERATURE))
        target[MAIN_EVAPORATOR_AIR_TEMP_UPPER] = message->getAttr(ERROR_ATTR_FLOAT::ERROR_EVAPORATOR_UPPER_TEMPERATURE);
    if (message->hasAttr(ERROR_ATTR_FLOAT::ERROR_FAN_SPEED_PWM))
        target[MAIN_FAN_PWM] = message->getAttr(ERROR_ATTR_FLOAT::ERROR_FAN_SPEED_PWM);
    if (message->hasAttr(ERROR_ATTR_U16::ERROR_TOTAL_HEATPUMP_HOURS))
        target[ENERGY_TOTAL_HEATPUMP_HOURS] = message->getAttr(ERROR_ATTR_U16::ERROR_TOTAL_HEATPUMP_HOURS);
    if (message->hasAttr(ERROR_ATTR_U16::ERROR_TOTAL_HEATING_ELEMENT_HOURS))
        target[ENERGY_TOTAL_HEATING_ELEM_HOURS] = message->getAttr(ERROR_ATTR_U16::ERROR_TOTAL_HEATING_ELEMENT_HOURS);
}

}  // namespace aquamqtt
