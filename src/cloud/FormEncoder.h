// Builds application/x-www-form-urlencoded bodies exactly like v1 did:
// fields in the given order, values as raw text. Only characters that would
// corrupt the body (& = + % space, control and non-ASCII bytes) are escaped;
// for every value v1 could send correctly the bytes are identical.
#pragma once

#include <Arduino.h>

class FormEncoder {
 public:
  FormEncoder& add(const char* name, const String& value) {
    if (body_.length()) body_ += '&';
    body_ += name;
    body_ += '=';
    for (size_t i = 0; i < value.length(); i++) {
      uint8_t c = (uint8_t)value[i];
      if (c == '&' || c == '=' || c == '+' || c == '%' || c == ' ' || c < 0x20 || c > 0x7E) {
        static const char hex[] = "0123456789ABCDEF";
        body_ += '%';
        body_ += hex[c >> 4];
        body_ += hex[c & 0x0F];
      } else {
        body_ += (char)c;
      }
    }
    return *this;
  }
  const String& str() const { return body_; }

 private:
  String body_;
};
