// 
// Maintains persistent settings for tunnel (embedded Linux) device
//

#include "tunnelsettings.h"
#include "vpnextender.h"
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

// settings file format
//
static const char s_fmt[] =
	"{ \"vid\": %04X, \"pid\": %04X }\n";

// command line to wpa_cli
//
static const char s_wpa_config[] =
	"sudo wpa_cli set_network 0 ssid \'\"%s\"\';wpa_cli set_network 0 psk \'\"%s\"\';wpa_cli save config;wpa_cli enable_network 0";

static const char s_wpa_restart[] =
	"sudo systemctl restart wpa_supplicant\n";

static int tunnel_setup_wifi(const char *netname, const char *netpass)
{
	char cmd[256];
	int result;
	int len;
	
	len = snprintf(cmd, sizeof(cmd), s_wpa_config, netname, netpass);
	if (len <= sizeof(s_wpa_config))
	{
		vpnx_log(0, "Can't format wifi config\n");
		return -1;
	}
	vpnx_log(2, "wifi:%s\n", cmd);
	result = system(cmd);
	return 0;
}

static int tunnel_restart_wifi(void)
{
	vpnx_log(2, "wifi restart\n");
	int result = system(s_wpa_restart);
}

int tunnel_get_vidpid(uint16_t *vid, uint16_t *pid)
{
	char buf[256];
	uint16_t myvid;
	uint16_t mypid;
	int settings_file;
	bool newfile;
	
	myvid = kVendorID;
	mypid = kProductID;
	newfile = false;
	
	settings_file = open("tunnelsettings.json", O_RDONLY);
	if (settings_file < 0)
	{
		vpnx_log(0, "Can't open settings file, creating new one\n");
		newfile = true;
	}
	if (settings_file >= 0)
	{
		char *pvp;
		char *npvp;
		
		// simple json parser
		
		pvp = strstr(buf, "\"vid\":");
		if (pvp)
		{		
			pvp += 7;
			while (*pvp == ' ' || *pvp == ',' || *pvp == '\t' || *pvp == '\n')
			{
				pvp++;
			}
			myvid = (uint16_t)strtoul(pvp, &npvp, 16);
			
			if (npvp)
			{
				pvp = strstr(npvp, "\"pid\":");
				if (pvp)
				{
					pvp += 7;
					while (*pvp == ' ' || *pvp == ',' || *pvp == '\t' || *pvp == '\n')
					{
						pvp++;
					}
					mypid = (uint16_t)strtoul(pvp, &npvp, 16);
				}
			}
			else
			{
				newfile = true;
			}
		}
		else
		{
			newfile = true;
		}
	}
	if (vid)
	{
		*vid = myvid;
	}
	if (pid)
	{
		*pid = mypid;
	}
	if (newfile)
	{
		myvid = kVendorID;
		mypid = kProductID;
		tunnel_set_vidpid(myvid, mypid);
	}
	vpnx_log(1, "Get tunnel vid=%04X pid=%04X\n", myvid, mypid);
}

int tunnel_set_vidpid(uint16_t vid, uint16_t pid)
{
	char cmd[256];
	int settings_file;
	int len;
	int wc;
	
	len = snprintf(cmd, sizeof(cmd), s_fmt, vid, pid);
	if (len < sizeof(s_fmt))
	{
		vpnx_log(0, "Can't make settings string\n");
		return -1;
	}
	settings_file = open("tunnelsettings.json", O_WRONLY | O_CREAT | O_TRUNC);
	if (settings_file < 0)
	{
		vpnx_log(0, "Can't create settings file\n");
		return -1;
	}
	wc = write(settings_file, cmd, len);	
	close(settings_file);
	
	if (wc < len)
	{
		vpnx_log(0, "Can't write settings file\n");
		return -1;
	}
	vpnx_log(1, "Set tunnel vid=%04X pid=%04X\n", vid, pid);
}

int tunnel_get_netconfig(char *netname, int nnetname, char *netpass, int nnetpass)
{
}

int tunnel_set_netconfig(const char *netname, const char *netpass)
{
	vpnx_log(1, "Setting wifi network %s\n", netname);
	
	if (!tunnel_setup_wifi(netname, netpass))
	{
		tunnel_restart_wifi();
	}
}

int tunnel_reboot_device()
{
	int result = system("reboot\n");
}


