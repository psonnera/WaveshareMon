#ifndef DEBUGINJECT_H
#define DEBUGINJECT_H

// serial test hooks, usable without any phone:
//   bg <mgdl> [angle]   inject a reading (angle -90..90, 180 = hidden)
//   demo                inject a demo curve into the history
//   time <h> <m>        set the clock (today's date if unknown)
//   status              print state
//   cfg                 print the configuration (secrets masked)
//   set <key> <value>   change a config value (same keys as the BLE Config JSON)
//   setup [on|off]      toggle BLE setup advertising
//   refresh             force a display refresh
//   warn / alarm        play the test sounds
//   reboot / factory
void debugInjectPoll();

#endif
