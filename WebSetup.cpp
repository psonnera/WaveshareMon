/*
  WebSetup.cpp - configuration page over Wi-Fi during the setup window
  (part of WaveshareMon, GPL v3, see LICENSE)

  Copyright (C) 2026 Patrick Sonnerat
*/
#include "WebSetup.h"
#include "WebPage.h"
#include "AppConfig.h"
#include "BleSetupServer.h"
#include "WifiService.h"
#include "PowerCycle.h"
#include "Log.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

#define WEB_HOLD_MS   60000UL      // a page access keeps the device awake this long (as the app does)
#define WEB_LOG_LINES 60

static WebServer  s_server(80);
static DNSServer  s_dns;
static bool       s_active = false;
static char       s_apIp[20] = "192.168.4.1";

bool webSetupActive() { return s_active; }
const char *webSetupApIp() { return s_apIp; }

// a request from a browser: keep the setup window open a while longer
static void touched() { cycleStayAwake(WEB_HOLD_MS); }

static void sendJson(const std::string &s) {
  s_server.sendHeader("Cache-Control", "no-store");
  s_server.send(200, "application/json", s.c_str());
}

// the captive portal probes of the phones and computers: any answer that is
// not what they expect makes them open the page
static bool isOurHost() {
  String host = s_server.hostHeader();
  return host == s_apIp || host == WiFi.localIP().toString() || host.startsWith("192.168.4.1");
}

static void redirectToPortal() {
  s_server.sendHeader("Location", String("http://") + s_apIp + "/", true);
  s_server.send(302, "text/plain", "");
}

static void handleRoot() {
  touched();
  s_server.sendHeader("Cache-Control", "no-store");
  s_server.send_P(200, "text/html; charset=utf-8", WEB_PAGE);
}

static void handleInfo() {
  std::string s; setupBuildInfo(s); sendJson(s);
}

static void handleConfigGet() {
  touched();
  std::string s; setupBuildConfig(s); sendJson(s);
}

static void handleConfigPost() {
  touched();
  String body = s_server.arg("plain");
  if (setupApplyConfig(body.c_str(), body.length(), "web")) sendJson("{\"ok\":true}");
  else s_server.send(400, "application/json", "{\"ok\":false,\"err\":\"bad JSON\"}");
}

static void handleCmd() {
  touched();
  String c = s_server.arg("c");
  if (setupCommand(c.c_str())) sendJson("{\"ok\":true}");
  else s_server.send(400, "application/json", "{\"ok\":false,\"err\":\"unknown command\"}");
}

static void handleScan() {
  touched();
  if (s_server.hasArg("start")) wifiScanStart();
  char json[512];
  wifiScanJson(json, sizeof(json));
  sendJson(json);
}

// the device log, oldest first
static void handleLog() {
  String out;
  uint32_t total = logTotal();
  int n = total < WEB_LOG_LINES ? (int)total : WEB_LOG_LINES;
  for (int i = n - 1; i >= 0; i--) {
    const LogEntry *e = logGet(i);
    if (!e) continue;
    char stamp[16];
    logStamp(e, stamp, sizeof(stamp));
    out += stamp; out += ' '; out += e->text; out += '\n';
  }
  s_server.sendHeader("Cache-Control", "no-store");
  s_server.send(200, "text/plain; charset=utf-8", out);
}

static void handleNotFound() {
  if (!isOurHost()) { redirectToPortal(); return; }
  s_server.send(404, "text/plain", "not found");
}

static void begin() {
  wifiApStart();
  strlcpy(s_apIp, WiFi.softAPIP().toString().c_str(), sizeof(s_apIp));
  s_dns.setErrorReplyCode(DNSReplyCode::NoError);
  s_dns.start(53, "*", WiFi.softAPIP());

  s_server.on("/", HTTP_GET, handleRoot);
  s_server.on("/api/info", HTTP_GET, handleInfo);
  s_server.on("/api/config", HTTP_GET, handleConfigGet);
  s_server.on("/api/config", HTTP_POST, handleConfigPost);
  s_server.on("/api/cmd", HTTP_POST, handleCmd);
  s_server.on("/api/scan", HTTP_GET, handleScan);
  s_server.on("/api/log", HTTP_GET, handleLog);
  // captive portal detection (Android, Apple, Windows, Firefox)
  for (const char *p : {"/generate_204", "/gen_204", "/hotspot-detect.html", "/library/test/success.html",
                        "/connecttest.txt", "/ncsi.txt", "/fwlink", "/redirect", "/canonical.html", "/success.txt"})
    s_server.on(p, redirectToPortal);
  s_server.onNotFound(handleNotFound);
  s_server.begin();
  s_active = true;
  logAdd("web setup: Wi-Fi %s, http://%s", cfg.name(), s_apIp);
}

static void end() {
  s_server.stop();
  s_dns.stop();
  wifiApStop();
  s_active = false;
  logAdd("web setup off");
}

void webSetupTick() {
  bool want = setupServerAdvertising();
  if (want && !s_active) begin();
  else if (!want && s_active) end();
  if (!s_active) return;
  s_dns.processNextRequest();
  s_server.handleClient();
}
