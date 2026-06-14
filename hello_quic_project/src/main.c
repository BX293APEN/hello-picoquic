#include <stdio.h>
#include "picoquic.h"

int main(void)
{
    printf("picoquic version: %s\n", PICOQUIC_VERSION);

    picoquic_quic_t* quic = picoquic_create(
        8, NULL, NULL, NULL, "hello-picoquic", NULL, NULL, NULL, NULL, NULL,
        picoquic_current_time(), NULL, NULL, NULL, 0);

    if (quic == NULL) {
        printf("picoquic_create failed\n");
        return 1;
    }

    printf("picoquic_create succeeded\n");
    picoquic_free(quic);
    return 0;
}
