/*
  HttpsClient.cpp - one HTTPS request with the embedded root bundle
  (part of WaveshareMon, GPL v3, see LICENSE)

  Copyright (C) 2026 Patrick Sonnerat
*/
#include "HttpsClient.h"
#include "AppConfig.h"
#include "CaRoots.h"
#include "Log.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

int httpsRequest(const char *method, const char *url, const char *body,
                 const HttpHeader *headers, int nHeaders, String &response) {
  HTTPClient http;
  WiFiClientSecure secure;
  WiFiClient plain;
  bool https = strncmp(url, "https", 5) == 0;
  if (https) {
    if (cfg.tlsVerify) secure.setCACert(CA_ROOTS_PEM);
    else               secure.setInsecure();
  }
  http.setConnectTimeout(10000);
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setReuse(false);
  if (!(https ? http.begin(secure, url) : http.begin(plain, url))) return -100;
  for (int i = 0; i < nHeaders; i++) http.addHeader(headers[i].name, headers[i].value);
  int code;
  if (strcmp(method, "POST") == 0) code = http.POST(body ? String(body) : String());
  else                             code = http.GET();
  if (code > 0) response = http.getString();
  else          response = "";
  http.end();
  if (code <= 0) logDebug("http %s: %s", method, httpsErrorText(code));
  return code;
}

const char *httpsErrorText(int code) {
  switch (code) {
    case -1:  return "no connection";
    case -2:  return "send failed";
    case -3:  return "send failed";
    case -4:  return "not connected";
    case -5:  return "connection lost";
    case -6:  return "no stream";
    case -7:  return "no server";
    case -8:  return "too little RAM";
    case -9:  return "bad encoding";
    case -10: return "stream write";
    case -11: return "timeout";
    case -100: return "bad URL";
    default:  return code < 0 ? "network error" : "";
  }
}
