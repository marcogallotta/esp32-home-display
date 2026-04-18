#ifdef ARDUINO
#include <Arduino.h>

#include "src/main.h"
#include "src/platform.h"

void setup() {
    Serial.begin(115200);
    run();
}

void loop(){
}
#endif
