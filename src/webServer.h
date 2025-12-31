#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <Arduino.h>

class WebServer {
  public:
    void startAPMode();
    void startNormalMode();
    void registerRoutes();
};

extern WebServer webServer;
#endif
