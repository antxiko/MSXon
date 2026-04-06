// Texas Hold'em Poker — MSX2 Screen 4 — MSXonLINE
// Cliente puro: el servidor es el dealer
// GAME_ID=0x05, RELAY mode
#include "msxgl.h"
#include "vdp.h"
#include "input.h"
#include "bios.h"
#include "system.h"
#include "dos.h"
#include "protocol.h"
#include "network.h"
#include "log.h"
#include "tileset_data.h"
#include "screen_data.h"

// ── Constantes ──────────────────────────────────────────────────────
#define MAX_SEATS   6

// Tiles
#define T_BLK   0
#define T_CTL   1
#define T_CTR   2
#define T_CBL   3
#define T_CBR   4
#define T_BTL   5
#define T_BTR   6
#define T_BBL   7
#define T_BBR   8
#define T_SPADE 9
#define T_HEART 10
#define T_DIAMOND 11
#define T_CLUB  12
#define T_VB    13  // values black A=13..K=25
#define T_VR    26  // values red A=26..K=38
#define T_FELT  39
#define T_SEP   49
#define T_FA    52  // font A=52..Z=77
#define T_F0    78  // font 0=78..9=87
#define T_SPC   88
#define T_COLON 89
#define T_SLASH 90
#define T_DASH  92
#define T_EMPTY 94
#define T_BML   94  // back middle left
#define T_BMR   95  // back middle right

#define CARD_VAL(c)  ((c)&0x0F)
#define CARD_SUIT(c) (((c)>>4)&0x03)

// Actions
#define ACT_FOLD  0
#define ACT_CHECK 1
#define ACT_CALL  2
#define ACT_RAISE 3
#define ACT_ALLIN 4

// Server packet types (first byte of STATE_UPDATE payload from server)
#define PKT_HAND_START    0x01
#define PKT_DEAL_HOLE     0x02
#define PKT_COMMUNITY     0x03
#define PKT_ACTION_PROMPT 0x04
#define PKT_ACTION_RESULT 0x05
#define PKT_CHIP_UPDATE   0x06
#define PKT_SHOWDOWN      0x07
#define PKT_ELIMINATED    0x08
#define PKT_GAME_OVER     0x09

// ── Variables ───────────────────────────────────────────────────────
static u8 g_NB[768];
static bool g_FullFlush = TRUE;

// Seats
static u16 g_Chips[MAX_SEATS];
static u8 g_MyCards[2];
static u8 g_Community[5];
static u8 g_CommunityCount;
static u16 g_Pot;
static u16 g_CurrentBet;
static u16 g_MinRaise;
static u8 g_DealerSeat;
static u8 g_ActionSeat;
static u16 g_HandNum;
static u8 g_MySeat;
static u8 g_LastAction[MAX_SEATS];

// UI state
static u8 g_SelAction = ACT_CHECK;
static u16 g_RaiseAmount;
static bool g_WaitingInput = FALSE;
static u8 g_MoveDelay = 0;

// Network
static const u8 SERVER_IP[4] = {217,154,107,144};
#define SERVER_PORT   9876
#define GAME_ID_TEX   0x05
#define PING_INTERVAL 250
static NetConn g_Conn = NET_INVALID_CONN;
static u8 g_MyPid = 0, g_RoomId = 0;
static u16 g_PingTimer = 0;
static u8 g_SendBuf[20];
static bool g_Online = FALSE;
static u8 g_ActiveP = 0;

// Game states
#define GS_LOBBY_WAIT 0
#define GS_LOBBY      1
#define GS_WAITING    2
#define GS_PLAYING    3
static u8 g_GS = GS_LOBBY_WAIT;

// Lobby
#define LB_MAX 20
typedef struct { u8 rid,gid,np; } LBRoom;
static LBRoom g_LB[LB_MAX];
static u8 g_LBN=0, g_LBC=0;

// Key flags
static u8 g_KUp=0,g_KDown=0,g_KRet=0,g_KC=0,g_KR=0,g_KEsc=0,g_KS=0;

// Action names
const c8* g_ActNames[5] = {"NO IR","PASAR","IGUALAR","SUBIR","TODO"};

// Seat positions (from table_layout.json)
const u8 g_SeatNameCol[6] = {  0,  0,  0, 24, 24, 24 };
const u8 g_SeatNameRow[6] = { 15, 10,  5,  5, 10, 15 };
const u8 g_SeatCardCol[6] = {  3,  2,  4, 24, 26, 25 };
const u8 g_SeatCardRow[6] = { 12,  7,  2,  2,  7, 12 };

// ── Name buffer ─────────────────────────────────────────────────────

#define NB_SET(x,y,v) do{u16 _i=(u16)(y)*32+(x);u8 _v=(v);if(g_NB[_i]!=_v)g_NB[_i]=_v;}while(0)

static void NB_Flush(void) { VDP_WriteVRAM(g_NB, 0x1800, 0, 768); }

static void NB_Text(u8 x, u8 y, const c8* s) {
    while(*s && x<32) {
        u8 ch=(u8)*s, t;
        if(ch>='A'&&ch<='Z') t=T_FA+ch-'A';
        else if(ch>='0'&&ch<='9') t=T_F0+ch-'0';
        else if(ch==':') t=T_COLON;
        else if(ch=='/') t=T_SLASH;
        else if(ch=='-') t=T_DASH;
        else if(ch=='>') t=T_SEP;
        else t=T_SPC;
        NB_SET(x,y,t); x++; s++;
    }
}

static void NB_Num(u8 x, u8 y, u16 v) {
    c8 b[6]; u8 n=0,i; u16 d=10000;
    for(i=0;i<5;i++){u8 g=(u8)(v/d);v%=d;d/=10;if(g||n||i==4)b[n++]='0'+g;}
    b[n]=0; NB_Text(x,y,b);
}

static void NB_ClearRow(u8 y) { u8 x; for(x=0;x<32;x++) NB_SET(x,y,T_BLK); }

// ── VDP ─────────────────────────────────────────────────────────────

void VDP_LoadTileset(void) {
    u8 bank;
    for(bank=0;bank<3;bank++) {
        VDP_WriteVRAM(g_TilePatterns, bank*2048, 0, 2048);
        VDP_WriteVRAM(g_TileColors, 0x2000+bank*2048, 0, 2048);
    }
}

void Board_Draw(void) {
    u16 i;
    for(i=0;i<768;i++) g_NB[i]=g_ScreenMap[i];
    g_FullFlush=TRUE;
}

// ── Card drawing ────────────────────────────────────────────────────

void DrawCardTall(u8 col, u8 row, u8 card) {
    u8 val, suit, vt, st;
    if(card==0){
        NB_SET(col,row,T_EMPTY);NB_SET(col+1,row,T_EMPTY);
        NB_SET(col,row+1,T_EMPTY);NB_SET(col+1,row+1,T_EMPTY);
        NB_SET(col,row+2,T_EMPTY);NB_SET(col+1,row+2,T_EMPTY);
        return;
    }
    val=CARD_VAL(card); suit=CARD_SUIT(card);
    vt=(suit==1||suit==2)?T_VR+val-1:T_VB+val-1;
    st=T_SPADE+suit;
    NB_SET(col,row,vt); NB_SET(col+1,row,st);
    NB_SET(col,row+1,T_CTL); NB_SET(col+1,row+1,T_CTR);
    NB_SET(col,row+2,T_CBL); NB_SET(col+1,row+2,T_CBR);
}

void DrawCardBackTall(u8 col, u8 row) {
    NB_SET(col,row,T_BTL); NB_SET(col+1,row,T_BTR);
    NB_SET(col,row+1,T_BML); NB_SET(col+1,row+1,T_BMR);
    NB_SET(col,row+2,T_BBL); NB_SET(col+1,row+2,T_BBR);
}

// ── Drawing ─────────────────────────────────────────────────────────

void DrawHUD(void) {
    NB_ClearRow(0);
    NB_Text(0,0,"BOTE:      ");
    NB_Num(5,0,g_Pot);
    NB_Text(12,0,"CIEGA:"); NB_Num(18,0,10);
    NB_SET(21,0,T_SLASH); NB_Num(22,0,20);
    NB_Text(26,0,"M:"); NB_Num(28,0,g_HandNum);
}

void DrawSeatInfo(u8 s) {
    u8 col=g_SeatNameCol[s], row=g_SeatNameRow[s];
    NB_Text(col,row,"        ");
    NB_Text(col,row,"J"); NB_Num(col+1,row,s+1);
    NB_Text(col+3,row," "); NB_Num(col+4,row,g_Chips[s]);
}

void DrawSeatCards(u8 s) {
    u8 col=g_SeatCardCol[s], row=g_SeatCardRow[s];
    if(s==g_MySeat) {
        DrawCardTall(col,row,g_MyCards[0]);
        DrawCardTall(col+2,row,g_MyCards[1]);
    } else {
        if(g_Chips[s]>0) { DrawCardBackTall(col,row); DrawCardBackTall(col+2,row); }
        else { u8 r2; for(r2=0;r2<3;r2++){NB_SET(col,row+r2,T_BLK);NB_SET(col+1,row+r2,T_BLK);NB_SET(col+2,row+r2,T_BLK);NB_SET(col+3,row+r2,T_BLK);} }
    }
}

void DrawMyCards(void) {
    DrawCardTall(13,18,g_MyCards[0]);
    NB_SET(15,18,T_BLK); NB_SET(15,19,T_BLK); NB_SET(15,20,T_BLK);
    DrawCardTall(16,18,g_MyCards[1]);
}

void DrawCommunity(void) {
    u8 i,col;
    for(i=0;i<5;i++) {
        col=11+i*2;
        if(i<g_CommunityCount) DrawCardTall(col,8,g_Community[i]);
        else { NB_SET(col,8,93);NB_SET(col+1,8,93);NB_SET(col,9,93);NB_SET(col+1,9,93);NB_SET(col,10,93);NB_SET(col+1,10,93); }
    }
}

void DrawActionBar(void) {
    NB_ClearRow(22); NB_ClearRow(23);
    if(!g_WaitingInput) return;
    NB_Text(1,22,"NO IR");
    if(g_CurrentBet<=0) NB_Text(8,22,"PASAR");
    else NB_Text(8,22,"IGUALAR");
    NB_Text(17,22,"SUBIR"); NB_Text(24,22,"TODO");
    {
        u8 cc;
        if(g_SelAction==ACT_FOLD) cc=3;
        else if(g_SelAction==ACT_CHECK||g_SelAction==ACT_CALL) cc=10;
        else if(g_SelAction==ACT_RAISE) cc=18;
        else cc=25;
        NB_Text(cc,23,">>>");
    }
    if(g_SelAction==ACT_RAISE) { NB_Text(17,23,"+"); NB_Num(18,23,g_RaiseAmount); }
}

void DrawAll(void) {
    u8 s;
    DrawHUD();
    for(s=0;s<MAX_SEATS;s++) { DrawSeatInfo(s); DrawSeatCards(s); }
    DrawCommunity();
    DrawMyCards();
    DrawActionBar();
}

// ── Diagnostico + Conexion (patrón tetris) ──────────────────────────

void Diag_PrintDec(u8 val) {
    u8 h=val/100,t=(val%100)/10,u2=val%10;
    if(h) DOS_CharOutput('0'+h);
    if(h||t) DOS_CharOutput('0'+t);
    DOS_CharOutput('0'+u2);
}

void Diag_PrintIP(const u8* ip) {
    u8 i; for(i=0;i<4;i++){Diag_PrintDec(ip[i]);if(i<3)DOS_CharOutput('.');}
}

void Diag_ShowNetInfo(void)
{
    u8 localIP[4];
    u8 ok;

    DOS_StringOutput("================================\r\n$");
    DOS_StringOutput("   TEXAS HOLDEM - DIAGNOSTICS\r\n$");
    DOS_StringOutput("================================\r\n\r\n$");

    DOS_StringOutput("UNAPI TCP/IP: $");
    ok = (tcpip_enumerate() > 0) ? 1 : 0;
    if(ok)
        DOS_StringOutput("ENCONTRADO\r\n$");
    else
    {
        DOS_StringOutput("NO ENCONTRADO\r\n\r\n$");
        DOS_StringOutput("Juego necesita conexion.\r\n$");
        DOS_StringOutput("Pulsa ESPACIO para continuar...$");
        DOS_CharInput();
        return;
    }

    tcpip_get_ipinfo(&g_IpInfo);

    DOS_StringOutput("IP local    : $");
    localIP[0] = (u8)g_IpInfo.local_ip[0];
    localIP[1] = (u8)g_IpInfo.local_ip[1];
    localIP[2] = (u8)g_IpInfo.local_ip[2];
    localIP[3] = (u8)g_IpInfo.local_ip[3];
    Diag_PrintIP(localIP);
    DOS_StringOutput("\r\n$");

    DOS_StringOutput("Mascara     : $");
    localIP[0] = (u8)g_IpInfo.subnet_mask[0];
    localIP[1] = (u8)g_IpInfo.subnet_mask[1];
    localIP[2] = (u8)g_IpInfo.subnet_mask[2];
    localIP[3] = (u8)g_IpInfo.subnet_mask[3];
    Diag_PrintIP(localIP);
    DOS_StringOutput("\r\n$");

    DOS_StringOutput("Gateway     : $");
    localIP[0] = (u8)g_IpInfo.gateway_ip[0];
    localIP[1] = (u8)g_IpInfo.gateway_ip[1];
    localIP[2] = (u8)g_IpInfo.gateway_ip[2];
    localIP[3] = (u8)g_IpInfo.gateway_ip[3];
    Diag_PrintIP(localIP);
    DOS_StringOutput("\r\n$");

    DOS_StringOutput("\r\nServidor    : $");
    Diag_PrintIP(SERVER_IP);
    DOS_StringOutput("\r\n$");

    DOS_StringOutput("Puerto      : $");
    {
        u16 port = SERVER_PORT;
        u8 d[5];
        u8 i, started;
        d[0] = (u8)(port / 10000); port %= 10000;
        d[1] = (u8)(port / 1000);  port %= 1000;
        d[2] = (u8)(port / 100);   port %= 100;
        d[3] = (u8)(port / 10);    port %= 10;
        d[4] = (u8)(port);
        started = 0;
        for(i = 0; i < 5; i++)
        {
            if(d[i] > 0 || started || i == 4)
            {
                DOS_CharOutput('0' + d[i]);
                started = 1;
            }
        }
    }
    DOS_StringOutput("\r\n$");

    DOS_StringOutput("\r\nPulsa ESPACIO para continuar...$");
    DOS_CharInput();
    DOS_StringOutput("\r\n$");
}

void Net_Wait50(void) { u8 w; for(w = 0; w < 25; w++) Halt(); }

bool Net_ConnectToServer(void)
{
    u8 tcpState;
    u16 timeout;

    Log_Init();
    Log_Write("[INIT] Texas arrancando");

    if(Net_Init() != NET_OK)
    {
        Log_Write("[CONN] UNAPI no hallado");
        return FALSE;
    }
    Log_WriteHex("[CONN] UNAPI OK impl=", g_NetImplCount);

    Net_Wait50();
    tcpip_get_ipinfo(&g_IpInfo);
    Net_Wait50();

    Log_Write("[CONN] Abriendo TCP...");
    g_Conn = Net_Open(SERVER_IP, SERVER_PORT);
    if(g_Conn == NET_INVALID_CONN)
    {
        Log_WriteHex("[CONN] Fallo err=", g_NetLastError);
        return FALSE;
    }

    timeout = 0;
    while(timeout < 500)
    {
        Halt();
        tcpState = Net_GetConnState(g_Conn);
        if(tcpState == TCP_STATE_ESTABLISHED) break;
        if(tcpState == 0xFF) return FALSE;
        timeout++;
    }
    if(timeout >= 500) return FALSE;
    Log_Write("[CONN] ESTABLISHED");

    Net_Wait50();

    {
        u8 token[4];
        token[0] = AUTH_TOKEN_0; token[1] = AUTH_TOKEN_1;
        token[2] = AUTH_TOKEN_2; token[3] = AUTH_TOKEN_3;
        g_SendBuf[0] = PROTO_MAGIC_0; g_SendBuf[1] = PROTO_MAGIC_1;
        g_SendBuf[2] = CMD_AUTH; g_SendBuf[3] = 0; g_SendBuf[4] = 0; g_SendBuf[5] = 4;
        g_SendBuf[6] = token[0]; g_SendBuf[7] = token[1];
        g_SendBuf[8] = token[2]; g_SendBuf[9] = token[3];
        Net_Send(g_Conn, g_SendBuf, 10);
        Log_Write("[AUTH] Enviado");
    }

    timeout = 0;
    while(timeout < 250)
    {
        u16 avail;
        Halt();
        avail = Net_Available(g_Conn);
        if(avail >= 6)
        {
            u8 hdr[6];
            Net_Recv(g_Conn, hdr, 6);
            if(hdr[2] == CMD_AUTH_OK) { Log_Write("[AUTH] OK"); break; }
            if(hdr[2] == CMD_AUTH_FAIL) return FALSE;
        }
        timeout++;
    }
    if(timeout >= 250) return FALSE;

    return TRUE;
}

void Net_RequestRoomList(void) {
    if(g_Conn==NET_INVALID_CONN)return;
    g_SendBuf[0]=PROTO_MAGIC_0;g_SendBuf[1]=PROTO_MAGIC_1;g_SendBuf[2]=CMD_ROOM_LIST;
    g_SendBuf[3]=0;g_SendBuf[4]=0;g_SendBuf[5]=0;Net_Send(g_Conn,g_SendBuf,6);
}

void Net_CreateRoom(void) {
    if(g_Conn==NET_INVALID_CONN)return;
    g_SendBuf[0]=PROTO_MAGIC_0;g_SendBuf[1]=PROTO_MAGIC_1;g_SendBuf[2]=CMD_ROOM_CREATE;
    g_SendBuf[3]=0;g_SendBuf[4]=0;g_SendBuf[5]=3;
    g_SendBuf[6]=GAME_ID_TEX;g_SendBuf[7]=6;g_SendBuf[8]=0x01;
    Net_Send(g_Conn,g_SendBuf,9);
}

void Net_JoinRoom(u8 rid) {
    if(g_Conn==NET_INVALID_CONN)return;
    g_SendBuf[0]=PROTO_MAGIC_0;g_SendBuf[1]=PROTO_MAGIC_1;g_SendBuf[2]=CMD_ROOM_JOIN;
    g_SendBuf[3]=0;g_SendBuf[4]=0;g_SendBuf[5]=1;g_SendBuf[6]=rid;
    Net_Send(g_Conn,g_SendBuf,7);
}

void Net_SendPing(void) {
    if(g_Conn==NET_INVALID_CONN)return;
    g_SendBuf[0]=PROTO_MAGIC_0;g_SendBuf[1]=PROTO_MAGIC_1;g_SendBuf[2]=CMD_PING;
    g_SendBuf[3]=g_RoomId;g_SendBuf[4]=g_MyPid;g_SendBuf[5]=0;
    Net_Send(g_Conn,g_SendBuf,6);
}

// Send my action to server: [action, amountHi, amountLo]
void Net_SendAction(u8 action, u16 amount) {
    if(g_Conn==NET_INVALID_CONN)return;
    g_SendBuf[0]=PROTO_MAGIC_0;g_SendBuf[1]=PROTO_MAGIC_1;g_SendBuf[2]=CMD_STATE_UPDATE;
    g_SendBuf[3]=g_RoomId;g_SendBuf[4]=g_MyPid;g_SendBuf[5]=3;
    g_SendBuf[6]=action;g_SendBuf[7]=(u8)(amount>>8);g_SendBuf[8]=(u8)(amount&0xFF);
    Net_Send(g_Conn,g_SendBuf,9);
}

// Forward
void Lobby_Draw(void);

void Net_ProcessPacket(u8 cmd, u8 senderPid, u8* pl, u8 len) {
    if(cmd==CMD_ROOM_LIST && len>=1) {
        u8 cnt=pl[0],i; g_LBN=0;
        for(i=0;i<cnt&&i<LB_MAX;i++){
            u8 off=1+i*3;
            if(pl[off+1]==GAME_ID_TEX){g_LB[g_LBN].rid=pl[off];g_LB[g_LBN].gid=pl[off+1];g_LB[g_LBN].np=pl[off+2];g_LBN++;}
        }
        g_LBC=0;g_GS=GS_LOBBY;Lobby_Draw();
    }
    else if(cmd==CMD_ROOM_INFO && len>=4) {
        u16 si; g_RoomId=pl[0];g_MyPid=pl[3];g_MySeat=g_MyPid-1;
        g_ActiveP=0;{u8 n=pl[2],j;for(j=0;j<n;j++)g_ActiveP|=(1<<j);}
        for(si=0;si<768;si++)g_NB[si]=T_BLK; g_FullFlush=TRUE;
        g_GS=GS_WAITING;
    }
    else if(cmd==CMD_PLAYER_JOINED && len>=1) {
        u8 jp=pl[0]; if(jp>=1&&jp<=8)g_ActiveP|=(1<<(jp-1));
    }
    else if(cmd==CMD_PLAYER_LEFT && len>=1) {
        u8 lp=pl[0]; if(lp>=1&&lp<=8)g_ActiveP&=~(1<<(lp-1));
    }
    else if(cmd==CMD_GAME_START) {
        u16 si;
        for(si=0;si<768;si++) g_NB[si]=T_BLK;
        Board_Draw();
        g_FullFlush=TRUE;
        g_GS=GS_PLAYING;
    }
    else if(cmd==CMD_STATE_UPDATE && len>=1) {
        u8 pkt=pl[0];

        if(pkt==PKT_HAND_START && len>=10) {
            g_DealerSeat=pl[1]; g_HandNum=(pl[8]<<8)|pl[9];
            g_Pot=0; g_CurrentBet=0; g_CommunityCount=0;
            g_MyCards[0]=0;g_MyCards[1]=0;
            {u8 i;for(i=0;i<5;i++)g_Community[i]=0;}
            g_WaitingInput=FALSE;
            DrawAll();
        }
        else if(pkt==PKT_DEAL_HOLE && len>=3) {
            g_MyCards[0]=pl[1]; g_MyCards[1]=pl[2];
            DrawMyCards(); DrawSeatCards(g_MySeat);
        }
        else if(pkt==PKT_COMMUNITY && len>=7) {
            u8 phase=pl[1],i;
            if(phase==1){g_CommunityCount=3;for(i=0;i<3;i++)g_Community[i]=pl[2+i];}
            else if(phase==2){g_CommunityCount=4;g_Community[3]=pl[5];}
            else if(phase==3){g_CommunityCount=5;g_Community[4]=pl[6];}
            DrawCommunity();
        }
        else if(pkt==PKT_ACTION_PROMPT && len>=8) {
            g_ActionSeat=pl[1];
            g_CurrentBet=(pl[2]<<8)|pl[3];
            g_MinRaise=(pl[4]<<8)|pl[5];
            g_Pot=(pl[6]<<8)|pl[7];
            DrawHUD();
            if(g_ActionSeat==g_MySeat) {
                g_WaitingInput=TRUE;
                g_SelAction=(g_CurrentBet>0)?ACT_CALL:ACT_CHECK;
                g_RaiseAmount=g_MinRaise;
                if(g_RaiseAmount<20)g_RaiseAmount=20;
                DrawActionBar();
            } else {
                g_WaitingInput=FALSE;
                NB_ClearRow(22);NB_ClearRow(23);
                NB_Text(6,22,"TURNO DE J");
                NB_Num(16,22,g_ActionSeat+1);
            }
        }
        else if(pkt==PKT_ACTION_RESULT && len>=7) {
            u8 seat=pl[1],action=pl[2];
            NB_ClearRow(21);
            NB_Text(10,21,"J"); NB_Num(11,21,seat+1);
            NB_Text(13,21,g_ActNames[action<5?action:0]);
            g_LastAction[seat]=action;
        }
        else if(pkt==PKT_CHIP_UPDATE && len>=1+MAX_SEATS*2) {
            u8 i;
            for(i=0;i<MAX_SEATS;i++) g_Chips[i]=(pl[1+i*2]<<8)|pl[1+i*2+1];
            {u8 s;for(s=0;s<MAX_SEATS;s++)DrawSeatInfo(s);}
        }
        else if(pkt==PKT_SHOWDOWN && len>=7) {
            u8 winner=pl[1],rank=pl[2];
            u16 potWon=(pl[3]<<8)|pl[4];
            NB_ClearRow(21);NB_ClearRow(22);
            NB_Text(8,21,"GANA J"); NB_Num(14,21,winner+1);
            NB_Text(16,21,"BOTE:"); NB_Num(22,21,potWon);
            // Show winner cards if provided
            if(pl[5]!=0) {
                DrawCardTall(13,18,pl[5]);
                DrawCardTall(16,18,pl[6]);
            }
            g_WaitingInput=FALSE;
        }
        else if(pkt==PKT_ELIMINATED && len>=3) {
            u8 seat=pl[1];
            g_Chips[seat]=0;
            DrawSeatInfo(seat);
        }
        else if(pkt==PKT_GAME_OVER && len>=2) {
            u8 winner=pl[1];
            NB_ClearRow(10);NB_ClearRow(11);
            NB_Text(8,10,"PARTIDA TERMINADA");
            NB_Text(10,11,"GANA J"); NB_Num(16,11,winner+1);
            NB_Text(8,23,"ESPACIO PARA SALIR");
            g_WaitingInput=FALSE;
        }
    }
}

void Net_Poll(void) {
    u16 avail; u8 hdr[6]; u8 payload[200]; u8 maxPkts;
    if(g_Conn==NET_INVALID_CONN)return;
    maxPkts=4;
    while(maxPkts--){
        avail=Net_Available(g_Conn); if(avail<6)break;
        Net_Recv(g_Conn,hdr,6);
        if(hdr[0]!=PROTO_MAGIC_0||hdr[1]!=PROTO_MAGIC_1)break;
        if(hdr[5]>0){avail=Net_Available(g_Conn);if(avail<hdr[5])break;Net_Recv(g_Conn,payload,hdr[5]);}
        Net_ProcessPacket(hdr[2],hdr[4],payload,hdr[5]);
    }
    g_PingTimer++;if(g_PingTimer>=PING_INTERVAL){g_PingTimer=0;Net_SendPing();}
}

// ── Lobby ───────────────────────────────────────────────────────────

void Lobby_Draw(void) {
    u8 i; u16 idx;
    for(idx=0;idx<768;idx++)g_NB[idx]=T_BLK; g_FullFlush=TRUE;
    NB_Text(8,1,"TEXAS HOLDEM"); NB_Text(6,3,"SALAS DISPONIBLES");
    if(g_LBN==0){NB_Text(6,8,"NO HAY SALAS");NB_Text(6,10,"PULSA C PARA CREAR");}
    else{NB_Text(8,5,"SALA  JUGADORES");
        for(i=0;i<g_LBN;i++){u8 row=7+i*2;if(row>20)break;if(i==g_LBC)NB_Text(6,row,">");NB_Num(8,row,g_LB[i].rid);NB_Num(14,row,g_LB[i].np);NB_Text(16,row,"/6");}}
    NB_Text(4,22,"C CREAR  ENTER UNIR");NB_Text(4,23,"R REFRESCAR  ESC SALIR");
}

void Lobby_ProcessInput(void) {
    if(g_KUp){g_KUp=0;if(g_LBC>0){g_LBC--;Lobby_Draw();}}
    if(g_KDown){g_KDown=0;if(g_LBN>0&&g_LBC<g_LBN-1){g_LBC++;Lobby_Draw();}}
    if(g_KRet){g_KRet=0;if(g_LBN>0){Net_JoinRoom(g_LB[g_LBC].rid);g_GS=GS_LOBBY_WAIT;}}
    if(g_KC){g_KC=0;Net_CreateRoom();g_GS=GS_LOBBY_WAIT;}
    if(g_KR){g_KR=0;Net_RequestRoomList();g_GS=GS_LOBBY_WAIT;}
}

// ── Main ────────────────────────────────────────────────────────────

void main(void) {
    u8 i;

    *((u8*)0xF3DB)=0;

    // Diag + connect (Screen 0)
    Diag_ShowNetInfo();
    g_Online=Net_ConnectToServer();

    // Screen 4
    VDP_SetMode(VDP_MODE_SCREEN4);
    VDP_SetColor(1);
    VDP_SetSpriteFlag(VDP_SPRITE_SIZE_8|VDP_SPRITE_SCALE_1);
    for(i=0;i<32;i++) VDP_SetSpriteExUniColor(i,0,209,0,0);
    VDP_LoadTileset();

    // Init chips
    for(i=0;i<MAX_SEATS;i++){g_Chips[i]=0;g_LastAction[i]=255;}
    g_MyCards[0]=0;g_MyCards[1]=0;g_CommunityCount=0;g_Pot=0;

    if(g_Online){g_GS=GS_LOBBY_WAIT;Net_RequestRoomList();}
    else{
        // No hay modo offline — necesita servidor
        {u16 si;for(si=0;si<768;si++)g_NB[si]=T_BLK;}
        NB_Text(6,10,"NECESITA CONEXION");NB_Text(6,12,"ESC PARA SALIR");
        Halt();NB_Flush();
        while(1){Halt();Keyboard_Update();if(Keyboard_IsKeyPressed(KEY_ESC))break;}
        Bios_Exit(0);return;
    }

    // Single game loop
    while(1) {
        Halt(); NB_Flush();
        Keyboard_Update();
        *((u16*)0xF3F8)=*((u16*)0xF3FA);

        // Keys with anti-bounce
        if(g_GS!=GS_PLAYING||!g_WaitingInput){
            if(g_MoveDelay>0){g_MoveDelay--;}
            else{
                if(Keyboard_IsKeyPressed(KEY_UP)){g_KUp=1;g_MoveDelay=8;}
                if(Keyboard_IsKeyPressed(KEY_DOWN)){g_KDown=1;g_MoveDelay=8;}
                if(Keyboard_IsKeyPressed(KEY_RET)){g_KRet=1;g_MoveDelay=15;}
                if(Keyboard_IsKeyPressed(KEY_C)){g_KC=1;g_MoveDelay=15;}
                if(Keyboard_IsKeyPressed(KEY_R)){g_KR=1;g_MoveDelay=15;}
                if(Keyboard_IsKeyPressed(KEY_ESC)){g_KEsc=1;}
                if(Keyboard_IsKeyPressed(KEY_S)){g_KS=1;g_MoveDelay=15;}
            }
        }

        if(g_KEsc){g_KEsc=0;break;}

        // ── LOBBY_WAIT ──
        if(g_GS==GS_LOBBY_WAIT){Net_Poll();}
        // ── LOBBY ──
        else if(g_GS==GS_LOBBY){
            Lobby_ProcessInput();
            {u16 av=Net_Available(g_Conn);
             if(av>=6){u8 hdr[6],pl2[200];Net_Recv(g_Conn,hdr,6);
               if(hdr[0]==PROTO_MAGIC_0&&hdr[1]==PROTO_MAGIC_1){
                 if(hdr[5]>0){while(Net_Available(g_Conn)<hdr[5])Halt();Net_Recv(g_Conn,pl2,hdr[5]);}
                 Net_ProcessPacket(hdr[2],hdr[4],pl2,hdr[5]);}}}
        }
        // ── WAITING ──
        else if(g_GS==GS_WAITING){
            Net_Poll();
            NB_Text(6,8,"SALA:"); NB_Num(12,8,g_RoomId);
            NB_Text(6,10,"TU ERES P"); NB_SET(15,10,T_F0+g_MyPid);
            NB_Text(6,12,"ESPERANDO JUGADORES");
            {u8 pi2,row2=14;
             for(pi2=0;pi2<MAX_SEATS;pi2++){
                 NB_Text(8,row2,"P"); NB_SET(9,row2,T_F0+pi2+1);
                 if(g_ActiveP&(1<<pi2)) NB_Text(11,row2,"OK    ");
                 else NB_Text(11,row2,"      ");
                 row2++;
             }}
            if(g_MyPid==1) NB_Text(4,22,"S EMPEZAR  ESC SALIR");
            else NB_Text(4,22,"ESPERANDO HOST  ESC");
            if(g_KS&&g_MyPid==1){
                g_KS=0;
                g_SendBuf[0]=PROTO_MAGIC_0;g_SendBuf[1]=PROTO_MAGIC_1;
                g_SendBuf[2]=CMD_GAME_START;g_SendBuf[3]=g_RoomId;
                g_SendBuf[4]=g_MyPid;g_SendBuf[5]=0;
                Net_Send(g_Conn,g_SendBuf,6);
                {u16 si;for(si=0;si<768;si++)g_NB[si]=T_BLK;}
                Board_Draw();
                g_FullFlush=TRUE;
                g_GS=GS_PLAYING;
            }
        }
        // ── PLAYING ──
        else if(g_GS==GS_PLAYING){
            Net_Poll();

            // Input when it's my turn
            if(g_WaitingInput){
                if(g_MoveDelay>0){g_MoveDelay--;}
                else{
                    if(Keyboard_IsKeyPressed(KEY_LEFT)){
                        if(g_SelAction==ACT_ALLIN) g_SelAction=ACT_RAISE;
                        else if(g_SelAction==ACT_RAISE) g_SelAction=(g_CurrentBet>0)?ACT_CALL:ACT_CHECK;
                        else if(g_SelAction==ACT_CHECK||g_SelAction==ACT_CALL) g_SelAction=ACT_FOLD;
                        DrawActionBar();g_MoveDelay=8;
                    }
                    if(Keyboard_IsKeyPressed(KEY_RIGHT)){
                        if(g_SelAction==ACT_FOLD) g_SelAction=(g_CurrentBet>0)?ACT_CALL:ACT_CHECK;
                        else if(g_SelAction==ACT_CHECK||g_SelAction==ACT_CALL) g_SelAction=ACT_RAISE;
                        else if(g_SelAction==ACT_RAISE) g_SelAction=ACT_ALLIN;
                        DrawActionBar();g_MoveDelay=8;
                    }
                    if(g_SelAction==ACT_RAISE){
                        if(Keyboard_IsKeyPressed(KEY_UP)){g_RaiseAmount+=20;DrawActionBar();g_MoveDelay=6;}
                        if(Keyboard_IsKeyPressed(KEY_DOWN)){if(g_RaiseAmount>g_MinRaise)g_RaiseAmount-=20;if(g_RaiseAmount<g_MinRaise)g_RaiseAmount=g_MinRaise;DrawActionBar();g_MoveDelay=6;}
                    }
                    if(Keyboard_IsKeyPressed(KEY_SPACE)||Keyboard_IsKeyPressed(KEY_RET)){
                        Net_SendAction(g_SelAction,g_RaiseAmount);
                        g_WaitingInput=FALSE;
                        NB_ClearRow(22);NB_ClearRow(23);
                        g_MoveDelay=15;
                    }
                }
            }
        }
    }

    // Room leave
    if(g_Online&&g_Conn!=NET_INVALID_CONN){
        g_SendBuf[0]=PROTO_MAGIC_0;g_SendBuf[1]=PROTO_MAGIC_1;
        g_SendBuf[2]=CMD_ROOM_LEAVE;g_SendBuf[3]=g_RoomId;
        g_SendBuf[4]=g_MyPid;g_SendBuf[5]=0;
        Net_Send(g_Conn,g_SendBuf,6);
    }
    for(i=0;i<32;i++) VDP_SetSpriteExUniColor(i,0,209,0,0);
    Bios_Exit(0);
}
