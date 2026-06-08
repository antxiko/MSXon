// msxon.c — MSXon (standalone MSXON.COM)
// Boot Screen 5: intro logo + arpegio + LOGIN/REGISTER/QR
// Tras LOGIN_OK: -> Screen 0: selector juegos + lobby + waiting + launch
#include "msxgl.h"
#include "vdp.h"
#include "input.h"
#include "bios.h"
#include "bios_hook.h"
#include "system.h"
#include "dos.h"
#include "psg.h"
#include "tool/qrcode_tiny.h"
#include "font/font_mgl_sample6.h"
#include "content/msxon_logo.h"
#include "protocol.h"
#include "network.h"
#include "log.h"

// ── Server ─────────────────────────────────────────────────────────
static const u8 SERVER_IP[4] = {217, 154, 107, 144};
#define SERVER_PORT 9876

// ── Game table — rellenada dinámicamente desde CMD_GAME_LIST tras LOGIN_OK ─
#define MAX_GAMES      16
#define GAME_NAME_MAX  16
#define GAME_COM_MAX   8

typedef struct {
    const c8* name;       // → g_GameNames[i]
    const c8* comFile;    // → g_GameComs[i]
    u8 gameId;
    u8 maxPlayers;
    u8 protoVersion;
} GameDef;

static GameDef g_Games[MAX_GAMES];
static c8      g_GameNames[MAX_GAMES][GAME_NAME_MAX + 1];
static c8      g_GameComs [MAX_GAMES][GAME_COM_MAX  + 1];
static u8      g_GameFlags[MAX_GAMES];   // bit0 = private (marca visual en menú)
static u8      g_NumGames = 0;

// ── Intro / Screen 5 constants ────────────────────────────────────
#define NOTE_C5           214
#define NOTE_E5           170
#define NOTE_G5           143
#define NOTE_C6           107
#define DELAY_BLANK       18
#define DELAY_BEFORE_ON   28
#define DELAY_BEFORE_SND  18
#define DELAY_NOTE        9
#define DELAY_LAST_NOTE   22
#define DELAY_HOLD        30

// V9938 RGB encoding (3 bits por canal)
#define PAL_WHITE     0x0777
#define PAL_BLACK     0x0000
#define PAL_BLUE      0x0115
#define PAL_GREEN     0x0700
#define PAL_DIMGREEN  0x0400
#define PAL_RED       0x0070

#define IDX_BLUE      1
#define IDX_WHITE     2
#define IDX_BLACK     3

// Logo
#define LOGO_W        256
#define LOGO_H        40
#define LOGO_BYTES    (LOGO_W * LOGO_H / 2)
#define LOGO_Y_TOP    86
#define LOGO_VRAM     (LOGO_Y_TOP * 128)
#define VRAM_VISIBLE  27136

// Input
#define MAX_INPUT     16
#define FONT_W        6

// ASCII codes (locales — evitan colisión con KEY_* de MSXgl)
#define ASCII_CR      0x0D
#define ASCII_BS      0x08
#define ASCII_ESC     0x1B
#define ASCII_TAB     0x09

// ── State ──────────────────────────────────────────────────────────
#define ST_INTRO      0
#define ST_CHOICE     1
#define ST_LOGIN      2
#define ST_REGISTER   3
#define ST_QR         4
#define ST_NETOP      5    // reserved (no usado por ahora)
#define ST_MENU       6
#define ST_DIAG       7
#define ST_CONNECTING 8
#define ST_LOBBY      9
#define ST_WAITING    10
#define ST_LAUNCHING  11
#define ST_CHAT       12
#define ST_RECOVER    13   // formulario "recuperar contrasena" (pide solo username)

static u8 g_State;
static u8 g_SelGame;
static const GameDef* g_CurGame;

// Network
static NetConn g_Conn;
static u8 g_MyPid;
static u8 g_RoomId;
static u8 g_Active;
static u8 g_SendBuf[64];
static bool g_Online;

// Input buffers (LOGIN/REGISTER)
static c8 g_BufUser[MAX_INPUT + 1] = {0};
static c8 g_BufPass[MAX_INPUT + 1] = {0};
static c8 g_BufNick[MAX_INPUT + 1] = {0};

// QR
static u8 g_QR[QRCODE_TINY_BUFFER_LEN];
static u8 g_QRTemp[QRCODE_TINY_BUFFER_LEN];
static c8 g_URL[96];
volatile u8 g_BlinkFrame = 0;

// Sesión post-LOGIN
static u8 g_Role;
static c8 g_Nick[MAX_INPUT + 1] = {0};
static u8 g_SessionId[4];
static c8 g_RegToken[9];

// Form error (1..5 reason FAIL, 0xFF red error)
static u8 g_LastErrCode;

// Buffers de recepción (compartidos por NetLogin/NetRegister/NetFetchGameList).
// Globales para evitar reservar 200+ bytes de stack en cada llamada.
static u8 g_RecvHdr[6];
static u8 g_RecvPl[200];

// Forward declarations
static void DrawErrorLine(u16 x, u8 y, const c8* text);
static bool NetFetchGameList(void);

// Room list
#define LB_MAX 20
typedef struct { u8 rid, gid, np; } LBRoom;
static LBRoom g_LB[LB_MAX];
static u8 g_LBN, g_LBC;
// Mensaje transitorio mostrado en la pantalla de lobby (NULL = sin mensaje)
static const c8* g_LobbyMsg = 0;

// Nicks de los jugadores en la sala (indexado por PID-1). nicks[i][0]=0 si vacio.
#define MSXON_MAX_NICKS 16
#define MSXON_NICK_LEN  16
static c8 g_PlayerNicks[MSXON_MAX_NICKS][MSXON_NICK_LEN + 1];

// ── Chat global ───────────────────────────────────────────────
#define CHAT_MAX_MSG          64
#define CHAT_CHARS_PER_LINE   36   // cuanto cabe en linea visible tras "> "
#define CHAT_HIST_ROWS        12
#define CHAT_LINE_BUF         50   // chars por linea VISUAL en el historial
#define CHAT_LINE_VISUAL_MAX  40   // chars que caben en una linea pintada
static c8 g_ChatLines[CHAT_HIST_ROWS][CHAT_LINE_BUF];
static u8 g_ChatCount = 0;
static c8 g_ChatInput[CHAT_MAX_MSG + 1] = {0};
static u8 g_ChatDirty = 0;   // forzar repintar zona de mensajes
// Flags de "ya pintado" — globales en lugar de static local porque SDCC para
// MSX-DOS no inicializa siempre a 0 los static locals (CRT0 no zero-init BSS
// dentro del scope local). Se resetean en main() y en los cambios de estado.
static u8 g_LobbyDrawn = 0;
static u8 g_WaitDrawn  = 0;
static u8 g_PrevKeyRet = 1;
// Auto-arranque desde ST_WAITING cuando sala llena y P1 es BOT (o AGGREGATE).
// Contador en frames (~60Hz). 0 = no activo.
static u16 g_BotCountdown = 0;

// Key debounce
static u8 g_KeyDly;

// Ping
#define PING_INT 250
static u16 g_PingT;

static void Wait50(void) { u8 w; for(w = 0; w < 25; w++) Halt(); }

// ── Screen 5 string helper: u8 -> ASCII ─────────────────────────────

static u8 ItoaU8(u8 val, c8* buf) {
    u8 h = val / 100, t = (val % 100) / 10, u = val % 10;
    u8 len = 0;
    if (h) buf[len++] = '0' + h;
    if (h || t) buf[len++] = '0' + t;
    buf[len++] = '0' + u;
    buf[len] = 0;
    return len;
}

static void PrintAtNum(u16 x, u8 y, u8 val) {
    c8 buf[4];
    ItoaU8(val, buf);
    Print_SetPosition(x, y);
    Print_DrawText(buf);
}

// ── Screen 5 helpers ───────────────────────────────────────────────

static void Wait(u8 frames) { while (frames--) Halt(); }

static void PlayNote(u16 period, u8 frames) {
    PSG_SetTone(0, period);
    PSG_SetVolume(0, 13);
    PSG_Apply();
    Wait(frames);
}

static u8 StrLen(const c8* s) { u8 n=0; while (s[n]) n++; return n; }

static c8* StrAppend(c8* dst, const c8* s) {
    while (*s) *dst++ = *s++;
    *dst = 0;
    return dst;
}

static void BuildRegisterURL(const c8* user, const c8* token) {
    c8* p = g_URL;
    p = StrAppend(p, "https://msxon.nosignalbbs.com/r?u=");
    p = StrAppend(p, user);
    p = StrAppend(p, "&t=");
    p = StrAppend(p, token);
}

void OnVBlankSpinner(void)
{
    g_BlinkFrame++;
    if ((g_BlinkFrame & 0x07) != 0) return;
    u8 phase = (g_BlinkFrame >> 3) & 0x03;
    VDP_SetPaletteEntry(4, (phase == 0) ? PAL_BLACK : PAL_WHITE);
    VDP_SetPaletteEntry(5, (phase == 1) ? PAL_BLACK : PAL_WHITE);
    VDP_SetPaletteEntry(6, (phase == 2) ? PAL_BLACK : PAL_WHITE);
    VDP_SetPaletteEntry(7, (phase == 3) ? PAL_BLACK : PAL_WHITE);
}

static void DrawQR(u16 cx, u16 cy, u8 scale, u8 fgColor, const u8* qrc) {
    u8 size = QRCode_GetSize(qrc);
    for (u8 j = 0; j < size; j++)
        for (u8 i = 0; i < size; i++)
            if (QRCode_GetModule(qrc, i, j)) {
                u16 x = cx + (u16)i * scale;
                u16 y = cy + (u16)j * scale;
                VDP_CommandHMMV(x, y, scale, scale, fgColor);
            }
}

static void EnterBBSMode(void) {
    VDP_SetPaletteEntry(IDX_BLACK, PAL_BLACK);
    VDP_SetPaletteEntry(IDX_WHITE, PAL_GREEN);
    VDP_SetPaletteEntry(IDX_BLUE,  PAL_DIMGREEN);
    VDP_SetColor(IDX_BLACK);
    VDP_FillVRAM(0x33, 0x0000, 0x00, VRAM_VISIBLE);
    Print_SetBitmapFont(g_Font_MGL_Sample6);
    Print_SetColor(IDX_WHITE, IDX_BLACK);
}

static void DrawField(const c8* buf, u8 len, u16 x, u8 y, u8 mask, u8 active) {
    Print_SetColor(0x22, 0x33);
    Print_SetPosition(x, y);
    u8 i;
    for (i = 0; i < len; i++)
        Print_DrawChar(mask ? '*' : buf[i]);
    u8 cursorShown = (active && len < MAX_INPUT) ? 1 : 0;
    if (cursorShown) Print_DrawChar('_');
    for (i = len + cursorShown; i < MAX_INPUT; i++)
        Print_DrawChar(' ');
}

static c8 ScanInputChar(void) {
    u8 shift = Keyboard_IsKeyPressed(KEY_SHIFT) ? 1 : 0;
    if (Keyboard_IsKeyPressed(KEY_RETURN)) return ASCII_CR;
    if (Keyboard_IsKeyPressed(KEY_ESC))    return ASCII_ESC;
    if (Keyboard_IsKeyPressed(KEY_TAB))    return ASCII_TAB;
    if (Keyboard_IsKeyPressed(KEY_BS))     return ASCII_BS;
    if (Keyboard_IsKeyPressed(KEY_SPACE))  return ' ';
    if (Keyboard_IsKeyPressed(KEY_A)) return shift ? 'A' : 'a';
    if (Keyboard_IsKeyPressed(KEY_B)) return shift ? 'B' : 'b';
    if (Keyboard_IsKeyPressed(KEY_C)) return shift ? 'C' : 'c';
    if (Keyboard_IsKeyPressed(KEY_D)) return shift ? 'D' : 'd';
    if (Keyboard_IsKeyPressed(KEY_E)) return shift ? 'E' : 'e';
    if (Keyboard_IsKeyPressed(KEY_F)) return shift ? 'F' : 'f';
    if (Keyboard_IsKeyPressed(KEY_G)) return shift ? 'G' : 'g';
    if (Keyboard_IsKeyPressed(KEY_H)) return shift ? 'H' : 'h';
    if (Keyboard_IsKeyPressed(KEY_I)) return shift ? 'I' : 'i';
    if (Keyboard_IsKeyPressed(KEY_J)) return shift ? 'J' : 'j';
    if (Keyboard_IsKeyPressed(KEY_K)) return shift ? 'K' : 'k';
    if (Keyboard_IsKeyPressed(KEY_L)) return shift ? 'L' : 'l';
    if (Keyboard_IsKeyPressed(KEY_M)) return shift ? 'M' : 'm';
    if (Keyboard_IsKeyPressed(KEY_N)) return shift ? 'N' : 'n';
    if (Keyboard_IsKeyPressed(KEY_O)) return shift ? 'O' : 'o';
    if (Keyboard_IsKeyPressed(KEY_P)) return shift ? 'P' : 'p';
    if (Keyboard_IsKeyPressed(KEY_Q)) return shift ? 'Q' : 'q';
    if (Keyboard_IsKeyPressed(KEY_R)) return shift ? 'R' : 'r';
    if (Keyboard_IsKeyPressed(KEY_S)) return shift ? 'S' : 's';
    if (Keyboard_IsKeyPressed(KEY_T)) return shift ? 'T' : 't';
    if (Keyboard_IsKeyPressed(KEY_U)) return shift ? 'U' : 'u';
    if (Keyboard_IsKeyPressed(KEY_V)) return shift ? 'V' : 'v';
    if (Keyboard_IsKeyPressed(KEY_W)) return shift ? 'W' : 'w';
    if (Keyboard_IsKeyPressed(KEY_X)) return shift ? 'X' : 'x';
    if (Keyboard_IsKeyPressed(KEY_Y)) return shift ? 'Y' : 'y';
    if (Keyboard_IsKeyPressed(KEY_Z)) return shift ? 'Z' : 'z';
    if (Keyboard_IsKeyPressed(KEY_0)) return shift ? '_' : '0';
    if (Keyboard_IsKeyPressed(KEY_1)) return shift ? '!' : '1';
    if (Keyboard_IsKeyPressed(KEY_2)) return shift ? '"' : '2';
    if (Keyboard_IsKeyPressed(KEY_3)) return shift ? '#' : '3';
    if (Keyboard_IsKeyPressed(KEY_4)) return shift ? '$' : '4';
    if (Keyboard_IsKeyPressed(KEY_5)) return shift ? '%' : '5';
    if (Keyboard_IsKeyPressed(KEY_6)) return shift ? '&' : '6';
    if (Keyboard_IsKeyPressed(KEY_7)) return shift ? '\'' : '7';
    if (Keyboard_IsKeyPressed(KEY_8)) return shift ? '(' : '8';
    if (Keyboard_IsKeyPressed(KEY_9)) return shift ? ')' : '9';
    // Simbolos en la matriz del teclado MSX (layout internacional)
    if (Keyboard_IsKeyPressed(KEY_1_2)) return shift ? '=' : '-';
    if (Keyboard_IsKeyPressed(KEY_1_3)) return shift ? '~' : '^';
    if (Keyboard_IsKeyPressed(KEY_1_4)) return shift ? '|' : '\\';
    if (Keyboard_IsKeyPressed(KEY_1_5)) return shift ? '{' : '[';
    if (Keyboard_IsKeyPressed(KEY_1_6)) return shift ? '}' : ']';
    if (Keyboard_IsKeyPressed(KEY_1_7)) return shift ? '+' : ';';
    if (Keyboard_IsKeyPressed(KEY_2_0)) return shift ? '*' : ':';
    if (Keyboard_IsKeyPressed(KEY_2_1)) return shift ? '<' : ',';
    if (Keyboard_IsKeyPressed(KEY_2_2)) return shift ? '>' : '.';
    if (Keyboard_IsKeyPressed(KEY_2_3)) return shift ? '?' : '/';
    return 0;
}

static u8 InputText(c8* buf, u16 x, u8 y, u8 mask) {
    u8 len = StrLen(buf);
    c8 prevChar = 0;
    u8 frame = 0;
    u8 cursorOn = 1;
    DrawField(buf, len, x, y, mask, 1);
    while (1) {
        Halt();
        frame++;
        if ((frame & 0x0F) == 0) {
            cursorOn = !cursorOn;
            DrawField(buf, len, x, y, mask, cursorOn);
        }
        c8 c = ScanInputChar();
        if (c == prevChar) continue;
        prevChar = c;
        if (c == 0) continue;
        if (c == ASCII_CR)  return 0;
        if (c == ASCII_ESC) return 1;
        if (c == ASCII_TAB) return 2;
        if (c == ASCII_BS) {
            if (len > 0) {
                len--; buf[len] = 0;
                DrawField(buf, len, x, y, mask, 1);
                cursorOn = 1; frame = 0;
            }
        }
        else if (c >= 0x20 && c <= 0x7E && len < MAX_INPUT) {
            buf[len++] = c; buf[len] = 0;
            DrawField(buf, len, x, y, mask, 1);
            cursorOn = 1; frame = 0;
        }
    }
}

// ── Network layer ──────────────────────────────────────────────────

// Conexión silenciosa (sin DOS_CharOutput) para Screen 5.
// Hace Net_Init + Net_Open + AUTH legacy DEADBEEF.
// Devuelve TRUE si todo OK, FALSE si fallo.
static bool NetConnectAndAuth(void) {
    u8 tcpState;
    u8 prevSt;
    u16 timeout;
    u16 maxAv;

    if (g_Online) return TRUE;

    DrawErrorLine(8, 128, "1a INIT");
    Log_Init();
    if(Net_Init() != NET_OK) { Log_Write("[MSXON] No UNAPI"); return FALSE; }
    Log_WriteHex("NI=", g_NetImplCount);

    DrawErrorLine(8, 128, "1b WAIT");
    // [FIX cuelgue 2026-06-06, confirmado en HW] tcpip_get_ipinfo() colgaba DENTRO del
    // CALSLT a la INL ObsoNET. Era codigo muerto (resultado descartado; Net_GetLocalIP
    // sin call-sites; Net_Open usa SERVER_IP). Neutralizada -> ya no cuelga. NO reactivar.
    // tcpip_get_ipinfo(&g_IpInfo);
    EnableInterrupt();
    Wait50();

    DrawErrorLine(8, 128, "1c OPEN");
    g_Conn = Net_Open(SERVER_IP, SERVER_PORT);
    Log_WriteHex("O e=", g_NetLastError);
    if(g_Conn == NET_INVALID_CONN) { Log_Write("O FAIL"); return FALSE; }

    DrawErrorLine(8, 128, "1d ESTAB");
    prevSt = 0xFE;
    timeout = 0;
    while(timeout < 500) {
        EnableInterrupt();      // [FIX INL] el wrapper UNAPI deja DI; INL procesa TCP en H.TIMI => sin EI el halt cuelga y la pila no avanza
        Halt();
        tcpState = Net_GetConnState(g_Conn);
        if(tcpState != prevSt) { Log_WriteHex("st=", tcpState); prevSt = tcpState; }
        if(tcpState == TCP_STATE_ESTABLISHED) break;
        if(tcpState == 0xFF) { Log_WriteHex("E e=", g_NetLastError); return FALSE; }
        timeout++;
    }
    if(timeout >= 500) { Log_WriteHex("E TO st=", tcpState); Log_WriteHex("cr=", (u8)g_TcpParms.close_reason); return FALSE; }
    DrawErrorLine(8, 128, "1e AUTH");
    Log_WriteHex("A st=", tcpState);
    EnableInterrupt();
    Wait50();

    g_SendBuf[0] = PROTO_MAGIC_0; g_SendBuf[1] = PROTO_MAGIC_1;
    g_SendBuf[2] = CMD_AUTH; g_SendBuf[3] = 0;
    g_SendBuf[4] = 0; g_SendBuf[5] = 4;
    g_SendBuf[6] = AUTH_TOKEN_0; g_SendBuf[7] = AUTH_TOKEN_1;
    g_SendBuf[8] = AUTH_TOKEN_2; g_SendBuf[9] = AUTH_TOKEN_3;
    Log_WriteHex("snd=", Net_Send(g_Conn, g_SendBuf, 10));

    maxAv = 0;
    timeout = 0;
    while(timeout < 500) {
        u16 avail;
        EnableInterrupt();      // [FIX INL] EI para que INL procese la recepcion en H.TIMI
        Halt();
        avail = Net_Available(g_Conn);
        if(avail > maxAv) maxAv = avail;
        if(avail >= 6) {
            u8 hdr[6];
            Net_Recv(g_Conn, hdr, 6);
            Log_WriteHex("R c=", hdr[2]);
            if(hdr[2] == CMD_AUTH_OK) { Log_Write("OK"); g_Online = TRUE; return TRUE; }
            if(hdr[2] == CMD_AUTH_FAIL) { Log_Write("A REJ"); return FALSE; }
        }
        timeout++;
    }
    Log_WriteHex("A TO av=", (u8)maxAv);
    return FALSE;
}

// Espera próximo paquete con timeout en Halts.
// Si recibe, rellena outHdr (6 bytes) y outPayload (hdr[5] bytes).
static bool NetRecvPacket(u8* outHdr, u8* outPayload, u16 timeoutHalts) {
    u16 t = 0;
    while (t < timeoutHalts) {
        EnableInterrupt();      // [FIX INL] el wrapper deja DI; INL necesita EI para procesar rx en H.TIMI
        Halt();
        if (Net_Available(g_Conn) >= 6) {
            Net_Recv(g_Conn, outHdr, 6);
            if (outHdr[0] != PROTO_MAGIC_0 || outHdr[1] != PROTO_MAGIC_1) {
                t++; continue;
            }
            if (outHdr[5] > 0) {
                while (Net_Available(g_Conn) < outHdr[5]) { EnableInterrupt(); Halt(); }
                Net_Recv(g_Conn, outPayload, outHdr[5]);
            }
            return TRUE;
        }
        t++;
    }
    return FALSE;
}

// LOGIN. Rellena g_Role, g_Nick, g_SessionId si OK.
// Retorno: 0=OK, 1..5=reason FAIL, 0xFF=timeout/red error.
static u8 NetLogin(const c8* user, const c8* pass) {
    u8 ulen = StrLen(user);
    u8 plen = StrLen(pass);
    if (ulen == 0 || plen == 0) return 0xFF;

    g_SendBuf[0] = PROTO_MAGIC_0; g_SendBuf[1] = PROTO_MAGIC_1;
    g_SendBuf[2] = CMD_LOGIN; g_SendBuf[3] = 0;
    g_SendBuf[4] = 0; g_SendBuf[5] = 1 + ulen + 1 + plen;
    u8 idx = 6;
    g_SendBuf[idx++] = ulen;
    for (u8 i = 0; i < ulen; i++) g_SendBuf[idx++] = user[i];
    g_SendBuf[idx++] = plen;
    for (u8 i = 0; i < plen; i++) g_SendBuf[idx++] = pass[i];
    Net_Send(g_Conn, g_SendBuf, idx);

    if (!NetRecvPacket(g_RecvHdr, g_RecvPl, 250)) return 0xFF;

    if (g_RecvHdr[2] == CMD_LOGIN_OK) {
        if (g_RecvHdr[5] < 6) return 0xFF;
        g_Role = g_RecvPl[0];
        u8 nlen = g_RecvPl[1];
        if (nlen > MAX_INPUT) nlen = MAX_INPUT;
        for (u8 i = 0; i < nlen; i++) g_Nick[i] = g_RecvPl[2 + i];
        g_Nick[nlen] = 0;
        u8 sidOff = 2 + g_RecvPl[1];
        g_SessionId[0] = g_RecvPl[sidOff];
        g_SessionId[1] = g_RecvPl[sidOff + 1];
        g_SessionId[2] = g_RecvPl[sidOff + 2];
        g_SessionId[3] = g_RecvPl[sidOff + 3];
        return 0;
    }
    if (g_RecvHdr[2] == CMD_LOGIN_FAIL) {
        if (g_RecvHdr[5] >= 1) return g_RecvPl[0];
    }
    return 0xFF;
}

// REGISTER. Rellena g_RegToken si OK.
// Retorno: 0=OK, 1..3=reason FAIL, 0xFF=timeout/red error.
static u8 NetRegister(const c8* user, const c8* nick) {
    u8 ulen = StrLen(user);
    u8 nlen = StrLen(nick);
    if (ulen == 0 || nlen == 0) return 0xFF;

    g_SendBuf[0] = PROTO_MAGIC_0; g_SendBuf[1] = PROTO_MAGIC_1;
    g_SendBuf[2] = CMD_REGISTER; g_SendBuf[3] = 0;
    g_SendBuf[4] = 0; g_SendBuf[5] = 1 + ulen + 1 + nlen;
    u8 idx = 6;
    g_SendBuf[idx++] = ulen;
    for (u8 i = 0; i < ulen; i++) g_SendBuf[idx++] = user[i];
    g_SendBuf[idx++] = nlen;
    for (u8 i = 0; i < nlen; i++) g_SendBuf[idx++] = nick[i];
    Net_Send(g_Conn, g_SendBuf, idx);

    if (!NetRecvPacket(g_RecvHdr, g_RecvPl, 250)) return 0xFF;

    if (g_RecvHdr[2] == CMD_REG_PENDING) {
        if (g_RecvHdr[5] < 1) return 0xFF;
        u8 tlen = g_RecvPl[0];
        if (tlen > 8) tlen = 8;
        for (u8 i = 0; i < tlen; i++) g_RegToken[i] = g_RecvPl[1 + i];
        g_RegToken[tlen] = 0;
        return 0;
    }
    if (g_RecvHdr[2] == CMD_REG_FAIL) {
        if (g_RecvHdr[5] >= 1) return g_RecvPl[0];
    }
    return 0xFF;
}

// Recuperar password: envia [ULEN][user]. Responde como REGISTER: CMD_REG_PENDING
// con token (rellena g_RegToken) o CMD_REG_FAIL con razon.
// Retorno: 0=OK, 1..5=reason FAIL, 0xFF=red error.
static u8 NetRecover(const c8* user) {
    u8 ulen = StrLen(user);
    if (ulen == 0) return 0xFF;

    g_SendBuf[0] = PROTO_MAGIC_0; g_SendBuf[1] = PROTO_MAGIC_1;
    g_SendBuf[2] = CMD_RECOVER_REQ; g_SendBuf[3] = 0;
    g_SendBuf[4] = 0; g_SendBuf[5] = 1 + ulen;
    u8 idx = 6;
    g_SendBuf[idx++] = ulen;
    for (u8 i = 0; i < ulen; i++) g_SendBuf[idx++] = user[i];
    Net_Send(g_Conn, g_SendBuf, idx);

    if (!NetRecvPacket(g_RecvHdr, g_RecvPl, 250)) return 0xFF;

    if (g_RecvHdr[2] == CMD_REG_PENDING) {
        if (g_RecvHdr[5] < 1) return 0xFF;
        u8 tlen = g_RecvPl[0];
        if (tlen > 8) tlen = 8;
        for (u8 i = 0; i < tlen; i++) g_RegToken[i] = g_RecvPl[1 + i];
        g_RegToken[tlen] = 0;
        return 0;
    }
    if (g_RecvHdr[2] == CMD_REG_FAIL) {
        if (g_RecvHdr[5] >= 1) return g_RecvPl[0];
    }
    return 0xFF;
}

// Pide CMD_GAME_LIST y rellena g_Games[]/g_GameNames/g_GameComs/g_GameFlags.
// SESSION_RESUME: usa el session_id de g_SessionId. Si el server lo acepta,
// rellena g_Role, g_Nick, g_SessionId (igual que LOGIN_OK).
// Retorno: 0=OK, 1..5=reason FAIL, 0xFF=red error.
static u8 NetSessionResume(void) {
    g_SendBuf[0] = PROTO_MAGIC_0; g_SendBuf[1] = PROTO_MAGIC_1;
    g_SendBuf[2] = CMD_SESSION_RESUME; g_SendBuf[3] = 0;
    g_SendBuf[4] = 0; g_SendBuf[5] = 4;
    g_SendBuf[6] = g_SessionId[0];
    g_SendBuf[7] = g_SessionId[1];
    g_SendBuf[8] = g_SessionId[2];
    g_SendBuf[9] = g_SessionId[3];
    Net_Send(g_Conn, g_SendBuf, 10);

    if (!NetRecvPacket(g_RecvHdr, g_RecvPl, 250)) return 0xFF;

    if (g_RecvHdr[2] == CMD_LOGIN_OK) {
        if (g_RecvHdr[5] < 6) return 0xFF;
        g_Role = g_RecvPl[0];
        u8 nlen = g_RecvPl[1];
        if (nlen > MAX_INPUT) nlen = MAX_INPUT;
        for (u8 i = 0; i < nlen; i++) g_Nick[i] = g_RecvPl[2 + i];
        g_Nick[nlen] = 0;
        u8 sidOff = 2 + g_RecvPl[1];
        g_SessionId[0] = g_RecvPl[sidOff];
        g_SessionId[1] = g_RecvPl[sidOff + 1];
        g_SessionId[2] = g_RecvPl[sidOff + 2];
        g_SessionId[3] = g_RecvPl[sidOff + 3];
        return 0;
    }
    if (g_RecvHdr[2] == CMD_LOGIN_FAIL) {
        if (g_RecvHdr[5] >= 1) return g_RecvPl[0];
    }
    return 0xFF;
}

// ── SESSION.DAT persistente ────────────────────────────────────────
// V1 (magic 0xBE, 24B): session_id 4B + role 1B + nickLen 1B + nick 16B + 1 res
// V2 (magic 0xBF, 60B): V1 + ulen 1B + user 17B + plen 1B + pass 17B
//   V2 se usa cuando el usuario marca "RECORDAR?" en el LOGIN — permite auto-
//   login persistente mas alla del TTL del session_id (que en el server es 5
//   min). Si en el web admin el usuario cambia password, el LOGIN con las
//   credenciales guardadas dara LOGIN_FAIL y el cliente borra SESSION.DAT.
#define SESSION_DAT_MAGIC_V1  0xBE
#define SESSION_DAT_MAGIC_V2  0xBF
#define SESSION_DAT_FILE      "SESSION.DAT"
#define SESSION_DAT_SIZE_V1   24
#define SESSION_DAT_SIZE_V2   60

// Credenciales rescatadas de SESSION.DAT V2 (vacias si fue V1 o no existia)
static c8   g_SavedUser[MAX_INPUT + 1];
static c8   g_SavedPass[MAX_INPUT + 1];
static bool g_HasSavedCreds;

// Si remember_user/pass son NULL o vacios → escribe V1 (sin creds).
// Si tienen contenido → escribe V2 (auto-login persistente).
static void WriteSessionDat(const c8* remember_user, const c8* remember_pass) {
    u8 buf[SESSION_DAT_SIZE_V2];
    u8 i;
    bool v2 = (remember_user && remember_user[0] && remember_pass && remember_pass[0]);
    u8 sz = v2 ? SESSION_DAT_SIZE_V2 : SESSION_DAT_SIZE_V1;
    buf[0] = v2 ? SESSION_DAT_MAGIC_V2 : SESSION_DAT_MAGIC_V1;
    buf[1] = g_SessionId[0]; buf[2] = g_SessionId[1];
    buf[3] = g_SessionId[2]; buf[4] = g_SessionId[3];
    buf[5] = g_Role;
    u8 nlen = StrLen(g_Nick); if (nlen > MAX_INPUT) nlen = MAX_INPUT;
    buf[6] = nlen;
    for (i = 0; i < nlen; i++) buf[7 + i] = g_Nick[i];
    for (i = 7 + nlen; i < SESSION_DAT_SIZE_V1; i++) buf[i] = 0;
    if (v2) {
        u8 ulen = StrLen(remember_user); if (ulen > MAX_INPUT) ulen = MAX_INPUT;
        buf[24] = ulen;
        for (i = 0; i < ulen; i++) buf[25 + i] = remember_user[i];
        for (i = 25 + ulen; i < 42; i++) buf[i] = 0;
        u8 plen = StrLen(remember_pass); if (plen > MAX_INPUT) plen = MAX_INPUT;
        buf[42] = plen;
        for (i = 0; i < plen; i++) buf[43 + i] = remember_pass[i];
        for (i = 43 + plen; i < SESSION_DAT_SIZE_V2; i++) buf[i] = 0;
    }
    u8 fh = DOS_CreateHandle(SESSION_DAT_FILE, O_WRONLY, 0x00);
    if (fh < 0xFE) {
        DOS_WriteHandle(fh, buf, sz);
        DOS_CloseHandle(fh);
    }
}

// Lee SESSION.DAT. Si magic OK, popula g_SessionId, g_Role, g_Nick. Devuelve
// TRUE. Si es V2 ademas popula g_SavedUser/g_SavedPass y pone g_HasSavedCreds.
static bool ReadSessionDat(void) {
    u8 buf[SESSION_DAT_SIZE_V2];
    g_HasSavedCreds = FALSE;
    g_SavedUser[0] = 0;
    g_SavedPass[0] = 0;
    u8 fh = DOS_OpenHandle(SESSION_DAT_FILE, O_RDONLY);
    if (fh >= 0xFE) return FALSE;
    DOS_ReadHandle(fh, buf, SESSION_DAT_SIZE_V2);
    DOS_CloseHandle(fh);
    if (buf[0] != SESSION_DAT_MAGIC_V1 && buf[0] != SESSION_DAT_MAGIC_V2) return FALSE;
    g_SessionId[0] = buf[1]; g_SessionId[1] = buf[2];
    g_SessionId[2] = buf[3]; g_SessionId[3] = buf[4];
    g_Role = buf[5];
    u8 nlen = buf[6]; if (nlen > MAX_INPUT) nlen = MAX_INPUT;
    for (u8 i = 0; i < nlen; i++) g_Nick[i] = buf[7 + i];
    g_Nick[nlen] = 0;
    if (buf[0] == SESSION_DAT_MAGIC_V2) {
        u8 ulen = buf[24]; if (ulen > MAX_INPUT) ulen = MAX_INPUT;
        for (u8 i = 0; i < ulen; i++) g_SavedUser[i] = buf[25 + i];
        g_SavedUser[ulen] = 0;
        u8 plen = buf[42]; if (plen > MAX_INPUT) plen = MAX_INPUT;
        for (u8 i = 0; i < plen; i++) g_SavedPass[i] = buf[43 + i];
        g_SavedPass[plen] = 0;
        g_HasSavedCreds = (ulen > 0 && plen > 0);
    }
    return TRUE;
}

static void DeleteSessionDat(void) {
    DOS_Delete(SESSION_DAT_FILE);
    g_HasSavedCreds = FALSE;
    g_SavedUser[0] = 0;
    g_SavedPass[0] = 0;
}

// Intenta restaurar sesion desde SESSION.DAT.
//
// Si OK: conecta, AUTH legacy, SESSION_RESUME (o LOGIN con creds guardadas),
// NetFetchGameList → devuelve TRUE (caller debe ir directo a ST_MENU).
//
// Logica:
//  1. SESSION_RESUME con el session_id guardado (rapido si <5min del ultimo
//     login).
//  2. Si SESSION_RESUME falla y SESSION.DAT es V2 (con creds), intentar LOGIN
//     normal con user+password guardados. Si OK, re-escribir SESSION.DAT con
//     el nuevo session_id manteniendo las creds. Si LOGIN tambien falla (p.ej.
//     el usuario cambio password en el web), borrar SESSION.DAT y caer al
//     flujo manual.
//  3. Si SESSION.DAT es V1 (sin creds) y SESSION_RESUME falla, borrar y caer
//     al flujo manual.
static bool TryResumeSession(void) {
    if (!ReadSessionDat())     return FALSE;
    if (!NetConnectAndAuth()) { DeleteSessionDat(); return FALSE; }
    if (NetSessionResume() == 0) {
        if (!NetFetchGameList()) { DeleteSessionDat(); return FALSE; }
        return TRUE;
    }
    // SESSION_RESUME fallo. Fallback a LOGIN si tenemos creds (V2).
    if (g_HasSavedCreds) {
        // Copiamos a g_BufUser/g_BufPass porque WriteSessionDat los va a leer
        // de estas variables. NetLogin tambien las usa indirectamente al
        // poblar g_Role/g_Nick/g_SessionId.
        u8 i;
        for (i = 0; g_SavedUser[i] && i < MAX_INPUT; i++) g_BufUser[i] = g_SavedUser[i];
        g_BufUser[i] = 0;
        for (i = 0; g_SavedPass[i] && i < MAX_INPUT; i++) g_BufPass[i] = g_SavedPass[i];
        g_BufPass[i] = 0;
        u8 r = NetLogin(g_SavedUser, g_SavedPass);
        if (r == 0) {
            // LOGIN_OK: persistir el NUEVO session_id pero manteniendo creds.
            WriteSessionDat(g_SavedUser, g_SavedPass);
            if (!NetFetchGameList()) { DeleteSessionDat(); return FALSE; }
            return TRUE;
        }
    }
    DeleteSessionDat();
    return FALSE;
}

// Payload server: [N][gameId, flags, max, proto, comLen, com, nameLen, name] x N
// Devuelve TRUE si OK (g_NumGames actualizado), FALSE si red error.
static bool NetFetchGameList(void) {
    g_SendBuf[0] = PROTO_MAGIC_0; g_SendBuf[1] = PROTO_MAGIC_1;
    g_SendBuf[2] = CMD_GAME_LIST; g_SendBuf[3] = 0;
    g_SendBuf[4] = 0; g_SendBuf[5] = 0;
    Net_Send(g_Conn, g_SendBuf, 6);

    if (!NetRecvPacket(g_RecvHdr, g_RecvPl, 250)) return FALSE;
    if (g_RecvHdr[2] != CMD_GAME_LIST || g_RecvHdr[5] < 1) return FALSE;

    u8 n = g_RecvPl[0];
    if (n > MAX_GAMES) n = MAX_GAMES;
    u8 off = 1;
    u8 count = 0;
    u8 plen = g_RecvHdr[5];
    for (u8 i = 0; i < n; i++) {
        if (off + 5 > plen) break;
        u8 id     = g_RecvPl[off++];
        u8 flags  = g_RecvPl[off++];
        u8 maxP   = g_RecvPl[off++];
        u8 proto  = g_RecvPl[off++];
        u8 comLen = g_RecvPl[off++];
        if (off + comLen > plen || comLen > GAME_COM_MAX) break;
        for (u8 j = 0; j < comLen; j++) g_GameComs[count][j] = g_RecvPl[off + j];
        g_GameComs[count][comLen] = 0;
        off += comLen;
        if (off + 1 > plen) break;
        u8 nameLen = g_RecvPl[off++];
        if (off + nameLen > plen || nameLen > GAME_NAME_MAX) break;
        for (u8 j = 0; j < nameLen; j++) g_GameNames[count][j] = g_RecvPl[off + j];
        g_GameNames[count][nameLen] = 0;
        off += nameLen;

        g_Games[count].name         = g_GameNames[count];
        g_Games[count].comFile      = g_GameComs[count];
        g_Games[count].gameId       = id;
        g_Games[count].maxPlayers   = maxP;
        g_Games[count].protoVersion = proto;
        g_GameFlags[count]          = flags;
        count++;
    }
    g_NumGames = count;
    return TRUE;
}

// ── Room operations (post-login) ───────────────────────────────────

static void SendRoomList(void) {
    g_SendBuf[0]=PROTO_MAGIC_0; g_SendBuf[1]=PROTO_MAGIC_1;
    g_SendBuf[2]=CMD_ROOM_LIST; g_SendBuf[3]=0;
    g_SendBuf[4]=0; g_SendBuf[5]=0;
    Net_Send(g_Conn, g_SendBuf, 6);
}

static void SendCreateRoom(void) {
    g_SendBuf[0]=PROTO_MAGIC_0; g_SendBuf[1]=PROTO_MAGIC_1;
    g_SendBuf[2]=CMD_ROOM_CREATE; g_SendBuf[3]=0;
    g_SendBuf[4]=0; g_SendBuf[5]=3;
    g_SendBuf[6]=g_CurGame->gameId;
    g_SendBuf[7]=g_CurGame->maxPlayers;
    g_SendBuf[8]=g_CurGame->protoVersion;
    Net_Send(g_Conn, g_SendBuf, 9);
}

static void SendJoinRoom(u8 roomId) {
    g_SendBuf[0]=PROTO_MAGIC_0; g_SendBuf[1]=PROTO_MAGIC_1;
    g_SendBuf[2]=CMD_ROOM_JOIN; g_SendBuf[3]=0;
    g_SendBuf[4]=0; g_SendBuf[5]=1; g_SendBuf[6]=roomId;
    Net_Send(g_Conn, g_SendBuf, 7);
}

static void SendPing(void) {
    g_SendBuf[0]=PROTO_MAGIC_0; g_SendBuf[1]=PROTO_MAGIC_1;
    g_SendBuf[2]=CMD_PING; g_SendBuf[3]=g_RoomId;
    g_SendBuf[4]=g_MyPid; g_SendBuf[5]=0;
    Net_Send(g_Conn, g_SendBuf, 6);
}

// ── Chat helpers ───────────────────────────────────────────────────

// Inserta una linea visual ya formateada en el buffer circular (scroll si lleno).
static void chatPushVisualLine(const c8* line) {
    if (g_ChatCount == CHAT_HIST_ROWS) {
        u8 i, j;
        for (i = 0; i < CHAT_HIST_ROWS - 1; i++)
            for (j = 0; j < CHAT_LINE_BUF; j++) g_ChatLines[i][j] = g_ChatLines[i+1][j];
        g_ChatCount--;
    }
    u8 idx = g_ChatCount;
    u8 p = 0;
    while (line[p] && p < CHAT_LINE_BUF - 1) {
        g_ChatLines[idx][p] = line[p];
        p++;
    }
    g_ChatLines[idx][p] = 0;
    g_ChatCount++;
}

// Anyade un mensaje al historial. Si "[nick] msg" excede CHAT_LINE_VISUAL_MAX,
// lo parte en 2 entradas (la segunda sin prefijo, sangrada 2 espacios) — el
// scroll automatico del buffer ya hace el desplazamiento de 2 filas.
static void ChatAddLine(const u8* nick, u8 nlen, const u8* msg, u8 mlen) {
    c8 buf[CHAT_LINE_BUF];
    u8 p = 0, k;
    // "[nick] " prefix
    buf[p++] = '[';
    for (k = 0; k < nlen && p < 20; k++) buf[p++] = nick[k];
    buf[p++] = ']';
    buf[p++] = ' ';
    u8 prefixLen = p;
    u8 firstMax = CHAT_LINE_VISUAL_MAX - prefixLen;
    u8 first = (mlen <= firstMax) ? mlen : firstMax;
    for (k = 0; k < first && p < CHAT_LINE_BUF - 1; k++) buf[p++] = msg[k];
    buf[p] = 0;
    chatPushVisualLine(buf);
    if (mlen > firstMax) {
        // Continuacion en una segunda linea, sangrada 2 espacios.
        p = 0;
        buf[p++] = ' ';
        buf[p++] = ' ';
        for (k = firstMax; k < mlen && p < CHAT_LINE_BUF - 1; k++) buf[p++] = msg[k];
        buf[p] = 0;
        chatPushVisualLine(buf);
    }
    g_ChatDirty = 1;
}

static void SendChatMsg(const c8* buf, u8 len) {
    if (len == 0 || len > CHAT_MAX_MSG) return;
    g_SendBuf[0]=PROTO_MAGIC_0; g_SendBuf[1]=PROTO_MAGIC_1;
    g_SendBuf[2]=CMD_CHAT_SEND; g_SendBuf[3]=0;
    g_SendBuf[4]=0; g_SendBuf[5]=1 + len;
    g_SendBuf[6]=len;
    u8 i; for (i = 0; i < len; i++) g_SendBuf[7 + i] = buf[i];
    Net_Send(g_Conn, g_SendBuf, 7 + len);
}

static void SendPlayerListReq(void) {
    g_SendBuf[0]=PROTO_MAGIC_0; g_SendBuf[1]=PROTO_MAGIC_1;
    g_SendBuf[2]=CMD_PLAYER_LIST; g_SendBuf[3]=0;
    g_SendBuf[4]=0; g_SendBuf[5]=0;
    Net_Send(g_Conn, g_SendBuf, 6);
}

static void SendChatHistoryReq(void) {
    g_SendBuf[0]=PROTO_MAGIC_0; g_SendBuf[1]=PROTO_MAGIC_1;
    g_SendBuf[2]=CMD_CHAT_HISTORY; g_SendBuf[3]=0;
    g_SendBuf[4]=0; g_SendBuf[5]=0;
    Net_Send(g_Conn, g_SendBuf, 6);
}

static void SendGameStart(void) {
    g_SendBuf[0]=PROTO_MAGIC_0; g_SendBuf[1]=PROTO_MAGIC_1;
    g_SendBuf[2]=CMD_GAME_START; g_SendBuf[3]=g_RoomId;
    g_SendBuf[4]=g_MyPid; g_SendBuf[5]=0;
    Net_Send(g_Conn, g_SendBuf, 6);
}

// Pone en marcha el countdown de 3s para auto-arrancar.
//  - AGGREGATE (burdyn MMO): siempre auto, no exige sala llena.
//  - RELAY con sala llena y P1 = BOT: auto 3s (BOT no pulsa nada).
//  - Resto: countdown=0, espera ENTER/S del host humano P1.
static void StartBotCountdownIfNeeded(void) {
    if (g_CurGame->protoVersion >= 0x02) { g_BotCountdown = 180; return; }
    u8 cnt = 0, b;
    for (b = 0; b < g_CurGame->maxPlayers; b++)
        if (g_Active & (1 << b)) cnt++;
    if (cnt < g_CurGame->maxPlayers) { g_BotCountdown = 0; return; }
    const c8* p1 = g_PlayerNicks[0];
    if (p1[0] == 'B' && p1[1] == 'O' && p1[2] == 'T' && p1[3] == 0)
        g_BotCountdown = 180;
    else
        g_BotCountdown = 0;
}

// ── Packet processing ──────────────────────────────────────────────

static void ProcessPacket(u8 cmd, u8* pl, u8 len) {
    if(cmd == CMD_ROOM_LIST && len >= 1) {
        u8 cnt = pl[0], i;
        g_LBN = 0;
        for(i = 0; i < cnt && i < LB_MAX; i++) {
            u8 off = 1 + i * 3;
            if(pl[off + 1] == g_CurGame->gameId) {
                g_LB[g_LBN].rid = pl[off];
                g_LB[g_LBN].gid = pl[off + 1];
                g_LB[g_LBN].np = pl[off + 2];
                g_LBN++;
            }
        }
        g_LBC = 0;
        // Esperar a que el ENTER del menú se suelte antes de entrar al lobby
        while (Keyboard_IsKeyPressed(KEY_RET)) Halt();
        // Debounce extra: 60 frames (~1.2s) antes de procesar input del lobby.
        // Garantiza que el usuario VE las salas antes de que un rebote del
        // teclado dispare un JOIN inmediato.
        g_KeyDly = 60;
        g_State = ST_LOBBY;
    }
    else if(cmd == CMD_ROOM_INFO && len >= 4) {
        g_RoomId = pl[0];
        u8 numP = pl[2];
        g_MyPid = pl[3];
        g_Active = 0;
        { u8 j; for(j = 0; j < numP; j++) g_Active |= (1 << j); }
        // Pedir lista de nicks de la sala y ESPERAR la respuesta antes de
        // continuar — si entramos a ST_LAUNCHING sin nicks, WriteLobbyDat
        // copia basura.
        SendPlayerListReq();
        {
            u8 waitFrames = 0;
            bool gotList = FALSE;
            while (waitFrames < 30 && !gotList) {
                Halt();
                u16 av = Net_Available(g_Conn);
                if (av >= 6) {
                    u8 hdr2[6], pl2[200];
                    Net_Recv(g_Conn, hdr2, 6);
                    if (hdr2[0] == PROTO_MAGIC_0 && hdr2[1] == PROTO_MAGIC_1) {
                        if (hdr2[5] > 0) {
                            while (Net_Available(g_Conn) < hdr2[5]) Halt();
                            Net_Recv(g_Conn, pl2, hdr2[5]);
                        }
                        if (hdr2[2] == CMD_PLAYER_LIST) {
                            u8 n = pl2[0];
                            u8 off2 = 1, i2, k2;
                            for(i2 = 0; i2 < MSXON_MAX_NICKS; i2++) g_PlayerNicks[i2][0] = 0;
                            for(i2 = 0; i2 < n && off2 + 2 <= hdr2[5]; i2++) {
                                u8 pid_i = pl2[off2++];
                                u8 nlen2 = pl2[off2++];
                                if(nlen2 > MSXON_NICK_LEN) nlen2 = MSXON_NICK_LEN;
                                if(off2 + nlen2 > hdr2[5]) break;
                                if(pid_i >= 1 && pid_i <= MSXON_MAX_NICKS) {
                                    for(k2 = 0; k2 < nlen2; k2++) g_PlayerNicks[pid_i - 1][k2] = pl2[off2 + k2];
                                    g_PlayerNicks[pid_i - 1][nlen2] = 0;
                                }
                                off2 += nlen2;
                            }
                            gotList = TRUE;
                        }
                        else {
                            // Otros packets durante el wait: procesar normal
                            // (p.ej. GAME_START que el server envia tras
                            // ROOM_INFO en salas AGGREGATE con partida en curso).
                            ProcessPacket(hdr2[2], pl2, hdr2[5]);
                        }
                    }
                }
                waitFrames++;
            }
        }
        // Esperar a que el ENTER del lobby se suelte antes de seguir
        while (Keyboard_IsKeyPressed(KEY_RET)) Halt();
        // Si durante el wait inline ya llego un GAME_START (caso AGGREGATE
        // con partida en curso), g_State ya esta en ST_LAUNCHING — no pisarlo.
        if (g_State != ST_LAUNCHING) {
            // Siempre pasamos por la pantalla de lobby con nicks (ST_WAITING).
            // El handler ST_WAITING decide:
            //  - Sala llena + P1 = BOT (o AGGREGATE) → countdown 3s y lanza.
            //  - Sala llena + P1 humano → host pulsa ENTER/S, resto espera.
            //  - Sala no llena → como siempre: host pulsa ENTER/S cuando quiera.
            g_State = ST_WAITING;
            g_WaitDrawn = 0;
            StartBotCountdownIfNeeded();
        }
    }
    else if(cmd == CMD_PLAYER_JOINED && len >= 1) {
        u8 jp = pl[0];
        if(jp >= 1 && jp <= 16) g_Active |= (1 << (jp - 1));
        // Refrescar nicks (alguien nuevo en la sala). Cuando llegue la lista
        // se forzara repintado y, si toca, se activara el countdown del BOT.
        SendPlayerListReq();
    }
    else if(cmd == CMD_PLAYER_LEFT && len >= 1) {
        u8 lp = pl[0];
        if(lp >= 1 && lp <= 16) g_Active &= ~(1 << (lp - 1));
        if(lp >= 1 && lp <= MSXON_MAX_NICKS) g_PlayerNicks[lp - 1][0] = 0;
        // Si alguien se va y estabamos en countdown, parar y reevaluar.
        if (g_BotCountdown > 0) {
            g_BotCountdown = 0;
            g_WaitDrawn = 0;
        }
    }
    else if(cmd == CMD_PLAYER_LIST && len >= 1) {
        u8 n = pl[0];
        u8 off = 1, i, k;
        // Limpiar todos los nicks (los rellenamos con lo que llega)
        for(i = 0; i < MSXON_MAX_NICKS; i++) g_PlayerNicks[i][0] = 0;
        for(i = 0; i < n && off + 2 <= len; i++) {
            u8 pid_i = pl[off++];
            u8 nlen  = pl[off++];
            if(nlen > MSXON_NICK_LEN) nlen = MSXON_NICK_LEN;
            if(off + nlen > len) break;
            if(pid_i >= 1 && pid_i <= MSXON_MAX_NICKS) {
                for(k = 0; k < nlen; k++) g_PlayerNicks[pid_i - 1][k] = pl[off + k];
                g_PlayerNicks[pid_i - 1][nlen] = 0;
            }
            off += nlen;
        }
        // Forzar repintado del ST_WAITING para que muestre los nicks frescos.
        g_WaitDrawn = 0;
        // Recalcular countdown: justo ahora puede haberse llenado la sala
        // o entrado un BOT como P1. Si countdown ya estaba corriendo y siguen
        // las mismas condiciones, no lo reiniciamos.
        if (g_State == ST_WAITING && g_BotCountdown == 0) {
            StartBotCountdownIfNeeded();
        }
    }
    else if(cmd == CMD_GAME_START) {
        // Si el host humano YA estaba mostrando la pantalla de waiting y
        // pulso ENTER, este GAME_START es la senal directa para lanzar.
        if (g_State == ST_WAITING) {
            g_State = ST_LAUNCHING;
        }
        else {
            // Server lo disparo automaticamente durante el JOIN
            //   - AGGREGATE (burdyn): sala "siempre abierta"
            //   - RELAY con maxPlayers>=3 (parchis/tetris): sala llena al unirse
            // Mostramos la nueva pantalla con nicks y countdown 3s antes de lanzar.
            g_State = ST_WAITING;
            g_WaitDrawn = 0;
            g_BotCountdown = 180;
        }
    }
    else if(cmd == CMD_ROOM_FULL) {
        // Sala llena o partida en curso (server rechaza JOIN si gameStarted
        // en modo RELAY). Volver al lobby con mensaje y auto-refresh.
        g_LobbyMsg = "SALA OCUPADA O EN PARTIDA";
        g_LobbyDrawn = 0;
        SendRoomList();
        g_State = ST_CONNECTING;
    }
    else if(cmd == CMD_ROOM_NOT_FOUND) {
        g_LobbyMsg = "LA SALA YA NO EXISTE";
        g_LobbyDrawn = 0;
        SendRoomList();
        g_State = ST_CONNECTING;
    }
    else if(cmd == CMD_CHAT_RECV && len >= 2) {
        u8 nlen = pl[0];
        if (1 + nlen + 1 <= len) {
            u8 mlen = pl[1 + nlen];
            if (1 + nlen + 1 + mlen <= len) {
                ChatAddLine(pl + 1, nlen, pl + 2 + nlen, mlen);
            }
        }
    }
}

static void Poll(void) {
    u16 avail;
    u8 hdr[6];
    u8 payload[200];
    u8 maxPkts;

    if(g_Conn == NET_INVALID_CONN) return;

    maxPkts = 4;
    while(maxPkts--) {
        avail = Net_Available(g_Conn);
        if(avail < 6) break;
        Net_Recv(g_Conn, hdr, 6);
        if(hdr[0] != PROTO_MAGIC_0 || hdr[1] != PROTO_MAGIC_1) break;
        if(hdr[5] > 0) {
            avail = Net_Available(g_Conn);
            if(avail < hdr[5]) break;
            Net_Recv(g_Conn, payload, hdr[5]);
        }
        ProcessPacket(hdr[2], payload, hdr[5]);
    }

    g_PingT++;
    if(g_PingT >= PING_INT) { g_PingT = 0; SendPing(); }
}

// Vacía paquetes residuales antes de cambiar modo VDP
static void DrainPackets(void) {
    while (Net_Available(g_Conn) >= 6) {
        u8 hdr[6], pl[200];
        Net_Recv(g_Conn, hdr, 6);
        if (hdr[5] > 0) {
            while (Net_Available(g_Conn) < hdr[5]) Halt();
            Net_Recv(g_Conn, pl, hdr[5]);
        }
    }
}

// ── LOBBY.DAT v2 (magic 0xAB, 8 bytes header + 16*17 bytes tabla nicks) ────
// Tabla de nicks indexada por PID-1: [NLEN(1)][nick(16)] x 16 entradas.
// Juegos que solo soportan v1 (magic 0xAA) leerian magic 0xAB y harian fail
// graceful; los que ya entienden v2 (lobby_client.h moderno) leen los nicks.

static void WriteLobbyDat(void) {
    u8 data[8];
    u8 entry[1 + 16];
    u8 fh;
    u8 i, k;
    data[0] = 0xAB;   // magic v2
    data[1] = (u8)g_Conn;
    data[2] = g_MyPid;
    data[3] = g_RoomId;
    data[4] = g_Active;
    data[5] = g_CurGame->gameId;
    data[6] = g_CurGame->protoVersion;
    data[7] = 0x00;
    fh = DOS_CreateHandle("LOBBY.DAT", O_WRONLY, 0x00);
    if(fh >= 0xFE) return;
    DOS_WriteHandle(fh, data, 8);
    // Tabla de nicks (16 slots, padding cero si vacio)
    for(i = 0; i < 16; i++) {
        u8 nlen = 0;
        if(g_PlayerNicks[i][0]) {
            while(nlen < 16 && g_PlayerNicks[i][nlen]) nlen++;
        }
        entry[0] = nlen;
        for(k = 0; k < 16; k++) entry[1 + k] = (k < nlen) ? g_PlayerNicks[i][k] : 0;
        DOS_WriteHandle(fh, entry, 17);
    }
    DOS_CloseHandle(fh);
}

// ── Keyboard-buffer stuffing ───────────────────────────────────────
// Escribe un comando al buffer del teclado MSX (0xFBF0..0xFC0F) y
// resetea PUTPNT/GETPNT (0xF3F8/0xF3FA). Cuando MSXON termina con
// Bios_Exit(0), el shell MSX-DOS al recuperar control lee del buffer
// y ejecuta el comando — sin _LAUNCH.BAT, sin autoexec.bat trampolín.
static void StuffCommand(const c8* cmd) {
    u8* buf = (u8*)0xFBF0;
    u8 i = 0;
    while (cmd[i] && i < 30) { buf[i] = cmd[i]; i++; }
    buf[i++] = 0x0D;   // Enter
    *((u16*)0xF3FA) = (u16)buf;          // GETPNT = inicio
    *((u16*)0xF3F8) = (u16)buf + i;      // PUTPNT = fin
}

static void LaunchGame(bool online) {
    if(online) WriteLobbyDat();
    Log_Close();
    StuffCommand(g_CurGame->comFile);
    Bios_Exit(0);
}

// ── Screen 0 draws ─────────────────────────────────────────────────

// ── Screen 5 menus (post-login) ────────────────────────────────────

#define LINE_H  10        // separación vertical entre líneas (pixels)
#define ROW_Y(n) (16 + (n) * LINE_H)

// Repinta solo los 2 caracteres del cursor "> " / "  " sin tocar el resto
// de la linea. Usa HMMV para borrar (la fuente bitmap no pinta el fondo).
static void DrawCursorAt(u16 x, u16 y, u8 selected) {
    VDP_CommandHMMV(x, y, FONT_W * 2, 8, 0x33);
    VDP_CommandWait();
    Print_SetColor(0x22, 0x33);
    Print_SetPosition(x, y);
    Print_DrawText(selected ? "> " : "  ");
}

static void MoveMenuCursor(u8 oldSel, u8 newSel) {
    if (oldSel == newSel) return;
    DrawCursorAt(8, ROW_Y(4 + oldSel), 0);
    DrawCursorAt(8, ROW_Y(4 + newSel), 1);
}

static void MoveLobbyCursor(u8 oldSel, u8 newSel) {
    if (oldSel == newSel) return;
    DrawCursorAt(8, ROW_Y(4 + oldSel), 0);
    DrawCursorAt(8, ROW_Y(4 + newSel), 1);
}

static void DrawMenu(void) {
    EnterBBSMode();

    Print_SetColor(0x11, 0x33);   // header dim green
    Print_SetPosition(96, ROW_Y(0));
    Print_DrawText("MSXon");
    Print_SetPosition(8, ROW_Y(1));
    Print_DrawText("------------------------------------------");
    Print_SetColor(0x22, 0x33);
    Print_SetPosition(8, ROW_Y(2));
    Print_DrawText("USER: ");
    Print_DrawText(g_Nick);

    if (g_NumGames == 0) {
        Print_SetColor(0x44, 0x33);   // rojo
        Print_SetPosition(8, ROW_Y(5));
        Print_DrawText("Sin juegos disponibles.");
        Print_SetColor(0x11, 0x33);
        Print_SetPosition(8, ROW_Y(18));
        Print_DrawText("[ESC] salir a DOS");
        return;
    }

    Print_SetColor(0x22, 0x33);
    u8 i;
    for (i = 0; i < g_NumGames; i++) {
        u16 y = ROW_Y(4 + i);
        Print_SetPosition(8, y);
        Print_DrawText(i == g_SelGame ? "> " : "  ");
        c8 num[2] = { '0' + (i + 1), 0 };
        Print_DrawText(num);
        Print_DrawText(". ");
        Print_DrawText(g_Games[i].name);
        if (g_GameFlags[i] & 0x01) Print_DrawText(" [P]");
    }
    Print_SetColor(0x11, 0x33);
    Print_SetPosition(8, ROW_Y(17));
    Print_DrawText("Cursores+ENTER  [C] chat  [ESC] salir");
}

static void DrawLobby(void) {
    EnterBBSMode();

    Print_SetColor(0x11, 0x33);
    Print_SetPosition(8, ROW_Y(0));
    Print_DrawText("Salas de ");
    Print_DrawText(g_CurGame->name);
    Print_SetPosition(8, ROW_Y(1));
    Print_DrawText("------------------------------------------");

    if (g_LobbyMsg) {
        Print_SetColor(0x88, 0x33); // rojo
        Print_SetPosition(8, ROW_Y(2));
        Print_DrawText(g_LobbyMsg);
        g_LobbyMsg = 0; // one-shot
        Print_SetColor(0x11, 0x33);
    }

    if (g_LBN == 0) {
        Print_SetColor(0x22, 0x33);
        Print_SetPosition(8, ROW_Y(4));
        Print_DrawText("No hay salas disponibles.");
        Print_SetColor(0x11, 0x33);
        Print_SetPosition(8, ROW_Y(17));
        Print_DrawText("[C] crear  [R] refrescar  [ESC] volver");
    } else {
        Print_SetColor(0x22, 0x33);
        Print_SetPosition(8, ROW_Y(3));
        Print_DrawText("SALA    JUGADORES");
        u8 i;
        for (i = 0; i < g_LBN; i++) {
            u16 y = ROW_Y(4 + i);
            Print_SetPosition(8, y);
            Print_DrawText(i == g_LBC ? "> " : "  ");
            PrintAtNum(8 + 2 * FONT_W, y, g_LB[i].rid);
            Print_SetPosition(8 + 8 * FONT_W, y);
            PrintAtNum(8 + 8 * FONT_W, y, g_LB[i].np);
            Print_SetPosition(8 + 11 * FONT_W, y);
            Print_DrawText("/");
            PrintAtNum(8 + 12 * FONT_W, y, g_CurGame->maxPlayers);
        }
        Print_SetColor(0x11, 0x33);
        Print_SetPosition(8, ROW_Y(17));
        Print_DrawText("[ENTER] unir  [C] crear  [R] refr");
        Print_SetPosition(8, ROW_Y(18));
        Print_DrawText("[ESC] volver al menu");
    }
}

// ── Chat UI ────────────────────────────────────────────────────────

// Reparte mensajes pegados al fondo de la zona de chat (filas ROW_Y(2) -> ROW_Y(13)).
static void DrawChatHistory(void) {
    // Limpiar zona de chat
    VDP_CommandHMMV(8, ROW_Y(2), 240, LINE_H * CHAT_HIST_ROWS, 0x33);
    VDP_CommandWait();
    Print_SetColor(0x22, 0x33);
    u8 startRow = CHAT_HIST_ROWS - g_ChatCount;
    u8 i;
    for (i = 0; i < g_ChatCount; i++) {
        Print_SetPosition(8, ROW_Y(2 + startRow + i));
        Print_DrawText(g_ChatLines[i]);
    }
}

// Input en 2 lineas (ROW_Y(15) y ROW_Y(16)) para soportar hasta 64 chars sin
// que el texto se salga del ancho de pantalla. Linea 1 tiene "> " + 36 chars,
// linea 2 tiene 28 chars alineados a x=8. Solo se pintan/borran chars delta
// para evitar flicker de repintar la linea entera cada frame.
#define CHAT_INPUT_Y1    ROW_Y(15)
#define CHAT_INPUT_Y2    ROW_Y(16)
#define CHAT_INPUT_X0    (8 + 2 * FONT_W)   // tras "> " en linea 1

static void chatPosToXY(u8 pos, u16* x, u16* y) {
    if (pos < CHAT_CHARS_PER_LINE) {
        *x = CHAT_INPUT_X0 + pos * FONT_W;
        *y = CHAT_INPUT_Y1;
    } else {
        *x = 8 + (pos - CHAT_CHARS_PER_LINE) * FONT_W;
        *y = CHAT_INPUT_Y2;
    }
}

static void InitChatInputRow(void) {
    VDP_CommandHMMV(8, CHAT_INPUT_Y1, 240, 8, 0x33);
    VDP_CommandWait();
    VDP_CommandHMMV(8, CHAT_INPUT_Y2, 240, 8, 0x33);
    VDP_CommandWait();
    Print_SetColor(0x22, 0x33);
    Print_SetPosition(8, CHAT_INPUT_Y1);
    Print_DrawText("> ");
}

static void DrawChatChar(u8 pos, c8 c) {
    u16 x, y;
    chatPosToXY(pos, &x, &y);
    Print_SetColor(0x22, 0x33);
    Print_SetPosition(x, y);
    Print_DrawChar(c);
}

static void ClearChatChar(u8 pos) {
    u16 x, y;
    chatPosToXY(pos, &x, &y);
    VDP_CommandHMMV(x, y, FONT_W, 8, 0x33);
    VDP_CommandWait();
}

// Bucle principal del chat. Bloquea hasta ESC -> ST_MENU.
static void RunChat(void) {
    EnterBBSMode();
    Print_SetColor(0x11, 0x33);
    Print_SetPosition(8, ROW_Y(0));
    Print_DrawText("MSXon CHAT");
    Print_SetPosition(8, ROW_Y(1));
    Print_DrawText("------------------------------------------");
    Print_SetPosition(8, ROW_Y(14));
    Print_DrawText("------------------------------------------");
    Print_SetPosition(8, ROW_Y(18));
    Print_DrawText("ENTER manda   ESC sale");

    // Pedir historial al server.
    SendChatHistoryReq();

    g_ChatInput[0] = 0;
    u8 inputLen = 0;
    c8 prevChar = 0xFF;
    u8 frame = 0, cursorOn = 1;
    g_ChatDirty = 1;
    InitChatInputRow();
    DrawChatChar(0, '_'); // cursor inicial

    // Esperar a que se suelte la C (sino disparariamos ENTER por edge).
    while (Keyboard_IsKeyPressed(KEY_C)) Halt();

    while (1) {
        Halt();

        // Recoger paquetes (CHAT_RECV llega aqui).
        {
            u16 av = Net_Available(g_Conn);
            while (av >= 6) {
                u8 hdr2[6], pl2[200];
                Net_Recv(g_Conn, hdr2, 6);
                if (hdr2[0] == PROTO_MAGIC_0 && hdr2[1] == PROTO_MAGIC_1) {
                    if (hdr2[5] > 0) {
                        while (Net_Available(g_Conn) < hdr2[5]) Halt();
                        Net_Recv(g_Conn, pl2, hdr2[5]);
                    }
                    ProcessPacket(hdr2[2], pl2, hdr2[5]);
                }
                av = Net_Available(g_Conn);
            }
        }

        if (g_ChatDirty) {
            DrawChatHistory();
            g_ChatDirty = 0;
        }

        // Cursor parpadeante: toggle solo el caracter en posicion `inputLen`.
        frame++;
        if ((frame & 0x0F) == 0) {
            cursorOn = !cursorOn;
            if (inputLen < CHAT_MAX_MSG) {
                if (cursorOn) DrawChatChar(inputLen, '_');
                else          ClearChatChar(inputLen);
            }
        }

        c8 c = ScanInputChar();
        if (c == prevChar) continue;
        prevChar = c;
        if (c == 0) continue;
        if (c == ASCII_ESC) {
            g_State = ST_MENU;
            return;
        }
        if (c == ASCII_CR) {
            if (inputLen > 0) {
                SendChatMsg(g_ChatInput, inputLen);
                // Limpiar visualmente todo el input.
                u8 k;
                for (k = 0; k <= inputLen; k++) ClearChatChar(k);
                inputLen = 0;
                g_ChatInput[0] = 0;
                DrawChatChar(0, '_');
                cursorOn = 1; frame = 0;
            }
            continue;
        }
        if (c == ASCII_BS) {
            if (inputLen > 0) {
                ClearChatChar(inputLen);   // borrar cursor antiguo
                inputLen--;
                g_ChatInput[inputLen] = 0;
                ClearChatChar(inputLen);   // borrar el caracter
                DrawChatChar(inputLen, '_');
                cursorOn = 1; frame = 0;
            }
            continue;
        }
        if (c >= 0x20 && c <= 0x7E && inputLen < CHAT_MAX_MSG) {
            ClearChatChar(inputLen);  // borrar cursor en pos actual
            DrawChatChar(inputLen, c);
            inputLen++;
            g_ChatInput[inputLen - 1] = c;
            g_ChatInput[inputLen] = 0;
            if (inputLen < CHAT_MAX_MSG) DrawChatChar(inputLen, '_');
            cursorOn = 1; frame = 0;
        }
    }
}

static void DrawWaitingFooter(void); // forward decl

static void DrawWaiting(void) {
    EnterBBSMode();

    Print_SetColor(0x11, 0x33);
    Print_SetPosition(8, ROW_Y(0));
    Print_DrawText("Sala ");
    PrintAtNum(8 + 5 * FONT_W, ROW_Y(0), g_RoomId);
    Print_SetPosition(8 + 9 * FONT_W, ROW_Y(0));
    Print_DrawText(" - ");
    Print_DrawText(g_CurGame->name);
    Print_SetPosition(8, ROW_Y(1));
    Print_DrawText("------------------------------------------");

    Print_SetColor(0x22, 0x33);
    Print_SetPosition(8, ROW_Y(3));
    Print_DrawText("Tu eres ");
    {
        const c8* myNick = (g_MyPid >= 1 && g_MyPid <= MSXON_MAX_NICKS && g_PlayerNicks[g_MyPid - 1][0])
                           ? g_PlayerNicks[g_MyPid - 1]
                           : "?";
        Print_DrawText(myNick);
    }

    Print_SetPosition(8, ROW_Y(5));
    Print_DrawText("Jugadores conectados:");
    u8 i;
    for (i = 0; i < g_CurGame->maxPlayers; i++) {
        u16 y = ROW_Y(6 + i);
        Print_SetPosition(16, y);
        Print_DrawText("P");
        PrintAtNum(16 + FONT_W, y, i + 1);
        Print_SetPosition(16 + 4 * FONT_W, y);
        if (g_Active & (1 << i)) {
            // Mostrar el nick si lo tenemos; si no, fallback "OK".
            const c8* nick = (i < MSXON_MAX_NICKS && g_PlayerNicks[i][0])
                             ? g_PlayerNicks[i]
                             : "OK";
            Print_DrawText(nick);
        } else {
            Print_DrawText("--");
        }
    }

    DrawWaitingFooter();
}

// Redibuja solo la linea del footer (status del countdown / host / espera).
// Se llama desde DrawWaiting (lo incluye al final) y tambien cada vez que
// cambia g_BotCountdown sin necesidad de borrar y repintar toda la pantalla.
static void DrawWaitingFooter(void) {
    Print_SetColor(0x11, 0x33);
    // Borrar primero la linea entera con espacios (el ancho real visible
    // depende del font, 42 chars cubren sobradamente la pantalla en BBS).
    Print_SetPosition(8, ROW_Y(17));
    {
        u8 i;
        for (i = 0; i < 42; i++) Print_DrawChar(' ');
    }
    Print_SetPosition(8, ROW_Y(17));
    if (g_BotCountdown > 0) {
        u8 secs = (u8)((g_BotCountdown + 59) / 60);
        Print_DrawText("Empezando en ");
        PrintAtNum(8 + 13 * FONT_W, ROW_Y(17), secs);
        Print_SetPosition(8 + 15 * FONT_W, ROW_Y(17));
        Print_DrawText("s...  [ESC] salir");
    } else if (g_MyPid == 1) {
        Print_DrawText("[ENTER] empezar  [ESC] salir");
    } else {
        Print_DrawText("Esperando al host...  [ESC] salir");
    }
}

// ── Run* — Screen 5 flow ───────────────────────────────────────────

static void RunIntro(void)
{
    VDP_SetMode(VDP_MODE_SCREEN5);
    VDP_EnableDisplay(FALSE);
    VDP_DisableSprite();

    VDP_SetPaletteEntry(IDX_WHITE, PAL_WHITE);
    VDP_SetPaletteEntry(IDX_BLACK, PAL_WHITE);
    VDP_SetPaletteEntry(IDX_BLUE,  PAL_WHITE);
    VDP_SetColor(IDX_WHITE);

    VDP_FillVRAM(0x22, 0x0000, 0x00, VRAM_VISIBLE);
    VDP_WriteVRAM(g_LogoData, LOGO_VRAM, 0x00, LOGO_BYTES);

    VDP_EnableDisplay(TRUE);
    Wait(DELAY_BLANK);

    VDP_SetPaletteEntry(IDX_BLACK, PAL_BLACK);
    Wait(DELAY_BEFORE_ON);

    VDP_SetPaletteEntry(IDX_BLUE, PAL_BLUE);
    Wait(DELAY_BEFORE_SND);

    PSG_SetMixer(0x01);
    PSG_Apply();
    PlayNote(NOTE_C5, DELAY_NOTE);
    PlayNote(NOTE_E5, DELAY_NOTE);
    PlayNote(NOTE_G5, DELAY_NOTE);
    PlayNote(NOTE_C6, DELAY_LAST_NOTE);
    PSG_Mute();
    PSG_Apply();

    Wait(DELAY_HOLD);

    while (!Keyboard_IsKeyPressed(KEY_SPACE) &&
           !Keyboard_IsKeyPressed(KEY_RETURN) &&
           !Keyboard_IsKeyPressed(KEY_ESC))
    {
        Halt();
    }

    g_State = ST_CHOICE;
}

// Indices de las opciones del menu CHOICE
#define CHOICE_LOGIN    0
#define CHOICE_REGISTER 1
#define CHOICE_RECOVER  2
#define CHOICE_AUTO     3
#define CHOICE_COUNT    4

// Dibuja una linea del menu en posicion fija. selected=TRUE → resalta con ">".
static void DrawChoiceLine(u8 idx, bool selected) {
    u16 y = 80 + idx * 16;
    Print_SetColor(0x11, 0x33);
    Print_SetPosition(40, y);
    Print_DrawChar(selected ? '>' : ' ');
    Print_DrawChar(' ');
    Print_SetColor(selected ? 0x22 : 0x11, 0x33);
    switch (idx) {
        case CHOICE_LOGIN:    Print_DrawText("LOGIN                "); break;
        case CHOICE_REGISTER: Print_DrawText("REGISTRAR            "); break;
        case CHOICE_RECOVER:  Print_DrawText("RECUPERAR CONTRASENA "); break;
        case CHOICE_AUTO:
            Print_DrawText("LOGIN AUTOMATICO ");
            Print_DrawChar('[');
            Print_DrawChar(g_HasSavedCreds ? 'X' : ' ');
            Print_DrawChar(']');
            Print_DrawText("  ");
            break;
    }
}

static void DrawChoiceMenu(u8 sel) {
    VDP_SetPaletteEntry(IDX_BLACK, PAL_BLACK);
    VDP_SetPaletteEntry(IDX_WHITE, PAL_GREEN);
    VDP_SetPaletteEntry(IDX_BLUE,  PAL_DIMGREEN);
    VDP_SetColor(IDX_BLACK);
    VDP_FillVRAM(0x33, 0x0000, 0x00, VRAM_VISIBLE);

    Print_SetBitmapFont(g_Font_MGL_Sample6);

    Print_SetColor(0x11, 0x33);
    Print_SetPosition(96, 16);
    Print_DrawText("MSXon");
    Print_SetPosition(40, 32);
    Print_DrawText("Plataforma de juegos online MSX");
    Print_SetPosition(8, 56);
    Print_DrawText("------------------------------");

    {
        u8 i;
        for (i = 0; i < CHOICE_COUNT; i++) DrawChoiceLine(i, i == sel);
    }

    Print_SetColor(0x11, 0x33);
    Print_SetPosition(8, 168);
    Print_DrawText("[Cursores] mover  [ENTER] elegir  [ESC] salir");
}

static void RunChoice(void)
{
    // Refrescar g_HasSavedCreds (puede haber cambiado si volvimos del flujo
    // RECOVER, por ejemplo). ReadSessionDat es inofensivo si no existe el file.
    ReadSessionDat();
    // Si hay creds (SESSION.DAT V2), arranca countdown 2s para auto-login.
    bool autoCountdownActive = g_HasSavedCreds;
    u8   autoFrames = autoCountdownActive ? 120 : 0;  // 120 frames @ 60Hz ~ 2s
    u8   sel = CHOICE_LOGIN;
    u8   prevSel = 0xFF;

    DrawChoiceMenu(sel);

    u8 keyDly = 8;
    bool prevAnyKey = TRUE;  // asumimos que algo se puede estar pulsando al entrar
    while (1)
    {
        Halt();
        *((u16*)0xF3F8) = *((u16*)0xF3FA);

        bool anyKey = Keyboard_IsKeyPressed(KEY_UP)    ||
                      Keyboard_IsKeyPressed(KEY_DOWN)  ||
                      Keyboard_IsKeyPressed(KEY_RET)   ||
                      Keyboard_IsKeyPressed(KEY_ESC)   ||
                      Keyboard_IsKeyPressed(KEY_SPACE);

        // Countdown auto-login: se cancela en cuanto se detecte CUALQUIER tecla.
        if (autoCountdownActive) {
            if (anyKey) {
                autoCountdownActive = FALSE;
                // Limpiar la linea del countdown
                Print_SetColor(0x11, 0x33);
                Print_SetPosition(8, 184);
                { u8 i; for (i = 0; i < 42; i++) Print_DrawChar(' '); }
            } else if (autoFrames > 0) {
                autoFrames--;
                if (autoFrames == 0) {
                    // Disparar auto-login. Si OK → menu de juegos. Si falla
                    // (creds caducadas en el server), TryResumeSession borra
                    // SESSION.DAT internamente; volvemos al menu manual.
                    Print_SetColor(0x11, 0x33);
                    Print_SetPosition(8, 184);
                    { u8 i; for (i = 0; i < 42; i++) Print_DrawChar(' '); }
                    Print_SetPosition(8, 184);
                    Print_DrawText("Entrando...");
                    if (TryResumeSession()) {
                        DrawMenu();
                        g_State = ST_MENU;
                        return;
                    }
                    // Resume fallo: redibuja menu sin creds y permite manual.
                    autoCountdownActive = FALSE;
                    DrawChoiceMenu(sel);
                }
                // Repintar contador cada 60 frames (1s)
                if ((autoFrames % 60) == 0) {
                    Print_SetColor(0x11, 0x33);
                    Print_SetPosition(8, 184);
                    { u8 i; for (i = 0; i < 42; i++) Print_DrawChar(' '); }
                    Print_SetPosition(8, 184);
                    Print_DrawText("Auto-login en ");
                    PrintAtNum(8 + 14 * FONT_W, 184, (autoFrames + 59) / 60);
                    Print_SetPosition(8 + 16 * FONT_W, 184);
                    Print_DrawText("s... (pulsa para cancelar)");
                }
            }
        }

        // Debounce navegacion
        if (keyDly > 0) keyDly--;

        if (keyDly == 0) {
            if (Keyboard_IsKeyPressed(KEY_UP) && sel > 0) {
                sel--; keyDly = 8;
            } else if (Keyboard_IsKeyPressed(KEY_DOWN) && sel < CHOICE_COUNT - 1) {
                sel++; keyDly = 8;
            } else if (Keyboard_IsKeyPressed(KEY_RET) && !prevAnyKey) {
                // ENTER pulsado: ejecutar accion seleccionada
                if (sel == CHOICE_LOGIN)    { g_State = ST_LOGIN;    return; }
                if (sel == CHOICE_REGISTER) { g_State = ST_REGISTER; return; }
                if (sel == CHOICE_RECOVER)  { g_State = ST_RECOVER;  return; }
                if (sel == CHOICE_AUTO) {
                    if (g_HasSavedCreds) {
                        // ON → OFF: borra SESSION.DAT (incluye creds)
                        DeleteSessionDat();
                        // Limpiar mensaje y repintar tick
                        Print_SetColor(0x11, 0x33);
                        Print_SetPosition(8, 184);
                        { u8 i; for (i = 0; i < 42; i++) Print_DrawChar(' '); }
                    } else {
                        // OFF sin creds → mensaje "haz LOGIN primero"
                        Print_SetColor(0x44, 0x33);
                        VDP_SetPaletteEntry(4, PAL_RED);
                        Print_SetPosition(8, 184);
                        { u8 i; for (i = 0; i < 42; i++) Print_DrawChar(' '); }
                        Print_SetPosition(8, 184);
                        Print_DrawText("Haz LOGIN primero para activar auto-login");
                    }
                    prevSel = 0xFF;  // forzar redibujar tick
                    keyDly = 12;
                }
            } else if (Keyboard_IsKeyPressed(KEY_ESC)) {
                Bios_Exit(0);
            }
        }
        prevAnyKey = anyKey;

        // Repintar lineas del menu si cambio la seleccion
        if (sel != prevSel) {
            if (prevSel != 0xFF) DrawChoiceLine(prevSel, FALSE);
            DrawChoiceLine(sel, TRUE);
            // Si CHOICE_AUTO se acaba de toggle, re-pintar tambien esa linea
            if (prevSel == 0xFF) DrawChoiceLine(CHOICE_AUTO, sel == CHOICE_AUTO);
            prevSel = sel;
        }
    }
}

static const c8* LoginErrText(u8 code) {
    if (code == 1) return "BAD CREDENTIALS";
    if (code == 2) return "USER NOT FOUND";
    if (code == 3) return "USER BANNED";
    if (code == 4) return "RATE LIMITED";
    if (code == 5) return "SETUP PENDING - SCAN QR";
    if (code == 0xFF) return "NETWORK ERROR";
    return "";
}

static const c8* RegisterErrText(u8 code) {
    if (code == 1) return "USER ALREADY EXISTS";
    if (code == 2) return "INVALID CHARS IN USER";
    if (code == 3) return "REGISTRATION DISABLED";
    if (code == 4) return "PENDING ALREADY - REVISA EL QR ANTERIOR";
    if (code == 5) return "USER NOT FOUND";
    if (code == 0xFF) return "NETWORK ERROR";
    return "";
}

// Línea de mensaje en rojo (paleta 4 = rojo) bajo los inputs
static void DrawErrorLine(u16 x, u8 y, const c8* text) {
    VDP_SetPaletteEntry(4, PAL_RED);
    Print_SetColor(0x44, 0x33);
    Print_SetPosition(x, y);
    for (u8 i = 0; i < 40; i++) Print_DrawChar(' ');
    Print_SetPosition(x, y);
    Print_DrawText(text);
}

static void RunLogin(void)
{
    EnterBBSMode();

    Print_SetColor(0x11, 0x33);
    Print_SetPosition(80, 16);
    Print_DrawText("MSXon - LOGIN");
    Print_SetPosition(8, 32);
    Print_DrawText("------------------------------------------");

    Print_SetColor(0x22, 0x33);
    Print_SetPosition(40, 80);
    Print_DrawText("USERNAME : ");
    Print_SetPosition(40, 96);
    Print_DrawText("PASSWORD : ");

    Print_SetColor(0x11, 0x33);
    Print_SetPosition(8, 184);
    Print_DrawText("[ENTER] submit  [TAB] cambiar  [ESC] volver");

    if (g_LastErrCode) {
        DrawErrorLine(8, 128, LoginErrText(g_LastErrCode));
        g_LastErrCode = 0;
    }

    g_BufUser[0] = 0;
    g_BufPass[0] = 0;

    u16 inputX = 40 + 11 * FONT_W;
    u8  yUser  = 80;
    u8  yPass  = 96;

    u8 field = 0;
    while (1)
    {
        u8 result;
        if (field == 0) {
            result = InputText(g_BufUser, inputX, yUser, 0);
            DrawField(g_BufUser, StrLen(g_BufUser), inputX, yUser, 0, 0);
        } else {
            result = InputText(g_BufPass, inputX, yPass, 1);
            DrawField(g_BufPass, StrLen(g_BufPass), inputX, yPass, 1, 0);
        }

        if (result == 1) { g_State = ST_CHOICE; return; }
        if (result == 2) { field = 1 - field; continue; }

        if (field == 0) { field = 1; continue; }

        // ENTER en password: submit
        DrawErrorLine(8, 128, "VERIFICANDO...");
        if (!NetConnectAndAuth()) {
            g_LastErrCode = 0xFF;
            g_State = ST_LOGIN;
            return;
        }
        u8 r = NetLogin(g_BufUser, g_BufPass);
        if (r == 0) {
            if (!NetFetchGameList()) {
                g_LastErrCode = 0xFF;
                g_State = ST_LOGIN;
                return;
            }
            // Preguntar si guardar credenciales para auto-login persistente.
            // S → SESSION.DAT V2 con user+pass (entra solo hasta que cambies
            //     la password en el web).
            // N o ESC → SESSION.DAT V1 (solo session_id, caduca a los 5 min).
            DrawErrorLine(8, 128, "RECORDAR ESTE USUARIO? [S/N]");
            bool remember = FALSE;
            while (1) {
                Halt();
                *((u16*)0xF3F8) = *((u16*)0xF3FA);
                if (Keyboard_IsKeyPressed(KEY_S)) { remember = TRUE;  break; }
                if (Keyboard_IsKeyPressed(KEY_N)) { remember = FALSE; break; }
                if (Keyboard_IsKeyPressed(KEY_ESC)) { remember = FALSE; break; }
            }
            // Esperar a que se suelte la tecla, evitar rebote al menu.
            while (Keyboard_IsKeyPressed(KEY_S)
                || Keyboard_IsKeyPressed(KEY_N)
                || Keyboard_IsKeyPressed(KEY_ESC)) Halt();
            if (remember) WriteSessionDat(g_BufUser, g_BufPass);
            else          WriteSessionDat(NULL, NULL);
            DrainPackets();
            DrawMenu();
            g_State = ST_MENU;
            return;
        }
        g_LastErrCode = r;
        g_State = ST_LOGIN;
        return;
    }
}

static void RunRegister(void)
{
    EnterBBSMode();

    Print_SetColor(0x11, 0x33);
    Print_SetPosition(72, 16);
    Print_DrawText("MSXon - REGISTRARSE");
    Print_SetPosition(8, 32);
    Print_DrawText("------------------------------------------");

    Print_SetColor(0x22, 0x33);
    Print_SetPosition(40, 80);
    Print_DrawText("USERNAME : ");
    Print_SetPosition(40, 96);
    Print_DrawText("NICK     : ");

    Print_SetColor(0x11, 0x33);
    Print_SetPosition(8, 144);
    Print_DrawText("Tras submit aparecera un QR para escanear");
    Print_SetPosition(8, 156);
    Print_DrawText("con tu movil y definir tu password.");
    Print_SetPosition(8, 184);
    Print_DrawText("[ENTER] submit  [TAB] cambiar  [ESC] volver");

    if (g_LastErrCode) {
        DrawErrorLine(8, 128, RegisterErrText(g_LastErrCode));
        g_LastErrCode = 0;
    }

    g_BufUser[0] = 0;
    g_BufNick[0] = 0;

    u16 inputX = 40 + 11 * FONT_W;
    u8  yUser  = 80;
    u8  yNick  = 96;

    u8 field = 0;
    while (1)
    {
        u8 result;
        if (field == 0) {
            result = InputText(g_BufUser, inputX, yUser, 0);
            DrawField(g_BufUser, StrLen(g_BufUser), inputX, yUser, 0, 0);
        } else {
            result = InputText(g_BufNick, inputX, yNick, 0);
            DrawField(g_BufNick, StrLen(g_BufNick), inputX, yNick, 0, 0);
        }

        if (result == 1) { g_State = ST_CHOICE; return; }
        if (result == 2) { field = 1 - field; continue; }

        if (field == 0) { field = 1; continue; }

        // ENTER en nick: submit
        DrawErrorLine(8, 128, "VERIFICANDO...");

        if (!NetConnectAndAuth()) {
            g_LastErrCode = 0xFF;
            g_State = ST_REGISTER;
            return;
        }

        u8 r = NetRegister(g_BufUser, g_BufNick);
        if (r == 0) {
            g_State = ST_QR;
            return;
        }
        g_LastErrCode = r;
        g_State = ST_REGISTER;
        return;
    }
}

static void RunRecover(void)
{
    EnterBBSMode();

    Print_SetColor(0x11, 0x33);
    Print_SetPosition(56, 16);
    Print_DrawText("MSXon - RECUPERAR PASSWORD");
    Print_SetPosition(8, 32);
    Print_DrawText("------------------------------------------");

    Print_SetColor(0x22, 0x33);
    Print_SetPosition(8, 56);
    Print_DrawText("Tipea tu usuario. Te daremos un QR para");
    Print_SetPosition(8, 68);
    Print_DrawText("definir una nueva password en el movil.");

    Print_SetPosition(40, 96);
    Print_DrawText("USERNAME : ");

    Print_SetColor(0x11, 0x33);
    Print_SetPosition(8, 184);
    Print_DrawText("[ENTER] submit  [ESC] volver");

    if (g_LastErrCode) {
        DrawErrorLine(8, 144, RegisterErrText(g_LastErrCode));
        g_LastErrCode = 0;
    }

    g_BufUser[0] = 0;
    u16 inputX = 40 + 11 * FONT_W;
    u8  yUser  = 96;

    while (1)
    {
        u8 result = InputText(g_BufUser, inputX, yUser, 0);
        DrawField(g_BufUser, StrLen(g_BufUser), inputX, yUser, 0, 0);

        if (result == 1) { g_State = ST_CHOICE; return; }   // ESC
        // result == 2 (TAB) no aplica con un solo campo: ignorar
        if (result == 2) continue;

        // ENTER en username: submit
        DrawErrorLine(8, 144, "VERIFICANDO...");
        if (!NetConnectAndAuth()) {
            g_LastErrCode = 0xFF;
            g_State = ST_RECOVER;
            return;
        }
        u8 r = NetRecover(g_BufUser);
        if (r == 0) {
            // REG_PENDING: g_RegToken poblado. Reusamos RunQR (mismo flujo que
            // registro: render QR con la URL de activacion).
            g_State = ST_QR;
            return;
        }
        g_LastErrCode = r;
        g_State = ST_RECOVER;
        return;
    }
}

static void RunQR(void)
{
    VDP_SetPaletteEntry(IDX_WHITE, PAL_WHITE);
    VDP_SetPaletteEntry(IDX_BLACK, PAL_BLACK);
    VDP_SetPaletteEntry(IDX_BLUE,  PAL_DIMGREEN);

    VDP_SetPaletteEntry(4, PAL_BLACK);
    VDP_SetPaletteEntry(5, PAL_WHITE);
    VDP_SetPaletteEntry(6, PAL_WHITE);
    VDP_SetPaletteEntry(7, PAL_WHITE);

    VDP_SetColor(IDX_WHITE);
    VDP_FillVRAM(0x22, 0x0000, 0x00, VRAM_VISIBLE);

    Print_SetBitmapFont(g_Font_MGL_Sample6);
    Print_SetColor(0x33, 0x22);
    Print_SetPosition(56, 80);
    Print_DrawText("GENERANDO QR");
    Print_SetPosition(8, 100);
    Print_DrawText("Esto puede tardar unos");
    Print_SetPosition(8, 110);
    Print_DrawText("20 segundos en MSX real.");

    Print_SetColor(0x44, 0x22); Print_SetPosition(180, 80); Print_DrawChar('*');
    Print_SetColor(0x55, 0x22); Print_SetPosition(192, 80); Print_DrawChar('*');
    Print_SetColor(0x66, 0x22); Print_SetPosition(204, 80); Print_DrawChar('*');
    Print_SetColor(0x77, 0x22); Print_SetPosition(216, 80); Print_DrawChar('*');

    g_BlinkFrame = 0;
    Bios_SetHookCallback(H_TIMI, OnVBlankSpinner);

    BuildRegisterURL(g_BufUser, g_RegToken);

    u8 ok = QRCode_EncodeText(g_URL, g_QRTemp, g_QR);
    u8 size = ok ? QRCode_GetSize(g_QR) : 0;

    Bios_ClearHook(H_TIMI);
    VDP_SetPaletteEntry(4, PAL_WHITE);
    VDP_SetPaletteEntry(5, PAL_WHITE);
    VDP_SetPaletteEntry(6, PAL_WHITE);
    VDP_SetPaletteEntry(7, PAL_WHITE);

    VDP_FillVRAM(0x22, 0x0000, 0x00, VRAM_VISIBLE);

    Print_SetColor(0x33, 0x22);
    Print_SetPosition(48, 8);
    Print_DrawText("MSXon - Activa tu cuenta");

    if (ok && size > 0) {
        u8 scale = 4;
        while ((size * scale) > 130 && scale > 1) scale--;

        u16 totalPx = size * scale;
        u16 qrX = (256 - totalPx) / 2;
        u16 qrY = 28;

        DrawQR(qrX, qrY, scale, IDX_BLACK, g_QR);

        Print_SetPosition(8, qrY + totalPx + 8);
        Print_DrawText("Escanea con tu movil para definir");
        Print_SetPosition(8, qrY + totalPx + 18);
        Print_DrawText("tu password.");
    } else {
        Print_SetPosition(40, 60);
        Print_DrawText("ERROR generando QR");
    }

    Print_SetPosition(64, 184);
    Print_DrawText("[ESC] cancelar");

    while (1) {
        if (Keyboard_IsKeyPressed(KEY_ESC)) { g_State = ST_CHOICE; return; }
        Halt();
    }
}

// ── Main ───────────────────────────────────────────────────────────

void main(void)
{
    *((u8*)0xF3DB) = 0; // disable key click

    g_State = ST_INTRO;
    g_SelGame = 0;
    g_Conn = NET_INVALID_CONN;
    g_Online = FALSE;
    g_KeyDly = 0;
    g_PingT = 0;
    g_LBN = 0;
    g_LBC = 0;
    g_LastErrCode = 0;
    g_LobbyDrawn = 0;
    g_WaitDrawn  = 0;
    g_PrevKeyRet = 1;

    // Limpiar tabla de nicks (BSS no garantizada en MSX-DOS). Sin esto, si
    // entramos a una sala donde el JOIN dispara ST_LAUNCHING inmediato
    // (ghost + nosotros = sala llena), WriteLobbyDat copia basura como nick.
    { u8 _i, _j; for(_i = 0; _i < MSXON_MAX_NICKS; _i++) for(_j = 0; _j <= MSXON_NICK_LEN; _j++) g_PlayerNicks[_i][_j] = 0; }

    // Modo Screen 5 antes que nada (al volver de un juego, el VDP estaba
    // en Screen 4 u otro con paletas raras). Pantalla en negro hasta que
    // la pinte intro o menu post-resume.
    VDP_SetMode(VDP_MODE_SCREEN5);
    VDP_DisableSprite();
    VDP_SetPaletteEntry(IDX_BLACK, PAL_BLACK);
    VDP_SetPaletteEntry(IDX_WHITE, PAL_GREEN);
    VDP_SetPaletteEntry(IDX_BLUE,  PAL_DIMGREEN);
    VDP_SetColor(IDX_BLACK);
    VDP_FillVRAM(0x33, 0x0000, 0x00, VRAM_VISIBLE);
    Print_SetBitmapFont(g_Font_MGL_Sample6);

    // Intento de "skip intro" si VENGO DE UN JUEGO (segundos despues del ultimo
    // login). Solo probamos SESSION_RESUME (TTL ~5min en server). NO usamos el
    // fallback de NetLogin con creds — ese pertenece al countdown del CHOICE.
    //
    //  - SESSION_RESUME OK  → vengo de juego, directo al menu sin intro.
    //  - SESSION_RESUME FAIL → primera vez del dia (o sin SESSION.DAT). Mostramos
    //                          intro + CHOICE; si hay V2, el CHOICE hara el
    //                          auto-login con creds tras 2s de countdown.
    if (ReadSessionDat()) {
        Print_SetColor(0x11, 0x33);
        Print_SetPosition(8, 100);
        Print_DrawText("RESUMING SESSION...");
        if (NetConnectAndAuth() && NetSessionResume() == 0 && NetFetchGameList()) {
            DrawMenu();
            g_State = ST_MENU;
        } else {
            // Resume rapido fallo. NO borramos SESSION.DAT — si era V2, el
            // CHOICE intentara el fallback LOGIN con las creds guardadas.
            VDP_FillVRAM(0x33, 0x0000, 0x00, VRAM_VISIBLE);
        }
    }

    while(1) {
        // Screen 5 states (intro / forms / QR / chat)
        if (g_State == ST_INTRO)    { RunIntro();    continue; }
        if (g_State == ST_CHOICE)   { RunChoice();   continue; }
        if (g_State == ST_LOGIN)    { RunLogin();    continue; }
        if (g_State == ST_REGISTER) { RunRegister(); continue; }
        if (g_State == ST_RECOVER)  { RunRecover();  continue; }
        if (g_State == ST_QR)       { RunQR();       continue; }
        if (g_State == ST_CHAT)     { RunChat();
            // Tras volver del chat hay que repintar el menu (DrawLobby/DrawMenu
            // limpian la pantalla en EnterBBSMode).
            DrawMenu();
            continue;
        }

        // Screen 5 post-login flow (todo en BBS verde)
        Halt();
        *((u16*)0xF3F8) = *((u16*)0xF3FA); // flush keyboard buffer

        if (g_KeyDly > 0) { g_KeyDly--; }

        if(g_State == ST_MENU) {
            if(g_KeyDly == 0) {
                if(Keyboard_IsKeyPressed(KEY_UP) && g_SelGame > 0) {
                    u8 old = g_SelGame; g_SelGame--;
                    MoveMenuCursor(old, g_SelGame); g_KeyDly = 8;
                }
                if(Keyboard_IsKeyPressed(KEY_DOWN) && g_NumGames > 0 && g_SelGame < g_NumGames - 1) {
                    u8 old = g_SelGame; g_SelGame++;
                    MoveMenuCursor(old, g_SelGame); g_KeyDly = 8;
                }
                if(Keyboard_IsKeyPressed(KEY_RET) && g_NumGames > 0) {
                    g_CurGame = &g_Games[g_SelGame];
                    g_KeyDly = 15;
                    SendRoomList();
                    g_State = ST_CONNECTING;
                }
                if(Keyboard_IsKeyPressed(KEY_C)) {
                    // Entrar al chat global.
                    g_State = ST_CHAT;
                    g_KeyDly = 15;
                }
                if(Keyboard_IsKeyPressed(KEY_ESC)) {
                    Bios_Exit(0);
                }
                {
                    u8 k;
                    for(k = 0; k < g_NumGames && k < 9; k++) {
                        if(Keyboard_IsKeyPressed(KEY_1 + k)) {
                            u8 old = g_SelGame; g_SelGame = k;
                            MoveMenuCursor(old, g_SelGame);
                            g_KeyDly = 8;
                            break;
                        }
                    }
                }
            }
        }
        else if(g_State == ST_CONNECTING) {
            Poll();
        }
        else if(g_State == ST_LOBBY) {
            if(!g_LobbyDrawn) {
                DrawLobby();
                g_LobbyDrawn = 1;
            }

            if(g_KeyDly == 0) {
                if(Keyboard_IsKeyPressed(KEY_UP) && g_LBC > 0) {
                    u8 old = g_LBC; g_LBC--;
                    MoveLobbyCursor(old, g_LBC); g_KeyDly = 8;
                }
                if(Keyboard_IsKeyPressed(KEY_DOWN) && g_LBN > 0 && g_LBC < g_LBN - 1) {
                    u8 old = g_LBC; g_LBC++;
                    MoveLobbyCursor(old, g_LBC); g_KeyDly = 8;
                }
                if(Keyboard_IsKeyPressed(KEY_RET) && g_LBN > 0) {
                    SendJoinRoom(g_LB[g_LBC].rid);
                    g_State = ST_CONNECTING;
                    g_LobbyDrawn = 0;
                    g_KeyDly = 15;
                }
                if(Keyboard_IsKeyPressed(KEY_C)) {
                    SendCreateRoom();
                    g_State = ST_CONNECTING;
                    g_LobbyDrawn = 0;
                    g_KeyDly = 15;
                }
                if(Keyboard_IsKeyPressed(KEY_R)) {
                    SendRoomList();
                    g_State = ST_CONNECTING;
                    g_LobbyDrawn = 0;
                    g_KeyDly = 15;
                }
                if(Keyboard_IsKeyPressed(KEY_ESC)) {
                    g_State = ST_MENU;
                    g_LobbyDrawn = 0;
                    DrawMenu();
                    g_KeyDly = 15;
                }
            }

            {
                u16 av = Net_Available(g_Conn);
                if(av >= 6) {
                    u8 hdr2[6], pl2[200];
                    Net_Recv(g_Conn, hdr2, 6);
                    if(hdr2[0] == PROTO_MAGIC_0 && hdr2[1] == PROTO_MAGIC_1) {
                        if(hdr2[5] > 0) { while(Net_Available(g_Conn) < hdr2[5]) Halt(); Net_Recv(g_Conn, pl2, hdr2[5]); }
                        ProcessPacket(hdr2[2], pl2, hdr2[5]);
                    }
                }
            }
        }
        else if(g_State == ST_WAITING) {
            static u8 prevAct = 0;
            static u8 prevSecs = 0xFF;

            // Durante countdown NO drenamos el buffer TCP: los paquetes que el
            // ghost emite mientras decrementa el contador (p.ej. STATE_UPDATE
            // del primer turno en damas) deben quedar en cola para que el .COM
            // del juego los lea al arrancar. Si llamamos Poll(), ProcessPacket
            // los consume y descarta porque MSXon no maneja STATE_UPDATE.
            if (g_BotCountdown == 0) Poll();
            if(g_State == ST_LAUNCHING) continue;

            u8 currSecs = (g_BotCountdown == 0) ? 0
                          : (u8)((g_BotCountdown + 59) / 60);
            // Redibujo completo (limpia VRAM) solo si entramos a este estado
            // o cambia g_Active (alguien join/leave). El countdown no fuerza
            // redibujo completo — basta con actualizar la linea del footer.
            if(!g_WaitDrawn || g_Active != prevAct) {
                DrawWaiting();
                g_WaitDrawn = 1;
                prevAct = g_Active;
                prevSecs = currSecs;
            }
            else if(currSecs != prevSecs) {
                DrawWaitingFooter();
                prevSecs = currSecs;
            }

            // Countdown auto-arranque (BOT host o AGGREGATE)
            if (g_BotCountdown > 0) {
                g_BotCountdown--;
                if (g_BotCountdown == 0) {
                    g_State = ST_LAUNCHING;
                    continue;
                }
            }

            if(g_KeyDly == 0) {
                // Solo el host humano (P1) puede arrancar manualmente con ENTER o S.
                // Si countdown ya esta corriendo (BOT/AGGREGATE), las teclas
                // no hacen nada — el arranque lo dispara el contador.
                if((Keyboard_IsKeyPressed(KEY_RET) || Keyboard_IsKeyPressed(KEY_S))
                   && g_MyPid == 1 && g_BotCountdown == 0) {
                    SendGameStart();
                    g_State = ST_LAUNCHING;
                    g_KeyDly = 15;
                }
                if(Keyboard_IsKeyPressed(KEY_ESC)) {
                    g_SendBuf[0]=PROTO_MAGIC_0; g_SendBuf[1]=PROTO_MAGIC_1;
                    g_SendBuf[2]=CMD_ROOM_LEAVE; g_SendBuf[3]=g_RoomId;
                    g_SendBuf[4]=g_MyPid; g_SendBuf[5]=0;
                    Net_Send(g_Conn, g_SendBuf, 6);
                    g_State = ST_MENU;
                    g_WaitDrawn = 0;
                    g_BotCountdown = 0;
                    DrawMenu();
                    g_KeyDly = 15;
                }
            }
        }
        else if(g_State == ST_LAUNCHING) {
            LaunchGame(TRUE);
        }
    }
}
