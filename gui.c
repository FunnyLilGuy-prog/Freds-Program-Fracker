#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <psapi.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "msimg32.lib")
#include <windows.h>
#include <commdlg.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "comdlg32.lib")

// ── IDs ───────────────────────────────────────────────────────────────────────
#define ID_LISTBOX        1
#define ID_BTN_REFRESH    2
#define ID_TITLE          3
#define ID_TIMER          4
#define ID_RAINBOW_TIMER  5
#define ID_BTN_START      6
#define ID_MSG_TIMER      7
#define ID_STRINGS_LIST   8
#define ID_BTN_CLOSE      9
#define ID_KEYS_LIST      10
#define ID_BTN_FONTCOLOR  13
#define ID_BTN_BGCOLOR    14
#define ID_BTN_BGIMAGE    15
#define ID_BLUR_SLIDER    16
#define ID_BTN_RESETBG    17
#define ID_PANEL_MAIN     18
#define ID_PANEL_SETTINGS 19
#define ID_FREDDED_LIST   20
#define ID_FREDDED_TIMER  21
#define ID_BTN_SAVELOG    22

// ── Globals ───────────────────────────────────────────────────────────────────
HWND hList, hTitle, hStartBtn, hMsgWnd;
HWND hPanelMain, hPanelSettings;
HWND hTabCtrl;
int  colorIndex   = 0;
int  rainbowIndex = 0;
int  freddedRgb   = 0; // cycling index for fredded window

COLORREF gFontColor = RGB(255,255,255);
COLORREF gBgColor   = RGB(20,20,30);
COLORREF gCustomColors[16] = {0};
char     gBgImagePath[MAX_PATH] = {0};
int      gBlurAmount = 0;
HBITMAP  hBgBitmap = NULL, hBlurBitmap = NULL;

HFONT hTitleFont, hListFont, hBtnFont, hTabFont, hSettingsFont, hFreddedFont;

COLORREF flashColors[] = {
    RGB(255,0,0),RGB(255,165,0),RGB(255,255,0),
    RGB(0,200,0),RGB(0,100,255),RGB(148,0,211),RGB(255,0,200)
};

const char *KEYWORDS[] = {
    "key","password","passwd","secret","token",
    "auth","login","credential","api","private"
};
#define KEYWORD_COUNT 10

// ── Fredded hit storage ───────────────────────────────────────────────────────
#define MAX_HITS 2000
typedef struct {
    char keyword[64];
    char context[256];
    char source[16];   // "STRING" or "BINARY"
    DWORD offset;      // binary offset (0 for strings)
} HitEntry;

HitEntry  gHits[MAX_HITS];
int       gHitCount = 0;

// ── Blur helper ───────────────────────────────────────────────────────────────
HBITMAP BlurBitmap(HBITMAP hSrc, int w, int h, int r) {
    if(r<=0||!hSrc) return hSrc;
    HDC s=CreateCompatibleDC(NULL), d=CreateCompatibleDC(NULL);
    HBITMAP hDst=CreateCompatibleBitmap(GetDC(NULL),w,h);
    SelectObject(s,hSrc); SelectObject(d,hDst);
    for(int y=0;y<h;y++) for(int x=0;x<w;x++){
        int rv=0,gv=0,bv=0,cnt=0;
        for(int dy=-r;dy<=r;dy++) for(int dx=-r;dx<=r;dx++){
            int nx=x+dx,ny=y+dy;
            if(nx>=0&&nx<w&&ny>=0&&ny<h){
                COLORREF c=GetPixel(s,nx,ny);
                rv+=GetRValue(c);gv+=GetGValue(c);bv+=GetBValue(c);cnt++;
            }
        }
        SetPixel(d,x,y,RGB(rv/cnt,gv/cnt,bv/cnt));
    }
    DeleteDC(s); DeleteDC(d);
    return hDst;
}

void LoadBackgroundImage(HWND hwnd) {
    if(hBgBitmap){DeleteObject(hBgBitmap);hBgBitmap=NULL;}
    if(hBlurBitmap){DeleteObject(hBlurBitmap);hBlurBitmap=NULL;}
    if(!gBgImagePath[0]) return;
    hBgBitmap=(HBITMAP)LoadImage(NULL,gBgImagePath,IMAGE_BITMAP,0,0,LR_LOADFROMFILE);
    if(!hBgBitmap) return;
    RECT rc; GetClientRect(hwnd,&rc);
    int wi=rc.right,hi=rc.bottom;
    HDC hw=GetDC(hwnd),hs=CreateCompatibleDC(hw),hd=CreateCompatibleDC(hw);
    HBITMAP hSc=CreateCompatibleBitmap(hw,wi,hi);
    SelectObject(hs,hBgBitmap); SelectObject(hd,hSc);
    BITMAP bm; GetObject(hBgBitmap,sizeof(bm),&bm);
    SetStretchBltMode(hd,HALFTONE);
    StretchBlt(hd,0,0,wi,hi,hs,0,0,bm.bmWidth,bm.bmHeight,SRCCOPY);
    DeleteObject(hBgBitmap); hBgBitmap=hSc;
    if(gBlurAmount>0) hBlurBitmap=BlurBitmap(hBgBitmap,wi,hi,gBlurAmount);
    DeleteDC(hs); DeleteDC(hd); ReleaseDC(hwnd,hw);
}

// ── Keyword helpers ───────────────────────────────────────────────────────────
BOOL ContainsKeyword(const char *str, char *matchedKw) {
    char low[512]; int i=0;
    while(str[i]&&i<511){low[i]=(char)tolower((unsigned char)str[i]);i++;}
    low[i]='\0';
    for(int k=0;k<KEYWORD_COUNT;k++){
        if(strstr(low,KEYWORDS[k])){
            if(matchedKw) lstrcpyn(matchedKw,(char*)KEYWORDS[k],64);
            return TRUE;
        }
    }
    return FALSE;
}

long BinaryFind(const char *buf,DWORD len,const char *kw,DWORD kwl){
    for(DWORD i=0;i+kwl<=len;i++){
        BOOL m=TRUE;
        for(DWORD j=0;j<kwl;j++)
            if(tolower((unsigned char)buf[i+j])!=tolower((unsigned char)kw[j])){m=FALSE;break;}
        if(m) return (long)i;
    }
    return -1;
}

void ExtractCtx(const char *buf,DWORD blen,DWORD off,char *out,int olen){
    DWORD s=off>40?off-40:0,e=off+80<blen?off+80:blen;
    int o=0;
    for(DWORD i=s;i<e&&o<olen-1;i++){
        unsigned char c=(unsigned char)buf[i];
        out[o++]=(c>=0x20&&c<0x7F)?(char)c:'.';
    }
    out[o]='\0';
}

// ── Program Fredded window ────────────────────────────────────────────────────
HWND hFreddedWnd = NULL;
HWND hFreddedList = NULL;
int  freddedTimerIdx = 0;

// Each row gets painted with a cycling RGB color
LRESULT CALLBACK FreddedListProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam,
                                   UINT_PTR uid,DWORD_PTR ref){
    if(msg==WM_PAINT){
        LRESULT res=DefSubclassProc(hwnd,msg,wParam,lParam);
        HDC hdc=GetDC(hwnd);

        HFONT f=CreateFont(14,0,0,0,FW_BOLD,0,0,0,ANSI_CHARSET,0,0,
            CLEARTYPE_QUALITY,FIXED_PITCH|FF_MODERN,"Courier New");
        SelectObject(hdc,f);

        int count=(int)SendMessage(hwnd,LB_GETCOUNT,0,0);
        int top=(int)SendMessage(hwnd,LB_GETTOPINDEX,0,0);

        for(int i=top;i<count;i++){
            RECT ir;
            if(SendMessage(hwnd,LB_GETITEMRECT,i,(LPARAM)&ir)==LB_ERR) break;

            char text[512]={0};
            SendMessage(hwnd,LB_GETTEXT,i,(LPARAM)text);

            // Skip separator/header lines - paint them differently
            if(text[0]=='-'||text[0]=='='){
                HBRUSH hb=CreateSolidBrush(RGB(15,15,25));
                FillRect(hdc,&ir,hb); DeleteObject(hb);
                SetBkMode(hdc,TRANSPARENT);
                SetTextColor(hdc,RGB(100,100,120));
                RECT tr=ir; tr.left+=6;
                DrawText(hdc,text,-1,&tr,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
                continue;
            }

            // Cycle RGB color per row offset by row index
            float hue = (float)((freddedTimerIdx * 3 + i * 25) % 360);
            // HSV->RGB conversion
            float h=hue/60.0f;
            int   hi2=(int)h;
            float f2=h-(float)hi2;
            float q=1.0f-f2;
            float rv,gv,bv;
            switch(hi2%6){
                case 0: rv=1;    gv=f2;  bv=0;   break;
                case 1: rv=q;    gv=1;   bv=0;   break;
                case 2: rv=0;    gv=1;   bv=f2;  break;
                case 3: rv=0;    gv=q;   bv=1;   break;
                case 4: rv=f2;   gv=0;   bv=1;   break;
                default:rv=1;    gv=0;   bv=q;   break;
            }
            COLORREF rowColor=RGB((int)(rv*255),(int)(gv*255),(int)(bv*255));

            // Dark background per row
            HBRUSH hb=CreateSolidBrush(RGB(8,8,18));
            FillRect(hdc,&ir,hb); DeleteObject(hb);

            // Left colored bar
            RECT bar=ir; bar.right=bar.left+5;
            HBRUSH hbar=CreateSolidBrush(rowColor);
            FillRect(hdc,&bar,hbar); DeleteObject(hbar);

            // Draw text in row color
            SetBkMode(hdc,TRANSPARENT);
            SetTextColor(hdc,rowColor);
            RECT tr=ir; tr.left+=10;
            DrawText(hdc,text,-1,&tr,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        }

        DeleteObject(f);
        ReleaseDC(hwnd,hdc);
        return res;
    }
    return DefSubclassProc(hwnd,msg,wParam,lParam);
}

LRESULT CALLBACK FreddedTitleProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam,
                                    UINT_PTR uid,DWORD_PTR ref){
    if(msg==WM_PAINT){
        PAINTSTRUCT ps; HDC hdc=BeginPaint(hwnd,&ps);
        RECT rc; GetClientRect(hwnd,&rc);

        // Black bg
        FillRect(hdc,&rc,(HBRUSH)GetStockObject(BLACK_BRUSH));

        // Draw each character of "Program Fredded" in a different RGB color
        const char *txt="  !! PROGRAM FREDDED !!  ";
        int len=(int)strlen(txt);
        SetBkMode(hdc,TRANSPARENT);
        SelectObject(hdc,hFreddedFont);

        // Measure char width
        SIZE sz; GetTextExtentPoint32(hdc,"W",1,&sz);
        int cw=sz.cx;
        int totalW=len*cw;
        int startX=(rc.right-totalW)/2;
        int y=(rc.bottom-sz.cy)/2;

        for(int i=0;i<len;i++){
            float hue=(float)((freddedTimerIdx*4+i*24)%360);
            float h=hue/60.0f; int hi2=(int)h; float f2=h-(float)hi2,q=1.0f-f2;
            float rv,gv,bv;
            switch(hi2%6){
                case 0:rv=1;gv=f2;bv=0;break; case 1:rv=q;gv=1;bv=0;break;
                case 2:rv=0;gv=1;bv=f2;break; case 3:rv=0;gv=q;bv=1;break;
                case 4:rv=f2;gv=0;bv=1;break; default:rv=1;gv=0;bv=q;break;
            }
            SetTextColor(hdc,RGB((int)(rv*255),(int)(gv*255),(int)(bv*255)));
            char ch[2]={txt[i],0};
            TextOut(hdc,startX+i*cw,y,ch,1);
        }
        EndPaint(hwnd,&ps); return 0;
    }
    return DefSubclassProc(hwnd,msg,wParam,lParam);
}

void PopulateFreddedList(HWND hLB, const char *progName) {
    SendMessage(hLB,LB_RESETCONTENT,0,0);

    char hdr[300];
    wsprintf(hdr,"=== %d keyword hit(s) found in: %s ===", gHitCount, progName);
    SendMessage(hLB,LB_ADDSTRING,0,(LPARAM)hdr);
    SendMessage(hLB,LB_ADDSTRING,0,(LPARAM)"");

    // Group by keyword
    for(int k=0;k<KEYWORD_COUNT;k++){
        BOOL headerPrinted=FALSE;
        for(int i=0;i<gHitCount;i++){
            if(lstrcmpi(gHits[i].keyword,(char*)KEYWORDS[k])==0){
                if(!headerPrinted){
                    char kh[128];
                    wsprintf(kh,"--- Keyword: \"%s\" ---",KEYWORDS[k]);
                    SendMessage(hLB,LB_ADDSTRING,0,(LPARAM)kh);
                    headerPrinted=TRUE;
                }
                char line[400];
                if(gHits[i].offset>0){
                    wsprintf(line,"  [%s @ 0x%08X]  %s",
                             gHits[i].source,gHits[i].offset,gHits[i].context);
                } else {
                    wsprintf(line,"  [%s]  %s",
                             gHits[i].source,gHits[i].context);
                }
                SendMessage(hLB,LB_ADDSTRING,0,(LPARAM)line);
            }
        }
    }
}

LRESULT CALLBACK FreddedWndProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam){
    static HWND hTitleBar;
    switch(msg){
        case WM_CREATE: {
            CREATESTRUCT *cs=(CREATESTRUCT*)lParam;
            const char *progName=(const char*)cs->lpCreateParams;

            // RGB title bar
            hTitleBar=CreateWindow("STATIC","",
                WS_VISIBLE|WS_CHILD,
                0,0,700,55,
                hwnd,NULL,NULL,NULL);
            SetWindowSubclass(hTitleBar,FreddedTitleProc,0,0);

            // Summary label
            char summary[128];
            wsprintf(summary,"%d keyword hit(s) found",gHitCount);
            CreateWindow("STATIC",summary,
                WS_VISIBLE|WS_CHILD|SS_CENTER,
                0,55,700,22,
                hwnd,NULL,NULL,NULL);

            // Hit list
            hFreddedList=CreateWindow("LISTBOX",NULL,
                WS_VISIBLE|WS_CHILD|WS_BORDER|WS_VSCROLL|WS_HSCROLL|LBS_NOTIFY,
                10,82,680,430,
                hwnd,(HMENU)ID_FREDDED_LIST,NULL,NULL);

            HFONT mono=CreateFont(13,0,0,0,FW_NORMAL,0,0,0,ANSI_CHARSET,0,0,
                CLEARTYPE_QUALITY,FIXED_PITCH|FF_MODERN,"Courier New");
            SendMessage(hFreddedList,WM_SETFONT,(WPARAM)mono,TRUE);
            SetWindowSubclass(hFreddedList,FreddedListProc,0,0);

            // Save log button
            CreateWindow("BUTTON","Save Log",
                WS_VISIBLE|WS_CHILD|BS_PUSHBUTTON,
                490,522,100,30,
                hwnd,(HMENU)ID_BTN_SAVELOG,NULL,NULL);

            CreateWindow("BUTTON","Close",
                WS_VISIBLE|WS_CHILD|BS_PUSHBUTTON,
                600,522,90,30,
                hwnd,(HMENU)ID_BTN_CLOSE,NULL,NULL);

            PopulateFreddedList(hFreddedList,progName);

            // RGB animation timer
            SetTimer(hwnd,ID_FREDDED_TIMER,50,NULL);
            break;
        }

        case WM_TIMER:
            if(wParam==ID_FREDDED_TIMER){
                freddedTimerIdx=(freddedTimerIdx+1)%360;
                InvalidateRect(hTitleBar,NULL,FALSE);
                // Redraw list every 3 ticks to avoid flicker
                if(freddedTimerIdx%3==0)
                    InvalidateRect(hFreddedList,NULL,FALSE);
            }
            break;

        case WM_COMMAND:
            if(LOWORD(wParam)==ID_BTN_CLOSE){
                KillTimer(hwnd,ID_FREDDED_TIMER);
                hFreddedWnd=NULL; hFreddedList=NULL;
                DestroyWindow(hwnd);
            }
            if(LOWORD(wParam)==ID_BTN_SAVELOG){
                OPENFILENAME ofn={sizeof(ofn)};
                char buf[MAX_PATH]={0};
                lstrcpy(buf,"fredded_log.txt");
                ofn.hwndOwner=hwnd;
                ofn.lpstrFilter="Text Files\0*.txt\0All Files\0*.*\0";
                ofn.lpstrFile=buf;
                ofn.nMaxFile=MAX_PATH;
                ofn.lpstrDefExt="txt";
                ofn.Flags=OFN_OVERWRITEPROMPT;
                if(GetSaveFileName(&ofn)){
                    HANDLE hF=CreateFile(buf,GENERIC_WRITE,0,NULL,
                        CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
                    if(hF!=INVALID_HANDLE_VALUE){
                        int cnt=(int)SendMessage(hFreddedList,LB_GETCOUNT,0,0);
                        for(int i=0;i<cnt;i++){
                            char line[512]={0};
                            SendMessage(hFreddedList,LB_GETTEXT,i,(LPARAM)line);
                            lstrcat(line,"\r\n");
                            DWORD w; WriteFile(hF,line,lstrlen(line),&w,NULL);
                        }
                        CloseHandle(hF);
                        MessageBox(hwnd,"Log saved!","Freds Fracker",MB_OK|MB_ICONINFORMATION);
                    }
                }
            }
            break;

        case WM_DESTROY:
            KillTimer(hwnd,ID_FREDDED_TIMER);
            hFreddedWnd=NULL; hFreddedList=NULL;
            break;

        default:
            return DefWindowProc(hwnd,msg,wParam,lParam);
    }
    return 0;
}

void OpenFreddedWindow(HWND hParent, const char *progName) {
    if(hFreddedWnd){ SetForegroundWindow(hFreddedWnd); return; }

    WNDCLASS wc={0};
    wc.lpfnWndProc=FreddedWndProc;
    wc.hInstance=GetModuleHandle(NULL);
    wc.lpszClassName="FreddedWnd";
    wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.hCursor=LoadCursor(NULL,IDC_ARROW);
    RegisterClass(&wc);

    char cap[300]; wsprintf(cap,"Program Fredded - %s",progName);
    hFreddedWnd=CreateWindow("FreddedWnd",cap,
        WS_OVERLAPPEDWINDOW|WS_VISIBLE,
        120,80,720,600,
        hParent,NULL,GetModuleHandle(NULL),(LPVOID)progName);
}

// ── Scanner thread ────────────────────────────────────────────────────────────
typedef struct { HWND hAllList,hKeyList,hParent; char path[MAX_PATH]; char prog[256]; } ScanArgs;

DWORD WINAPI ScanThread(LPVOID param){
    ScanArgs *a=(ScanArgs*)param;
    HWND hAllList=a->hAllList,hKeyList=a->hKeyList,hParent=a->hParent;
    const char *filePath=a->path;
    char progName[256]; lstrcpyn(progName,a->prog,256);

    SendMessage(hAllList,LB_RESETCONTENT,0,0);
    SendMessage(hKeyList,LB_RESETCONTENT,0,0);
    SendMessage(hKeyList,LB_ADDSTRING,0,(LPARAM)"Scanning...");

    gHitCount=0;

    HANDLE hFile=CreateFile(filePath,GENERIC_READ,FILE_SHARE_READ,
                             NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(hFile==INVALID_HANDLE_VALUE){
        SendMessage(hAllList,LB_RESETCONTENT,0,0);
        SendMessage(hAllList,LB_ADDSTRING,0,(LPARAM)"[Error: run as Administrator]");
        SendMessage(hKeyList,LB_RESETCONTENT,0,0);
        SendMessage(hKeyList,LB_ADDSTRING,0,(LPARAM)"[Cannot open]");
        HeapFree(GetProcessHeap(),0,a); return 0;
    }

    DWORD fs=GetFileSize(hFile,NULL);
    if(!fs||fs==INVALID_FILE_SIZE){
        CloseHandle(hFile);
        SendMessage(hAllList,LB_ADDSTRING,0,(LPARAM)"[Empty]");
        HeapFree(GetProcessHeap(),0,a); return 0;
    }

    DWORD readSize=fs<32*1024*1024?fs:32*1024*1024;
    char *buf=(char*)VirtualAlloc(NULL,readSize+2,MEM_COMMIT,PAGE_READWRITE);
    if(!buf){ CloseHandle(hFile); HeapFree(GetProcessHeap(),0,a); return 0; }

    DWORD br=0;
    ReadFile(hFile,buf,readSize,&br,NULL);
    CloseHandle(hFile);
    buf[br]='\0';

    // Pass 1: ASCII strings
    char token[512]; int tlen=0,allAdded=0,keyAdded=0;
    for(DWORD i=0;i<=br;i++){
        unsigned char c=(unsigned char)buf[i];
        if(c>=0x20&&c<0x7F&&c!='\r'){
            if(tlen<511) token[tlen++]=(char)c;
        } else {
            if(tlen>=4){
                token[tlen]='\0';
                if(allAdded<5000){
                    SendMessage(hAllList,LB_ADDSTRING,0,(LPARAM)token);
                    allAdded++;
                }
                char kw[64]={0};
                if(ContainsKeyword(token,kw)&&gHitCount<MAX_HITS){
                    // Store hit
                    lstrcpyn(gHits[gHitCount].keyword,kw,64);
                    lstrcpyn(gHits[gHitCount].context,token,256);
                    lstrcpy(gHits[gHitCount].source,"STRING");
                    gHits[gHitCount].offset=0;
                    gHitCount++;
                    keyAdded++;

                    char tagged[570];
                    wsprintf(tagged,">>> [STRING] (%s) %s",kw,token);
                    SendMessage(hKeyList,LB_ADDSTRING,0,(LPARAM)tagged);
                }
            }
            tlen=0;
        }
    }

    if(allAdded==0)
        SendMessage(hAllList,LB_ADDSTRING,0,(LPARAM)"[No ASCII strings found]");

    // Pass 2: binary fallback
    if(keyAdded==0){
        SendMessage(hKeyList,LB_RESETCONTENT,0,0);
        SendMessage(hKeyList,LB_ADDSTRING,0,
            (LPARAM)"--- No string hits. Scanning raw binary... ---");
        int hits=0;
        for(int k=0;k<KEYWORD_COUNT;k++){
            DWORD kwl=(DWORD)strlen(KEYWORDS[k]);
            DWORD from=0;
            while(from+kwl<=br){
                long off=BinaryFind(buf+from,br-from,KEYWORDS[k],kwl);
                if(off<0) break;
                DWORD abs=from+(DWORD)off;
                char ctx[200]={0};
                ExtractCtx(buf,br,abs,ctx,sizeof(ctx));

                if(gHitCount<MAX_HITS){
                    lstrcpyn(gHits[gHitCount].keyword,(char*)KEYWORDS[k],64);
                    lstrcpyn(gHits[gHitCount].context,ctx,256);
                    lstrcpy(gHits[gHitCount].source,"BINARY");
                    gHits[gHitCount].offset=abs;
                    gHitCount++;
                }

                char entry[350];
                wsprintf(entry,">>> [BIN:0x%08X] (%s) %s",abs,KEYWORDS[k],ctx);
                SendMessage(hKeyList,LB_ADDSTRING,0,(LPARAM)entry);
                hits++; keyAdded++;
                from=abs+kwl;
                if(hits>=500){
                    SendMessage(hKeyList,LB_ADDSTRING,0,(LPARAM)"[Capped at 500]");
                    goto done;
                }
            }
        }
        if(hits==0){
            SendMessage(hKeyList,LB_ADDSTRING,0,(LPARAM)"[No matches found in binary]");
        } else {
            char s[128]; wsprintf(s,"--- Binary scan: %d hits ---",hits);
            SendMessage(hKeyList,LB_ADDSTRING,0,(LPARAM)s);
        }
    }

done:
    VirtualFree(buf,0,MEM_RELEASE);

    // If we found hits, open the Fredded window on the main thread
    if(gHitCount>0){
        // Post a custom message to the parent to open the fredded window
        PostMessage(hParent,WM_APP+1,0,(LPARAM)HeapAlloc(GetProcessHeap(),
            HEAP_ZERO_MEMORY,256));
        // Store prog name in the allocated block
        char *pn=(char*)HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,256);
        lstrcpyn(pn,progName,256);
        PostMessage(hParent,WM_APP+1,0,(LPARAM)pn);
    }

    HeapFree(GetProcessHeap(),0,a);
    return 0;
}

void ScanAsync(HWND hAll,HWND hKey,HWND hParent,const char *path,const char *prog){
    ScanArgs *a=(ScanArgs*)HeapAlloc(GetProcessHeap(),0,sizeof(ScanArgs));
    a->hAllList=hAll; a->hKeyList=hKey; a->hParent=hParent;
    lstrcpyn(a->path,path,MAX_PATH);
    lstrcpyn(a->prog,prog,256);
    HANDLE hT=CreateThread(NULL,0,ScanThread,a,0,NULL);
    if(hT) CloseHandle(hT);
}

// ── Keyword list painter ──────────────────────────────────────────────────────
LRESULT CALLBACK KeyListProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam,
                               UINT_PTR uid,DWORD_PTR ref){
    if(msg==WM_PAINT){
        LRESULT res=DefSubclassProc(hwnd,msg,wParam,lParam);
        HDC hdc=GetDC(hwnd);
        HFONT f=CreateFont(13,0,0,0,FW_BOLD,0,0,0,ANSI_CHARSET,0,0,
            CLEARTYPE_QUALITY,FIXED_PITCH|FF_MODERN,"Courier New");
        SelectObject(hdc,f);
        int cnt=(int)SendMessage(hwnd,LB_GETCOUNT,0,0);
        int top=(int)SendMessage(hwnd,LB_GETTOPINDEX,0,0);
        for(int i=top;i<cnt;i++){
            RECT ir;
            if(SendMessage(hwnd,LB_GETITEMRECT,i,(LPARAM)&ir)==LB_ERR) break;
            char text[600]={0};
            SendMessage(hwnd,LB_GETTEXT,i,(LPARAM)text);
            if(text[0]=='>'&&text[1]=='>'&&text[2]=='>'){
                BOOL bin=(strstr(text,"[BIN:")!=NULL);
                COLORREF fg=bin?RGB(0,220,255):RGB(0,255,80);
                COLORREF bg=bin?RGB(0,20,50):RGB(0,40,0);
                HBRUSH br=CreateSolidBrush(bg);
                FillRect(hdc,&ir,br); DeleteObject(br);
                SetBkMode(hdc,TRANSPARENT); SetTextColor(hdc,fg);
                RECT tr=ir; tr.left+=4;
                DrawText(hdc,text,-1,&tr,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
            }
        }
        DeleteObject(f); ReleaseDC(hwnd,hdc);
        return res;
    }
    return DefSubclassProc(hwnd,msg,wParam,lParam);
}

// ── Settings panel ────────────────────────────────────────────────────────────
LRESULT CALLBACK SettingsPanelProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam){
    switch(msg){
        case WM_PAINT:{
            PAINTSTRUCT ps; HDC hdc=BeginPaint(hwnd,&ps);
            RECT rc; GetClientRect(hwnd,&rc);
            if(hBlurBitmap||hBgBitmap){
                HDC hm=CreateCompatibleDC(hdc);
                SelectObject(hm,hBlurBitmap?hBlurBitmap:hBgBitmap);
                BitBlt(hdc,0,0,rc.right,rc.bottom,hm,0,0,SRCCOPY);
                DeleteDC(hm);
                HDC ho=CreateCompatibleDC(hdc);
                HBITMAP hob=CreateCompatibleBitmap(hdc,rc.right,rc.bottom);
                SelectObject(ho,hob);
                HBRUSH ob=CreateSolidBrush(RGB(0,0,0));
                FillRect(ho,&rc,ob); DeleteObject(ob);
                BLENDFUNCTION bf={AC_SRC_OVER,0,160,0};
                AlphaBlend(hdc,0,0,rc.right,rc.bottom,ho,0,0,rc.right,rc.bottom,bf);
                DeleteObject(hob); DeleteDC(ho);
            } else {
                HBRUSH br=CreateSolidBrush(gBgColor);
                FillRect(hdc,&rc,br); DeleteObject(br);
            }
            SetBkMode(hdc,TRANSPARENT); SetTextColor(hdc,gFontColor);
            SelectObject(hdc,hSettingsFont);
            RECT r={20,15,400,40};
            DrawText(hdc,"Settings",-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            r=(RECT){20,78,200,98};
            DrawText(hdc,"Font Color:",-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            r=(RECT){20,128,220,148};
            DrawText(hdc,"Background Color:",-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            r=(RECT){20,178,260,198};
            DrawText(hdc,"Background Image (.bmp):",-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            r=(RECT){20,228,200,248};
            DrawText(hdc,"Background Blur:",-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            if(gBgImagePath[0]){
                r=(RECT){20,268,460,285};
                SetTextColor(hdc,RGB(150,150,255));
                DrawText(hdc,gBgImagePath,-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
                SetTextColor(hdc,gFontColor);
            }
            // Swatches
            RECT s1={310,73,360,98};
            HBRUSH fc=CreateSolidBrush(gFontColor); FillRect(hdc,&s1,fc); DeleteObject(fc);
            HPEN bp=CreatePen(PS_SOLID,1,RGB(200,200,200));
            HPEN op=(HPEN)SelectObject(hdc,bp);
            SelectObject(hdc,GetStockObject(NULL_BRUSH));
            Rectangle(hdc,s1.left,s1.top,s1.right,s1.bottom);
            RECT s2={310,123,360,148};
            HBRUSH bc=CreateSolidBrush(gBgColor); FillRect(hdc,&s2,bc); DeleteObject(bc);
            Rectangle(hdc,s2.left,s2.top,s2.right,s2.bottom);
            SelectObject(hdc,op); DeleteObject(bp);
            EndPaint(hwnd,&ps); return 0;
        }
        case WM_HSCROLL:{
            if((HWND)lParam==GetDlgItem(hwnd,ID_BLUR_SLIDER)){
                gBlurAmount=(int)SendMessage((HWND)lParam,TBM_GETPOS,0,0);
                if(gBgImagePath[0]){
                    LoadBackgroundImage(GetParent(GetParent(hwnd)));
                    InvalidateRect(hwnd,NULL,TRUE);
                }
                char buf[32]; wsprintf(buf,"Blur: %d",gBlurAmount);
                SetWindowText(GetDlgItem(hwnd,ID_BLUR_SLIDER+100),buf);
            }
            return 0;
        }
        case WM_COMMAND:{
            HWND hMain=GetParent(GetParent(hwnd));
            if(LOWORD(wParam)==ID_BTN_FONTCOLOR){
                CHOOSECOLOR cc={sizeof(cc)};
                cc.hwndOwner=hwnd; cc.rgbResult=gFontColor;
                cc.lpCustColors=gCustomColors; cc.Flags=CC_FULLOPEN|CC_RGBINIT;
                if(ChooseColor(&cc)){gFontColor=cc.rgbResult;InvalidateRect(hwnd,NULL,TRUE);InvalidateRect(hPanelMain,NULL,TRUE);}
            }
            if(LOWORD(wParam)==ID_BTN_BGCOLOR){
                CHOOSECOLOR cc={sizeof(cc)};
                cc.hwndOwner=hwnd; cc.rgbResult=gBgColor;
                cc.lpCustColors=gCustomColors; cc.Flags=CC_FULLOPEN|CC_RGBINIT;
                if(ChooseColor(&cc)){gBgColor=cc.rgbResult;InvalidateRect(hwnd,NULL,TRUE);InvalidateRect(hPanelMain,NULL,TRUE);}
            }
            if(LOWORD(wParam)==ID_BTN_BGIMAGE){
                OPENFILENAME ofn={sizeof(ofn)}; char buf[MAX_PATH]={0};
                ofn.hwndOwner=hwnd; ofn.lpstrFilter="Bitmap Files\0*.bmp\0All Files\0*.*\0";
                ofn.lpstrFile=buf; ofn.nMaxFile=MAX_PATH; ofn.Flags=OFN_FILEMUSTEXIST;
                if(GetOpenFileName(&ofn)){
                    lstrcpyn(gBgImagePath,buf,MAX_PATH);
                    LoadBackgroundImage(hMain);
                    InvalidateRect(hwnd,NULL,TRUE); InvalidateRect(hPanelMain,NULL,TRUE);
                }
            }
            if(LOWORD(wParam)==ID_BTN_RESETBG){
                gBgImagePath[0]='\0';
                if(hBgBitmap){DeleteObject(hBgBitmap);hBgBitmap=NULL;}
                if(hBlurBitmap){DeleteObject(hBlurBitmap);hBlurBitmap=NULL;}
                gBlurAmount=0;
                SendMessage(GetDlgItem(hwnd,ID_BLUR_SLIDER),TBM_SETPOS,TRUE,0);
                InvalidateRect(hwnd,NULL,TRUE); InvalidateRect(hPanelMain,NULL,TRUE);
            }
            return 0;
        }
    }
    return DefWindowProc(hwnd,msg,wParam,lParam);
}

// ── Main panel bg painter ─────────────────────────────────────────────────────
LRESULT CALLBACK MainPanelProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam){
    if(msg==WM_PAINT){
        PAINTSTRUCT ps; HDC hdc=BeginPaint(hwnd,&ps);
        RECT rc; GetClientRect(hwnd,&rc);
        if(hBlurBitmap||hBgBitmap){
            HDC hm=CreateCompatibleDC(hdc);
            SelectObject(hm,hBlurBitmap?hBlurBitmap:hBgBitmap);
            BitBlt(hdc,0,0,rc.right,rc.bottom,hm,0,0,SRCCOPY); DeleteDC(hm);
            HDC ho=CreateCompatibleDC(hdc);
            HBITMAP hob=CreateCompatibleBitmap(hdc,rc.right,rc.bottom);
            SelectObject(ho,hob);
            HBRUSH ob=CreateSolidBrush(RGB(0,0,0)); FillRect(ho,&rc,ob); DeleteObject(ob);
            BLENDFUNCTION bf={AC_SRC_OVER,0,140,0};
            AlphaBlend(hdc,0,0,rc.right,rc.bottom,ho,0,0,rc.right,rc.bottom,bf);
            DeleteObject(hob); DeleteDC(ho);
        } else {
            HBRUSH br=CreateSolidBrush(gBgColor); FillRect(hdc,&rc,br); DeleteObject(br);
        }
        EndPaint(hwnd,&ps); return 0;
    }
    return DefWindowProc(hwnd,msg,wParam,lParam);
}

// ── Strings window ────────────────────────────────────────────────────────────
LRESULT CALLBACK StringsWndProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam){
    static HWND hAllList,hKeyList;
    switch(msg){
        case WM_CREATE:{
            CREATESTRUCT *cs=(CREATESTRUCT*)lParam;
            const char *fp=(const char*)cs->lpCreateParams;
            CreateWindow("STATIC","All Strings:",WS_VISIBLE|WS_CHILD,10,8,200,18,hwnd,NULL,NULL,NULL);
            hAllList=CreateWindow("LISTBOX",NULL,WS_VISIBLE|WS_CHILD|WS_BORDER|WS_VSCROLL|WS_HSCROLL|LBS_NOTIFY,
                10,28,480,490,hwnd,(HMENU)ID_STRINGS_LIST,NULL,NULL);
            HFONT m=CreateFont(13,0,0,0,FW_NORMAL,0,0,0,ANSI_CHARSET,0,0,CLEARTYPE_QUALITY,FIXED_PITCH|FF_MODERN,"Courier New");
            SendMessage(hAllList,WM_SETFONT,(WPARAM)m,TRUE);
            CreateWindow("STATIC","Keyword Hits  [green=string  cyan=binary]:",WS_VISIBLE|WS_CHILD,510,8,460,18,hwnd,NULL,NULL,NULL);
            hKeyList=CreateWindow("LISTBOX",NULL,WS_VISIBLE|WS_CHILD|WS_BORDER|WS_VSCROLL|WS_HSCROLL|LBS_NOTIFY,
                510,28,460,490,hwnd,(HMENU)ID_KEYS_LIST,NULL,NULL);
            HFONT mk=CreateFont(13,0,0,0,FW_BOLD,0,0,0,ANSI_CHARSET,0,0,CLEARTYPE_QUALITY,FIXED_PITCH|FF_MODERN,"Courier New");
            SendMessage(hKeyList,WM_SETFONT,(WPARAM)mk,TRUE);
            SetWindowSubclass(hKeyList,KeyListProc,0,0);
            CreateWindow("STATIC",fp,WS_VISIBLE|WS_CHILD|SS_WORDELLIPSIS,10,528,860,18,hwnd,NULL,NULL,NULL);
            CreateWindow("BUTTON","Close",WS_VISIBLE|WS_CHILD|BS_PUSHBUTTON,430,552,120,30,hwnd,(HMENU)ID_BTN_CLOSE,NULL,NULL);

            // Get parent's parent (main window) for the fredded popup
            HWND hMain=GetParent(hwnd);
            ScanAsync(hAllList,hKeyList,hMain,fp,
                      (const char*)((CREATESTRUCT*)lParam)->lpszName);
            break;
        }
        case WM_COMMAND:
            if(LOWORD(wParam)==ID_BTN_CLOSE) DestroyWindow(hwnd);
            break;
        default:
            return DefWindowProc(hwnd,msg,wParam,lParam);
    }
    return 0;
}

void OpenStringsWindow(HWND hParent,const char *title){
    char exePath[MAX_PATH]={0};
    WNDCLASS wcs={0};
    wcs.lpfnWndProc=StringsWndProc; wcs.hInstance=GetModuleHandle(NULL);
    wcs.lpszClassName="FredsStringsWnd";
    wcs.hbrBackground=(HBRUSH)(COLOR_WINDOW+1); wcs.hCursor=LoadCursor(NULL,IDC_ARROW);
    RegisterClass(&wcs);

    HWND hWnd=FindWindow(NULL,title);
    if(hWnd){
        DWORD pid=0; GetWindowThreadProcessId(hWnd,&pid);
        if(pid){
            HANDLE hP=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,pid);
            if(hP){ GetModuleFileNameEx(hP,NULL,exePath,MAX_PATH); CloseHandle(hP); }
        }
    }
    if(!exePath[0]) lstrcpyn(exePath,title,MAX_PATH);

    char cap[MAX_PATH+32]; wsprintf(cap,"Strings - %s",title);
    // Pass both path and prog name - use window title as prog name stored in lpszName
    HWND hSW=CreateWindow("FredsStringsWnd",title,
        WS_OVERLAPPEDWINDOW|WS_VISIBLE,80,60,1000,640,
        hParent,NULL,GetModuleHandle(NULL),(LPVOID)exePath);
    (void)hSW;
}

// ── Subclasses ────────────────────────────────────────────────────────────────
LRESULT CALLBACK MsgWndProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam){
    if(msg==WM_PAINT){
        PAINTSTRUCT ps; HDC hdc=BeginPaint(hwnd,&ps); RECT rc; GetClientRect(hwnd,&rc);
        HBRUSH br=CreateSolidBrush(RGB(30,30,30)); FillRect(hdc,&rc,br); DeleteObject(br);
        SetBkMode(hdc,TRANSPARENT); SetTextColor(hdc,RGB(255,80,80));
        HFONT f=CreateFont(18,0,0,0,FW_BOLD,0,0,0,ANSI_CHARSET,0,0,CLEARTYPE_QUALITY,0,"Segoe UI");
        SelectObject(hdc,f);
        DrawText(hdc,"Please select a program!",-1,&rc,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        DeleteObject(f); EndPaint(hwnd,&ps); return 0;
    }
    if(msg==WM_DESTROY){hMsgWnd=NULL;return 0;}
    return DefWindowProc(hwnd,msg,wParam,lParam);
}

LRESULT CALLBACK TitleProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam,UINT_PTR uid,DWORD_PTR ref){
    if(msg==WM_PAINT){
        PAINTSTRUCT ps; HDC hdc=BeginPaint(hwnd,&ps); RECT rc; GetClientRect(hwnd,&rc);
        SetBkMode(hdc,TRANSPARENT); SetTextColor(hdc,flashColors[colorIndex]);
        SelectObject(hdc,hTitleFont);
        FillRect(hdc,&rc,(HBRUSH)GetStockObject(BLACK_BRUSH));
        DrawText(hdc,"Freds Fracker",-1,&rc,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        EndPaint(hwnd,&ps); return 0;
    }
    return DefSubclassProc(hwnd,msg,wParam,lParam);
}

LRESULT CALLBACK ListProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam,UINT_PTR uid,DWORD_PTR ref){
    if(msg==WM_PAINT){
        LRESULT res=DefSubclassProc(hwnd,msg,wParam,lParam);
        int sel=(int)SendMessage(hwnd,LB_GETCURSEL,0,0);
        if(sel!=LB_ERR){
            RECT ir; SendMessage(hwnd,LB_GETITEMRECT,(WPARAM)sel,(LPARAM)&ir);
            HDC hdc=GetDC(hwnd);
            for(int i=0;i<3;i++){
                COLORREF c=flashColors[(rainbowIndex+i)%7];
                HPEN pen=CreatePen(PS_SOLID,2,c);
                HPEN op=(HPEN)SelectObject(hdc,pen);
                HBRUSH ob=(HBRUSH)SelectObject(hdc,GetStockObject(NULL_BRUSH));
                RECT r=ir; InflateRect(&r,-i,-i);
                Rectangle(hdc,r.left,r.top,r.right,r.bottom);
                SelectObject(hdc,op); SelectObject(hdc,ob); DeleteObject(pen);
            }
            ReleaseDC(hwnd,hdc);
        }
        return res;
    }
    return DefSubclassProc(hwnd,msg,wParam,lParam);
}

LRESULT CALLBACK StartBtnProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam,UINT_PTR uid,DWORD_PTR ref){
    if(msg==WM_PAINT){
        PAINTSTRUCT ps; HDC hdc=BeginPaint(hwnd,&ps); RECT rc; GetClientRect(hwnd,&rc);
        HBRUSH bg=CreateSolidBrush(RGB(135,206,250)); FillRect(hdc,&rc,bg); DeleteObject(bg);
        HPEN pen=CreatePen(PS_SOLID,2,RGB(30,120,200));
        HPEN op=(HPEN)SelectObject(hdc,pen);
        HBRUSH ob=(HBRUSH)SelectObject(hdc,GetStockObject(NULL_BRUSH));
        Rectangle(hdc,rc.left,rc.top,rc.right-1,rc.bottom-1);
        SelectObject(hdc,op); SelectObject(hdc,ob); DeleteObject(pen);
        SetBkMode(hdc,TRANSPARENT); SetTextColor(hdc,RGB(10,50,120));
        SelectObject(hdc,hBtnFont);
        DrawText(hdc,"Start",-1,&rc,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        EndPaint(hwnd,&ps); return 0;
    }
    if(msg==WM_MOUSEMOVE||msg==WM_MOUSELEAVE||msg==WM_SETFOCUS||msg==WM_KILLFOCUS)
        InvalidateRect(hwnd,NULL,FALSE);
    return DefSubclassProc(hwnd,msg,wParam,lParam);
}

// ── Main window ───────────────────────────────────────────────────────────────
LRESULT CALLBACK WndProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam){
    switch(msg){
        case WM_CREATE:{
            hTitleFont=CreateFont(42,0,0,0,FW_HEAVY,0,0,0,ANSI_CHARSET,0,0,ANTIALIASED_QUALITY,DEFAULT_PITCH|FF_DONTCARE,"Impact");
            hListFont=CreateFont(16,0,0,0,FW_NORMAL,0,0,0,ANSI_CHARSET,0,0,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,"Segoe UI");
            hBtnFont=CreateFont(16,0,0,0,FW_BOLD,0,0,0,ANSI_CHARSET,0,0,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,"Segoe UI");
            hTabFont=CreateFont(15,0,0,0,FW_SEMIBOLD,0,0,0,ANSI_CHARSET,0,0,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,"Segoe UI");
            hSettingsFont=CreateFont(18,0,0,0,FW_BOLD,0,0,0,ANSI_CHARSET,0,0,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,"Segoe UI");
            hFreddedFont=CreateFont(26,0,0,0,FW_BLACK,0,0,0,ANSI_CHARSET,0,0,ANTIALIASED_QUALITY,DEFAULT_PITCH|FF_DONTCARE,"Impact");

            hTabCtrl=CreateWindow(WC_TABCONTROL,"",WS_VISIBLE|WS_CHILD|TCS_FLATBUTTONS,
                0,0,500,30,hwnd,(HMENU)11,GetModuleHandle(NULL),NULL);
            SendMessage(hTabCtrl,WM_SETFONT,(WPARAM)hTabFont,TRUE);
            TCITEM ti={TCIF_TEXT};
            ti.pszText="Programs"; TabCtrl_InsertItem(hTabCtrl,0,&ti);
            ti.pszText="Settings"; TabCtrl_InsertItem(hTabCtrl,1,&ti);

            WNDCLASS wcp={0};
            wcp.hInstance=GetModuleHandle(NULL);
            wcp.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);
            wcp.hCursor=LoadCursor(NULL,IDC_ARROW);
            wcp.lpfnWndProc=MainPanelProc; wcp.lpszClassName="MainPanel";
            RegisterClass(&wcp);
            wcp.lpfnWndProc=SettingsPanelProc; wcp.lpszClassName="SettingsPanel";
            RegisterClass(&wcp);

            hPanelMain=CreateWindow("MainPanel","",WS_VISIBLE|WS_CHILD,
                0,30,500,470,hwnd,(HMENU)ID_PANEL_MAIN,GetModuleHandle(NULL),NULL);

            hTitle=CreateWindow("STATIC","",WS_VISIBLE|WS_CHILD,
                0,0,500,60,hPanelMain,(HMENU)ID_TITLE,NULL,NULL);
            SetWindowSubclass(hTitle,TitleProc,0,0);

            CreateWindow("STATIC","Currently Open Programs:",WS_VISIBLE|WS_CHILD,
                10,68,380,20,hPanelMain,NULL,NULL,NULL);

            hList=CreateWindow("LISTBOX",NULL,WS_VISIBLE|WS_CHILD|WS_BORDER|WS_VSCROLL|LBS_NOTIFY,
                10,90,460,290,hPanelMain,(HMENU)ID_LISTBOX,NULL,NULL);
            SendMessage(hList,WM_SETFONT,(WPARAM)hListFont,TRUE);
            SetWindowSubclass(hList,ListProc,0,0);

            CreateWindow("BUTTON","Refresh",WS_VISIBLE|WS_CHILD|BS_PUSHBUTTON,
                10,395,100,35,hPanelMain,(HMENU)ID_BTN_REFRESH,NULL,NULL);

            hStartBtn=CreateWindow("BUTTON","",WS_VISIBLE|WS_CHILD|BS_OWNERDRAW,
                350,395,110,35,hPanelMain,(HMENU)ID_BTN_START,NULL,NULL);
            SetWindowSubclass(hStartBtn,StartBtnProc,0,0);

            hPanelSettings=CreateWindow("SettingsPanel","",WS_CHILD,
                0,30,500,470,hwnd,(HMENU)ID_PANEL_SETTINGS,GetModuleHandle(NULL),NULL);

            CreateWindow("BUTTON","Choose Font Color",WS_VISIBLE|WS_CHILD|BS_PUSHBUTTON,
                20,70,180,28,hPanelSettings,(HMENU)ID_BTN_FONTCOLOR,NULL,NULL);
            CreateWindow("BUTTON","Choose Background Color",WS_VISIBLE|WS_CHILD|BS_PUSHBUTTON,
                20,120,180,28,hPanelSettings,(HMENU)ID_BTN_BGCOLOR,NULL,NULL);
            CreateWindow("BUTTON","Choose Background Image",WS_VISIBLE|WS_CHILD|BS_PUSHBUTTON,
                20,170,180,28,hPanelSettings,(HMENU)ID_BTN_BGIMAGE,NULL,NULL);
            CreateWindow("BUTTON","Reset Background",WS_VISIBLE|WS_CHILD|BS_PUSHBUTTON,
                20,300,180,28,hPanelSettings,(HMENU)ID_BTN_RESETBG,NULL,NULL);

            HWND hSlider=CreateWindow(TRACKBAR_CLASS,"",WS_VISIBLE|WS_CHILD|TBS_HORZ|TBS_AUTOTICKS,
                20,250,300,30,hPanelSettings,(HMENU)ID_BLUR_SLIDER,NULL,NULL);
            SendMessage(hSlider,TBM_SETRANGE,TRUE,MAKELONG(0,20));
            SendMessage(hSlider,TBM_SETPOS,TRUE,0);
            CreateWindow("STATIC","Blur: 0",WS_VISIBLE|WS_CHILD,
                330,255,100,20,hPanelSettings,(HMENU)(ID_BLUR_SLIDER+100),NULL,NULL);

            RefreshList();
            SetTimer(hwnd,ID_TIMER,120,NULL);
            SetTimer(hwnd,ID_RAINBOW_TIMER,80,NULL);
            break;
        }

        case WM_APP+1: {
            // Signal from scan thread: keywords found, open Fredded window
            char *progName=(char*)lParam;
            if(progName&&gHitCount>0)
                OpenFreddedWindow(hwnd,progName);
            if(progName) HeapFree(GetProcessHeap(),0,progName);
            break;
        }

        case WM_NOTIFY:{
            NMHDR *nm=(NMHDR*)lParam;
            if(nm->idFrom==11&&nm->code==TCN_SELCHANGE){
                int tab=TabCtrl_GetCurSel(hTabCtrl);
                ShowWindow(hPanelMain,  tab==0?SW_SHOW:SW_HIDE);
                ShowWindow(hPanelSettings,tab==1?SW_SHOW:SW_HIDE);
                if(tab==1) InvalidateRect(hPanelSettings,NULL,TRUE);
            }
            break;
        }

        case WM_TIMER:
            if(wParam==ID_TIMER){colorIndex=(colorIndex+1)%7;InvalidateRect(hTitle,NULL,FALSE);}
            else if(wParam==ID_RAINBOW_TIMER){
                rainbowIndex=(rainbowIndex+1)%7;
                if(SendMessage(hList,LB_GETCURSEL,0,0)!=LB_ERR) InvalidateRect(hList,NULL,FALSE);
            }
            else if(wParam==ID_MSG_TIMER){KillTimer(hwnd,ID_MSG_TIMER);if(hMsgWnd){DestroyWindow(hMsgWnd);hMsgWnd=NULL;}}
            break;

        case WM_COMMAND:
            if(LOWORD(wParam)==ID_BTN_REFRESH) RefreshList();
            if(LOWORD(wParam)==ID_LISTBOX&&HIWORD(wParam)==LBN_SELCHANGE) InvalidateRect(hList,NULL,FALSE);
            if(LOWORD(wParam)==ID_BTN_START){
                int sel=(int)SendMessage(hList,LB_GETCURSEL,0,0);
                if(sel==LB_ERR){
                    if(!hMsgWnd){
                        WNDCLASS wcp={0};
                        wcp.lpfnWndProc=MsgWndProc; wcp.hInstance=GetModuleHandle(NULL);
                        wcp.lpszClassName="FredsPopup";
                        wcp.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);
                        RegisterClass(&wcp);
                        RECT wr; GetWindowRect(hwnd,&wr);
                        int pw=280,ph=60;
                        hMsgWnd=CreateWindow("FredsPopup","",WS_POPUP|WS_VISIBLE|WS_BORDER,
                            wr.left+(wr.right-wr.left-pw)/2,wr.top+(wr.bottom-wr.top-ph)/2,
                            pw,ph,hwnd,NULL,GetModuleHandle(NULL),NULL);
                    }
                    SetTimer(hwnd,ID_MSG_TIMER,2000,NULL);
                } else {
                    char prog[256]={0};
                    SendMessage(hList,LB_GETTEXT,sel,(LPARAM)prog);
                    OpenStringsWindow(hwnd,prog);
                }
            }
            break;

        case WM_SIZE:{
            int w=LOWORD(lParam),h=HIWORD(lParam);
            SetWindowPos(hTabCtrl,NULL,0,0,w,30,SWP_NOZORDER);
            SetWindowPos(hPanelMain,NULL,0,30,w,h-30,SWP_NOZORDER);
            SetWindowPos(hPanelSettings,NULL,0,30,w,h-30,SWP_NOZORDER);
            SetWindowPos(hTitle,NULL,0,0,w,60,SWP_NOZORDER);
            SetWindowPos(hList,NULL,10,90,w-40,h-200,SWP_NOZORDER);
            SetWindowPos(hStartBtn,NULL,w-150,h-105,110,35,SWP_NOZORDER);
            if(gBgImagePath[0]) LoadBackgroundImage(hwnd);
            break;
        }

        case WM_DESTROY:
            KillTimer(hwnd,ID_TIMER); KillTimer(hwnd,ID_RAINBOW_TIMER); KillTimer(hwnd,ID_MSG_TIMER);
            DeleteObject(hTitleFont); DeleteObject(hListFont); DeleteObject(hBtnFont);
            DeleteObject(hTabFont); DeleteObject(hSettingsFont); DeleteObject(hFreddedFont);
            if(hBgBitmap) DeleteObject(hBgBitmap);
            if(hBlurBitmap) DeleteObject(hBlurBitmap);
            PostQuitMessage(0);
            break;

        default:
            return DefWindowProc(hwnd,msg,wParam,lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInst,HINSTANCE hPrev,LPSTR lpCmd,int nShow){
    InitCommonControls();
    WNDCLASS wc={0};
    wc.lpfnWndProc=WndProc; wc.hInstance=hInst;
    wc.lpszClassName="FredsFragcker";
    wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.hCursor=LoadCursor(NULL,IDC_ARROW);
    RegisterClass(&wc);
    HWND hwnd=CreateWindow("FredsFragcker","Freds Fracker",
        WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,520,540,
        NULL,NULL,hInst,NULL);
    ShowWindow(hwnd,nShow); UpdateWindow(hwnd);
    MSG msg;
    while(GetMessage(&msg,NULL,0,0)){TranslateMessage(&msg);DispatchMessage(&msg);}
    return (int)msg.wParam;
}
