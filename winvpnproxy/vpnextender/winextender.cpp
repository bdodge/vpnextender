// vpnextender.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "vpnextender.h"
#include "resource.h"

#include <windowsx.h>
#include <shellapi.h>
#include <shlwapi.h>

#define MAX_LOADSTRING 100

static TCHAR szTitle[MAX_LOADSTRING];
static TCHAR szWindowClass[MAX_LOADSTRING];
HINSTANCE g_hInstance;
HICON g_hIcon;

static const UINT WM_TRAY = WM_USER + 1;
static NOTIFYICONDATA niData;

static TCHAR szRemoteHosts[VPNX_MAX_PORTS][VPNX_MAX_HOST];
static uint16_t remote_ports[VPNX_MAX_PORTS];
static uint16_t local_ports[VPNX_MAX_PORTS];
static int def_log_level;
static uint16_t def_vid;
static uint16_t def_pid;
static TCHAR netname[VPNX_MAX_HOST];
static TCHAR netpass[VPNX_MAX_HOST];

static HKEY g_hKey;
static HWND g_hLogWindow;

static bool extender_running;

int OpenSettings(void)
{
    TCHAR keyname[MAX_PATH * 2];
    bool allusers = false;

    g_hKey = NULL;

    _tcscpy(keyname, _T("Software\\BSA Software\\VPNx\\"));
    int err = RegCreateKey((allusers ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER), keyname, &g_hKey);
    if (err != ERROR_SUCCESS)
    {
        g_hKey = NULL;
    }
    return err != ERROR_SUCCESS;
}

void CloseSettings(void)
{
    if (g_hKey != NULL)
    {
        RegCloseKey(g_hKey);
        g_hKey = NULL;
    }
}

static void SetStringSetting(LPCTSTR name, int index, LPTSTR value)
{
    TCHAR keyname[MAX_PATH];
    HKEY hKey;
    int  err;

    keyname[0] = '\0';
    _sntprintf(keyname, _countof(keyname), _T("%u"), index);

    err = RegCreateKey(g_hKey, name, &hKey);
    err = RegSetValueEx(hKey, keyname, NULL, REG_SZ, (LPBYTE)value, (_tcslen(value) + 1) * sizeof(TCHAR));

    RegCloseKey(hKey);
}

static void SetPortSetting(LPCTSTR name, int index, uint16_t value)
{
    TCHAR keyname[MAX_PATH];
    HKEY hKey;
    int  err;
    DWORD val = (DWORD)value;

    keyname[0] = '\0';
    _sntprintf(keyname, _countof(keyname), _T("%u"), index);

    err = RegCreateKey(g_hKey, name, &hKey);
    err = RegSetValueEx(hKey, keyname, NULL, REG_DWORD, (LPBYTE)&val, sizeof(DWORD));

    RegCloseKey(hKey);
}

static void GetStringSetting(LPCTSTR name, int index, LPTSTR value, LPCTSTR default_value)
{
    TCHAR keyname[MAX_PATH];
    HKEY hKey;
    DWORD dwType = REG_SZ;
    DWORD cbData = VPNX_MAX_HOST;
    int  err;

    _tcsncpy(value, default_value, VPNX_MAX_HOST);

    keyname[0] = '\0';
    _sntprintf(keyname, _countof(keyname), _T("%u"), index);

    err = RegCreateKey(g_hKey, name, &hKey);
    err = RegQueryValueEx(hKey, keyname, NULL, &dwType, (LPBYTE)value, &cbData);

    RegCloseKey(hKey);

    if (err != ERROR_SUCCESS)
    {
        if (err == 2)
        {
            // set default value
            //
            SetStringSetting(name, index, value);
        }
    }
}

static void GetPortSetting(LPCTSTR name, int index, uint16_t *value, uint16_t default_value)
{
    TCHAR keyname[MAX_PATH];
    HKEY  hKey;
    BYTE  rawData[32];
    DWORD dwType = REG_DWORD;
    DWORD cbData = sizeof(rawData);
    DWORD val;
    int  err;

    val = default_value;

    _sntprintf(keyname, _countof(keyname), _T("%u"), index);

    err = RegCreateKey(g_hKey, name, &hKey);
    err = RegQueryValueEx(hKey, keyname, NULL, &dwType, rawData, &cbData);
    RegCloseKey(hKey);

    if (err != ERROR_SUCCESS)
    {
        if (err == 2)
        {
            // set default value
            //
            SetPortSetting(name, index, (uint16_t)val);
        }
    }
    else
    {
        if (dwType == REG_SZ)
        {
            val = _tcstol((LPTSTR)rawData, NULL, 0);
        }
        else
        {
            memcpy(&val, rawData, sizeof(DWORD));
        }
    }
    *value = (uint16_t)val;
}

void RestoreSettings(void)
{
    int i;

    def_log_level = 3;
    def_vid = kVendorID;
    def_pid = kProductID;

    if (!OpenSettings())
    {
        for (i = 0; i < VPNX_MAX_PORTS; i++)
        {
            GetStringSetting(_T("Remote Host"), i, &szRemoteHosts[i][0], _T(""));
            GetPortSetting(_T("Remote Port"), i, &remote_ports[i], 0);
            GetPortSetting(_T("Local Port"), i, &local_ports[i], 0);
        }
        GetStringSetting(_T("Network"), 0, netname, _T(""));
        GetStringSetting(_T("Password"), 0, netpass, _T(""));
        CloseSettings();
    }
}

void SaveSettings(void)
{
    int i;

    if (!OpenSettings())
    {
        for (i = 0; i < VPNX_MAX_PORTS; i++)
        {
            SetStringSetting(_T("Remote Host"), i, &szRemoteHosts[i][0]);
            SetPortSetting(_T("Remote Port"), i, remote_ports[i]);
            SetPortSetting(_T("Local Port"), i, local_ports[i]);
        }
        SetStringSetting(_T("Network"), 0, netname);
        SetStringSetting(_T("Password"), 0, netpass);
        CloseSettings();
    }
}

void StopExtender(void)
{
    extender_running = false;
}

void RestartExtender(void)
{
    LPCWSTR phost;
    LPCWSTR plasthost;
    int result;
    char hostlist[(VPNX_MAX_HOST + 1) * VPNX_MAX_PORTS];
    int offset;
    int len;
    int i;

    plasthost = NULL;
    hostlist[0] = '\0';

    StopExtender();

    for (i = 0, offset = 0; i < 4 && i < VPNX_MAX_PORTS; i++)
    {
        if (remote_ports[i] == 0 || local_ports[i] == 0)
        {
            break;
        }
        // assume blank hosts with valid ports uses last specified host for convenience
        //
        phost = &szRemoteHosts[i][0];
        if (phost[0] == _T('\0'))
        {
            phost = plasthost;
        }
        else
        {
            plasthost = phost;
        }
        if (!phost)
        {
            break;
        }
#ifdef UNICODE
        int inlen = _tcslen(phost);

        len = WideCharToMultiByte(CP_UTF8, 0, phost, inlen, hostlist + offset, sizeof(hostlist) - offset, NULL, NULL);
        offset += len;
        hostlist[offset++] = ',';
#else
        len = snprintf(hostlist + offset, sizeof(hostlist) - offset, "%s,", phost);
        offset += len;
#endif
    }
    if (offset > 0)
    {
        hostlist[offset - 1] = '\0';
    }
    // Initialize the prtproxy
    //
    result = vpnx_gui_init(
                            true,
                            hostlist,
                            remote_ports,
                            def_vid,
                            def_pid,
                            local_ports,
                            def_log_level,
                            vpnx_mem_logger
                        );

    if (result == 0)
    {
        extender_running = true;
    }
}

INT_PTR CALLBACK AboutProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

INT_PTR CALLBACK LoggingProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND hWndCb;
    HWND hWndLog;
    int level;


    switch (message)
    {
    case WM_INITDIALOG:
        level = vpnx_get_log_level();
        hWndCb = GetDlgItem(hDlg, IDC_LOGLEVEL);
        ComboBox_AddString(hWndCb, _T("Errors"));
        ComboBox_AddString(hWndCb, _T("Basic"));
        ComboBox_AddString(hWndCb, _T("Basic+"));
        ComboBox_AddString(hWndCb, _T("Moderate"));
        ComboBox_AddString(hWndCb, _T("Detailed"));
        ComboBox_AddString(hWndCb, _T("Verbose"));
        ComboBox_SetCurSel(hWndCb, level);
        g_hLogWindow = hDlg;
        break;
    case WM_COMMAND:
        if (HIWORD(wParam) == 0)
        {
            switch (LOWORD(wParam))
            {
            case IDOK:
                hWndCb = GetDlgItem(hDlg, IDC_LOGLEVEL);
                level = ComboBox_GetCurSel(hWndCb);
                vpnx_set_log_level(level);
                EndDialog(hDlg, LOWORD(wParam));
                g_hLogWindow = NULL;
                return (INT_PTR)TRUE;
            case IDCANCEL:
                EndDialog(hDlg, LOWORD(wParam));
                g_hLogWindow = NULL;
                return (INT_PTR)TRUE;
            case IDC_CLEAR:
                hWndLog = GetDlgItem(hDlg, IDC_LOGTEXT);
                ListBox_ResetContent(hWndLog);
                break;
                /*
                case IDM_ABOUT:
                    DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_ABOUTBOX), hDlg, AboutProc);
                    break;
                case IDM_EXIT:
                    g_hLogWindow = NULL;
                    DestroyWindow(GetParent(hDlg));
                    break;
                */
            }
        }
        else
        {
            switch (HIWORD(wParam))
            {
            case CBN_SELCHANGE:
                hWndCb = GetDlgItem(hDlg, IDC_LOGLEVEL);
                level = ComboBox_GetCurSel(hWndCb);
                vpnx_set_log_level(level);
                break;
            default:
                break;
            }
        }
        break;
    case WM_TIMER:
    {
        char bytes[256];
        TCHAR nstr[32];

        do
        {
            vpnx_get_log_string(bytes, 256);
            if (bytes[0] != 0)
            {
                hWndLog = GetDlgItem(hDlg, IDC_LOGTEXT);
#ifdef UNICODE
                TCHAR logstring[256];
                int len;

                len = MultiByteToWideChar(CP_UTF8, 0, bytes, strlen(bytes), logstring, _countof(logstring));
                logstring[len] = _T('\0');
#else
                LPCTSTR logstring = bytes;
#endif
                ListBox_AddString(hWndLog, logstring);
            }
        }
        while (bytes[0] != '\0');

        _sntprintf(nstr, _countof(nstr), _T("%u"), vpnx_xfer_tx_count());
        SetDlgItemText(hDlg, IDC_XFERTX, nstr);
        _sntprintf(nstr, _countof(nstr), _T("%u"), vpnx_xfer_rx_count());
        SetDlgItemText(hDlg, IDC_XFERRX, nstr);

#ifdef UNICODE
        TCHAR statuni[256];
        const char* statstr;
        int len;

        statstr = vpnx_local_status();
        len = strlen(statstr);

        len = MultiByteToWideChar(CP_UTF8, 0, statstr, len, statuni, _countof(statuni));
        statuni[len] = _T('\0');
        SetDlgItemText(hDlg, IDC_LOCALSTATUS, statuni);

        statstr = vpnx_extender_status();
        len = strlen(statstr);

        len = MultiByteToWideChar(CP_UTF8, 0, statstr, len, statuni, _countof(statuni));
        statuni[len] = _T('\0');
        SetDlgItemText(hDlg, IDC_EXTENDERSTATUS, statuni);
#else
        SetDlgItemText(hDlg, IDC_LOCALSTATUS, vpnx_local_status());
        SetDlgItemText(hDlg, IDC_EXTENDERSTATUS, vpnx_extender_status());
#endif
        break;
    }
    case WM_DESTROY:
        g_hLogWindow = NULL;
        break;
    }
    return (INT_PTR)FALSE;
}

INT_PTR CALLBACK ExtenderProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        SetDlgItemText(hDlg, IDC_WIFINET, netname);
        SetDlgItemText(hDlg, IDC_WIFIPASS, netpass);
        break;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
            GetDlgItemText(hDlg, IDC_WIFINET, netname, _countof(netname));
            GetDlgItemText(hDlg, IDC_WIFIPASS, netpass, _countof(netpass));
            SaveSettings();
#ifdef UNICODE
            {
                char netstr[VPNX_MAX_HOST];
                char passstr[VPNX_MAX_HOST];
                int inlen = _tcslen(netname);
                int len = WideCharToMultiByte(CP_UTF8, 0, netname, inlen, netstr, sizeof(netstr), NULL, NULL);
                netstr[len] = '\0';

                inlen = _tcslen(netpass);
                len = WideCharToMultiByte(CP_UTF8, 0, netpass, inlen, passstr, sizeof(passstr), NULL, NULL);
                passstr[len] = '\0';
                vpnx_set_network(netstr, passstr);
            }
#else
            vpnx_set_network(netname, netpass);
#endif
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        case IDCANCEL:
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        case IDC_RESTART_EXTENDER:
            vpnx_reboot_extender();
            break;
        }
        break;
    }
    return (INT_PTR)FALSE;
}


static LPCTSTR szPortNum(uint16_t port, LPTSTR buffer, size_t nbuffer)
{
    if (port != 0)
    {
        _sntprintf(buffer, nbuffer, _T("%u"), port);
    }
    else
    {
        buffer[0] = _T('\0');
    }
    return buffer;
}

INT_PTR CALLBACK SettingsProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    TCHAR portstring[32];
    int i;

    switch (message)
    {
    case WM_INITDIALOG:
        for (i = 0; i < 4; i++)
        {
            SetDlgItemText(hDlg, IDC_RHOST1 + i, szRemoteHosts[i]);
            SetDlgItemText(hDlg, IDC_RPORT1 + i, szPortNum(remote_ports[i], portstring, _countof(portstring)));
            SetDlgItemText(hDlg, IDC_LPORT1 + i, szPortNum(local_ports[i], portstring, _countof(portstring)));
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
            for (i = 0; i < 4; i++)
            {
                GetDlgItemText(hDlg, IDC_RHOST1 + i, szRemoteHosts[i], VPNX_MAX_HOST);
                GetDlgItemText(hDlg, IDC_RPORT1 + i, portstring, _countof(portstring));
                remote_ports[i] = (uint16_t)_tcstoul(portstring, NULL, 0);
                GetDlgItemText(hDlg, IDC_LPORT1 + i, portstring, _countof(portstring));
                local_ports[i] = (uint16_t)_tcstoul(portstring, NULL, 0);
            }
            SaveSettings();
            RestartExtender();
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        case IDCANCEL:
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        case IDM_ABOUT:
            DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_ABOUTBOX), hDlg, AboutProc);
            break;
        case IDM_EXIT:
            DestroyWindow(GetParent(hDlg));
            break;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

LRESULT CALLBACK VpnxProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    HWND hDlg;

    switch (message)
    {
    case WM_TRAY:
        switch (lParam)
      	{
     	case WM_LBUTTONDBLCLK:
      		SendMessage(hWnd, WM_COMMAND, ID_POPUP_SETTINGS, 0);
      		break;    
      	case WM_RBUTTONDOWN:
      	{
      		HMENU hMenu = LoadMenu(g_hInstance, MAKEINTRESOURCE(IDR_POPUP));
      		if (hMenu)
     		{
     			HMENU hSubMenu = GetSubMenu(hMenu, 0);
                if (hSubMenu)
                {
                    POINT stPoint;
                    GetCursorPos(&stPoint);

                    TrackPopupMenu(hSubMenu, TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON, stPoint.x, stPoint.y, 0, hWnd, NULL);
                }
                DestroyMenu(hMenu);
            }
            break;
  		}
  		}
        break;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDM_ABOUT:
        case ID_POPUP_ABOUT:
            DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, AboutProc);
            break;
        case IDM_EXIT:
        case ID_POPUP_EXIT:
            DestroyWindow(hWnd);
            break;
        case ID_POPUP_SETTINGS:
            DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_SETTINGS), hWnd, SettingsProc);
            break;
        case ID_POPUP_EXTENDER:
            hDlg = CreateDialog(g_hInstance, MAKEINTRESOURCE(IDD_EXTENDER), hWnd, ExtenderProc);
            ShowWindow(hDlg, SW_SHOW);
            break;
        case ID_POPUP_LOGGING:
            hDlg = CreateDialog(g_hInstance, MAKEINTRESOURCE(IDD_LOGGING), hWnd, LoggingProc);
            ShowWindow(hDlg, SW_SHOW);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        break;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
        break;
    }
    case WM_TIMER:
        // if logging dialog window is showing, pass off timer to it
        if (g_hLogWindow)
        {
            PostMessage(g_hLogWindow, WM_TIMER, wParam, lParam);
        }
        break;
    case WM_DESTROY:
        Shell_NotifyIcon(NIM_DELETE, &niData);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

ATOM VpnxRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    g_hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_VPNEXTENDER));
    g_hInstance = hInstance;

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = VpnxProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = g_hIcon;
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = 0; // MAKEINTRESOURCEW(IDC_VPNEXTENDER);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

ULONGLONG GetDllVersion(LPCTSTR lpszDllName)
{
    ULONGLONG ullVersion = 0;
    HINSTANCE hinstDll;
    hinstDll = LoadLibrary(lpszDllName);
    if (hinstDll)
    {
        DLLGETVERSIONPROC pDllGetVersion;
        pDllGetVersion = (DLLGETVERSIONPROC)GetProcAddress(hinstDll, "DllGetVersion");
        if (pDllGetVersion)
        {
            DLLVERSIONINFO dvi;
            HRESULT hr;
            ZeroMemory(&dvi, sizeof(dvi));
            dvi.cbSize = sizeof(dvi);
            hr = (*pDllGetVersion)(&dvi);
            if (SUCCEEDED(hr))
                ullVersion = MAKEDLLVERULL(dvi.dwMajorVersion, dvi.dwMinorVersion, 0, 0);
        }
        FreeLibrary(hinstDll);
    }
    return ullVersion;
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    WCHAR szString[MAX_LOADSTRING];
    int i;

    g_hInstance = hInstance;

    HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
             CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

    if (!hWnd)
    {
       return FALSE;
    }

    ZeroMemory(&niData, sizeof(niData));

    ULONGLONG ullVersion = GetDllVersion(_T("Shell32.dll"));
    if (ullVersion >= MAKEDLLVERULL(6, 0, 6, 0))
        niData.cbSize = sizeof(NOTIFYICONDATAW);
    else if (ullVersion >= MAKEDLLVERULL(6, 0, 0, 0))
        niData.cbSize = NOTIFYICONDATA_V3_SIZE;
    else if (ullVersion >= MAKEDLLVERULL(5, 0, 0, 0))
        niData.cbSize = NOTIFYICONDATA_V2_SIZE;
    else
        niData.cbSize = NOTIFYICONDATA_V1_SIZE;

    niData.uID = 0xfeed;

    niData.hWnd              = hWnd;
    niData.uFlags            = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    niData.uCallbackMessage  = WM_TRAY;
    niData.hIcon             = g_hIcon;

    LoadStringW(hInstance, IDS_BALLOON_TITLE, szString, MAX_LOADSTRING);
    wcsncpy_s(niData.szInfoTitle, _countof(niData.szInfoTitle), szString, _countof(niData.szInfoTitle));

    LoadStringW(hInstance, IDS_BALLOON_BODY, szString, MAX_LOADSTRING);
    wcsncpy_s(niData.szInfo, _countof(niData.szInfo), szString, _countof(niData.szInfo));

    LoadStringW(hInstance, IDS_TOOLTIP, szString, MAX_LOADSTRING);
    wcsncpy_s(niData.szTip, _countof(niData.szTip), szString, _countof(niData.szTip));

    RestoreSettings();

    // if all local or remote ports are 0, open settings
    //

    if (!Shell_NotifyIcon(NIM_ADD, &niData))
    {
        return -1;
    }
 
    if (0)
    {
        ShowWindow(hWnd, nCmdShow);
        UpdateWindow(hWnd);
    }
    for (i = 0; i < 4; i++)
    {
        if (remote_ports[i] != 0 && local_ports[i] != 0 && szRemoteHosts[i][0] != _T('\0'))
        {
            break;
        }
    }
    if (i < 4)
    {
        RestartExtender();
    }
    else
    {
        DialogBox(g_hInstance, MAKEINTRESOURCE(IDD_SETTINGS), hWnd, SettingsProc);
    }
    // start a slow timer for main window to allow polling for usb device every 1/2 second
    //
    SetTimer(hWnd, 0xB00BFACE, 500, NULL);

    return TRUE;
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_VPNEXTENDER, szWindowClass, MAX_LOADSTRING);

    HWND hHiddenWnd = FindWindow(szWindowClass, NULL);
    if (hHiddenWnd)
    {
        PostMessage(hHiddenWnd, WM_TRAY, 0, WM_LBUTTONDBLCLK);
        return 0;
    }
    VpnxRegisterClass(hInstance);

    if (!InitInstance(hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_VPNEXTENDER));
    MSG msg;
    BOOL gotmsg;
    BOOL ret;

    do
    {
        gotmsg = FALSE;

        if (extender_running)
        {
            ret = PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE);
            if (!ret)
            {
                vpnx_gui_slice();
                ret = TRUE;
            }
            else
            {
                if (msg.message == WM_QUIT)
                {
                    ret = FALSE;
                }
                else
                {
                    gotmsg = TRUE;
                }
            }
        }
        else
        {
            ret = GetMessage(&msg, nullptr, 0, 0);
            gotmsg = ret;
        }
        if (gotmsg)
        {
            if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    }
    while (ret);

    return (int)msg.wParam;
}

