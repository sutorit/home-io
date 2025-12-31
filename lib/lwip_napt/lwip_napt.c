#include "lwip_napt.h"
#include "lwip/ip.h"
#include "lwip/netif.h"
#include "lwip/init.h"
#include "lwip/err.h"
#include "lwip/ip_addr.h"
#include "lwip/priv/tcpip_priv.h"

// Declare the low-level NAT function from lwIP if available
err_t ip_napt_enable_no(struct ip4_addr *ip, int enable)
{
    // This is just a stub for now — you need the real NAT code here.
    // Return ERR_OK so linker is happy
    return ERR_OK;
}

// Simple wrapper for NAT enable
void ip_napt_enable(uint32_t ip, int enable)
{
    ip_addr_t napt_ip;
    napt_ip.type = IPADDR_TYPE_V4;
    ip4_addr_set_u32(ip_2_ip4(&napt_ip), ip);
    ip_napt_enable_no(ip_2_ip4(&napt_ip), enable);
}
