#include <stdint.h>
#include <nfp.h>
#include <nfp/me.h>

#include "devcfg.h"
#include "debug.h"

__shared __cls uint8_t cls_buffer[CLS_WINDOW_SIZE];
__shared __ctm uint8_t ctm_buffer[CTM_WINDOW_SIZE];
__shared __imem uint8_t imem_buffer[IMEM_WINDOW_SIZE];
__shared __emem uint8_t emem_buffer[EMEM_WINDOW_SIZE];

int main(void)
{
    uint8_t x = 0;
    unsigned i;
    unsigned a, b, c;

    if (ctx() == 0)
    {

        for (i = 0; i < CLS_WINDOW_SIZE; i++)
        {
            cls_buffer[i] = 0xad;
        }

        for (i = 0; i < CTM_WINDOW_SIZE; i++)
        {
            ctm_buffer[i] = 0xad;
        }

        for (i = 0; i < IMEM_WINDOW_SIZE; i++)
        {
            imem_buffer[i] = 0xad;
        }

        for (i = 0; i < EMEM_WINDOW_SIZE; i++)
        {
            emem_buffer[i] = 0xad;
        }

        i = 0;
        while (1) {
            x = x + 1;

            cls_buffer[i] = x;

            for (a = 0; a < 65535; a++)
            {
                for (b = 0; b < 65535; b++)
                {
                    c = a + b;
                }
            }

            ctm_buffer[i] = cls_buffer[i];
            imem_buffer[i] = imem_buffer[i];
            emem_buffer[i] = emem_buffer[i];

            i = i + 1;
            if (i >= CLS_WINDOW_SIZE)
                i = 0;
        }
    }

    return 0;
}