// vpnextender.cpp : Defines the entry point for the application.
//

#include "framework.h"
#include "vpnextender.h"
#include "resource.h"

#include <shellapi.h>
#include <shlwapi.h>

#define MAX_LOADSTRING 100

static WCHAR szTitle[MAX_LOADSTRING];
static WCHAR szWindowClass[MAX_LOADSTRING];
HINSTANCE g_hInstance;
HICON g_hIcon;

static const UINT WM_TRAY = WM_USER + 1;
static NOTIFYICONDATA niData;

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

INT_PTR CALLBACK SettingsProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        break;
    case WM_CLOSE:
        PostQuitMessage(0);
        break;
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
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

   if (!Shell_NotifyIcon(NIM_ADD, &niData))
   {
       return -1;
   }
 
   if (0)
   {
       ShowWindow(hWnd, nCmdShow);
       UpdateWindow(hWnd);
   }
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

    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int)msg.wParam;
}

