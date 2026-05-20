#include "WebhookNotifier.h"
#include "Logger.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

void WebhookNotifier::trigger(Config* config, bool sessionSucceeded,
                              int filesProcessed, unsigned long bytesTransferred) {
    if (!config) return;

    // ── Build payload ──
    String status = sessionSucceeded ? "success" : "error";
    String payload = "{\"event\":\"cpap_sync_session\",\"status\":\"";
    payload += status;
    payload += "\"";
    if (config->getWebhookExtendedMetadata()) {
        payload += ",\"files_processed\":";
        payload += String(filesProcessed);
        payload += ",\"bytes_transferred\":";
        payload += String(bytesTransferred);
    }
    payload += "}";

    // ── SleepLab ──
    String slDomain = config->getSleepLabDomain();
    if (slDomain.length() > 0) {
        String slUser  = config->getSleepLabUserId();
        String slSecret = config->getSleepLabSecret();

        String endpoint = slUser.length() > 0
            ? slDomain + "/import/webhook/" + slUser
            : slDomain + "/import/trigger/all";

        {
            HTTPClient http;
            http.begin(endpoint);
            http.addHeader("Content-Type", "application/json");
            if (slSecret.length() > 0) {
                http.addHeader("X-Import-Secret", slSecret);
            }
            http.setTimeout(5000);
            int code = http.POST(payload);
            LOGF("[Webhook] SleepLab POST %s → %d", endpoint.c_str(), code);
            http.end();
        }
        vTaskDelay(10);
    }

    // ── Generic ──
    String genUrl = config->getGenericWebhookUrl();
    if (genUrl.length() > 0) {
        String url = genUrl;
        if (!sessionSucceeded && config->getWebhookAppendFailPath()) {
            while (url.endsWith("/")) {
                url = url.substring(0, url.length() - 1);
            }
            url += "/fail";
        }
        WiFiClientSecure client;
        client.setInsecure();
        HTTPClient http;
        http.begin(client, url);
        http.addHeader("Content-Type", "application/json");
        http.setTimeout(5000);
        int code = http.POST(payload);
        LOGF("[Webhook] Generic POST %s → %d", url.c_str(), code);
        http.end();
        vTaskDelay(10);
    }
}
