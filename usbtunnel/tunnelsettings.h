#ifndef TUNNELSETTINGS_H
#define TUNNELSETTINGS_H 1

#include <stdint.h>

int tunnel_get_vidpid(uint16_t *vid, uint16_t *pid);
int tunnel_set_vidpid(uint16_t vid, uint16_t pid);

int tunnel_get_netconfig(char *netname, int nnetname, char *netpass, int nnetpass);
int tunnel_set_netconfig(const char *netname, const char *netpass);

int tunnel_reboot_device(void);

#endif

