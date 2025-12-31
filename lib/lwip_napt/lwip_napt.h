#ifndef _LWIP_NAPT_H_
#define _LWIP_NAPT_H_

#include "lwip/ip_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

// Enable/disable NAT for a given IP
void ip_napt_enable(uint32_t ip, int enable);

#ifdef __cplusplus
}
#endif

#endif // _LWIP_NAPT_H_
