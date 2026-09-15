/*
  HttpsClient.h - one HTTPS request with the embedded root bundle
  (part of WaveshareMon, GPL v3, see LICENSE)

  Shared by the Nightscout, Dexcom Share and LibreLinkUp clients.

  Copyright (C) 2026 Patrick Sonnerat
*/
#ifndef HTTPSCLIENT_H
#define HTTPSCLIENT_H

#include <Arduino.h>

struct HttpHeader { const char *name; const char *value; };

// method "GET" or "POST"; body may be nullptr; the response body is returned
// for every HTTP status (the cloud APIs put their error codes in it).
// Returns the HTTP status, or a negative HTTPClient error (-1 connection
// refused, -11 read timeout, ...) / -100 when the URL was refused.
int httpsRequest(const char *method, const char *url, const char *body,
                 const HttpHeader *headers, int nHeaders, String &response);

// last transport-level error text for the status bar ("no reply", "TLS", ...)
const char *httpsErrorText(int code);

#endif
