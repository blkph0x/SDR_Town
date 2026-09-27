#include "decoder_status.h"
#include <libacars/cpdlc.h>

bool sdr_acars_cpdlc_error(const la_proto_node* node) {
    return node->td == &la_DEF_cpdlc_message && ((const la_cpdlc_msg*)node->data)->err;
}
bool sdr_acars_has_cpdlc(la_proto_node* root) {
    return la_proto_tree_find_cpdlc(root) != NULL;
}
