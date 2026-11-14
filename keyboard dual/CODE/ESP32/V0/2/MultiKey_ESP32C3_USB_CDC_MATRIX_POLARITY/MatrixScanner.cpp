#include "MatrixScanner.h"

void MatrixScanner::begin() {
  for (uint8_t col = 0; col < Config::COLS; ++col) {
    pinMode(Config::COL_PINS[col], INPUT_PULLDOWN);
  }
  for (uint8_t row = 0; row < Config::ROWS; ++row) {
    digitalWrite(Config::ROW_PINS[row], Config::MATRIX_ROW_INACTIVE_LEVEL);
    pinMode(Config::ROW_PINS[row], OUTPUT);
  }
  memset(_raw, 0, sizeof(_raw));
  memset(_stable, 0, sizeof(_stable));
  memset(_rawChangedAt, 0, sizeof(_rawChangedAt));
  _changedMask = 0;
}

void MatrixScanner::update(uint32_t nowMs) {
  _changedMask = 0;

  for (uint8_t row = 0; row < Config::ROWS; ++row) {
    for (uint8_t other = 0; other < Config::ROWS; ++other) {
      digitalWrite(Config::ROW_PINS[other], Config::MATRIX_ROW_INACTIVE_LEVEL);
    }
    digitalWrite(Config::ROW_PINS[row], Config::MATRIX_ROW_ACTIVE_LEVEL);
    delayMicroseconds(Config::ROW_SETTLE_US);

    for (uint8_t col = 0; col < Config::COLS; ++col) {
      const uint8_t index = row * Config::COLS + col;
      const bool pressed = digitalRead(Config::COL_PINS[col]) == Config::MATRIX_PRESSED_LEVEL;

      if (pressed != _raw[index]) {
        _raw[index] = pressed;
        _rawChangedAt[index] = nowMs;
      }
      if (_stable[index] != _raw[index] && static_cast<uint32_t>(nowMs - _rawChangedAt[index]) >= Config::DEBOUNCE_MS) {
        _stable[index] = _raw[index];
        _changedMask |= (1ULL << index);
      }
    }
    digitalWrite(Config::ROW_PINS[row], Config::MATRIX_ROW_INACTIVE_LEVEL);
  }
}

bool MatrixScanner::isPressed(uint8_t index) const {
  return index < Config::NUM_KEYS && _stable[index];
}

bool MatrixScanner::rawIsPressed(uint8_t index) const {
  return index < Config::NUM_KEYS && _raw[index];
}

bool MatrixScanner::changed(uint8_t index) const {
  return index < Config::NUM_KEYS && ((_changedMask & (1ULL << index)) != 0);
}

uint64_t MatrixScanner::pressedMask() const {
  uint64_t mask = 0;
  for (uint8_t index = 0; index < Config::NUM_KEYS; ++index) {
    if (_stable[index]) mask |= (1ULL << index);
  }
  return mask;
}
