// Windows app part
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
#include <limits> 
#include <thread>
#include <mutex>
#include <future>
#include <algorithm>
#include <unordered_set>
#include <cmath>
#include <regex>

#include "espmanger.h"


#define MAXZOOM 6
#define MAXTEXTZOOM 2
#define SCALECORD 1000 // used to scale up star cord system to get better resolution when panning and zooming
#define STARICONSIZE 2.0f
#define ZOOMSCALING (0.5f) // adjust how much affect zooming has on the size of things

// Needed to pass data to the star map
CEsp::StarPlotData gdlgData;
size_t iZoomlevel = 0;
POINT ptStart(0, 0);
POINT ptLast(0, 0);
bool bInPan = false;
std::vector<CEsp::StarPlotData>srcPlots;
std::vector<CEsp::StarPlotData>dstPlots;
CEsp::fPos stmap_min(0,0,0);
CEsp::fPos stmap_max(0,0,0);
CEsp::POSSWAP stmap_eSwap = CEsp::PSWAP_XY;
CEsp::fPos  stdmap_zoominc(0,0,0);

// for planet map
CEsp::SystemPlotData pmap_oplotdata;
double pmap_min = std::numeric_limits<double>::max();
double pmap_max = std::numeric_limits<double>::min();

// externs from espmain
std::wstring strToWstr(const std::string& str);
extern CEsp* pEspSrc;
extern CEsp* pEspDst;
extern HWND hMainWnd;


int DrawSmallText(HDC hdc, size_t iZoomllvl, int x, int top, int bottom, const std::string str, bool bAddCircle = false, bool bRecText = false)
{
    size_t fontSize = 15;

    // limit how big the text gets since the point of zooming is to decluter the text
    if (iZoomllvl > MAXTEXTZOOM)
        iZoomllvl = MAXTEXTZOOM;

    float zoomlvl = static_cast<float>(iZoomllvl);
    float zoomadj = static_cast<float>(fontSize) * (zoomlvl * ZOOMSCALING)/3.0f;

    if (iZoomllvl)
        fontSize += static_cast<int>(zoomadj);

    HFONT hFont = CreateFont(static_cast<int>(fontSize), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, 
        ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, TEXT("Arial"));

    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
    SetTextColor(hdc, RGB(255, 255, 255)); // Black text
    SetBkMode(hdc, TRANSPARENT);

    // Convert std::string to std::wstring
    std::wstring wstr;
    if (bAddCircle)
    {
        if (bRecText)
            wstr = strToWstr(str) + L" \u25EF";
        else
            wstr = std::wstring(1,L'\u25EF') + L" " + strToWstr(str);
    }
    else
        wstr = strToWstr(str);

    SIZE textSize;
    GetTextExtentPoint32(hdc, wstr.c_str(), static_cast<int>(wstr.length()), &textSize);
    int rectCenterY = (top + bottom) / 2;
    int textStartY = rectCenterY - (textSize.cy / 2);
    RECT textRect;
    textRect.top = textStartY;
    textRect.bottom = textStartY + textSize.cy;
    textRect.left = bRecText ? x - textSize.cx : x;
    textRect.right = x + textSize.cx;
    int iFontHeight = textRect.bottom - textRect.top;

    // Draw the text
    DrawText(hdc, wstr.c_str(), -1, &textRect, DT_LEFT | DT_TOP | DT_NOCLIP);

    SelectObject(hdc, hOldFont);
    DeleteObject(hFont);
    return iFontHeight;
}

POINT DenormalizePos(const POINT screenPos, const size_t izoomlvl, const CEsp::fPos &zoominc, const RECT& rect, float minX, float minY, float maxX, float maxY)
{
    float zoomlvl = static_cast<float>(izoomlvl);
    float dx = (zoominc.m_xPos * zoomlvl);
    float dy = (zoominc.m_yPos * zoomlvl);
    
    minX += dx;
    minY += dy;
    maxX -= dx;
    maxY -= dy;

    double dw = static_cast<double>(rect.right - rect.left);
    double dh = static_cast<double>(rect.bottom - rect.top);

    double normalizedX = (screenPos.x - rect.left) / dw;
    double normalizedY = (screenPos.y - rect.top) / dh;
    double xRange = maxX - minX;
    double yRange = maxY - minY;

    double originalX = normalizedX * xRange + minX;
    double originalY = normalizedY * yRange + minY;

    return POINT(static_cast<int>(originalX), static_cast<int>(originalY));
}

CEsp::fPos NormalizePos(const CEsp::fPos& pos, const RECT& rect, float minX, float minY, float maxX, float maxY) 
{
    // Prevent division by zero
    double epsilon = 1e-12f;
    double xRange = maxX - minX;
    double yRange = maxY - minY;
    
    if (xRange >= 0 && xRange < epsilon) xRange = epsilon;
    if (xRange <= 0 && xRange > -epsilon) xRange = -epsilon;
    if (yRange >= 0 && yRange < epsilon) yRange = epsilon;
    if (yRange <= 0 && yRange > -epsilon) yRange = -epsilon;

    double fx = pos.m_xPos - minX;
    double fy = pos.m_yPos - minY;

    // Normalize the input coordinates to [0, 1] range
    double normalizedX = fx / xRange;
    double normalizedY = fy / yRange;

    // Map to the rectangle's coordinate system
    double dw = (rect.right - rect.left);
    double dh = (rect.bottom - rect.top);
    double xn = normalizedX * dw;
    double yn = normalizedY * dh;

    float rx = static_cast<float>(xn) + static_cast<float>(rect.left);
    float ry = static_cast<float>(yn) + static_cast<float>(rect.top);

    return CEsp::fPos(rx, ry, pos.m_zPos);
}

// Starmap
void _drawStar(HDC hdc, size_t iZoomllvl, CEsp::fPos zoominc, int iOffX, int iOffY, const CEsp::StarPlotData &plot, RECT rect, CEsp::fPos min, CEsp::fPos max, bool bShowNames = true)
{
    // Base size of the star marker
    float zoomlvl = static_cast<float>(iZoomllvl);
    float dx = (zoominc.m_xPos * zoomlvl);
    float dy = (zoominc.m_yPos * zoomlvl);

    min.m_xPos += dx;
    min.m_yPos += dy;
    max.m_xPos -= dx;
    max.m_yPos -= dy;

    CEsp::fPos normPos = NormalizePos(plot.m_oPos, rect,  min.m_xPos, min.m_yPos,  max.m_xPos, max.m_yPos);

    const float minsize = STARICONSIZE;
    float rectsize = minsize + minsize * (zoomlvl * ZOOMSCALING);
    float fx1 = iOffX + normPos.m_xPos - std::max(minsize, rectsize/2);
    float fy1 = iOffY + normPos.m_yPos - std::max(minsize, rectsize/2);
    float fx2 = iOffX + normPos.m_xPos + std::max(minsize, rectsize/2);
    float fy2 = iOffY + normPos.m_yPos + std::max(minsize, rectsize/2);

    int rmr = static_cast<int>(rectsize);
    int rx1 = static_cast<int>(fx1);
    int ry1 = static_cast<int>(fy1);
    int rx2 = static_cast<int>(fx2);
    int ry2 = static_cast<int>(fy2);

    // keep a min size if zoomed too far out
    if ((rx2 - rx1) < static_cast<int>(minsize))
    {
        rx1 = iOffX + static_cast<int>(normPos.m_xPos) - static_cast<int>(minsize) / 2;
        ry1 = iOffY + static_cast<int>(normPos.m_yPos) - static_cast<int>(minsize) / 2;
        rx2 = iOffX + static_cast<int>(normPos.m_xPos) + static_cast<int>(minsize) / 2;
        ry2 = iOffY + static_cast<int>(normPos.m_yPos) + static_cast<int>(minsize) / 2;
    }

    Rectangle(hdc, rx1, ry1, rx2, ry2);

    int px = rx2 + rmr; // draw text scaled margin from the rect
    if (bShowNames && !plot.m_strStarName.empty())
        DrawSmallText(hdc, iZoomllvl, px, ry1, ry2, plot.m_strStarName);
}

void _setMinMaxPosStars(CEsp::POSSWAP eSwap, std::vector<CEsp::StarPlotData>&srcPlots, std::vector<CEsp::StarPlotData>&dstPlots, CEsp::fPos &min, CEsp::fPos &max, CEsp::fPos &zoominc)
{
    srcPlots.clear();
    dstPlots.clear();

    min.m_xPos = min.m_yPos = min.m_zPos = std::numeric_limits<float>::max();
    max.m_xPos = max.m_yPos = max.m_zPos = std::numeric_limits<float>::min();

    // Must always do this one after other so min/max get set correct
    std::vector<CEsp::StarPlotData>mysrcplots;
    std::vector<CEsp::StarPlotData>mydstplots;
    if (pEspSrc) pEspSrc->getStarPositons(mysrcplots, min, max, eSwap);
    if (pEspDst) pEspDst->getStarPositons(mydstplots, min, min, eSwap);

    // make sure min and max is not too small
    const float minmapsize = 2.0f;
    if (pEspSrc->calcDist(min, max) < minmapsize)
    {
        float inf = minmapsize / 2;
        min.m_xPos -= inf;
        min.m_yPos -= inf;
        min.m_zPos -= inf;
        max.m_xPos += inf;
        max.m_yPos += inf;
        max.m_zPos += inf;
    }

    // Scale to get better resolution
    min.m_xPos *= SCALECORD;
    min.m_yPos *= SCALECORD;
    min.m_zPos *= SCALECORD;
    max.m_xPos *= SCALECORD;
    max.m_yPos *= SCALECORD;
    max.m_zPos *= SCALECORD;

    for (const CEsp::StarPlotData& myplot : mysrcplots)
    {
        CEsp::StarPlotData plot = myplot;
        plot.m_oPos.m_xPos *= SCALECORD;
        plot.m_oPos.m_yPos *= SCALECORD;
        plot.m_oPos.m_zPos *= SCALECORD;
        srcPlots.push_back(plot);
    }

    for (const CEsp::StarPlotData& myplot : mydstplots)
    {
        CEsp::StarPlotData plot = myplot;
        plot.m_oPos.m_xPos *= SCALECORD;
        plot.m_oPos.m_yPos *= SCALECORD;
        plot.m_oPos.m_zPos *= SCALECORD;
        dstPlots.push_back(plot);
    }

    zoominc.m_xPos = ((max.m_xPos - min.m_xPos)/2 - (max.m_xPos - min.m_xPos) / 20) / MAXZOOM;
    zoominc.m_yPos = ((max.m_yPos - min.m_yPos)/2 - (max.m_yPos - min.m_yPos) / 20) / MAXZOOM;
    zoominc.m_zPos = ((max.m_zPos - min.m_zPos)/2 - (max.m_zPos - min.m_zPos) / 20) / MAXZOOM;
}

void _createBrushandPen(HDC hdc, COLORREF rgb, HBRUSH &hBr, HPEN &hPen)
{
    hPen = CreatePen(PS_SOLID, 2, rgb);
    hBr = CreateSolidBrush(rgb);
    SelectObject(hdc, hBr);
    SelectObject(hdc, hPen);
}

void _deleteBrushandPen(HBRUSH &hBr, HPEN &hPen)
{
    DeleteObject(hPen);
    DeleteObject(hBr);
    hPen = NULL;
    hBr = NULL;
}

void _drawblkbkg(HDC hdc, POINT pt1, POINT pt2)
{
    HBRUSH hBrush = CreateSolidBrush(RGB(0, 0, 0));
    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
    Rectangle(hdc, pt1.x, pt1.y, pt2.x, pt2.y);
    SelectObject(hdc, hOldBrush);
    DeleteObject(hBrush);
}

void _invalidDlgitem(HWND hDlg, int iItem)
{
    RECT rcItem;
    HWND hItem = GetDlgItem(hDlg, iItem);
    GetWindowRect(hItem, &rcItem);
    MapWindowPoints(HWND_DESKTOP, hDlg, (LPPOINT)&rcItem, 2);
    InvalidateRect(hDlg, &rcItem, FALSE);
    UpdateWindow(hDlg);
}

bool _IsInDlgItem(HWND hDlg, int iDlgItem, POINT& pt)
{
    GetCursorPos(&pt);
    ScreenToClient(hDlg, &pt);
    HWND hControl = GetDlgItem(hDlg, iDlgItem);
    RECT rcControl;
    GetWindowRect(hControl, &rcControl);
    MapWindowPoints(NULL, hDlg, (LPPOINT)&rcControl, 2);
    return PtInRect(&rcControl, pt);
}

void _getMapCalcs(HWND hDlg, int iDlgItem, RECT & rect, CEsp::fPos & min, CEsp::fPos & max)
{
    RECT rectClientRect; 
    HWND hItem = GetDlgItem(hDlg, iDlgItem);
    GetClientRect(hItem, &rectClientRect);  // Get the dimensions of the static control
    rect = rectClientRect;
    InflateRect(&rect, -10, -10); // add hard margin
    rect.right -= 50; // allow addition margin for text flowing to the right
    rect.bottom -= 10; // allow some space for text which is offset under the star

    // Add a 10% margin based on overal map size
    float fmarX = (max.m_xPos - min.m_xPos) / 20;
    float fmarY = (max.m_yPos - min.m_yPos) / 10;
    float fmarZ = (max.m_zPos - min.m_zPos) / 10;
    min.m_xPos -= fmarX; max.m_xPos += fmarX;
    min.m_yPos -= fmarY; max.m_yPos += fmarY;
    min.m_zPos -= fmarZ; max.m_zPos += fmarZ;
}

void _adjustForPan(HWND hDlg, const size_t izoomlvl, const CEsp::fPos &zoominc,  int iDlgItem, const bool bPanning, const RECT rect, const POINT ptSrt, POINT &ptLst, CEsp::fPos & min, CEsp::fPos & max)
{
    POINT pt;
    if (bInPan && _IsInDlgItem(hDlg, iDlgItem, pt))
    {
        POINT ptInMap = DenormalizePos(pt, izoomlvl, zoominc, rect, min.m_xPos, min.m_yPos, max.m_xPos, max.m_yPos);

        min.m_xPos -= (ptInMap.x - ptSrt.x); 
        min.m_yPos -= (ptInMap.y - ptSrt.y); 
        max.m_xPos -= (ptInMap.x - ptSrt.x); 
        max.m_yPos -= (ptInMap.y - ptSrt.y); 
        ptLast = ptInMap;
    }
    else
    {
        min.m_xPos -= ptLst.x - ptSrt.x; 
        min.m_yPos -= ptLst.y - ptSrt.y;
        max.m_xPos -= ptLst.x - ptSrt.x; 
        max.m_yPos -= ptLst.y - ptSrt.y;
    }
}

// Star map dialog
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
            SendMessage(hSlider, TBM_SETRANGE, TRUE, MAKELPARAM(1, SLIDER_RNG_MAX-1));
            SendMessage(hSlider, TBM_SETTICFREQ, SLIDER_RNG_MAX, 0);
            SendMessage(hSlider, TBM_SETPOS, TRUE, SLIDER_RNG_MAX);
            iZoomlevel = 0;
            bInPan = false;
            ptStart = POINT(0, 0);
            ptLast = POINT(0, 0);
            stmap_eSwap = (CEsp::POSSWAP)SendMessage(hCombo, CB_GETITEMDATA, SendMessage(hCombo, CB_GETCURSEL, 0, 0), 0);
            _setMinMaxPosStars(stmap_eSwap, srcPlots, dstPlots, stmap_min, stmap_max, stdmap_zoominc);
        }
        return TRUE;
        break;

    case WM_MOUSEWHEEL:
        {
            int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (iZoomlevel!= 0  && zDelta < 0 )
                iZoomlevel--;
            else 
            if (iZoomlevel!=MAXZOOM && zDelta > 0)
                iZoomlevel++;
            _invalidDlgitem(hDlg, IDC_STATIC_P2);
            return TRUE;
        }

    case WM_MOUSEMOVE:
        if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0)
        {
            POINT pt;
            if (!_IsInDlgItem(hDlg, IDC_STATIC_P2, pt))
                bInPan = false;
            else
            if (bInPan)
                _invalidDlgitem(hDlg, IDC_STATIC_P2);
        }
        break;

    case WM_LBUTTONDOWN:
        {
            POINT pt;
            if (_IsInDlgItem(hDlg, IDC_STATIC_P2, pt))
            {
                RECT rect;
                CEsp::fPos min = stmap_min;
                CEsp::fPos max = stmap_max;
                _getMapCalcs(hDlg, IDC_STATIC_P2, rect, min, max);
                // work out our offset from before and add it in to the new postion
                POINT delta;
                delta.x = ptLast.x - ptStart.x;
                delta.y = ptLast.y - ptStart.y;
                ptStart = DenormalizePos(pt, iZoomlevel, stdmap_zoominc, rect, min.m_xPos, min.m_yPos, max.m_xPos, max.m_yPos);
                ptStart.x -= delta.x;
                ptStart.y -= delta.y;
                bInPan = true;
            }
            break;
        }
    case WM_LBUTTONUP:
        bInPan = false;
        break;

    case WM_HSCROLL:
        if ((HWND)lParam == GetDlgItem(hDlg, IDC_SLIDERDT))
            _invalidDlgitem(hDlg, IDC_STATIC_P2);
        break;

    case WM_COMMAND:
        if ((HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == IDC_COMBOVIEW))
        {
            iZoomlevel = 0;
            bInPan = false;
            ptStart = POINT(0, 0);
            ptLast = POINT(0, 0);
            stmap_eSwap = (CEsp::POSSWAP)SendMessage(GetDlgItem(hDlg, IDC_COMBOVIEW), CB_GETITEMDATA, SendMessage(GetDlgItem(hDlg, IDC_COMBOVIEW), CB_GETCURSEL, 0, 0), 0);
            _setMinMaxPosStars(stmap_eSwap, srcPlots, dstPlots, stmap_min, stmap_max, stdmap_zoominc);
            _invalidDlgitem(hDlg, IDC_STATIC_P2);
        }
        else
        if ((LOWORD(wParam) == IDC_HIDEDST || LOWORD(wParam) == IDC_HIDESRC || LOWORD(wParam) == IDC_HIDENAMES) && HIWORD(wParam) == BN_CLICKED)
        {
            _invalidDlgitem(hDlg, IDC_STATIC_P2);
        }
        else
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) 
        {
            EndDialog(hDlg, LOWORD(wParam));
            return TRUE;
        }
        break;

    case WM_PAINT: 
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hDlg, &ps);
            int sp = SLIDER_RNG_MAX - (int)SendMessage(GetDlgItem(hDlg, IDC_SLIDERDT), TBM_GETPOS, 0, 0);

            // Adjust the star map min.max based on panning
            RECT rect;
            CEsp::fPos min = stmap_min;
            CEsp::fPos max = stmap_max;
            _getMapCalcs(hDlg, IDC_STATIC_P2, rect, min, max);
            _adjustForPan(hDlg, iZoomlevel, stdmap_zoominc, IDC_STATIC_P2, bInPan, rect, ptStart, ptLast, min, max);

            // set up points for clipping 
            HWND hItem = GetDlgItem(hDlg, IDC_STATIC_P2);
            RECT rectW;
            GetWindowRect(hItem, &rectW);
            POINT pt1 = { rectW.left, rectW.top };
            POINT pt2 = { rectW.right, rectW.bottom };
            ScreenToClient(hDlg, &pt1);
            ScreenToClient(hDlg, &pt2);
            int iOffX = pt1.x;
            int iOffY = pt1.y;
            HRGN hRgn = CreateRectRgn(pt1.x, pt1.y, pt2.x, pt2.y);
            SelectClipRgn(hdc, hRgn);

            {
                HPEN hPen = 0;
                HBRUSH hBr = 0;
                bool bHideDst = IsDlgButtonChecked(hDlg, IDC_HIDEDST) == BST_CHECKED;
                bool bHideSrc = IsDlgButtonChecked(hDlg, IDC_HIDESRC) == BST_CHECKED;
                
                // Create a memory DC compatible with the window's DC to avoid flicker
                RECT rectClientRect; 
                GetClientRect(hItem, &rectClientRect);
                HDC hdcMem = CreateCompatibleDC(hdc);
                HBITMAP hbmMem = CreateCompatibleBitmap(hdc, rectClientRect.right - rectClientRect.left, rectClientRect.bottom - rectClientRect.top);
                HGDIOBJ hOld = SelectObject(hdcMem, hbmMem);
                _drawblkbkg(hdcMem, pt1, pt2);

                if (!bHideSrc)
                {
                    int i = 0;
                    _createBrushandPen(hdcMem, RGB(255, 255, 255), hBr, hPen);
                    for (const CEsp::StarPlotData& plot : srcPlots)
                        _drawStar(hdcMem, iZoomlevel, stdmap_zoominc, iOffX, iOffY, plot, rect, min, max, sp == 0 ? 1 : sp == SLIDER_RNG_MAX - 1 ? 0 : (i++ % sp) <= 0);
                    _deleteBrushandPen(hBr, hPen);
                }

                if (!bHideDst)
                {
                    _createBrushandPen(hdcMem, RGB(0, 255, 0), hBr, hPen);
                    for (const CEsp::StarPlotData& plot : dstPlots)
                        _drawStar(hdcMem, iZoomlevel, stdmap_zoominc, iOffX, iOffY, plot, rect, min, max, sp != SLIDER_RNG_MAX - 1);
                    _deleteBrushandPen(hBr, hPen);
                }

                // Draw the new star 
                if (GetParent(hDlg) != hMainWnd) // if not opened from main window
                {
                    CEsp::POSSWAP eSwap = (CEsp::POSSWAP)SendMessage(GetDlgItem(hDlg, IDC_COMBOVIEW), CB_GETITEMDATA, SendMessage(GetDlgItem(hDlg, IDC_COMBOVIEW), CB_GETCURSEL, 0, 0), 0);
                    CEsp::StarPlotData plot(pEspSrc->posSwap(gdlgData.m_oPos, eSwap), gdlgData.m_strStarName.empty() ? "(new unnamed)" : gdlgData.m_strStarName);
                    plot.m_oPos.m_xPos *= SCALECORD;
                    plot.m_oPos.m_zPos *= SCALECORD;
                    plot.m_oPos.m_yPos *= SCALECORD;
                    _createBrushandPen(hdcMem, RGB(255, 0, 0), hBr, hPen);
                    _drawStar(hdcMem, iZoomlevel, stdmap_zoominc, iOffX, iOffY, plot, rect, min, max);
                    _deleteBrushandPen(hBr, hPen);
                }
                // Copy the off-screen buffer to the window's DC
                BitBlt(hdc, 0, 0, rectClientRect.right, rectClientRect.bottom, hdcMem, 0, 0, SRCCOPY);

                // Clean up
                SelectObject(hdcMem, hOld);
                DeleteObject(hbmMem);
                DeleteDC(hdcMem);
            }

            SelectClipRgn(hdc, NULL);
            DeleteObject(hRgn);
            EndPaint(hDlg, &ps);
            return TRUE;
        }
    }

    return (INT_PTR)FALSE;
}


// Planet map dialog
INT_PTR CALLBACK DialogProcPlanetMap(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_INITDIALOG:
        {
            HWND hCombo = GetDlgItem(hDlg, IDC_COMBO2);
            SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)"Source");
            SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)"Destination");
            SendMessage(hCombo, CB_SETCURSEL, 0, 0);
            PostMessageA(hDlg, WM_COMMAND, MAKEWPARAM(IDC_COMBO2, CBN_SELCHANGE), (LPARAM)hCombo);

            HWND hSlider = GetDlgItem(hDlg, IDC_SLIDERDT);
            SendMessage(hSlider, TBM_SETRANGE, TRUE, MAKELPARAM(1, SLIDER_RNG_MAX-1));
            SendMessage(hSlider, TBM_SETTICFREQ, SLIDER_RNG_MAX, 0);
            SendMessage(hSlider, TBM_SETPOS, TRUE, SLIDER_RNG_MAX);

            // Hide a control in the dialog - not used at moment
            ShowWindow(hSlider, SW_HIDE);
            ShowWindow(GetDlgItem(hDlg, IDC_STATICDETAILLVL), SW_HIDE);

            iZoomlevel = 0;
            bInPan = false;
            ptStart = POINT(0, 0);
            ptLast = POINT(0, 0);
        }
        return TRUE;
        break;

    case WM_MOUSEWHEEL:
        {
            int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (iZoomlevel!= 0  && zDelta < 0 )
                iZoomlevel--;
            else 
            if (iZoomlevel!=MAXZOOM && zDelta > 0)
                iZoomlevel++;
            _invalidDlgitem(hDlg, IDC_STATIC_P2);
            return TRUE;
        }

    case WM_MOUSEMOVE:
        if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0)
        {
            POINT pt;
            if (!_IsInDlgItem(hDlg, IDC_STATIC_P2, pt))
                bInPan = false;
            else
            if (bInPan)
                _invalidDlgitem(hDlg, IDC_STATIC_P2);
        }
        break;

    case WM_LBUTTONDOWN:
        {
            POINT pt;
            if (_IsInDlgItem(hDlg, IDC_STATIC_P2, pt))
            {
                //RECT rect;
                //CEsp::fPos min = stmap_min;
                //CEsp::fPos max = stmap_max;
               // _getMapCalcs(hDlg, IDC_STATIC_P2, rect, min, max);
                // work out our offset from before and add it in to the new postion
                POINT delta;
                delta.x = ptLast.x - ptStart.x;
                delta.y = ptLast.y - ptStart.y;
               // ptStart = DenormalizePos(pt, iZoomlevel, stdmap_zoominc, rect, min.m_xPos, min.m_yPos, max.m_xPos, max.m_yPos);
                ptStart.x -= delta.x;
                ptStart.y -= delta.y;
                bInPan = true;
            }
            break;
        }
    case WM_LBUTTONUP:
        bInPan = false;
        break;

    case WM_HSCROLL:
        if ((HWND)lParam == GetDlgItem(hDlg, IDC_SLIDERDT))
            _invalidDlgitem(hDlg, IDC_STATIC_P2);
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) 
        {
            EndDialog(hDlg, LOWORD(wParam));
            return TRUE;
        }
        else
        if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == IDC_COMBO1)
        {
            HWND hCombo2 = GetDlgItem(hDlg, IDC_COMBO2);
            LRESULT selectedIndex = SendMessage(hCombo2, CB_GETCURSEL, 0, 0);

            if ((pEspSrc && selectedIndex == 0) || (pEspDst && selectedIndex == 1))
            {
                HWND hCombo1 = GetDlgItem(hDlg, IDC_COMBO1);
                LRESULT selectedStarIdx = SendMessage(hCombo1, CB_GETCURSEL, 0, 0);
                if (selectedStarIdx != CB_ERR)
                {
                    size_t iIdx = (size_t) SendMessage(hCombo1, CB_GETITEMDATA, (WPARAM)selectedStarIdx, 0);
                    // Populate Combo boxes
                    std::vector<CEsp::BasicInfoRec> oBasicInfoRecs;
                    if (selectedIndex == 0)
                    {
                        CEsp::BasicInfoRec oBasicInfoStar;
                        pEspSrc->getBasicInfo(CEsp::eESP_STDT, iIdx, oBasicInfoStar);
                        pEspSrc->getPlanetPerihelion(oBasicInfoStar.m_iIdx, pmap_oplotdata, pmap_min, pmap_max);
                    }
                    else
                    {
                        CEsp::BasicInfoRec oBasicInfoStar;
                        pEspDst->getBasicInfo(CEsp::eESP_STDT, iIdx, oBasicInfoStar);
                        pEspDst->getPlanetPerihelion(oBasicInfoStar.m_iIdx, pmap_oplotdata, pmap_min, pmap_max);
                    }
                }
                _invalidDlgitem(hDlg, IDC_STATIC_P2);
            }
        }
        else
        if (HIWORD(wParam) == CBN_SELCHANGE && LOWORD(wParam) == IDC_COMBO2)
        {
            HWND hCombo2 = GetDlgItem(hDlg, IDC_COMBO2);
            LRESULT selectedIndex = SendMessage(hCombo2, CB_GETCURSEL, 0, 0);

            if ((pEspSrc && selectedIndex==0) || (pEspDst && selectedIndex==1))
            {
                // Populate Combo boxes
                std::vector<CEsp::BasicInfoRec> oBasicInfoRecs;
                if (selectedIndex == 0)
                    pEspSrc->getBasicInfoRecs(CEsp::eESP_STDT, oBasicInfoRecs);
                else
                    pEspDst->getBasicInfoRecs(CEsp::eESP_STDT, oBasicInfoRecs);

                HWND hCombo1 = GetDlgItem(hDlg, IDC_COMBO1);
                SendMessage(hCombo1, CB_RESETCONTENT, 0, 0);
                for (const CEsp::BasicInfoRec& oBasicInfo : oBasicInfoRecs)
                    if (*oBasicInfo.m_pName) // leave out blank records (bad records)
                    {
                        LRESULT index = SendMessageA(hCombo1, CB_ADDSTRING, 0, (LPARAM)oBasicInfo.m_pAName);
                        if (index != CB_ERR && index != CB_ERRSPACE)
                            SendMessage(hCombo1, CB_SETITEMDATA, (WPARAM)index, (LPARAM)oBasicInfo.m_iIdx);
                    }
                SendMessage(hCombo1, CB_SETCURSEL, 0, 0);
                 PostMessageA(hDlg, WM_COMMAND, MAKEWPARAM(IDC_COMBO1, CBN_SELCHANGE), (LPARAM)hCombo1);
                 SetFocus(hCombo1);
                _invalidDlgitem(hDlg, IDC_STATIC_P2);
            }
        }
        break;

    case WM_PAINT: 
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hDlg, &ps);
            int sp = SLIDER_RNG_MAX - (int)SendMessage(GetDlgItem(hDlg, IDC_SLIDERDT), TBM_GETPOS, 0, 0);

            // set up points for clipping 
            HWND hItem = GetDlgItem(hDlg, IDC_STATIC_P2);
            RECT rectW;
            GetWindowRect(hItem, &rectW);
            POINT pt1 = { rectW.left, rectW.top };
            POINT pt2 = { rectW.right, rectW.bottom };
            ScreenToClient(hDlg, &pt1);
            ScreenToClient(hDlg, &pt2);
            HRGN hRgn = CreateRectRgn(pt1.x, pt1.y, pt2.x, pt2.y);
            SelectClipRgn(hdc, hRgn);

            {
                HPEN hPen = 0;
                HBRUSH hBr = 0;
                bool bHideDst = IsDlgButtonChecked(hDlg, IDC_HIDEDST) == BST_CHECKED;
                bool bHideSrc = IsDlgButtonChecked(hDlg, IDC_HIDESRC) == BST_CHECKED;
                
                // Create a memory DC compatible with the window's DC to avoid flicker
                RECT rectClientRect; 
                GetClientRect(hItem, &rectClientRect);
                HDC hdcMem = CreateCompatibleDC(hdc);
                HBITMAP hbmMem = CreateCompatibleBitmap(hdc, rectClientRect.right - rectClientRect.left, rectClientRect.bottom - rectClientRect.top);
                HGDIOBJ hOld = SelectObject(hdcMem, hbmMem);
                _drawblkbkg(hdcMem, pt1, pt2);

                 size_t numplans = pmap_oplotdata.m_oPlanetPlots.size();
                if (numplans)
                {
                    int top = 100;
                    int iInc = static_cast<int>(((rectW.bottom - rectW.top) - top) / numplans);
                    int istarsize = 30;
                    double r = pmap_oplotdata.m_max > 100000000 ? 100000 : 1;
                    double adjust = static_cast<double>((rectW.right - rectW.left) - istarsize*4) / (numplans==1 ? pmap_oplotdata.m_max*3/r : pmap_oplotdata.m_max/r);
                    Ellipse(hdcMem, pt1.x-istarsize,  pt1.y-istarsize, pt1.x+istarsize, pt1.y+istarsize);

                    for (size_t i=0; i<numplans; i++)
                    {
                        bool bDoRev = numplans > 1 && i == (numplans - 1);
                        double fx = (pmap_oplotdata.m_oPlanetPlots[i].m_fPerihelion/r) * adjust;
                        int rx = istarsize + pt1.x + static_cast<int>(fx);
                        int ifh = DrawSmallText(hdcMem, 1, rx + istarsize/2, top, top + 30, pmap_oplotdata.m_oPlanetPlots[i].m_strName, false, bDoRev);

                        double stz = pmap_oplotdata.m_oPlanetPlots[i].m_RadiusKm/1000;
                        int stx = static_cast<int>(stz);
                        HBRUSH hOldBrush = (HBRUSH)SelectObject(hdcMem, GetStockObject(NULL_BRUSH));
                        HPEN hWhitePen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
                        HPEN hOldPen = (HPEN)SelectObject(hdcMem, hWhitePen);
                        Ellipse(hdcMem, rx-stx, top+ifh-stx, rx+stx, top+ifh+stx);
                        SelectObject(hdcMem, hOldBrush);
                        SelectObject(hdcMem, hOldPen);

                        top += iInc;
                    }
                }

                // Copy the off-screen buffer to the window's DC
                BitBlt(hdc, 0, 0, rectClientRect.right, rectClientRect.bottom, hdcMem, 0, 0, SRCCOPY);

                // Clean up
                SelectObject(hdcMem, hOld);
                DeleteObject(hbmMem);
                DeleteDC(hdcMem);
            }

            SelectClipRgn(hdc, NULL);
            DeleteObject(hRgn);
            EndPaint(hDlg, &ps);
            return TRUE;
        }
    }

    return (INT_PTR)FALSE;
}