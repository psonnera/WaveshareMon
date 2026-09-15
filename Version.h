#ifndef VERSION_H
#define VERSION_H
// on-screen semantic version (the OTA/flasher build number lives in Binaries/*/update.inf)
#define WSMON_VERSION "1.1.0-dev"
// build number (YYYYMMDDnn) that the OTA update compares with the server's
// update.inf; Scripts/build.ps1 passes it as -DWSMON_BUILD. 0 = built by hand.
#ifndef WSMON_BUILD
#define WSMON_BUILD 0UL
#endif
#endif
