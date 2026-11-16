/* a_glue.c -- the one symbol the framework's cosim/harness_glue.c does not
 * cover: the recompiler's block-entry hook, which interp_bridge.c calls and
 * host_contract.c normally defines. No-op headless (trace builds only). */
#include <stdint.h>

void debug_on_block_enter(uint32_t pc, uint32_t a, uint32_t x, uint32_t y) {
    (void)pc; (void)a; (void)x; (void)y;
}
