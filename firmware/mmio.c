#include <stdint.h>
#include <nfp.h>

#include "devcfg.h"
#include "debug.h"

__shared __cls uint8_t cls_buffer[CLS_WINDOW_SIZE];
__shared __ctm uint8_t ctm_buffer[CTM_WINDOW_SIZE];
__shared __imem0 uint8_t imem_buffer[IMEM_WINDOW_SIZE];
__shared __emem uint8_t emem_buffer[EMEM_WINDOW_SIZE];

int main(void)
{
    uint8_t x = 0;

    while (1) {
        x = x + 1;

        ctx_swap();
    }

    return 0;
}