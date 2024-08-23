// sscreater.cpp : Defines the entry point for the application.
//
#define NOMINMAX
#include "framework.h"
#include "sfed.h"
#include "resource.h"
#include <commdlg.h>
#include <shellscalingapi.h>
#include <psapi.h>
#include <shlobj.h>  
#include <comdef.h>  
#include <map>
#include <format>
#include <filesystem>
#include <vector>
#include <fstream>
#include <string>
#include <memory>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <commctrl.h>  
#include <zlib.h> 
#include <limits> 
#include <thread>
#include <mutex>
#include <future>
#include <algorithm>
#include <unordered_set>
#include <cmath>

#pragma comment(lib, "comctl32.lib")  // Link against the common controls library
#pragma comment(lib, "Shcore.lib")

#include "espmanger.h"

// Forward function declarations
std::string GetTimeSince(LARGE_INTEGER);
BOOL OpenFileDialog(HWND, LPWSTR,DWORD, LPWSTR, BOOL);
ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK CreateStarDlg(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK CreatePlanetDlg(HWND, UINT, WPARAM, LPARAM);
void OutputStr(const std::string& strOut);
void OutputStr(const WCHAR* pwchar);

// Global Variables:
#define MAX_LISTITEMS 50000
#define MAX_LOADSTRING 100
#define ID_TIMER 1
#define TIMER_INTERVAL 30000 // 30 seconds
#define MAX_REC_NAME_LENGTH (48) // Must not be bigger than 254. But larger then this does not make sense for displayed names.

HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name
HWND hMainWnd;
HWND hStatusBar;                                // Handle to the status bar
HWND hTextOutWnd;                               // Handle for the text control
CEsp* pEspSrc = NULL;                           // class that holds a loaded source esp/esm file (parital PDNT and STDT recs only so far)
CEsp* pEspDst = NULL;                           // class that holds a loaded destination esp file 

// Why does this not already exist?
std::string toLowerCase(const std::string& str) 
{
    std::string lowerStr = str;
    std::transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(), [](unsigned char c) { return std::tolower(c); });
    return lowerStr;
}

void capsFirstLetter(std::string& str) 
{
    if (!str.empty() && std::isalpha(static_cast<unsigned char>(str[0]))) 
        str[0] = std::toupper(static_cast<unsigned char>(str[0]));
    }

// convert std::string to wchar for windows 
std::wstring strToWstr(const std::string& str)
{
    int str_len = (int)str.length() + 1;
    int len = MultiByteToWideChar(CP_ACP, 0, str.c_str(), str_len, 0, 0);
    std::wstring wstr(len, L'\0');
    MultiByteToWideChar(CP_ACP, 0, str.c_str(), str_len, &wstr[0], len);
    return wstr;
}

// wchar to std::string
std::string wcharTostr(const WCHAR* wideStr) {
    if (wideStr == nullptr) 
        return std::string();

    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, nullptr, 0, nullptr, nullptr);
    if (sizeNeeded <= 0) 
        return std::string(); // Handle the case where conversion failed

    std::string narrowStr(sizeNeeded - 1, 0); // -1 to exclude the null terminator

    int result = WideCharToMultiByte(CP_UTF8, 0, wideStr, -1, &narrowStr[0], sizeNeeded, nullptr, nullptr);
    if (result == 0) 
        return std::string(); 

    return narrowStr;
}

std::string wstrtostr(const std::wstring& wstr)
{
    return wcharTostr(wstr.c_str());
}

std::wstring wstrfnTrunc(std::wstring wfn, int iTruncate=36)
{
    return wfn.size() > iTruncate ? std::wstring(L"...") + wfn.substr(wfn.size() - iTruncate) : wfn;
}

std::string strfnTrunc(std::string sfn, int iTruncate=36)
{
    return sfn.size() > iTruncate ? std::string("...") + sfn.substr(sfn.size() - iTruncate) : sfn;
}

// Covert binary stream to hex editor style output for debugging
std::string binaryTostr(const char *pcbuff, size_t size) 
{
    std::stringstream ssHex;
    std::stringstream ssAscii;
    const uint8_t *buffer = (const uint8_t *)pcbuff;
    
    // Process each byte in the buffer
    for (size_t i = 0; i < size; ++i)
    {
        // Convert to hexadecimal representation
        ssHex << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(buffer[i]) << " ";
        
        if (isprint(buffer[i])) 
            ssAscii << static_cast<char>(buffer[i]);
        else 
            ssAscii << '.';
        
    }
    
    std::string result = ssHex.str() + "\t" + ssAscii.str();
    return result;
}

//////////////////////////////////////////////////////////////////////
// Windows functions
//////////////////////////////////////////////////////////////////////

std::string GetDownloadsFolderPath() // Dir used for requested dump output
{
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, NULL, &path)))
    {
        _bstr_t bstrPath(path);
        std::string downloadsPath = (const char*)bstrPath;
        CoTaskMemFree(path);
        return downloadsPath;
    }

    return "";
}

std::string GetExecutableDir()
{
    WCHAR path[MAX_PATH];
    // Get the full path of the executable
    GetModuleFileName(NULL, path, MAX_PATH);
    std::string fullPath = wcharTostr(path);
    std::string directory = fullPath.substr(0, fullPath.find_last_of("\\/"));
    return directory;
}

LARGE_INTEGER GetStartTime()
{
    LARGE_INTEGER startTime;
    QueryPerformanceCounter(&startTime);
    return startTime;
}

// Get time elapsted for performance debugging
std::string GetTimeSince(LARGE_INTEGER startTime)
{
    LARGE_INTEGER frequency;
    LARGE_INTEGER endTime;

    QueryPerformanceCounter(&endTime);
    QueryPerformanceFrequency(&frequency);

    double elapsedTime = static_cast<double>(endTime.QuadPart - startTime.QuadPart) / frequency.QuadPart;
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.4f seconds", elapsedTime);

    return std::string(buffer);
}

// Output a string to gui control used for text output, and new line by default
void OutputStr(const WCHAR* pwchar) { OutputStr(wcharTostr(pwchar)); }
void OutputStr(const std::string& strOut)
{
    if (!hTextOutWnd)
        return;

    // Add the string to the list box
    SendMessageA(hTextOutWnd, LB_ADDSTRING, 0, (LPARAM)strOut.c_str());

    // Check if the count exceeds the maximum
    LRESULT count = SendMessage(hTextOutWnd, LB_GETCOUNT, 0, 0);
    if (count > MAX_LISTITEMS)
        SendMessage(hTextOutWnd, LB_DELETESTRING, 0, 0);

    // Scroll to the last item
    count = (int)SendMessage(hTextOutWnd, LB_GETCOUNT, 0, 0);
    if (count > 0)
        SendMessage(hTextOutWnd, LB_SETTOPINDEX, count - 1, 0);

    RedrawWindow(hTextOutWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
}

bool IsNameUnique(CEsp* pEsp, CEsp::ESPRECTYPE eType, const std::string& strName)
{
    if (strName.empty())
        return false;

    // TODO maybe build this once:
    std::unordered_set<std::string> nameSet;

    std::vector<CEsp::BasicInfoRec> oBasicInfoRecs;
    pEsp->getBasicInfoRecs(eType, oBasicInfoRecs);
    for (const CEsp::BasicInfoRec& oBasicInfo : oBasicInfoRecs)
    {
        if (*oBasicInfo.m_pName) nameSet.insert(toLowerCase(oBasicInfo.m_pName));
        if (*oBasicInfo.m_pAName) nameSet.insert(toLowerCase(oBasicInfo.m_pAName));
    }

    return nameSet.find(toLowerCase(strName)) == nameSet.end();
}

bool ValidateDestFilename(HWND hWnd, const WCHAR* szFileName)
{
    if (!pEspSrc || !szFileName || !*szFileName)
        return false;

    // Get everying in lower case before checking it
    std::filesystem::path filePathlower(toLowerCase(std::filesystem::path(szFileName).string()));
    std::string sfull = filePathlower.string();
    std::string sname = filePathlower.filename().string();
    std::string strExt = filePathlower.extension().string();
    std::string src = toLowerCase(pEspSrc->getFnameAsStr());

    // Check source is not same as dest
    if (src == sfull)
        MessageBox(hWnd, L"Source file is same as selected destination.", L"Error", MB_OK | MB_ICONERROR);
    else
    if (sname == "starfield.esm")
        MessageBox(hWnd, L"Selected destination file is starfield.esm. It should be a ESP plugin file.", L"Error", MB_OK | MB_ICONERROR);
    else
    if (strExt != ".esp")
        MessageBox(hWnd, L"Selected should be a ESP plugin file.", L"Error", MB_OK | MB_ICONERROR);
    else
        return true;

    return false;
}

bool LoadESP(CEsp* &pEsp, LPCWCHAR wszfn)
{
    // Don't use pEspSrc or pEstDst inside this
    if (pEsp)
    {
        delete pEsp;
        pEsp = new CEsp();
    }

    std::wstring fn = wszfn;
        
    std::string strErr;
    LARGE_INTEGER liStart = GetStartTime();
    OutputStr(std::string("Loading ") + wstrtostr(fn) + std::string("..."));
    // Short cut if using debugger
    pEsp = new CEsp;
    if (!pEsp->load(fn, strErr))
    {
        OutputStr(std::string("Load failed with error: ") + strErr);
        delete pEsp;
        pEsp = nullptr;
        return false;
    }
    OutputStr(std::string("Finished in ") + GetTimeSince(liStart) + ".");

    std::string strNumST = std::to_string(pEsp->getNum(CEsp::eESP_STDT));
    std::string strNumPN = std::to_string(pEsp->getNum(CEsp::eESP_PNDT));
    std::string strNumLC = std::to_string(pEsp->getNum(CEsp::eESP_LCTN));
    std::string str = std::string("Found ") + strNumST + " star(s), " + strNumPN + " planet(s) and " + strNumLC + " locations.";
    if (!pEsp->isESM()) str += " Uses Master file " + pEsp->getMasterFname() + ".";
    OutputStr(str);
    std::vector<std::string> oOutputs;
    pEsp->dumpBadRecs(oOutputs);
    for (const std::string& oStr : oOutputs)
        OutputStr(oStr);
    if (pEsp->getMissingBfceCount())
        OutputStr("Found " + std::to_string(pEsp->getMissingBfceCount()) + " planet(s) or star(s) with missing end markers for BFCE block. They were not marked as bad.");

    return true;
}

void DebugdumpEspData(CEsp *pEsp, const std::string &pref)
{
    if (!pEsp)
        return;
    std::string strdir = GetDownloadsFolderPath();
    if (strdir.empty())
        strdir = GetExecutableDir();
    std::filesystem::path pathObj(pEsp->getFname());
    std::string strfile = toLowerCase(strdir + "\\" + pathObj.filename().string() + ".txt");
    pEsp->dumptofile(strfile);
    OutputStr(pref + " data dumped to file: " + strfile);
}

void UpdateStatusBar()
{
    std::string strSrc, strDst, strMem;

    if (pEspSrc) strSrc = "[" + pEspSrc->getFnameRoot()+ " " + pEspSrc->dumpStats() + "]";
    if (pEspDst) strDst = "[" + pEspDst->getFnameRoot()+ " " + pEspDst->dumpStats() + "]";

    PROCESS_MEMORY_COUNTERS_EX pmc{};
    HANDLE hProcess = GetCurrentProcess();
    GetProcessMemoryInfo(hProcess, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
    CloseHandle(hProcess);
    strMem = std::format("[Memory used : {} MB]", pmc.WorkingSetSize / 1024 / 1024);

    std::string str = strSrc + (!strSrc.empty() ? " " : "") +strDst + (!strSrc.empty() ? " " : "") + strMem;
    std::wstring wstr = strToWstr(str);
    SendMessage(hStatusBar, SB_SETTEXT, 0, (LPARAM)wstr.c_str()); // status bar expects WCHAR only, so sendmessageA does not work with CHAR
}

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
    // Fix blurry OpenFileDialog due to it not taking account the DPI
    SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);

    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_SFED, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    if (!InitInstance (hInstance, nCmdShow))
        return FALSE;

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_SFED));
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

    return (int) msg.wParam;
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex{};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = wcex.cbWndExtra = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_SFEDCR));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_SFED);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance; 
    int SIZE_FACTOR = 300; 
    int defaultWidth = GetSystemMetrics(SM_CXSCREEN);  
    int defaultHeight = GetSystemMetrics(SM_CYSCREEN); 
    int width = defaultWidth*200 / SIZE_FACTOR;
    int height = defaultHeight*200 / SIZE_FACTOR;

    // Create the window with the specified size
    HWND hMainWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW, (defaultWidth-width)/2, (defaultHeight-height)/2, 
        width, height, nullptr, nullptr, hInstance, nullptr);

   if (!hMainWnd)
      return FALSE;

   ShowWindow(hMainWnd, nCmdShow);
   UpdateWindow(hMainWnd);

   return TRUE;
}

// Select a File Dialog
BOOL OpenFileDialog(HWND hWnd, LPWSTR wszFileName, DWORD nMaxFile, LPWSTR wszTitle, BOOL bLimited)
{
    OPENFILENAME ofn{};

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    if (bLimited)
        ofn.lpstrFilter = L"ESP Files (*.esp)\0*.esp\0";
    else
        ofn.lpstrFilter = L"Mod Files (*.esm, *.esp)\0*.esm;*.esp\0ESM Files (*.esm)\0*.esm\0ESP Files (*.esp)\0*.esp\0Binary Files (*.bin)\0*.bin\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = wszFileName;
    ofn.lpstrTitle = wszTitle;
    ofn.nMaxFile = nMaxFile;
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = L"";

    return GetOpenFileName(&ofn);
}

BOOL ShowSaveAsDialog(HWND hWnd, std::wstring &wstrFn, DWORD nMaxFile)
{
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (!SUCCEEDED(hr))
        return false;

    IFileSaveDialog *pFileSave;
    hr = CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_ALL,  IID_IFileSaveDialog, reinterpret_cast<void**>(&pFileSave));

    if (SUCCEEDED(hr))
    {
        DWORD dwOptions;
        hr = pFileSave->GetOptions(&dwOptions);
        if (SUCCEEDED(hr)) hr = pFileSave->SetOptions(dwOptions | FOS_OVERWRITEPROMPT);
        if (SUCCEEDED(hr)) hr = pFileSave->SetTitle(L"Save Destination ESP As");
        if (SUCCEEDED(hr)) hr = pFileSave->SetOkButtonLabel(L"Save");

        if (SUCCEEDED(hr))
        {
            hr = pFileSave->Show(NULL);
            if (SUCCEEDED(hr))
            {
                IShellItem *pItem;
                hr = pFileSave->GetResult(&pItem);
                if (SUCCEEDED(hr))
                {
                    PWSTR pszFilePath;
                    hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
                    if (SUCCEEDED(hr))
                    {
                        wstrFn = pszFilePath;
                        CoTaskMemFree(pszFilePath);
                        return true;
                    }
                    pItem->Release();
                }
            }
        }
        pFileSave->Release();
    }

    CoUninitialize();
    return false;
}

bool SaveESP(HWND hWnd, BOOL bSaveAs)
{
    std::string strErr;
    if (!pEspDst)
        return false;

    if (!pEspDst->checkdata(strErr))
    {
        if (MessageBoxA(NULL, std::string(std::string("An error was found: ") + strErr
            + ". Are you sure you want to save?").c_str(), "Save confirmation", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) == IDNO)
            return false;
    }

    if (!bSaveAs)
    {
        std::string strBkName;
        if (!pEspDst->copyToBak(strBkName, strErr))
        {
            std::string strMsg = "Could not create backup of " + pEspDst->getFnameAsStr() + ". Error: " + strErr;
            MessageBoxA(hWnd, strMsg.c_str(), "Error", MB_OK | MB_ICONERROR);
            return false;
        }
        OutputStr("Created back up of destination to filename " + strBkName + ".");
    }

    if (!pEspDst->save(strErr))
    {
        std::string strMsg = "Could not save to " + pEspDst->getFnameAsStr() + ". Error: " + strErr;
        MessageBoxA(hWnd, strMsg.c_str(), "Error", MB_OK | MB_ICONERROR);
        return false;
    }
    OutputStr("Saved " + pEspDst->getFnameAsStr() + ".");
    return true;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_CREATE:
            {
                // Initialize common controls
                INITCOMMONCONTROLSEX icex;
                icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
                icex.dwICC = ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES;
                InitCommonControlsEx(&icex);

                // Create a font object 
                NONCLIENTMETRICS ncm = { sizeof(NONCLIENTMETRICS) };
                HFONT hFont = NULL;
                if (SystemParametersInfo(SPI_GETNONCLIENTMETRICS, sizeof(NONCLIENTMETRICS), &ncm, 0))
                     hFont = CreateFontIndirect(&ncm.lfMenuFont);
                if (!hFont)
                    hFont = CreateFont(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Arial");
                if (!hFont)
                {
                    MessageBox(hWnd, L"Failed to create Font for window.", L"Error", MB_OK | MB_ICONERROR);
                    return -1;
                }
            
                // Create the status bar
                if (!(hStatusBar = CreateWindowEx(0, STATUSCLASSNAME, nullptr, WS_CHILD | WS_VISIBLE, 
                    0, 0, 0, 0, hWnd, nullptr, hInst, nullptr)))
                {
                    MessageBox(hWnd, L"Failed to create status bar.", L"Error", MB_OK | MB_ICONERROR);
                    return -1;
                }
                SendMessage(hStatusBar, WM_SIZE, 0, 0);
            
                // Create a read-only multiline edit control to act as the text display area
                if (!(hTextOutWnd = CreateWindowEx(NULL, WC_LISTBOX, NULL, WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_AUTOVSCROLL, 
                    0, 0, 0, 0, hWnd, nullptr, hInst, NULL)))
                {
                    MessageBox(hWnd, L"Failed to create text control.", L"Error", MB_OK | MB_ICONERROR);
                    return -1;
                }
                SendMessage(hTextOutWnd, WM_SIZE, 0, 0);

                SendMessage(hStatusBar, WM_SETFONT, (WPARAM)hFont, TRUE);
                SendMessage(hTextOutWnd, WM_SETFONT, (WPARAM)hFont, TRUE);
                SendMessage(hStatusBar, SB_SETTEXT, 0, (LPARAM)L"Ready");

                SetTimer(hWnd, ID_TIMER, TIMER_INTERVAL, NULL); // Set the timer for 30 seconds

                if (IsDebuggerPresent()) // Shortcut while testing
                {
                    std::wstring wstrSrc = L"D:\\SteamLibrary\\steamapps\\common\\Starfield\\Data\\starfield.esm";
                    std::wstring wstrDst = L"D:\\SteamLibrary\\steamapps\\common\\Starfield\\Data\\test.esp";
                    LoadESP(pEspSrc, wstrSrc.c_str());
                    LoadESP(pEspDst, wstrDst.c_str());
                    UpdateStatusBar();
                }
                else
                {
                    // RemoveMenu(GetMenu(hWnd), ID_DEBUG_DUMPDATA, MF_BYCOMMAND);
                    // DrawMenuBar(GetActiveWindow());
                }
                return 0;
            }
            break;

        case WM_TIMER:
            if (wParam == ID_TIMER)
                UpdateStatusBar();
            break;

        case WM_SIZE:
            {
                RECT rcClient, rcStatus;
                GetClientRect(hWnd, &rcClient);
                GetWindowRect(hStatusBar, &rcStatus);
                int statusHeight = rcStatus.bottom - rcStatus.top;
                SendMessage(hStatusBar, WM_SIZE, 0, 0);
                SetWindowPos(hTextOutWnd, nullptr, 0, 0, rcClient.right, rcClient.bottom - statusHeight, SWP_NOZORDER);
            }
            break;

        case WM_CLOSE:
            {
                std::string strErr;
                if (pEspDst && !pEspDst->isSaved())
                {
                    if (MessageBoxA(NULL, std::string("There are unsaved changes. Are you sure you want to exit?").c_str(),
                        "Save Confirmation", MB_ICONWARNING | MB_YESNO | MB_DEFBUTTON2) == IDNO)
                        break;
                }

                DestroyWindow(hWnd);
                break;
            }

        case WM_COMMAND:
            {
                int wmId = LOWORD(wParam);
                switch (wmId)
                {
                    case ID_DEBUG_DUMPDATA:
                        DebugdumpEspData(pEspSrc, "Source");
                        DebugdumpEspData(pEspDst, "Destination");
                        break;

                    case IDM_ABOUT:
                        DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                        break;
                    case IDM_FILE_SOURCE:
                        {
                            WCHAR wszFileName[MAX_PATH] = L"";
                            WCHAR wszTitle[] = L"Open source";
                            if (OpenFileDialog(hWnd, wszFileName, MAX_PATH, wszTitle, false))
                            {
                                LoadESP(pEspSrc, wszFileName);
                                UpdateStatusBar();
                            }
                        }
                        break;
                    case IDM_FILE_DEST:
                        if (!pEspSrc) // Needed because below we valid the source name is not same as the dest
                            MessageBox(hWnd, L"Please select a source file before the destination.", L"Error", MB_OK | MB_ICONERROR);
                        else
                        {
                            WCHAR wszFileName[MAX_PATH] = L"";
                            WCHAR wszTitle[] = L"Open destination";
                            if (OpenFileDialog(hWnd, wszFileName, MAX_PATH, wszTitle, true) && *wszFileName)
                            {
                                if (ValidateDestFilename(hWnd, wszFileName)) // Will display any errors
                                {
                                    LoadESP(pEspDst, wszFileName);
                                    UpdateStatusBar();
                                }
                            }
                        }
                        break;
                    case ID_FILE_SAVEAS: [[fallthrough]]; 
                    case ID_FILE_SAVE:
                        if (!pEspDst)
                            MessageBox(hWnd, L"Please select a destination file.", L"Error", MB_OK | MB_ICONERROR);
                        else
                        if (wmId==ID_FILE_SAVE)
                            SaveESP(hWnd, false);
                        else
                        {
                            std::wstring wstrFn;
                            if (!ShowSaveAsDialog(hWnd, wstrFn, MAX_PATH) || wstrFn.empty())
                                break;
                            pEspDst->setNewFname(wstrFn);
                            SaveESP(hWnd, true);
                        }
                        break;
                    case IDM_FILE_CREATE_STAR: [[fallthrough]]; 
                    case IDM_FILE_CREATE_PLANET:
                        if (!pEspSrc || !pEspSrc->getNum(CEsp::eESP_STDT) || !pEspSrc->getNum(CEsp::eESP_PNDT))
                            MessageBox(hWnd, L"An ESP or ESM must be selected as the 'Source' which contains at least one star and one planet.", L"Error", MB_OK | MB_ICONERROR);
                        else
                        if (!pEspDst)
                            MessageBox(hWnd, L"An ESP must be selected as the 'Destination' to save.", L"Error", MB_OK | MB_ICONERROR);
                        else
                            DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(wmId==IDM_FILE_CREATE_STAR ? IDD_DIALOG0 : IDD_DIALOG1), 
                                hWnd, wmId==IDM_FILE_CREATE_STAR ? CreateStarDlg : CreatePlanetDlg);
                        break;

                    case IDM_EXIT:
                        PostMessage(hWnd, WM_CLOSE, 0, 0);
                        break;

                    default:
                        return DefWindowProc(hWnd, message, wParam, lParam);
                }
            }
            break;

        case WM_PAINT:
            {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hWnd, &ps);
                // TODO: draw here if needed
                EndPaint(hWnd, &ps);
            }
            break;

        case WM_DESTROY:
            if (pEspSrc) delete pEspSrc;
            if (pEspDst) delete pEspDst;
            pEspSrc = NULL;
            pEspDst = NULL;
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Message handler for aboutbo
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
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

bool _getdlgitempos(HWND hDlg, int id, float &f)
{
    f = 0.0;
    HWND hWndEdit = GetDlgItem(hDlg, id);  
    int length = GetWindowTextLength(hWndEdit);
    if (length <= 0) 
        return false;

    std::vector<char> buffer(length + 1);  
    GetWindowTextA(hWndEdit, buffer.data(), length + 1);
    std::string editText(buffer.data()); 

    try { f = std::stof(editText); } catch (...) { return false; } 
    return true;
}

bool GetfPosfromDialog(HWND hDlg, CEsp::fPos &ofPos, std::string &strErr)
{
    if (!pEspSrc || !pEspDst)
        return false;

    ofPos.clear();
    if (!_getdlgitempos(hDlg, IDC_EDIT_XPOS, ofPos.m_xPos) ||
        !_getdlgitempos(hDlg, IDC_EDIT_YPOS, ofPos.m_yPos) ||
        !_getdlgitempos(hDlg, IDC_EDIT_ZPOS, ofPos.m_zPos))
    {
        strErr = "Star position values are out of the valid range.";
        return false;
    }

    // Check if position is too close to an existing stars in src and dest overal min
    float fmin = pEspDst->getMinDistance(pEspSrc->getMinDistance());

    if (!pEspSrc->checkMinDistance(ofPos, fmin, strErr) ||
        !pEspDst->checkMinDistance(ofPos, fmin, strErr))
        return false;
    
    return true;
}

bool CheckIfHasFourConsecutiveUppercase(const std::string& str) 
{
    int count = 0;

    for (char ch : str) 
    {
        if (std::isupper(static_cast<unsigned char>(ch))) 
        {
            count++;
            if (count == 4) 
                return true;
        } 
        else 
            count = 0; 
    }

    return false;
}

bool GetEditItemText(HWND hDlg, int id, std::string & strName)
{
    strName.clear();
    HWND hWndEdit = GetDlgItem(hDlg, id);
    int length = GetWindowTextLength(hWndEdit);

    if (length <= 0) 
        return false;

    std::vector<char> buffer(length + 1);
    GetWindowTextA(hWndEdit, buffer.data(), length + 1);
    std::string editText(buffer.data());

    if (editText.empty())
        return false;

    strName = editText;
    return true;
}

bool GetAndValidateNamefromDlg(HWND hDlg, int id, int idFormName, std::string &strName, std::string &strFormName, std::string &strErrMsg)
{
    strName.clear();
    strErrMsg = std::string("Name is empty.");

    if (!pEspSrc || !pEspDst)
    {
        strErrMsg = std::string("No source or destination file selected.");
        return false;
    }

    std::string editText;
    if (!GetEditItemText(hDlg, id, editText))
        return false;

    if (!GetEditItemText(hDlg, idFormName, strFormName))
        return false;


    strErrMsg.clear();
    if (editText.size() <= 1)
    {
        strErrMsg = std::string("Star name is too short.");
            return false;
    }
    else
    if (editText.size() >= 4) 
    {
        std::string last4 = editText.substr(editText.size() - 4);

        // Me thinks I worry to much or about the wrong things

        if (editText.find("Star") != std::string::npos)
        {
            strErrMsg = std::string(editText.size()==4 ? "The Name is Star. This is not a good idea." : 
                "Name includes the postfix 'Star'. This should not used as it will be automatically added "
                "onto the form name for the star when it is created.");
            return false;
        }

        if (CheckIfHasFourConsecutiveUppercase(editText))
        {
            strErrMsg = std::string("Four uppercase letters in a row are not permited. "
                "They may clash with internal tags used in the plugin file format. ");
            return false;
        }

        if (editText.size()>MAX_REC_NAME_LENGTH)
        {
            strErrMsg = std::string("The name is too long. Consider making it shorter.");
            return false;
        }
    }

    // Check name is unique
    if (!IsNameUnique(pEspSrc, CEsp::eESP_STDT, editText) || !IsNameUnique(pEspSrc, CEsp::eESP_PNDT, editText))
    {
        strErrMsg = std::string("The name you provided already exists as a star or planet in the source data.");
        return false;
    }

    if (!IsNameUnique(pEspDst, CEsp::eESP_STDT, editText) || !IsNameUnique(pEspDst, CEsp::eESP_PNDT, editText))
    {
        strErrMsg = std::string("The name you provided already exists as a star or planet in the destination data.");
        return false;
    }

    strName = editText;
    return true;
}

// Handle reformating the form name and updating the edit control which shows it
void UpdateFormNameInDlg(HWND hDlg, int iSrcEdit, int iDstEdit, bool bStar = false)
{
    // Form the form name from the editable one
    HWND hEdit = GetDlgItem(hDlg, iSrcEdit);
    int length = GetWindowTextLength(hEdit);
    std::string newText(length, '\0');
    GetWindowTextA(hEdit, &newText[0], length + 1);

    capsFirstLetter(newText);
    newText.erase(std::remove_if(newText.begin(), newText.end(), ::isspace), newText.end());
    newText += bStar ? "Star" : "Data";
    SetWindowTextA(GetDlgItem(hDlg, iDstEdit), newText.c_str());
}

CEsp::fPos NormalizePos(const CEsp::fPos& pos, const RECT& rect, float minX, float minY, float maxX, float maxY) 
{
    // Prevent division by zero
    float epsilon = 1e-6f;
    float xRange = std::max(maxX - minX, epsilon);
    float yRange = std::max(maxY - minY, epsilon);

    // Normalize the input coordinates to [0, 1] range
    float normalizedX = (pos.m_xPos - minX) / xRange;
    float normalizedY = (pos.m_yPos - minY) / yRange;

    // Clamp the normalized values between 0 and 1
    normalizedX = std::clamp(normalizedX, 0.0f, 1.0f);
    normalizedY = std::clamp(normalizedY, 0.0f, 1.0f);

    // Map to the rectangle's coordinate system
    float xn = normalizedX * (rect.right - rect.left) + rect.left;
    float yn = normalizedY * (rect.bottom - rect.top) + rect.top;

    //OutputStr(std::to_string(xn) + "," + std::to_string(yn) + "," + std::to_string(pos.m_zPos));
        
    return CEsp::fPos(xn, yn, pos.m_zPos);
}

void DrawSmallText(HDC hdc, int x, int y, const std::string str, int fontSize = 15)
{
    HFONT hFont = CreateFont(fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, 
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, TEXT("Arial"));

    std::wstring strw = strToWstr(str);
    const TCHAR* text = strw.c_str();

    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
    SetTextColor(hdc, RGB(255, 255, 255)); // Black text
    SetBkMode(hdc, TRANSPARENT);
    TextOut(hdc, x, y, text, (int)_tcslen(text));
    SelectObject(hdc, hOldFont);
    DeleteObject(hFont);
}

// Starmap
void _drawStar(HDC hdc, int iOffX, int iOffY, const CEsp::StarPlotData &plot, RECT rect, const CEsp::fPos min, const CEsp::fPos max, bool bShowNames = true)
{
    int imar = 3;
    CEsp::fPos normPos = NormalizePos(plot.m_oPos, rect, min.m_xPos, min.m_yPos, max.m_xPos, max.m_yPos);
    Rectangle(hdc,  iOffX + static_cast<int>(normPos.m_xPos) - imar, 
                    iOffY + static_cast<int>(normPos.m_yPos) - imar,
                    iOffX + static_cast<int>(normPos.m_xPos) + imar, 
                    iOffY + static_cast<int>(normPos.m_yPos) + imar);

    if (bShowNames && !plot.m_strStarName.empty())
        DrawSmallText(hdc, iOffX + static_cast<int>(normPos.m_xPos)+imar*2, 
            iOffY + static_cast<int>(normPos.m_yPos), plot.m_strStarName);
}


void _createBrushandPen(HDC hdc, COLORREF rgb, HBRUSH hBr, HPEN hPen)
{
    hPen = CreatePen(PS_SOLID, 2, rgb);
    hBr = CreateSolidBrush(rgb);
    SelectObject(hdc, hBr);
    SelectObject(hdc, hPen);
}

void _deleteBrushandPen(HBRUSH hBr, HPEN hPen)
{
    DeleteObject(hPen);
    DeleteObject(hBr);
}

void _drawblkbkg(HDC hdc, POINT pt1, POINT pt2)
{
    HBRUSH hBrush = CreateSolidBrush(RGB(0, 0, 0));
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
    Rectangle(hdc, pt1.x, pt1.y, pt2.x, pt2.y);
    SelectObject(hdc, hOldBrush);
    DeleteObject(hBrush);
}

// Star map dialog
// TODO support zoom with mouse wheel
// Needed to pass data to the star map
CEsp::StarPlotData gdlgData;
INT_PTR CALLBACK DialogProcStarMap(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_INITDIALOG:
    {
        HWND hCombo = GetDlgItem(hDlg, IDC_COMBOVIEW);
        LRESULT index = SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)"Top (xy)");
        SendMessage(hCombo, CB_SETITEMDATA, (WPARAM)index, (LPARAM)CEsp::PSWAP_NONE);
        index = SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)"Front (xz)");
        SendMessage(hCombo, CB_SETITEMDATA, (WPARAM)index, (LPARAM)CEsp::PSWAP_XZ);
        index = SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)"Side (yz)");
        SendMessage(hCombo, CB_SETITEMDATA, (WPARAM)index, (LPARAM)CEsp::PSWAP_YZ);
        SendMessage(hCombo, CB_SETCURSEL, index, 0);
        HWND hSlider = GetDlgItem(hDlg, IDC_SLIDERDT);
        SendMessage(hSlider, TBM_SETRANGE, TRUE, MAKELPARAM(1, SLIDER_RNG_MAX));
        SendMessage(hSlider, TBM_SETTICFREQ, SLIDER_RNG_MAX, 0);
        SendMessage(hSlider, TBM_SETPOS, TRUE, SLIDER_RNG_MAX);
    }
    return TRUE;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hDlg, &ps);
        int sp = SLIDER_RNG_MAX - (int)SendMessage(GetDlgItem(hDlg, IDC_SLIDERDT), TBM_GETPOS, 0, 0);
        RECT rect, rectW;
        HWND hItem = GetDlgItem(hDlg, IDC_STATIC_P2);
        GetClientRect(hItem, &rect);  // Get the dimensions of the static control
        InflateRect(&rect, -10, -10); // Allow a margin
        rect.right -= 50; // allow addition margin for text flowing to the right
        rect.bottom -= 10; // allow some space for text which is offset under the star
        GetWindowRect(hItem, &rectW);
        POINT pt1 = { rectW.left, rectW.top };
        POINT pt2 = { rectW.right, rectW.bottom };
        ScreenToClient(hDlg, &pt1);
        ScreenToClient(hDlg, &pt2);
        int iOffX = pt1.x;
        int iOffY = pt1.y;
        HRGN hRgn = CreateRectRgn(pt1.x, pt1.y, pt2.x, pt2.y);
        SelectClipRgn(hdc, hRgn);
        _drawblkbkg(hdc, pt1, pt2);

        std::vector<CEsp::StarPlotData>srcPlots;
        std::vector<CEsp::StarPlotData>dstPlots;
       
        CEsp::fPos min, max;
        min.m_xPos = min.m_yPos = min.m_zPos = std::numeric_limits<float>::max();
        max.m_xPos = max.m_yPos = max.m_zPos = std::numeric_limits<float>::min();

        bool bHideDst = IsDlgButtonChecked(hDlg, IDC_HIDEDST) == BST_CHECKED;
        bool bHideSrc = IsDlgButtonChecked(hDlg, IDC_HIDESRC) == BST_CHECKED;

        HWND hCombo = GetDlgItem(hDlg, IDC_COMBOVIEW);
        CEsp::POSSWAP eSwap = (CEsp::POSSWAP)SendMessage(hCombo, CB_GETITEMDATA, SendMessage(hCombo, CB_GETCURSEL, 0, 0), 0);
        HPEN hPen = 0;
        HBRUSH hBr = 0;

        // Must always do this one after other so min/max get set correct
        if (pEspSrc) pEspSrc->getStarPositons(srcPlots, min, max, eSwap);
        if (pEspDst) pEspDst->getStarPositons(dstPlots, min, max, eSwap);

        if (!bHideSrc)
        {
            int i = 0;
            _createBrushandPen(hdc, RGB(255, 255, 255), hBr, hPen);
            for (const CEsp::StarPlotData& plot : srcPlots)
                _drawStar(hdc, iOffX, iOffY, plot, rect, min, max, sp==0 ? 1 : sp==SLIDER_RNG_MAX-1 ? 0 : (i++ % sp)<=0);
            _deleteBrushandPen(hBr, hPen);
        }

        if (!bHideDst)
        {
            _createBrushandPen(hdc, RGB(0, 255, 0), hBr, hPen);
            for (const CEsp::StarPlotData& plot : dstPlots)
                _drawStar(hdc, iOffX, iOffY, plot, rect, min, max, sp!=SLIDER_RNG_MAX-1);
             _deleteBrushandPen(hBr, hPen);
        }
        
        // Draw the new star 
        CEsp::StarPlotData plot(pEspSrc->posSwap(gdlgData.m_oPos, eSwap), gdlgData.m_strStarName.empty() ? "(new unnamed)" : gdlgData.m_strStarName);
        _createBrushandPen(hdc, RGB(255, 0, 0), hBr, hPen);
        _drawStar(hdc, iOffX, iOffY, plot, rect, min, max);
        _deleteBrushandPen(hBr, hPen);

        SelectClipRgn(hdc, NULL);
        DeleteObject(hRgn);
        EndPaint(hDlg, &ps);
        return TRUE;
    }
    case WM_HSCROLL:
        if ((HWND)lParam == GetDlgItem(hDlg, IDC_SLIDERDT))
        {
            InvalidateRect(hDlg, NULL, TRUE);
            UpdateWindow(hDlg);
        }
        break;
    case WM_COMMAND:
        if ((HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == IDC_COMBOVIEW) ||
            ((LOWORD(wParam) == IDC_HIDEDST || LOWORD(wParam) == IDC_HIDESRC || LOWORD(wParam) == IDC_HIDENAMES) && HIWORD(wParam) == BN_CLICKED))
        {
            InvalidateRect(hDlg, NULL, TRUE);
            UpdateWindow(hDlg);
        }
        else
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, LOWORD(wParam));
            return TRUE;
        }
        break;
    }

    return (INT_PTR)FALSE;
}


void _setdlgPos(HWND hDlg, const int iIDCx, const int iIDCy, const int iIDCz, const CEsp::fPos& oPos)
{
    SetDlgItemText(hDlg, iIDCx, std::to_wstring(oPos.m_xPos).c_str());
    SetDlgItemText(hDlg, iIDCy, std::to_wstring(oPos.m_yPos).c_str());
    SetDlgItemText(hDlg, iIDCz, std::to_wstring(oPos.m_zPos).c_str());
}

void _setdlgPos(HWND hDlg, const int iIDCx, const int iIDCy, const int iIDCz, const CEsp::BasicInfoRec &oBasicInfo)
{
    _setdlgPos(hDlg, iIDCx, iIDCy, iIDCz, oBasicInfo.m_StarMapPostion);
}

void _setdlgPos(HWND hDlg, const int iIDCx, const int iIDCy, const int iIDCz, float fvalue)
{
    CEsp::fPos oPos(fvalue, fvalue, fvalue);
    _setdlgPos(hDlg, iIDCx, iIDCy, iIDCz, oPos);
}



// Message handler for create new star/planet wizard
INT_PTR CALLBACK CreateStarDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_INITDIALOG:
            if (!pEspSrc || !pEspDst)
            {
                MessageBox(hDlg, L"A source and destination file must be selected first.", L"Error", MB_OK | MB_ICONERROR);
                return (INT_PTR)FALSE;
            }
            else
            {
                // Populate Combo boxes
                std::vector<CEsp::BasicInfoRec> oBasicInfoRecs;
                pEspSrc->getBasicInfoRecs(CEsp::eESP_STDT, oBasicInfoRecs);
                HWND hCombo1 = GetDlgItem(hDlg, IDC_COMBO1);
                for (const CEsp::BasicInfoRec &oBasicInfo : oBasicInfoRecs)
                    if (*oBasicInfo.m_pName) // leave out blank records (bad records)
                    {
                        LRESULT index = SendMessageA(hCombo1, CB_ADDSTRING, 0, (LPARAM)oBasicInfo.m_pName);
                        if (index != CB_ERR && index != CB_ERRSPACE)
                            SendMessage(hCombo1, CB_SETITEMDATA, (WPARAM)index, (LPARAM)oBasicInfo.m_iIdx);
                    }

                _setdlgPos(hDlg, IDC_EDIT_XPOS, IDC_EDIT_YPOS, IDC_EDIT_ZPOS, 0);

                HWND hComboPL = GetDlgItem(hDlg, IDC_PLAYER_LEVEL);
                HWND hComboPLM = GetDlgItem(hDlg, IDC_PLAYER_LEVEL_MAX);
                for (int i = 0; i <= 255; i++)
                {
                    SendMessageA(hComboPL, CB_ADDSTRING, 0, (LPARAM)std::to_string(i).c_str());
                    SendMessageA(hComboPLM, CB_ADDSTRING, 0, (LPARAM)std::to_string(i).c_str());
                }
                SendMessage(hComboPL, CB_SETCURSEL, 0, 0);
                SendMessage(hComboPLM, CB_SETCURSEL, 0, 0);
            }
            return (INT_PTR)TRUE;

        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_BUTTON_SMAP) 
            {
                CEsp::fPos ofPos;
                std::string strStarName; 
                _getdlgitempos(hDlg, IDC_EDIT_XPOS, ofPos.m_xPos);
                _getdlgitempos(hDlg, IDC_EDIT_YPOS, ofPos.m_yPos);
                _getdlgitempos(hDlg, IDC_EDIT_ZPOS, ofPos.m_zPos);
                GetEditItemText(hDlg, IDC_EDIT_STNAME, strStarName);
                gdlgData = CEsp::StarPlotData(ofPos, strStarName);
                DialogBox(hInst, MAKEINTRESOURCE(IDD_DIALOG_SM), hDlg, DialogProcStarMap); // Dispaly star map
            }
            else
                
            if (LOWORD(wParam) == IDC_BUTTON_SMAPCOPYSRC)
            {
                CEsp::formid_t iIdx = CB_ERR;
                HWND hCombo1 = GetDlgItem(hDlg, IDC_COMBO1);
                LRESULT selectedIndex = SendMessage(hCombo1, CB_GETCURSEL, 0, 0);
                if (selectedIndex != CB_ERR)
                    iIdx = (CEsp::formid_t) SendMessage(hCombo1, CB_GETITEMDATA, (WPARAM)selectedIndex, 0);

                if (iIdx != CB_ERR)
                {
                    CEsp::BasicInfoRec oBasicInfo;
                    pEspSrc->getBasicInfo(CEsp::eESP_STDT, iIdx, oBasicInfo);
                    _setdlgPos(hDlg, IDC_EDIT_XPOS, IDC_EDIT_YPOS, IDC_EDIT_ZPOS, oBasicInfo);
                    
                }
            }
            if (LOWORD(wParam) == IDC_BUTTON_SMAPSETCENTRE) 
            {   
                CEsp::fPos oPosCentre = pEspSrc->findCentre();
                _setdlgPos(hDlg, IDC_EDIT_XPOS, IDC_EDIT_YPOS, IDC_EDIT_ZPOS, oPosCentre);
            }
            else
            if (HIWORD(wParam) == EN_CHANGE && LOWORD(wParam) == IDC_EDIT_STNAME)
                UpdateFormNameInDlg(hDlg, IDC_EDIT_STNAME, IDC_EDIT_STNAMEFORM, true);
            else
            if (pEspSrc && HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == IDC_COMBO1)
            {
                // Get details for currently selected star
                CEsp::formid_t iIdx = CB_ERR;
                HWND hComboStar = GetDlgItem(hDlg, IDC_COMBO1);
                LRESULT selectedIndex = SendMessage(hComboStar, CB_GETCURSEL, 0, 0);
                if (selectedIndex != CB_ERR)
                    iIdx = (CEsp::formid_t) SendMessage(hComboStar, CB_GETITEMDATA, (WPARAM)selectedIndex, 0);

                if (iIdx != CB_ERR)
                {
                    CEsp::BasicInfoRec oBasicInfo;
                    // Set the player level from this data
                    pEspSrc->getBasicInfo(CEsp::eESP_STDT, iIdx, oBasicInfo);
                    SendMessage(GetDlgItem(hDlg, IDC_PLAYER_LEVEL), CB_SETCURSEL, oBasicInfo.m_iSysPlayerLvl, 0);
                    SendMessage(GetDlgItem(hDlg, IDC_PLAYER_LEVEL_MAX), CB_SETCURSEL, oBasicInfo.m_iSysPlayerLvlMax, 0);

                    // TODO: Display info from  selected star
                }
            }
            else
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
            {
                if (LOWORD(wParam) == IDOK)
                {
                    HWND hComboStar = GetDlgItem(hDlg, IDC_COMBO1);
                    LRESULT selectedIndex = SendMessage(hComboStar, CB_GETCURSEL, 0, 0);
                    if (selectedIndex == CB_ERR)
                    {
                        MessageBoxA(hDlg, "A star has not been selected to be used as the source star to create the new star from.", "Warning", MB_OK | MB_ICONWARNING);
                        break;
                    }
                    size_t iIdxStar = (CEsp::formid_t) SendMessage(hComboStar, CB_GETITEMDATA, (WPARAM)selectedIndex, 0);

                    CEsp::fPos ofPos;
                    std::string strErrMsg;
                    if (!GetfPosfromDialog(hDlg, ofPos, strErrMsg))
                    {
                        MessageBoxA(hDlg, strErrMsg.c_str(), "Warning", MB_OK | MB_ICONWARNING);
                        break;
                    }

                    std::string strStarName, strStarFormName;
                    if (!GetAndValidateNamefromDlg(hDlg, IDC_EDIT_STNAME, IDC_EDIT_STNAMEFORM, strStarName, strStarFormName, strErrMsg))
                    {
                        std::string msg = std::string("The name of the star has an error: ") + strErrMsg;
                        MessageBoxA(hDlg, msg.c_str(), "Warning", MB_OK | MB_ICONWARNING);
                        break;
                    }

                    size_t iSystemPlayerLevel = SendMessage(GetDlgItem(hDlg, IDC_PLAYER_LEVEL), CB_GETCURSEL, 0, 0);
                    size_t iSystemPlayerLevelMax = SendMessage(GetDlgItem(hDlg, IDC_PLAYER_LEVEL_MAX), CB_GETCURSEL, 0, 0);

                    CEsp::BasicInfoRec oBasicInfo(CEsp::eESP_STDT, strStarFormName.c_str(), strStarName.c_str(), false, ofPos, iIdxStar, NO_ORBIT, iSystemPlayerLevel, iSystemPlayerLevelMax);
                    if (!pEspDst->makestar(pEspSrc, oBasicInfo, strErrMsg))
                    {
                        std::string msg = std::string("Star creation failed: ") + strErrMsg;
                        MessageBoxA(hDlg, msg.c_str(), "Error", MB_OK | MB_ICONERROR);
                        break;
                    }
                    UpdateStatusBar();
                }

                EndDialog(hDlg, LOWORD(wParam));
                return (INT_PTR)TRUE;
            }
            break;

        case WM_CLOSE:
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
    }
    return (INT_PTR)FALSE;
}

INT_PTR CALLBACK CreatePlanetDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_INITDIALOG:
            if (pEspSrc && pEspDst)
            {
                // Hide moons by default
                CheckDlgButton(hDlg, IDC_CHECK2, BST_CHECKED); 

                // Populate Combo with src stars
                std::vector<CEsp::BasicInfoRec> oBasicInfoRecs;
                pEspSrc->getBasicInfoRecs(CEsp::eESP_STDT, oBasicInfoRecs);
                HWND hCombo2 = GetDlgItem(hDlg, IDC_COMBO2);
                for (const CEsp::BasicInfoRec &oBasicInfo : oBasicInfoRecs)
                {
                    if (*oBasicInfo.m_pName) // leave out blank records (bad records)
                    {
                        LRESULT index = SendMessageA(hCombo2, CB_ADDSTRING, 0, (LPARAM)oBasicInfo.m_pName);
                        if (index != CB_ERR && index != CB_ERRSPACE)
                            SendMessage(hCombo2, CB_SETITEMDATA, (WPARAM)index, (LPARAM)oBasicInfo.m_iIdx);
                    }
                }

                // Populate Combo with dst stars
                pEspDst->getBasicInfoRecs(CEsp::eESP_STDT, oBasicInfoRecs);
                HWND hCombo4 = GetDlgItem(hDlg, IDC_COMBO4);
                for (const CEsp::BasicInfoRec &oBasicInfo : oBasicInfoRecs)
                {
                    if (*oBasicInfo.m_pAName) // leave out blank records (bad records)
                    {
                        LRESULT index = SendMessageA(hCombo4, CB_ADDSTRING, 0, (LPARAM)oBasicInfo.m_pName);
                        if (index != CB_ERR && index != CB_ERRSPACE)
                            SendMessage(hCombo4, CB_SETITEMDATA, (WPARAM)index, (LPARAM)oBasicInfo.m_iIdx);
                    }
                }
                // Find out the other planets in the system and their positions
                // TODO: hard coded for now
                HWND hComboPpos = GetDlgItem(hDlg, IDC_COMBOPLNNUM);
                std::vector<std::string> strords = { "first", "second", "third", "fourth", "fifth", "sixth", "seventh", "eighth", "ninth", "last" };
                  for (const auto& word : strords) 
                     SendMessageA(hComboPpos, CB_ADDSTRING, 0, (LPARAM)word.c_str());
                SendMessage(hComboPpos, CB_SETCURSEL, 0, 0); 
            }


            return (INT_PTR)TRUE;

        case WM_COMMAND:
            if (HIWORD(wParam) == EN_CHANGE && LOWORD(wParam) == IDC_EDIT_PNNAME)
                UpdateFormNameInDlg(hDlg, IDC_EDIT_PNNAME, IDC_EDIT_PNNAMEFORM, false);
            else
            if (pEspSrc && HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == IDC_COMBO2)
            {
                // Get the selected item index
                CEsp::formid_t iIdx = CB_ERR;
                LRESULT selectedIndex = SendMessage((HWND)lParam, CB_GETCURSEL, 0, 0);
                if (selectedIndex != CB_ERR)
                    iIdx = (CEsp::formid_t) SendMessage((HWND)lParam, CB_GETITEMDATA, (WPARAM)selectedIndex, 0);

                if (iIdx != CB_ERR)
                {
                    std::vector<CEsp::BasicInfoRec> oBasicInfoRecs;
                    bool IncludeMoons = !(IsDlgButtonChecked(hDlg, IDC_CHECK2) == BST_CHECKED);
                    pEspSrc->getBasicInfoRecsOrbitingPrimary(CEsp::eESP_STDT, iIdx, oBasicInfoRecs, IncludeMoons);

                    HWND hCombo = GetDlgItem(hDlg, IDC_COMBO3);
                    SendMessage(hCombo, CB_RESETCONTENT, 0, 0);
                    for (const CEsp::BasicInfoRec &oBasicInfo : oBasicInfoRecs)
                    {
                        if (*oBasicInfo.m_pAName) // leave out blank records (bad)
                        {
                            std::string str;
                            str = std::string(oBasicInfo.m_pAName) + (oBasicInfo.m_bIsMoon ? " (moon)" : "");
                            LRESULT index = SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)str.c_str());
                            if (index != CB_ERR && index != CB_ERRSPACE)
                                SendMessage(hCombo, CB_SETITEMDATA, (WPARAM)index, (LPARAM)oBasicInfo.m_iIdx);
                        }
                    }
                }
            }
            else
            if (LOWORD(wParam) == IDC_CHECK2 && HIWORD(wParam) == BN_CLICKED)
            {
                SendMessageA(hDlg, WM_COMMAND, MAKEWPARAM(IDC_COMBO2, CBN_SELCHANGE), (LPARAM)GetDlgItem(hDlg, IDC_COMBO2));
            }
            else
            if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
            {
                if (LOWORD(wParam) == IDOK)
                {
                    if (!pEspDst)
                    {
                        MessageBoxA(hDlg, "No destination ESP file for saving was selected.", "Warning", MB_OK | MB_ICONWARNING);
                        break;
                    }

                    HWND hComboPlanet = GetDlgItem(hDlg, IDC_COMBO3);
                    LRESULT selectedIndex = SendMessage(hComboPlanet, CB_GETCURSEL, 0, 0);
                    if (selectedIndex == CB_ERR)
                    {
                        MessageBoxA(hDlg, "No source planet has been selected to be used to create the new planet.", "Warning", MB_OK | MB_ICONWARNING);
                        break;
                    }
                    size_t iIdxPlanet = (CEsp::formid_t) SendMessage(hComboPlanet, CB_GETITEMDATA, (WPARAM)selectedIndex, 0);

                    std::string strErrMsg, strPlanetName, strPlanetFormName;
                    if (!GetAndValidateNamefromDlg(hDlg, IDC_EDIT_PNNAME, IDC_EDIT_PNNAMEFORM, strPlanetName, strPlanetFormName, strErrMsg))
                    {
                        std::string msg = std::string("The name of the planet has an error: ") + strErrMsg;
                        MessageBoxA(hDlg, msg.c_str(), "Warning", MB_OK | MB_ICONWARNING);
                        break;
                    }

                    HWND hComboDestStar = GetDlgItem(hDlg, IDC_COMBO4);
                    selectedIndex = SendMessage(hComboDestStar, CB_GETCURSEL, 0, 0);
                    if (selectedIndex == CB_ERR)
                    {
                        MessageBoxA(hDlg, "No destination star for the planet has been selected.", "Warning", MB_OK | MB_ICONWARNING);
                        break;
                    }
                    size_t  iIdxPrimary = (size_t) SendMessage(hComboDestStar, CB_GETITEMDATA, (WPARAM)selectedIndex, 0);

                    HWND hComboPpos = GetDlgItem(hDlg, IDC_COMBOPLNNUM);
                    selectedIndex = SendMessage(hComboDestStar, CB_GETCURSEL, 0, 0);
                    size_t iPlanetPosition = (size_t) SendMessage(hComboDestStar, CB_GETITEMDATA, (WPARAM)selectedIndex, 0);

                    // Save details
                    CEsp::BasicInfoRec oBasicInfo(CEsp::eESP_STDT, strPlanetFormName.c_str(), strPlanetName.c_str(), 
                        false, CEsp::fPos(0,0,0), iIdxPlanet, iIdxPrimary, iPlanetPosition);
                    if (!pEspDst->makeplanet(pEspSrc, oBasicInfo, strErrMsg))
                    {
                        std::string msg = std::string("Planet creation failed: ") + strErrMsg;
                        MessageBoxA(hDlg, msg.c_str(), "Error", MB_OK | MB_ICONERROR);
                        break;
                    }
                    UpdateStatusBar();
                }

                EndDialog(hDlg, LOWORD(wParam));
                return (INT_PTR)TRUE;
            }
            break;

        case WM_CLOSE:
            EndDialog(hDlg, IDCANCEL);
            return (INT_PTR)TRUE;
    }
    return (INT_PTR)FALSE;
}

// TODO: support planet save
// TODO: show selected star/planet info in dialogs
// TODO: support moons
// TODO: ?when creating a star extend the dialog so it also has the creating planet part as a star must have at least one planet, or change to a wizard with steps
// TODO: extend dialog to allow for more editing of other records of data
// TODO: support editing of biom data
// TODO: allow 3d view of star/planet
// TODO: support adding POI


// TODO - test saving!
// 
// Handle faction value? requires loading of FACT record types (faction) and allowing one to be selected using it's formid from the location record
//     e.g. create eType for FACT, create maps, create FACTRec, create Ov records, create do_ operations to load, create dump functions.
// Fix up names in Houndini data for star name - not clear it's needed
// Save planet data (fixes ups etc)