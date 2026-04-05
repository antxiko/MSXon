// Tetris 4P — MSX2 Screen 4 — MSXonLINE
// 4 players, 8 cols each, 20 rows, shadow buffer
// GAME_ID=0x06, RELAY mode
#include "msxgl.h"
#include "vdp.h"
#include "input.h"
#include "bios.h"
#include "system.h"
#include "dos.h"
#include "protocol.h"
#include "network.h"
#include "log.h"

// ── Layout ──────────────────────────────────────────────────────────
#define NP    4
#define BW    8
#define BH    20
#define HDR   4

// ── Tiles ───────────────────────────────────────────────────────────
#define T_EMP   0
#define T_P1    1
#define T_P2    2
#define T_P3    3
#define T_P4    4
#define T_GRB   5
#define T_FLS   6
#define T_SEP   7
#define T_FA    8
#define T_F0    34
#define T_SPC   44
#define T_BG1   45
#define T_BG2   46
#define T_BG3   47
#define T_BG4   48

// ── Patterns ────────────────────────────────────────────────────────
static const u8 PAT_BLK[8]={0xFF,0xC1,0xBF,0xBF,0xBF,0xBF,0xBF,0xFF};
static const u8 PAT_EMP[8]={0,0,0,0,0,0,0,0};
static const u8 PAT_SEP[8]={0,0,0,0xFF,0xFF,0,0,0};
static const u8 PAT_GRB[8]={0xFF,0xAA,0xD5,0xAA,0xD5,0xAA,0xD5,0xFF};
static const u8 PAT_FLS[8]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

static const u8 FONT[][8]={
 {0,0x38,0x44,0x44,0x7C,0x44,0x44,0},{0,0x78,0x44,0x78,0x44,0x44,0x78,0},
 {0,0x3C,0x40,0x40,0x40,0x40,0x3C,0},{0,0x78,0x44,0x44,0x44,0x44,0x78,0},
 {0,0x7C,0x40,0x78,0x40,0x40,0x7C,0},{0,0x7C,0x40,0x78,0x40,0x40,0x40,0},
 {0,0x3C,0x40,0x4C,0x44,0x44,0x3C,0},{0,0x44,0x44,0x7C,0x44,0x44,0x44,0},
 {0,0x38,0x10,0x10,0x10,0x10,0x38,0},{0,0x1C,0x04,0x04,0x04,0x44,0x38,0},
 {0,0x44,0x48,0x70,0x48,0x44,0x44,0},{0,0x40,0x40,0x40,0x40,0x40,0x7C,0},
 {0,0x44,0x6C,0x54,0x44,0x44,0x44,0},{0,0x44,0x64,0x54,0x4C,0x44,0x44,0},
 {0,0x38,0x44,0x44,0x44,0x44,0x38,0},{0,0x78,0x44,0x44,0x78,0x40,0x40,0},
 {0,0x38,0x44,0x44,0x54,0x48,0x34,0},{0,0x78,0x44,0x44,0x78,0x48,0x44,0},
 {0,0x3C,0x40,0x38,0x04,0x04,0x78,0},{0,0x7C,0x10,0x10,0x10,0x10,0x10,0},
 {0,0x44,0x44,0x44,0x44,0x44,0x38,0},{0,0x44,0x44,0x44,0x44,0x28,0x10,0},
 {0,0x44,0x44,0x44,0x54,0x6C,0x44,0},{0,0x44,0x28,0x10,0x28,0x44,0x44,0},
 {0,0x44,0x44,0x28,0x10,0x10,0x10,0},{0,0x7C,0x04,0x08,0x10,0x20,0x7C,0},
 {0,0x38,0x4C,0x54,0x64,0x44,0x38,0},{0,0x10,0x30,0x10,0x10,0x10,0x38,0},
 {0,0x38,0x44,0x08,0x10,0x20,0x7C,0},{0,0x38,0x44,0x18,0x04,0x44,0x38,0},
 {0,0x08,0x18,0x28,0x48,0x7C,0x08,0},{0,0x7C,0x40,0x78,0x04,0x44,0x38,0},
 {0,0x38,0x40,0x78,0x44,0x44,0x38,0},{0,0x7C,0x04,0x08,0x10,0x10,0x10,0},
 {0,0x38,0x44,0x38,0x44,0x44,0x38,0},{0,0x38,0x44,0x44,0x3C,0x04,0x38,0},
 {0,0,0,0,0,0,0,0},
};

static const u8 CL_P[4][8]={
 {0x74,0x74,0x74,0x74,0x74,0x74,0x74,0x74},
 {0x96,0x96,0x96,0x96,0x96,0x96,0x96,0x96},
 {0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,0x3C},
 {0xBD,0xBD,0xBD,0xBD,0xBD,0xBD,0xBD,0xBD},
};
static const u8 CL_BG[4][8]={
 {0x44,0x44,0x44,0x44,0x44,0x44,0x44,0x44},
 {0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66},
 {0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC,0xCC},
 {0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD,0xDD},
};
static const u8 CL_FNT[8]={0xF1,0xF1,0xF1,0xF1,0xF1,0xF1,0xF1,0xF1};
static const u8 CL_EMP[8]={0x11,0x11,0x11,0x11,0x11,0x11,0x11,0x11};
static const u8 CL_SEP[8]={0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1,0xE1};
static const u8 CL_GRB[8]={0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE};
static const u8 CL_FLS[8]={0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

// ── Pieces ──────────────────────────────────────────────────────────
#define P4(a,b,c,d) (u16)(((u16)(a)<<12)|((u16)(b)<<8)|((u16)(c)<<4)|(u16)(d))
typedef struct { u8 w,h; u16 bits; } PRot;
typedef struct { u8 n; PRot r[4]; } PDef;

static const PDef PC[7]={
 {2,{{4,1,P4(0xF,0,0,0)},{1,4,P4(8,8,8,8)},{0,0,0},{0,0,0}}},
 {1,{{2,2,P4(0xC,0xC,0,0)},{0,0,0},{0,0,0},{0,0,0}}},
 {4,{{3,2,P4(4,0xE,0,0)},{2,3,P4(8,0xC,8,0)},{3,2,P4(0xE,4,0,0)},{2,3,P4(4,0xC,4,0)}}},
 {2,{{3,2,P4(6,0xC,0,0)},{2,3,P4(8,0xC,4,0)},{0,0,0},{0,0,0}}},
 {2,{{3,2,P4(0xC,6,0,0)},{2,3,P4(4,0xC,8,0)},{0,0,0},{0,0,0}}},
 {4,{{2,3,P4(8,8,0xC,0)},{3,2,P4(0xE,8,0,0)},{2,3,P4(0xC,4,4,0)},{3,2,P4(2,0xE,0,0)}}},
 {4,{{2,3,P4(4,4,0xC,0)},{3,2,P4(8,0xE,0,0)},{2,3,P4(0xC,8,8,0)},{3,2,P4(0xE,2,0,0)}}},
};

static const u16 BM[4][4]={
 {0x8000,0x4000,0x2000,0x1000},{0x0800,0x0400,0x0200,0x0100},
 {0x0080,0x0040,0x0020,0x0010},{0x0008,0x0004,0x0002,0x0001},
};
#define PB(b,r,c) ((b)&BM[r][c])

// ── Player ──────────────────────────────────────────────────────────
typedef struct {
    u8  bd[BH][BW];
    u16 sc;
    u8  ln, lv, dead;
    u8  pi, rot;
    i8  px, py;
    u8  nxt;
    u8  dt, di;
    u8  sd;
    u8  fl, fn;
    u8  fr[4];
    u8  dirty;
    u8  target;
    u8  garbQ;
} PL;

static PL g_P[NP];

// ── Network vars ────────────────────────────────────────────────────
static const u8 SERVER_IP[4] = { 217, 154, 107, 144 };
#define SERVER_PORT     9876
#define GAME_ID_TET     0x06
#define PING_INTERVAL   250
static NetConn g_Conn = NET_INVALID_CONN;
static u8 g_MyPid = 0;
static u8 g_RoomId = 0;
static u8 g_MySlot = 0;
static u16 g_PingTimer = 0;
static u8 g_SendBuf[20];
static bool g_Online = FALSE;
static u8 g_ActiveP = 0;
static u8 g_FrameCnt = 0;

// Game states
#define GSTATE_LOBBY_WAIT  0
#define GSTATE_LOBBY       1
#define GSTATE_WAITING     2
#define GSTATE_PLAYING     3
#define GSTATE_GAMEOVER    4
static u8 g_GameState = GSTATE_LOBBY_WAIT;
static u8 g_Winner = 0xFF;

// Lobby
#define LOBBY_MAX_ROOMS 20
typedef struct { u8 roomId; u8 gameId; u8 players; } LobbyRoom;
static LobbyRoom g_LobbyRooms[LOBBY_MAX_ROOMS];
static u8 g_LobbyCount = 0;
static u8 g_LobbyCursor = 0;
static u8 g_KeyUp = 0, g_KeyDown = 0, g_KeyRet = 0, g_KeyC = 0, g_KeyR = 0, g_KeyEsc = 0, g_KeyS = 0;

// ── Random ──────────────────────────────────────────────────────────
static u16 g_S;
static u8 R8(void){ g_S=g_S*25173+13849; return (u8)(g_S>>8); }

// ── Name buffer ─────────────────────────────────────────────────────
static u8 g_NB[768];
static bool g_FullFlush = TRUE;
static u8 g_PrevScore[NP];
static u8 g_PrevLines[NP];

#define NB_SET(x,y,v) do { \
    u16 _i=(u16)(y)*32+(x); u8 _v=(v); \
    if(g_NB[_i]!=_v) g_NB[_i]=_v; \
} while(0)

static void NB_Flush(void) {
    VDP_WriteVRAM(g_NB, 0x1800, 0, 768);
}

static void NB_Text(u8 x, u8 y, const c8* s) {
    while(*s && x<32) {
        u8 ch=(u8)*s, t;
        if(ch>='A'&&ch<='Z') t=T_FA+ch-'A';
        else if(ch>='0'&&ch<='9') t=T_F0+ch-'0';
        else if(ch=='>') t=T_SEP; // visible marker for cursor
        else t=T_SPC;
        NB_SET(x,y,t); x++; s++;
    }
}

static void NB_Num(u8 x, u8 y, u16 v) {
    c8 b[6]; u8 n=0,i; u16 d=10000;
    for(i=0;i<5;i++){u8 g=(u8)(v/d);v%=d;d/=10;if(g||n||i==4)b[n++]='0'+g;}
    b[n]=0; NB_Text(x,y,b);
}

// ── Load tileset ────────────────────────────────────────────────────
static void LoadTiles(void) {
    u8 b,t; u16 pb,cb;
    for(b=0;b<3;b++){
        pb=b*2048; cb=0x2000+pb;
        VDP_WriteVRAM(PAT_EMP,pb,0,8); VDP_WriteVRAM(CL_EMP,cb,0,8);
        for(t=0;t<4;t++){
            VDP_WriteVRAM(PAT_BLK,pb+(t+1)*8,0,8);
            VDP_WriteVRAM(CL_P[t],cb+(t+1)*8,0,8);
        }
        VDP_WriteVRAM(PAT_GRB,pb+5*8,0,8); VDP_WriteVRAM(CL_GRB,cb+5*8,0,8);
        VDP_WriteVRAM(PAT_FLS,pb+6*8,0,8); VDP_WriteVRAM(CL_FLS,cb+6*8,0,8);
        VDP_WriteVRAM(PAT_SEP,pb+7*8,0,8); VDP_WriteVRAM(CL_SEP,cb+7*8,0,8);
        for(t=0;t<26;t++){
            VDP_WriteVRAM(FONT[t],pb+(8+t)*8,0,8);
            VDP_WriteVRAM(CL_FNT,cb+(8+t)*8,0,8);
        }
        for(t=0;t<10;t++){
            VDP_WriteVRAM(FONT[26+t],pb+(34+t)*8,0,8);
            VDP_WriteVRAM(CL_FNT,cb+(34+t)*8,0,8);
        }
        VDP_WriteVRAM(PAT_EMP,pb+44*8,0,8); VDP_WriteVRAM(CL_EMP,cb+44*8,0,8);
        for(t=0;t<4;t++){
            VDP_WriteVRAM(PAT_EMP,pb+(45+t)*8,0,8);
            VDP_WriteVRAM(CL_BG[t],cb+(45+t)*8,0,8);
        }
    }
}

// ── Piece helpers ───────────────────────────────────────────────────
static const PRot* GR(u8 pi, u8 rot) { return &PC[pi].r[rot%PC[pi].n]; }

static u8 Valid(PL* p, u8 pi, u8 rot, i8 x, i8 y) {
    const PRot* s=GR(pi,rot); u8 r,c;
    for(r=0;r<s->h;r++) for(c=0;c<s->w;c++)
        if(PB(s->bits,r,c)){
            i8 nx=x+(i8)c, ny=y+(i8)r;
            if(nx<0||nx>=BW||ny>=BH) return 0;
            if(ny>=0&&p->bd[ny][nx]) return 0;
        }
    return 1;
}

static void Spawn(PL* p) {
    p->pi=p->nxt; p->nxt=R8()%7; p->rot=0;
    p->px=(BW/2)-1; p->py=0;
    if(!Valid(p,p->pi,p->rot,p->px,p->py)) p->dead=1;
}

static void AddGarbage(PL* p, u8 count);

static void Lock(PL* p, u8 pi) {
    const PRot* s=GR(p->pi,p->rot); u8 r,c;
    for(r=0;r<s->h;r++) for(c=0;c<s->w;c++)
        if(PB(s->bits,r,c)){
            i8 by=p->py+(i8)r, bx=p->px+(i8)c;
            if(by>=0&&by<BH&&bx>=0&&bx<BW)
                p->bd[by][bx]=pi+1;
        }
    p->fn=0;
    for(r=0;r<BH;r++){
        u8 full=1;
        for(c=0;c<BW;c++) if(!p->bd[r][c]){full=0;break;}
        if(full&&p->fn<4) p->fr[p->fn++]=r;
    }
    if(p->fn>0){
        static const u16 st[5]={0,1,3,6,10};
        static const u8 garbT[5]={0,0,1,2,4};
        p->fl=6; p->sc+=st[p->fn]; p->ln+=p->fn;
        p->lv=(p->ln/10)+1;
        p->garbQ=garbT[p->fn];
    } else { p->dirty=1; Spawn(p); }
}

static void AddGarbage(PL* p, u8 count) {
    u8 i, r, c, gap;
    if(p->dead) return;
    for(i=0;i<count;i++){
        for(c=0;c<BW;c++) if(p->bd[0][c]){p->dead=1;return;}
        for(r=0;r<BH-1;r++) for(c=0;c<BW;c++) p->bd[r][c]=p->bd[r+1][c];
        gap=R8()%BW;
        for(c=0;c<BW;c++) p->bd[BH-1][c]=(c==gap)?0:T_GRB;
    }
    p->dirty=1;
}

static void ClearLines(PL* p) {
    u8 i,r,c;
    for(i=0;i<p->fn;i++){
        u8 row=p->fr[i];
        for(r=row;r>0;r--) for(c=0;c<BW;c++) p->bd[r][c]=p->bd[r-1][c];
        for(c=0;c<BW;c++) p->bd[0][c]=0;
    }
    p->fn=0; p->dirty=1;
    if(p->garbQ>0){
        // Online: send garbage to target's real MSX
        // Offline: apply locally
        if(!g_Online)
            AddGarbage(&g_P[p->target], p->garbQ);
        else if(p == &g_P[g_MySlot]) {
            // Send garbage command to target
            u8 tgt = p->target;
            g_SendBuf[0]=PROTO_MAGIC_0; g_SendBuf[1]=PROTO_MAGIC_1;
            g_SendBuf[2]=CMD_STATE_UPDATE; g_SendBuf[3]=g_RoomId; g_SendBuf[4]=g_MyPid;
            g_SendBuf[5]=3; g_SendBuf[6]=0xFE; // special: garbage command
            g_SendBuf[7]=tgt; g_SendBuf[8]=p->garbQ;
            Net_Send(g_Conn, g_SendBuf, 9);
        }
        p->garbQ=0;
    }
    Spawn(p);
}

static void DoMove(PL* p, i8 dx) {
    if(p->dead||p->fl) return;
    if(Valid(p,p->pi,p->rot,p->px+dx,p->py)) p->px+=dx;
}

static void DoRotate(PL* p) {
    u8 nr;
    if(p->dead||p->fl) return;
    nr=(p->rot+1)%PC[p->pi].n;
    if(Valid(p,p->pi,nr,p->px,p->py)) p->rot=nr;
    else if(Valid(p,p->pi,nr,p->px-1,p->py)){p->px--;p->rot=nr;}
    else if(Valid(p,p->pi,nr,p->px+1,p->py)){p->px++;p->rot=nr;}
}

static void DoDrop(PL* p, u8 pi) {
    if(p->dead||p->fl) return;
    if(Valid(p,p->pi,p->rot,p->px,p->py+1)) p->py++;
    else Lock(p,pi);
}

static void PlayerUpdate(PL* p, u8 pi) {
    if(p->dead) return;
    if(p->fl>0){ p->fl--; if(p->fl==0) ClearLines(p); return; }
    p->dt++;
    if(p->dt>=(p->sd?1:p->di)){ p->dt=0; DoDrop(p,pi); }
}

static void InitP(PL* p, u8 idx) {
    u8 r,c;
    for(r=0;r<BH;r++) for(c=0;c<BW;c++) p->bd[r][c]=0;
    p->sc=0; p->ln=0; p->lv=1; p->dead=0;
    p->dt=0; p->di=32; p->sd=0;
    p->fl=0; p->fn=0; p->garbQ=0;
    p->target=(idx+1)%NP;
    p->pi=R8()%7; p->nxt=R8()%7;
    p->rot=0; p->px=(BW/2)-1; p->py=0;
    if(!Valid(p,p->pi,p->rot,p->px,p->py)) p->dead=1;
}

// Arrow sprite pattern (8x8, pointing down)
static const u8 g_ArrowSpr[8] = {
    0x18, 0x18, 0x18, 0x7E, 0x3C, 0x18, 0x00, 0x00
};

// Player colors for sprites: cyan, ltred, ltgreen, yellow
static const u8 g_SprColor[4] = { 7, 9, 3, 11 };

// ── Render ──────────────────────────────────────────────────────────
static i8 g_PrevPX[NP], g_PrevPY[NP];
static u8 g_PrevPI[NP], g_PrevRot[NP];

static void DrawHeader(u8 pi) {
    u8 x=pi*8;
    if(g_PrevScore[pi]!=(u8)g_P[pi].sc || g_PrevLines[pi]!=g_P[pi].ln) {
        NB_Text(x,0,"P"); NB_SET(x+1,0,T_F0+pi+1);
        NB_Text(x+3,0,"     "); NB_Num(x+3,0,g_P[pi].sc);
        NB_Text(x,1,"L       "); NB_Num(x+2,1,(u16)g_P[pi].ln);
        NB_Text(x,2,"LV      "); NB_Num(x+3,2,(u16)g_P[pi].lv);
        g_PrevScore[pi]=(u8)g_P[pi].sc;
        g_PrevLines[pi]=g_P[pi].ln;
    }
}

static void ErasePiece(u8 pi) {
    u8 x0=pi*8, bg=T_BG1+pi, r, c;
    PL* p=&g_P[pi];
    const PRot* s;
    if(g_PrevPI[pi]==0xFF) return;
    s=GR(g_PrevPI[pi], g_PrevRot[pi]);
    for(r=0;r<s->h;r++) for(c=0;c<s->w;c++)
        if(PB(s->bits,r,c)){
            i8 bx=g_PrevPX[pi]+(i8)c, by=g_PrevPY[pi]+(i8)r;
            if(bx>=0&&bx<BW&&by>=0&&by<BH){
                u8 v=p->bd[by][bx];
                u8 t=(v==0)?bg:(v>=1&&v<=4)?v:(v==5)?T_GRB:bg;
                NB_SET(x0+bx, HDR+by, t);
            }
        }
}

static void DrawPiece(u8 pi) {
    u8 x0=pi*8, r, c, pv=pi+1;
    PL* p=&g_P[pi];
    const PRot* s;
    if(p->dead||p->fl) { g_PrevPI[pi]=0xFF; return; }
    s=GR(p->pi, p->rot);
    for(r=0;r<s->h;r++) for(c=0;c<s->w;c++)
        if(PB(s->bits,r,c)){
            i8 bx=p->px+(i8)c, by=p->py+(i8)r;
            if(bx>=0&&bx<BW&&by>=0&&by<BH)
                NB_SET(x0+bx, HDR+by, pv);
        }
    g_PrevPX[pi]=p->px; g_PrevPY[pi]=p->py;
    g_PrevPI[pi]=p->pi; g_PrevRot[pi]=p->rot;
}

static void DrawBoardFull(u8 pi) {
    u8 r,c,x0,bg;
    PL* p;
    x0=pi*8; bg=T_BG1+pi; p=&g_P[pi];
    for(r=0;r<BH;r++)
        for(c=0;c<BW;c++){
            u8 v=p->bd[r][c], t;
            if(v==0) t=bg;
            else if(v>=1&&v<=4) t=v;
            else if(v==5) t=T_GRB;
            else t=bg;
            NB_SET(x0+c, HDR+r, t);
        }
    if(p->dead) NB_Text(x0+1,HDR+9,"DEAD");
}

static void DrawFlash(u8 pi) {
    u8 fi, c, x0=pi*8;
    PL* p=&g_P[pi];
    for(fi=0;fi<p->fn;fi++){
        u8 fr2=p->fr[fi];
        u8 t=(p->fl&1)?T_FLS:(T_BG1+pi);
        for(c=0;c<BW;c++) NB_SET(x0+c, HDR+fr2, t);
    }
}

static void DrawBoard(u8 pi) {
    PL* p=&g_P[pi];
    if(p->dirty){ DrawBoardFull(pi); p->dirty=0; DrawPiece(pi); return; }
    if(p->fl){ DrawFlash(pi); return; }
    ErasePiece(pi);
    DrawPiece(pi);
}

// ── Diagnostico red (copiado de damas) ──────────────────────────────

void Diag_PrintDec(u8 val)
{
    u8 h, t, u;
    h = val / 100;
    t = (val % 100) / 10;
    u = val % 10;
    if(h > 0) DOS_CharOutput('0' + h);
    if(h > 0 || t > 0) DOS_CharOutput('0' + t);
    DOS_CharOutput('0' + u);
}

void Diag_PrintIP(const u8* ip)
{
    u8 i;
    for(i = 0; i < 4; i++)
    {
        Diag_PrintDec(ip[i]);
        if(i < 3) DOS_CharOutput('.');
    }
}

void Diag_ShowNetInfo(void)
{
    u8 localIP[4];
    u8 ok;

    DOS_StringOutput("================================\r\n$");
    DOS_StringOutput("   TETRIS ONLINE - DIAGNOSTICS\r\n$");
    DOS_StringOutput("================================\r\n\r\n$");

    DOS_StringOutput("UNAPI TCP/IP: $");
    ok = (tcpip_enumerate() > 0) ? 1 : 0;
    if(ok)
        DOS_StringOutput("ENCONTRADO\r\n$");
    else
    {
        DOS_StringOutput("NO ENCONTRADO\r\n\r\n$");
        DOS_StringOutput("Juego arrancara OFFLINE.\r\n$");
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

// ── Conexion (copiado de damas) ─────────────────────────────────────

void Net_Wait50(void) { u8 w; for(w = 0; w < 25; w++) Halt(); }

bool Net_ConnectToServer(void)
{
    u8 tcpState;
    u16 timeout;

    Log_Init();
    Log_Write("[INIT] Tetris arrancando");

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

// ── Lobby + Sala (copiado de damas) ─────────────────────────────────

void Net_RequestRoomList(void)
{
    if(g_Conn == NET_INVALID_CONN) return;
    g_SendBuf[0] = PROTO_MAGIC_0; g_SendBuf[1] = PROTO_MAGIC_1;
    g_SendBuf[2] = CMD_ROOM_LIST; g_SendBuf[3] = 0; g_SendBuf[4] = 0; g_SendBuf[5] = 0;
    Net_Send(g_Conn, g_SendBuf, 6);
}

void Net_CreateRoom(void)
{
    if(g_Conn == NET_INVALID_CONN) return;
    g_SendBuf[0] = PROTO_MAGIC_0; g_SendBuf[1] = PROTO_MAGIC_1;
    g_SendBuf[2] = CMD_ROOM_CREATE; g_SendBuf[3] = 0; g_SendBuf[4] = 0;
    g_SendBuf[5] = 3; g_SendBuf[6] = GAME_ID_TET; g_SendBuf[7] = 4; g_SendBuf[8] = 0x01;
    Net_Send(g_Conn, g_SendBuf, 9);
}

void Net_JoinRoom(u8 roomId)
{
    if(g_Conn == NET_INVALID_CONN) return;
    g_SendBuf[0] = PROTO_MAGIC_0; g_SendBuf[1] = PROTO_MAGIC_1;
    g_SendBuf[2] = CMD_ROOM_JOIN; g_SendBuf[3] = 0; g_SendBuf[4] = 0;
    g_SendBuf[5] = 1; g_SendBuf[6] = roomId;
    Net_Send(g_Conn, g_SendBuf, 7);
}

void Net_SendPing(void)
{
    if(g_Conn == NET_INVALID_CONN) return;
    g_SendBuf[0] = PROTO_MAGIC_0; g_SendBuf[1] = PROTO_MAGIC_1;
    g_SendBuf[2] = CMD_PING; g_SendBuf[3] = g_RoomId; g_SendBuf[4] = g_MyPid; g_SendBuf[5] = 0;
    Net_Send(g_Conn, g_SendBuf, 6);
}

// STATE_UPDATE
// Sync packet: 6 header + 85 payload = 91 bytes
// Payload: [pi, rot, px, py, dead, 80 bytes packed board]
// Board packed: 2 cells per byte (high nibble = even col, low nibble = odd col)
#define SYNC_INTERVAL 30  // ~2 per second at 60fps
static u8 g_SyncBuf[91];

void Net_SendSync(void)
{
    u8 r, c, idx;
    PL* p;
    if(g_Conn == NET_INVALID_CONN) return;

    p = &g_P[g_MySlot];

    g_SyncBuf[0] = PROTO_MAGIC_0;
    g_SyncBuf[1] = PROTO_MAGIC_1;
    g_SyncBuf[2] = CMD_STATE_UPDATE;
    g_SyncBuf[3] = g_RoomId;
    g_SyncBuf[4] = g_MyPid;
    g_SyncBuf[5] = 85; // payload length

    // Payload starts at byte 6
    g_SyncBuf[6] = p->pi;
    g_SyncBuf[7] = p->rot;
    g_SyncBuf[8] = (u8)p->px;
    g_SyncBuf[9] = (u8)p->py;
    g_SyncBuf[10] = p->dead;

    // Pack board: 160 cells → 80 bytes (2 cells per byte)
    idx = 11;
    for(r = 0; r < BH; r++)
    {
        for(c = 0; c < BW; c += 2)
        {
            g_SyncBuf[idx] = (p->bd[r][c] << 4) | (p->bd[r][c+1] & 0x0F);
            idx++;
        }
    }

    Net_Send(g_Conn, g_SyncBuf, 91);
}

// Forward
void Lobby_Draw(void);

void Net_ProcessPacket(u8 cmd, u8 senderPid, u8* payload, u8 len)
{
    if(cmd == CMD_ROOM_LIST && len >= 1)
    {
        u8 count = payload[0];
        u8 i;
        g_LobbyCount = 0;
        for(i = 0; i < count && i < LOBBY_MAX_ROOMS; i++)
        {
            u8 off = 1 + i * 3;
            if(payload[off + 1] == GAME_ID_TET)
            {
                g_LobbyRooms[g_LobbyCount].roomId = payload[off];
                g_LobbyRooms[g_LobbyCount].gameId = payload[off + 1];
                g_LobbyRooms[g_LobbyCount].players = payload[off + 2];
                g_LobbyCount++;
            }
        }
        g_LobbyCursor = 0;
        g_GameState = GSTATE_LOBBY;
        Lobby_Draw();
    }
    else if(cmd == CMD_ROOM_INFO && len >= 4)
    {
        u16 si2;
        g_RoomId = payload[0];
        g_MyPid = payload[3];
        g_MySlot = g_MyPid - 1;
        g_ActiveP = 0;
        {
            u8 n = payload[2], j;
            for(j = 0; j < n; j++) g_ActiveP |= (1 << j);
        }
        // Clear screen for waiting room
        for(si2 = 0; si2 < 768; si2++) g_NB[si2] = T_EMP;
        g_FullFlush = TRUE;
        g_GameState = GSTATE_WAITING;
    }
    else if(cmd == CMD_PLAYER_JOINED && len >= 1)
    {
        u8 jp = payload[0];
        if(jp >= 1 && jp <= 4) g_ActiveP |= (1 << (jp - 1));
    }
    else if(cmd == CMD_PLAYER_LEFT && len >= 1)
    {
        u8 lp = payload[0];
        if(lp >= 1 && lp <= 4) g_ActiveP &= ~(1 << (lp - 1));
    }
    else if(cmd == CMD_GAME_START)
    {
        g_MySlot = g_MyPid - 1;
        StartGame();
        g_GameState = GSTATE_PLAYING;
        g_Winner = 0xFF;
    }
    else if(cmd == CMD_STATE_UPDATE && len == 3 && payload[0] == 0xFE)
    {
        // Garbage command: someone is sending garbage to a target
        u8 tgtSlot = payload[1];
        u8 garbCount = payload[2];
        if(tgtSlot == g_MySlot)
            AddGarbage(&g_P[g_MySlot], garbCount);
    }
    else if(cmd == CMD_STATE_UPDATE && len >= 85)
    {
        u8 slot = senderPid - 1;
        if(slot < NP && slot != g_MySlot)
        {
            u8 r, c, idx;
            // Unpack piece state
            g_P[slot].pi = payload[0];
            g_P[slot].rot = payload[1];
            g_P[slot].px = (i8)payload[2];
            g_P[slot].py = (i8)payload[3];
            g_P[slot].dead = payload[4];

            // Unpack board: 80 bytes → 160 cells
            idx = 5;
            for(r = 0; r < BH; r++)
            {
                for(c = 0; c < BW; c += 2)
                {
                    g_P[slot].bd[r][c] = (payload[idx] >> 4) & 0x0F;
                    g_P[slot].bd[r][c+1] = payload[idx] & 0x0F;
                    idx++;
                }
            }
            g_P[slot].dirty = 1;
        }
    }
}

void Net_Poll(void)
{
    u16 avail;
    u8 hdr[6];
    u8 payload[200];
    u8 maxPkts;

    if(g_Conn == NET_INVALID_CONN) return;

    maxPkts = 4;
    while(maxPkts--)
    {
        avail = Net_Available(g_Conn);
        if(avail < 6) break;
        Net_Recv(g_Conn, hdr, 6);
        if(hdr[0] != PROTO_MAGIC_0 || hdr[1] != PROTO_MAGIC_1) break;
        if(hdr[5] > 0)
        {
            avail = Net_Available(g_Conn);
            if(avail < hdr[5]) break;
            Net_Recv(g_Conn, payload, hdr[5]);
        }
        Net_ProcessPacket(hdr[2], hdr[4], payload, hdr[5]);
    }

    g_PingTimer++;
    if(g_PingTimer >= PING_INTERVAL) { g_PingTimer = 0; Net_SendPing(); }
}

// ── Lobby Draw (copiado de damas) ───────────────────────────────────

void Lobby_Draw(void)
{
    u8 i;
    u16 idx;

    for(idx = 0; idx < 768; idx++) g_NB[idx] = T_EMP;
    g_FullFlush = TRUE;

    NB_Text(8, 1, "TETRIS ONLINE");
    NB_Text(6, 3, "SALAS DISPONIBLES");

    if(g_LobbyCount == 0)
    {
        NB_Text(6, 8, "NO HAY SALAS");
        NB_Text(6, 10, "PULSA C PARA CREAR");
    }
    else
    {
        NB_Text(8, 5, "SALA  JUGADORES");
        for(i = 0; i < g_LobbyCount; i++)
        {
            u8 row = 7 + i * 2;
            if(row > 20) break;
            if(i == g_LobbyCursor) NB_Text(6, row, ">");
            NB_Num(8, row, g_LobbyRooms[i].roomId);
            NB_Num(14, row, g_LobbyRooms[i].players);
            NB_Text(16, row, "/4");
        }
    }

    NB_Text(4, 22, "C CREAR  ENTER UNIR");
    NB_Text(4, 23, "R REFRESCAR  ESC SALIR");
}

void Lobby_ProcessInput(void)
{
    if(g_KeyUp) { g_KeyUp = 0; if(g_LobbyCursor > 0) { g_LobbyCursor--; Lobby_Draw(); } }
    if(g_KeyDown) { g_KeyDown = 0; if(g_LobbyCount > 0 && g_LobbyCursor < g_LobbyCount-1) { g_LobbyCursor++; Lobby_Draw(); } }
    if(g_KeyRet) { g_KeyRet = 0; if(g_LobbyCount > 0) { Net_JoinRoom(g_LobbyRooms[g_LobbyCursor].roomId); g_GameState = GSTATE_LOBBY_WAIT; } }
    if(g_KeyC) { g_KeyC = 0; Net_CreateRoom(); g_GameState = GSTATE_LOBBY_WAIT; }
    if(g_KeyR) { g_KeyR = 0; g_GameState = GSTATE_LOBBY_WAIT; Net_RequestRoomList(); }
}

// ── Input ───────────────────────────────────────────────────────────
static u8 g_Prev8 = 0xFF;
static u8 g_RepL = 0, g_RepR = 0;
static u8 g_MoveDelay = 0;

static void DoInput(void) {
    u8 row8 = Keyboard_Read(8);
    PL* p = &g_P[g_MySlot];
    u8 l=!(row8&0x10), r=!(row8&0x80), u=!(row8&0x20), d=!(row8&0x40);

    if(!p->dead && !p->fl){
        if(u && (g_Prev8&0x20)) DoRotate(p);
        p->sd=d;
        if(l&&!r){
            if(g_Prev8&0x10){DoMove(p,-1);g_RepL=0;}
            else{g_RepL++;if(g_RepL>=4)DoMove(p,-1);}
        } else g_RepL=0;
        if(r&&!l){
            if(g_Prev8&0x80){DoMove(p,1);g_RepR=0;}
            else{g_RepR++;if(g_RepR>=4)DoMove(p,1);}
        } else g_RepR=0;
        // SPACE = cycle target (bit 0 of row 8)
        {
            u8 sp=!(row8&0x01);
            if(sp && (g_Prev8&0x01)){
                u8 next=p->target, tries=0;
                do { next=(next+1)%NP; tries++; }
                while((next==g_MySlot || g_P[next].dead) && tries<NP);
                p->target=next;
            }
        }
    }
    g_Prev8=row8;
}

// ── StartGame ───────────────────────────────────────────────────────

void StartGame(void) {
    u8 i; u16 si;
    for(i=0;i<NP;i++) InitP(&g_P[i],i);
    for(i=0;i<NP;i++){
        g_PrevScore[i]=0xFF; g_PrevLines[i]=0xFF;
        g_PrevPI[i]=0xFF; g_P[i].dirty=1;
        if(g_Online && !(g_ActiveP&(1<<i))) g_P[i].dead=1;
    }
    for(si=0;si<768;si++) g_NB[si]=T_EMP;
    for(i=0;i<NP;i++){
        u8 x0=i*8, bg=T_BG1+i, r, c;
        for(c=0;c<8;c++) NB_SET(x0+c,3,T_SEP);
        for(r=0;r<BH;r++) for(c=0;c<BW;c++) NB_SET(x0+c,HDR+r,bg);
    }
    for(i=0;i<NP;i++) DrawHeader(i);
    g_FullFlush = TRUE;
}

// ── Main (single loop like damas) ───────────────────────────────────

void main(void)
{
    u8 i;
    u8 prevDead;

    *((u8*)0xF3DB) = 0;

    // Diag + connect (Screen 0)
    Diag_ShowNetInfo();
    g_Online = Net_ConnectToServer();

    // Screen 4
    VDP_SetMode(VDP_MODE_SCREEN4);
    VDP_SetColor(1);
    VDP_SetSpriteFlag(VDP_SPRITE_SIZE_8 | VDP_SPRITE_SCALE_1);
    VDP_LoadSpritePattern(g_ArrowSpr, 0, 1);
    for(i = 0; i < 32; i++)
        VDP_SetSpriteExUniColor(i, 0, 209, 0, 0);
    LoadTiles();

    g_S = *((u16*)0xFC9E);

    if(g_Online)
    {
        g_GameState = GSTATE_LOBBY_WAIT;
        Net_RequestRoomList();
    }
    else
    {
        g_GameState = GSTATE_PLAYING;
        g_MySlot = 0;
        g_ActiveP = 0x0F;
        StartGame();
    }

    // Game loop
    while(1)
    {
        Halt();

        if(g_FullFlush)
        {
            NB_Flush();
            g_FullFlush = FALSE;
        }
        else
        {
            NB_Flush();
        }

        Keyboard_Update();
        *((u16*)0xF3F8) = *((u16*)0xF3FA);

        // Key capture with anti-bounce (lobby/waiting/gameover)
        if(g_GameState == GSTATE_LOBBY || g_GameState == GSTATE_LOBBY_WAIT || g_GameState == GSTATE_WAITING || g_GameState == GSTATE_GAMEOVER)
        {
            if(g_MoveDelay > 0) { g_MoveDelay--; }
            else
            {
                if(Keyboard_IsKeyPressed(KEY_UP))    { g_KeyUp = 1; g_MoveDelay = 8; }
                if(Keyboard_IsKeyPressed(KEY_DOWN))  { g_KeyDown = 1; g_MoveDelay = 8; }
                if(Keyboard_IsKeyPressed(KEY_RET))   { g_KeyRet = 1; g_MoveDelay = 15; }
                if(Keyboard_IsKeyPressed(KEY_C))     { g_KeyC = 1; g_MoveDelay = 15; }
                if(Keyboard_IsKeyPressed(KEY_R))     { g_KeyR = 1; g_MoveDelay = 15; }
                if(Keyboard_IsKeyPressed(KEY_ESC))   { g_KeyEsc = 1; }
                if(Keyboard_IsKeyPressed(KEY_S))     { g_KeyS = 1; g_MoveDelay = 15; }
            }
        }
        else
        {
            if(Keyboard_IsKeyPressed(KEY_ESC)) g_KeyEsc = 1;
        }

        if(g_KeyEsc) { g_KeyEsc = 0; break; }

        // States
        if(g_GameState == GSTATE_LOBBY_WAIT)
        {
            Net_Poll();
        }
        else if(g_GameState == GSTATE_LOBBY)
        {
            Lobby_ProcessInput();
            if(g_Online)
            {
                u16 avail = Net_Available(g_Conn);
                if(avail >= 6)
                {
                    u8 hdr[6]; u8 pl[200];
                    Net_Recv(g_Conn, hdr, 6);
                    if(hdr[0] == PROTO_MAGIC_0 && hdr[1] == PROTO_MAGIC_1 && hdr[5] > 0)
                    { while(Net_Available(g_Conn) < hdr[5]) Halt(); Net_Recv(g_Conn, pl, hdr[5]); }
                    if(hdr[0] == PROTO_MAGIC_0 && hdr[1] == PROTO_MAGIC_1)
                        Net_ProcessPacket(hdr[2], hdr[4], pl, hdr[5]);
                }
            }
        }
        else if(g_GameState == GSTATE_WAITING)
        {
            Net_Poll();

            NB_Text(6, 4, "SALA:");
            NB_Num(12, 4, g_RoomId);
            NB_Text(6, 6, "TU ERES P");
            NB_SET(15, 6, T_F0 + g_MyPid);

            NB_Text(6, 8, "JUGADORES CONECTADOS");
            // Show which players are in
            {
                u8 pi2, row2;
                row2 = 10;
                for(pi2 = 0; pi2 < NP; pi2++)
                {
                    NB_Text(8, row2, "P");
                    NB_SET(9, row2, T_F0 + pi2 + 1);
                    if(g_ActiveP & (1 << pi2))
                        NB_Text(11, row2, "OK    ");
                    else
                        NB_Text(11, row2, "      ");
                    row2++;
                }
            }

            if(g_MyPid == 1)
                NB_Text(4, 16, "S EMPEZAR  ESC SALIR");
            else
                NB_Text(4, 16, "ESPERANDO HOST  ESC");

            if(g_KeyS && g_MyPid == 1)
            {
                g_KeyS = 0;
                g_SendBuf[0]=PROTO_MAGIC_0; g_SendBuf[1]=PROTO_MAGIC_1;
                g_SendBuf[2]=CMD_GAME_START; g_SendBuf[3]=g_RoomId;
                g_SendBuf[4]=g_MyPid; g_SendBuf[5]=0;
                Net_Send(g_Conn, g_SendBuf, 6);
                g_GameState = GSTATE_PLAYING;
                g_MySlot = g_MyPid - 1;
                StartGame();
            }
        }
        else if(g_GameState == GSTATE_PLAYING)
        {
            DoInput();

            PlayerUpdate(&g_P[g_MySlot], g_MySlot);

            // Send full board sync periodically
            g_FrameCnt++;
            if(g_Online && (g_FrameCnt % SYNC_INTERVAL) == 0)
                Net_SendSync();

            // Offline: update others
            if(!g_Online)
            {
                for(i = 0; i < NP; i++)
                    if(i != g_MySlot) PlayerUpdate(&g_P[i], i);
            }

            if(g_Online) Net_Poll();

            // Draw
            for(i = 0; i < NP; i++)
            {
                DrawHeader(i);
                DrawBoard(i);
            }

            // Arrow sprite on target player
            {
                u8 tgt = g_P[g_MySlot].target;
                u8 sx = tgt * 8 * 8 + 28;
                VDP_SetSpriteExUniColor(0, sx, 0, 0, g_SprColor[g_MySlot]);
            }

            // Check winner
            {
                u8 alive = 0, last = 0, pi2;
                for(pi2 = 0; pi2 < NP; pi2++)
                {
                    if(!g_P[pi2].dead) { alive++; last = pi2; }
                }
                if(alive <= 1)
                {
                    g_Winner = last;
                    g_GameState = GSTATE_GAMEOVER;
                    // Hide arrow sprite
                    VDP_SetSpriteExUniColor(0, 0, 209, 0, 0);
                }
            }
        }
        else if(g_GameState == GSTATE_GAMEOVER)
        {
            NB_Text(5, 10, "GANA P");
            NB_SET(11, 10, T_F0 + g_Winner + 1);
            if(g_MyPid == 1 || !g_Online)
                NB_Text(4, 14, "S NUEVA PARTIDA  ESC");
            else
                NB_Text(4, 14, "ESPERANDO HOST  ESC");

            if(g_Online) Net_Poll();

            if(g_KeyS && (g_MyPid == 1 || !g_Online))
            {
                g_KeyS = 0;
                if(g_Online)
                {
                    g_SendBuf[0]=PROTO_MAGIC_0; g_SendBuf[1]=PROTO_MAGIC_1;
                    g_SendBuf[2]=CMD_GAME_START; g_SendBuf[3]=g_RoomId;
                    g_SendBuf[4]=g_MyPid; g_SendBuf[5]=0;
                    Net_Send(g_Conn, g_SendBuf, 6);
                }
                StartGame();
                g_GameState = GSTATE_PLAYING;
                g_Winner = 0xFF;
            }
        }
    }

    // Room leave
    if(g_Online && g_Conn != NET_INVALID_CONN)
    {
        g_SendBuf[0]=PROTO_MAGIC_0; g_SendBuf[1]=PROTO_MAGIC_1;
        g_SendBuf[2]=CMD_ROOM_LEAVE; g_SendBuf[3]=g_RoomId;
        g_SendBuf[4]=g_MyPid; g_SendBuf[5]=0;
        Net_Send(g_Conn, g_SendBuf, 6);
    }

    for(i = 0; i < 32; i++)
        VDP_SetSpriteExUniColor(i, 0, 209, 0, 0);
    Bios_Exit(0);
}
