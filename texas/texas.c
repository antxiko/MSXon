// Texas Hold'em Poker — MSX2 Screen 4 — MSXonLINE
// Cliente puro: el servidor es el dealer
// GAME_ID=0x05, RELAY mode
#include "msxgl.h"
#include "vdp.h"
#include "input.h"
#include "bios.h"
#include "system.h"
#include "dos.h"
#include "lobby.h"
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
#define T_VB    13
#define T_VR    26
#define T_FELT  39
#define T_SEP   49
#define T_FA    52
#define T_F0    78
#define T_BML   94
#define T_BMR   95

#define CARD_VAL(c)  ((c)&0x0F)
#define CARD_SUIT(c) (((c)>>4)&0x03)

// Actions
#define ACT_FOLD  0
#define ACT_CHECK 1
#define ACT_CALL  2
#define ACT_RAISE 3
#define ACT_ALLIN 4

// Server packet types
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
u8 g_NB[768]; // name buffer (lobby.c uses extern)

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

// UI
static u8 g_SelAction = ACT_CHECK;
static u16 g_RaiseAmount;
static bool g_WaitingInput = FALSE;
static u8 g_InputDelay = 0;

// Action names
const c8* g_ActNames[5] = {"NO IR","PASAR","IGUALAR","SUBIR","TODO"};

// Seat positions
const u8 g_SeatNameCol[6] = {  0,  0,  0, 24, 24, 24 };
const u8 g_SeatNameRow[6] = { 15, 10,  5,  5, 10, 15 };
const u8 g_SeatCardCol[6] = {  3,  2,  4, 24, 26, 25 };
const u8 g_SeatCardRow[6] = { 12,  7,  2,  2,  7, 12 };

// Lobby config
static const u8 SRV_IP[4] = {217,154,107,144};
static const LobbyConfig g_LobbyCfg = {
    "TEXAS HOLDEM", 0x05, 6, SRV_IP, 9876
};

// ── Drawing helpers ─────────────────────────────────────────────────

#define NB_SET(x,y,v) do{u16 _i=(u16)(y)*32+(x);u8 _v=(v);if(g_NB[_i]!=_v)g_NB[_i]=_v;}while(0)

void NB_Text(u8 x, u8 y, const c8* s) {
    while(*s && x<32) {
        u8 ch=(u8)*s, t;
        if(ch>='A'&&ch<='Z') t=T_FA+ch-'A';
        else if(ch>='0'&&ch<='9') t=T_F0+ch-'0';
        else if(ch=='>') t=T_SEP;
        else t=T_BLK;
        NB_SET(x,y,t); x++; s++;
    }
}

void NB_Num(u8 x, u8 y, u16 v) {
    c8 b[6]; u8 n=0,i; u16 d=10000;
    for(i=0;i<5;i++){u8 g=(u8)(v/d);v%=d;d/=10;if(g||n||i==4)b[n++]='0'+g;}
    b[n]=0; NB_Text(x,y,b);
}

void NB_ClearRow(u8 y) { u8 x; for(x=0;x<32;x++) NB_SET(x,y,T_BLK); }

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
}

// ── Card drawing ────────────────────────────────────────────────────

void DrawCardTall(u8 col, u8 row, u8 card) {
    u8 val, suit, vt, st;
    if(card==0){
        NB_SET(col,row,T_BLK);NB_SET(col+1,row,T_BLK);
        NB_SET(col,row+1,T_BLK);NB_SET(col+1,row+1,T_BLK);
        NB_SET(col,row+2,T_BLK);NB_SET(col+1,row+2,T_BLK);
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
    NB_Text(0,0,"BOTE"); NB_Text(4,0,"      "); NB_Num(5,0,g_Pot);
    NB_Text(12,0,"CIEGA"); NB_Num(18,0,10);
    NB_Num(22,0,20);
    NB_Text(26,0,"M"); NB_Num(28,0,g_HandNum);
}

void DrawSeatInfo(u8 s) {
    u8 col=g_SeatNameCol[s], row=g_SeatNameRow[s];
    NB_Text(col,row,"        ");
    NB_Text(col,row,"J"); NB_Num(col+1,row,s+1);
    NB_Num(col+4,row,g_Chips[s]);
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
    if(g_CurrentBet>0) NB_Text(8,22,"IGUALAR");
    else NB_Text(8,22,"PASAR");
    NB_Text(17,22,"SUBIR"); NB_Text(24,22,"TODO");
    { u8 cc;
      if(g_SelAction==ACT_FOLD) cc=3;
      else if(g_SelAction==ACT_CHECK||g_SelAction==ACT_CALL) cc=10;
      else if(g_SelAction==ACT_RAISE) cc=18;
      else cc=25;
      NB_Text(cc,23,">>>"); }
    if(g_SelAction==ACT_RAISE) { NB_Num(18,23,g_RaiseAmount); }
}

void DrawAll(void) {
    u8 s;
    DrawHUD();
    for(s=0;s<MAX_SEATS;s++) { DrawSeatInfo(s); DrawSeatCards(s); }
    DrawCommunity();
    DrawMyCards();
    DrawActionBar();
}

// ── Game packet handler (called by lobby) ───────────────────────────

void Game_OnPacket(u8 cmd, u8 senderPid, u8* pl, u8 len)
{
    if(cmd != CMD_STATE_UPDATE || len < 1) return;
    {
    u8 pkt = pl[0];

    if(pkt==PKT_HAND_START && len>=10) {
        g_DealerSeat=pl[1]; g_HandNum=(pl[8]<<8)|pl[9];
        g_Pot=0; g_CurrentBet=0; g_CommunityCount=0;
        g_MyCards[0]=0;g_MyCards[1]=0;
        {u8 i;for(i=0;i<5;i++)g_Community[i]=0;}
        g_WaitingInput=FALSE;
        Board_Draw();
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
            g_RaiseAmount=g_MinRaise; if(g_RaiseAmount<20)g_RaiseAmount=20;
            DrawActionBar();
        } else {
            g_WaitingInput=FALSE;
            NB_ClearRow(22);NB_ClearRow(23);
            NB_Text(6,22,"TURNO DE J"); NB_Num(16,22,g_ActionSeat+1);
        }
    }
    else if(pkt==PKT_ACTION_RESULT && len>=7) {
        u8 seat=pl[1],action=pl[2];
        NB_ClearRow(21);
        NB_Text(10,21,"J"); NB_Num(11,21,seat+1);
        NB_Text(13,21,g_ActNames[action<5?action:0]);
    }
    else if(pkt==PKT_CHIP_UPDATE && len>=1+MAX_SEATS*2) {
        u8 i;
        for(i=0;i<MAX_SEATS;i++) g_Chips[i]=(pl[1+i*2]<<8)|pl[1+i*2+1];
        {u8 s;for(s=0;s<MAX_SEATS;s++)DrawSeatInfo(s);}
    }
    else if(pkt==PKT_SHOWDOWN && len>=7) {
        u8 winner=pl[1];
        u16 potWon=(pl[3]<<8)|pl[4];
        NB_ClearRow(21);NB_ClearRow(22);
        NB_Text(8,21,"GANA J"); NB_Num(14,21,winner+1);
        NB_Text(16,21,"BOTE"); NB_Num(22,21,potWon);
        if(pl[5]!=0) { DrawCardTall(13,18,pl[5]); DrawCardTall(16,18,pl[6]); }
        g_WaitingInput=FALSE;
    }
    else if(pkt==PKT_ELIMINATED && len>=3) {
        g_Chips[pl[1]]=0; DrawSeatInfo(pl[1]);
    }
    else if(pkt==PKT_GAME_OVER && len>=2) {
        NB_ClearRow(10);NB_ClearRow(11);
        NB_Text(8,10,"PARTIDA TERMINADA");
        NB_Text(10,11,"GANA J"); NB_Num(16,11,pl[1]+1);
        g_WaitingInput=FALSE;
    }
    }
}

// ── Main ────────────────────────────────────────────────────────────

void main(void)
{
    u8 i;

    *((u8*)0xF3DB) = 0;

    // Lobby: diag + connect (Screen 0)
    Lobby_Init(&g_LobbyCfg, Game_OnPacket);
    Lobby_SetTileOffsets(T_FA, T_F0);
    Lobby_Diag();
    Lobby_Connect();

    // Screen 4
    VDP_SetMode(VDP_MODE_SCREEN4);
    VDP_SetColor(1);
    VDP_SetSpriteFlag(VDP_SPRITE_SIZE_8 | VDP_SPRITE_SCALE_1);
    for(i = 0; i < 32; i++) VDP_SetSpriteExUniColor(i, 0, 209, 0, 0);
    VDP_LoadTileset();

    // Init
    for(i=0;i<MAX_SEATS;i++){g_Chips[i]=0;g_LastAction[i]=255;}
    g_MyCards[0]=0;g_MyCards[1]=0;g_CommunityCount=0;g_Pot=0;

    if(g_LobbyOnline) {
        Lobby_RequestRooms();
        g_LobbyState = LOBBY_ST_LIST_WAIT;
    } else {
        // No offline mode
        {u16 si;for(si=0;si<768;si++)g_NB[si]=0;}
        NB_Text(6,10,"NECESITA CONEXION");
        NB_Text(6,12,"ESC PARA SALIR");
        Halt(); VDP_WriteVRAM(g_NB,0x1800,0,768);
        while(1){Halt();Keyboard_Update();if(Keyboard_IsKeyPressed(KEY_ESC))break;}
        Bios_Exit(0); return;
    }

    // Single game loop
    while(1) {
        Halt();
        VDP_WriteVRAM(g_NB, 0x1800, 0, 768);

        Keyboard_Update();
        *((u16*)0xF3F8) = *((u16*)0xF3FA);

        if(Keyboard_IsKeyPressed(KEY_ESC)) break;

        // Lobby states
        if(g_LobbyState < LOBBY_ST_PLAYING) {
            Lobby_Update();
            if(g_LobbyState == LOBBY_ST_PLAYING) {
                g_MySeat = g_LobbyPid - 1;
                Board_Draw();
            }
        }
        // Playing
        else {
            Lobby_Poll();

            // Input when it's my turn
            if(g_WaitingInput) {
                if(g_InputDelay>0){g_InputDelay--;}
                else{
                    if(Keyboard_IsKeyPressed(KEY_LEFT)){
                        if(g_SelAction==ACT_ALLIN) g_SelAction=ACT_RAISE;
                        else if(g_SelAction==ACT_RAISE) g_SelAction=(g_CurrentBet>0)?ACT_CALL:ACT_CHECK;
                        else if(g_SelAction==ACT_CHECK||g_SelAction==ACT_CALL) g_SelAction=ACT_FOLD;
                        DrawActionBar();g_InputDelay=8;
                    }
                    if(Keyboard_IsKeyPressed(KEY_RIGHT)){
                        if(g_SelAction==ACT_FOLD) g_SelAction=(g_CurrentBet>0)?ACT_CALL:ACT_CHECK;
                        else if(g_SelAction==ACT_CHECK||g_SelAction==ACT_CALL) g_SelAction=ACT_RAISE;
                        else if(g_SelAction==ACT_RAISE) g_SelAction=ACT_ALLIN;
                        DrawActionBar();g_InputDelay=8;
                    }
                    if(g_SelAction==ACT_RAISE){
                        if(Keyboard_IsKeyPressed(KEY_UP)){g_RaiseAmount+=20;DrawActionBar();g_InputDelay=6;}
                        if(Keyboard_IsKeyPressed(KEY_DOWN)){if(g_RaiseAmount>g_MinRaise)g_RaiseAmount-=20;if(g_RaiseAmount<g_MinRaise)g_RaiseAmount=g_MinRaise;DrawActionBar();g_InputDelay=6;}
                    }
                    if(Keyboard_IsKeyPressed(KEY_SPACE)||Keyboard_IsKeyPressed(KEY_RET)){
                        u8 pl[3];
                        pl[0]=g_SelAction; pl[1]=(u8)(g_RaiseAmount>>8); pl[2]=(u8)(g_RaiseAmount&0xFF);
                        Lobby_SendStateUpdate(pl, 3);
                        g_WaitingInput=FALSE;
                        NB_ClearRow(22);NB_ClearRow(23);
                        g_InputDelay=15;
                    }
                }
            }
        }
    }

    Lobby_SendRoomLeave();
    for(i=0;i<32;i++) VDP_SetSpriteExUniColor(i,0,209,0,0);
    Bios_Exit(0);
}
