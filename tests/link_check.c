/* Includes only the public header and calls one function, to prove a consumer
 * links against a library flavour without defining anything. */
#if defined(CHECK_FAAC)
#include <faac.h>
int main(void)
{
    faac_params p;
    return faac_params_init(&p, sizeof p) == FAAC_OK ? 0 : 1;
}
#elif defined(CHECK_FAAD)
#include <faad.h>
int main(void)
{
    faad_config c;
    return faad_config_init(&c, sizeof c) == FAAD_OK ? 0 : 1;
}
#else
#include <faam.h>
int main(void)
{
    faam_muxer_config c;
    return faam_muxer_config_init(&c, sizeof c) == FAAM_OK ? 0 : 1;
}
#endif
