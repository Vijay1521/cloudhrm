#include "TrustAnchors.h"

#include "trust_anchors.h"

namespace TrustAnchors {

BearSSL::X509List* get() {
  static BearSSL::X509List* list = nullptr;
  if (!list) list = new BearSSL::X509List(TRUST_ANCHORS_PEM);
  return list;
}

}  // namespace TrustAnchors
