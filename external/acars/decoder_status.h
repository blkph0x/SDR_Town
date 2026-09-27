#pragma once
#include <libacars/libacars.h>
#ifdef __cplusplus
extern "C" {
#endif
// Keep generated ASN.1/MSVC platform typedefs out of Qt/C++ translation units.
bool sdr_acars_cpdlc_error(const la_proto_node* node);
bool sdr_acars_has_cpdlc(la_proto_node* root);
#ifdef __cplusplus
}
#endif
