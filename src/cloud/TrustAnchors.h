// Shared root-CA list (parsed once, kept in RAM ~3 KB) for TLS certificate checks.
#pragma once

#include <WiFiClientSecureBearSSL.h>

namespace TrustAnchors {
BearSSL::X509List* get();
}
