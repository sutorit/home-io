#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

struct SystemState {
    bool servicesStarted = false; // all services started
    bool firebaseReady   = false; 
    bool mqttReady       = false; 
    bool no_config       = false; // no config yet, in AP mode
};

extern SystemState systemState;

#endif
