#pragma once

#include <Arduino.h>

class MultiKey;
struct KeyEvent;

class SerialCli {
public:
    static constexpr size_t LINE_CAPACITY = 256;
    explicit SerialCli(MultiKey &keyboard) : _keyboard(keyboard) {}

    void begin();
    void update();
    void processLine(const char *line);

    void emitKeyEvent(const KeyEvent &event);
    void emitLayerEvent(uint8_t active, uint8_t base);
    void emitBleEvent(bool connected);
    bool eventsEnabled() const { return _events; }

private:
    MultiKey &_keyboard;
    char _line[LINE_CAPACITY] = {};
    size_t _length = 0;
    bool _overflow = false;
    bool _events = false;
};
