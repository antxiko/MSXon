// Tetris 4P — MSX2 Screen 4 — MSXon
// 4 players, 8 cols each, 20 rows, shadow buffer
// GAME_ID=0x06, RELAY mode
#include "msxgl.h"
#include "vdp.h"
#include "input.h"
#include "bios.h"
#include "system.h"
#include "dos.h"
#include "lobby.h"
#include "lobby_client.h"

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

// ── Game vars ───────────────────────────────────────────────────────
u8 g_NB[768]; // name buffer (lobby uses extern)
static u8 g_MySlot = 0;
static u8 g_FrameCnt = 0;
static u8 g_Winner = 0xFF;
static u8 g_GameOver = 0;

// Random
static u16 g_S;
static u8 R8(void){ g_S=g_S*25173+13849; return (u8)(g_S>>8); }

// Render state
static u8 g_PrevScore[NP];
static u8 g_PrevLines[NP];
static i8 g_PrevPX[NP], g_PrevPY[NP];
static u8 g_PrevPI[NP], g_PrevRot[NP];

// Sprite
static const u8 g_ArrowSpr[8] = { 0x18,0x18,0x18,0x7E,0x3C,0x18,0x00,0x00 };
static const u8 g_SprColor[4] = { 7, 9, 3, 11 };

// Lobby config
static const u8 SRV_IP[4] = {217,154,107,144};
static const LobbyConfig g_LobbyCfg = {
    "TETRIS ONLINE", 0x06, 4, SRV_IP, 9876
};

// ── NB helpers ──────────────────────────────────────────────────────
#define NB_SET(x,y,v) do{u16 _i=(u16)(y)*32+(x);u8 _v=(v);if(g_NB[_i]!=_v)g_NB[_i]=_v;}while(0)

static void NB_Text(u8 x, u8 y, const c8* s) {
    while(*s && x<32) {
        u8 ch=(u8)*s, t;
        if(ch>='A'&&ch<='Z') t=T_FA+ch-'A';
        else if(ch>='0'&&ch<='9') t=T_F0+ch-'0';
        else t=T_EMP;
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

static void AddGarbageWithGap(PL* p, u8 count, u8 gap) {
    u8 i, r, c;
    if(p->dead) return;
    for(i=0;i<count;i++){
        for(c=0;c<BW;c++) if(p->bd[0][c]){p->dead=1;return;}
        for(r=0;r<BH-1;r++) for(c=0;c<BW;c++) p->bd[r][c]=p->bd[r+1][c];
        for(c=0;c<BW;c++) p->bd[BH-1][c]=(c==gap)?0:T_GRB;
    }
    p->dirty=1;
}

static void AddGarbage(PL* p, u8 count) { AddGarbageWithGap(p, count, R8()%BW); }

static void Lock(PL* p, u8 pi) {
    const PRot* s=GR(p->pi,p->rot); u8 r,c;
    for(r=0;r<s->h;r++) for(c=0;c<s->w;c++)
        if(PB(s->bits,r,c)){
            i8 by=p->py+(i8)r, bx=p->px+(i8)c;
            if(by>=0&&by<BH&&bx>=0&&bx<BW) p->bd[by][bx]=pi+1;
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
        p->lv=(p->ln/10)+1; p->garbQ=garbT[p->fn];
    } else { p->dirty=1; Spawn(p); }
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
        u8 gap=R8()%BW;
        if(!g_LobbyOnline) AddGarbageWithGap(&g_P[p->target],p->garbQ,gap);
        else if(p==&g_P[g_MySlot]){
            u8 pl[4]; pl[0]=0x03; pl[1]=p->target; pl[2]=p->garbQ; pl[3]=gap;
            Lobby_SendStateUpdate(pl,4);
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
    p->sc=0;p->ln=0;p->lv=1;p->dead=0;
    p->dt=0;p->di=32;p->sd=0;
    p->fl=0;p->fn=0;p->garbQ=0;
    p->target=(idx+1)%NP;
    p->pi=R8()%7;p->nxt=R8()%7;
    p->rot=0;p->px=(BW/2)-1;p->py=0;
    if(!Valid(p,p->pi,p->rot,p->px,p->py)) p->dead=1;
}

// ── Render ──────────────────────────────────────────────────────────
static void DrawHeader(u8 pi) {
    u8 x=pi*8;
    if(g_PrevScore[pi]!=(u8)g_P[pi].sc||g_PrevLines[pi]!=g_P[pi].ln){
        NB_Text(x,0,"P"); NB_SET(x+1,0,T_F0+pi+1);
        NB_Text(x+3,0,"     "); NB_Num(x+3,0,g_P[pi].sc);
        NB_Text(x,1,"L       "); NB_Num(x+2,1,(u16)g_P[pi].ln);
        NB_Text(x,2,"LV      "); NB_Num(x+3,2,(u16)g_P[pi].lv);
        g_PrevScore[pi]=(u8)g_P[pi].sc; g_PrevLines[pi]=g_P[pi].ln;
    }
}

static void ErasePiece(u8 pi) {
    u8 x0=pi*8,bg=T_BG1+pi,r,c; PL* p=&g_P[pi]; const PRot* s;
    if(g_PrevPI[pi]==0xFF) return;
    s=GR(g_PrevPI[pi],g_PrevRot[pi]);
    for(r=0;r<s->h;r++) for(c=0;c<s->w;c++)
        if(PB(s->bits,r,c)){
            i8 bx=g_PrevPX[pi]+(i8)c,by=g_PrevPY[pi]+(i8)r;
            if(bx>=0&&bx<BW&&by>=0&&by<BH){u8 v=p->bd[by][bx];NB_SET(x0+bx,HDR+by,(v==0)?bg:(v>=1&&v<=4)?v:(v==5)?T_GRB:bg);}
        }
}

static void DrawPiece(u8 pi) {
    u8 x0=pi*8,r,c,pv=pi+1; PL* p=&g_P[pi]; const PRot* s;
    if(p->dead||p->fl){g_PrevPI[pi]=0xFF;return;}
    s=GR(p->pi,p->rot);
    for(r=0;r<s->h;r++) for(c=0;c<s->w;c++)
        if(PB(s->bits,r,c)){i8 bx=p->px+(i8)c,by=p->py+(i8)r;if(bx>=0&&bx<BW&&by>=0&&by<BH) NB_SET(x0+bx,HDR+by,pv);}
    g_PrevPX[pi]=p->px;g_PrevPY[pi]=p->py;g_PrevPI[pi]=p->pi;g_PrevRot[pi]=p->rot;
}

static void DrawBoardFull(u8 pi) {
    u8 r,c,x0=pi*8,bg=T_BG1+pi; PL* p=&g_P[pi];
    for(r=0;r<BH;r++) for(c=0;c<BW;c++){u8 v=p->bd[r][c];NB_SET(x0+c,HDR+r,(v==0)?bg:(v>=1&&v<=4)?v:(v==5)?T_GRB:bg);}
    if(p->dead) NB_Text(x0+1,HDR+9,"DEAD");
}

static void DrawFlash(u8 pi) {
    u8 fi,c,x0=pi*8; PL* p=&g_P[pi];
    for(fi=0;fi<p->fn;fi++){u8 t=(p->fl&1)?T_FLS:(T_BG1+pi);for(c=0;c<BW;c++) NB_SET(x0+c,HDR+p->fr[fi],t);}
}

static void DrawBoard(u8 pi) {
    PL* p=&g_P[pi];
    if(p->dirty){DrawBoardFull(pi);p->dirty=0;DrawPiece(pi);return;}
    if(p->fl){DrawFlash(pi);return;}
    ErasePiece(pi);DrawPiece(pi);
}

// ── Sync packet (full board) ────────────────────────────────────────
#define PKT_FULL_SYNC 0x05
#define PKT_GARBAGE   0x03

static void Net_SendFullSync(void) {
    u8 syncBuf[92]; u8 r,c,idx; PL* p=&g_P[g_MySlot];
    if(g_LobbyConn==NET_INVALID_CONN) return;
    syncBuf[0]=PROTO_MAGIC_0;syncBuf[1]=PROTO_MAGIC_1;
    syncBuf[2]=CMD_STATE_UPDATE;syncBuf[3]=g_LobbyRoomId;syncBuf[4]=g_LobbyPid;
    syncBuf[5]=86;syncBuf[6]=PKT_FULL_SYNC;
    syncBuf[7]=p->pi;syncBuf[8]=p->rot;syncBuf[9]=(u8)p->px;syncBuf[10]=(u8)p->py;syncBuf[11]=p->dead;
    idx=12;
    for(r=0;r<BH;r++) for(c=0;c<BW;c+=2){syncBuf[idx]=(p->bd[r][c]<<4)|(p->bd[r][c+1]&0x0F);idx++;}
    Net_Send(g_LobbyConn,syncBuf,92);
}

// ── Game packet handler ─────────────────────────────────────────────

void Game_OnPacket(u8 cmd, u8 senderPid, u8* payload, u8 len)
{
    if(cmd != CMD_STATE_UPDATE || len < 1) return;
    {
    u8 slot=senderPid-1, pktType=payload[0];

    if(pktType==PKT_GARBAGE && len>=4) {
        u8 gTarget=payload[1],gCount=payload[2],gGap=payload[3];
        if(gTarget==g_MySlot) AddGarbageWithGap(&g_P[g_MySlot],gCount,gGap);
    }
    else if(pktType==PKT_FULL_SYNC && len>=86 && slot<NP && slot!=g_MySlot) {
        u8 r,c,idx2;
        g_P[slot].pi=payload[1];g_P[slot].rot=payload[2];
        g_P[slot].px=(i8)payload[3];g_P[slot].py=(i8)payload[4];
        g_P[slot].dead=payload[5];
        idx2=6;
        for(r=0;r<BH;r++) for(c=0;c<BW;c+=2){
            g_P[slot].bd[r][c]=(payload[idx2]>>4)&0x0F;
            g_P[slot].bd[r][c+1]=payload[idx2]&0x0F; idx2++;}
        g_P[slot].dirty=1;
    }
    }
}

// ── Input ───────────────────────────────────────────────────────────
static u8 g_Prev8=0xFF;
static u8 g_RepL=0, g_RepR=0;

static void DoInput(void) {
    u8 row8=Keyboard_Read(8);
    PL* p=&g_P[g_MySlot];
    u8 l=!(row8&0x10),r=!(row8&0x80),u=!(row8&0x20),d=!(row8&0x40);
    if(!p->dead&&!p->fl){
        if(u&&(g_Prev8&0x20)) DoRotate(p);
        p->sd=d;
        if(l&&!r){if(g_Prev8&0x10){DoMove(p,-1);g_RepL=0;}else{g_RepL++;if(g_RepL>=4)DoMove(p,-1);}}else g_RepL=0;
        if(r&&!l){if(g_Prev8&0x80){DoMove(p,1);g_RepR=0;}else{g_RepR++;if(g_RepR>=4)DoMove(p,1);}}else g_RepR=0;
        {u8 sp=!(row8&0x01);if(sp&&(g_Prev8&0x01)){u8 next=p->target,tries=0;do{next=(next+1)%NP;tries++;}while((next==g_MySlot||g_P[next].dead)&&tries<NP);p->target=next;}}
    }
    g_Prev8=row8;
}

// ── StartGame ───────────────────────────────────────────────────────

void StartGame(void) {
    u8 i; u16 si;
    for(i=0;i<NP;i++) InitP(&g_P[i],i);
    for(i=0;i<NP;i++){
        g_PrevScore[i]=0xFF;g_PrevLines[i]=0xFF;
        g_PrevPI[i]=0xFF;g_P[i].dirty=1;
        if(g_LobbyOnline&&!(g_LobbyActive&(1<<i))) g_P[i].dead=1;
    }
    for(si=0;si<768;si++) g_NB[si]=T_EMP;
    for(i=0;i<NP;i++){
        u8 x0=i*8,bg=T_BG1+i,r2,c;
        for(c=0;c<8;c++) NB_SET(x0+c,3,T_SEP);
        for(r2=0;r2<BH;r2++) for(c=0;c<BW;c++) NB_SET(x0+c,HDR+r2,bg);
    }
    for(i=0;i<NP;i++) DrawHeader(i);
    g_GameOver=0; g_Winner=0xFF;
}

// ── Main ────────────────────────────────────────────────────────────

void main(void)
{
    u8 i;

    *((u8*)0xF3DB)=0;

    // Check if launched from LOBBY.COM
    if(LobbyClient_Load()) {
        Lobby_Init(&g_LobbyCfg, Game_OnPacket);
        g_LobbyConn = (NetConn)(int)g_LobbyData.conn;
        g_LobbyPid = g_LobbyData.pid;
        g_LobbyRoomId = g_LobbyData.roomId;
        g_LobbyActive = g_LobbyData.active;
        g_LobbyOnline = TRUE;
        g_LobbyState = LOBBY_ST_PLAYING;
        Net_Init();
    } else {
        Lobby_Init(&g_LobbyCfg, Game_OnPacket);
        Lobby_SetTileOffsets(T_FA, T_F0);
        Lobby_Diag();
        Lobby_Connect();
    }

    // Screen 4
    VDP_SetMode(VDP_MODE_SCREEN4);
    VDP_SetColor(1);
    VDP_SetSpriteFlag(VDP_SPRITE_SIZE_8|VDP_SPRITE_SCALE_1);
    VDP_LoadSpritePattern(g_ArrowSpr,0,1);
    for(i=0;i<32;i++) VDP_SetSpriteExUniColor(i,0,209,0,0);
    LoadTiles();

    g_S=*((u16*)0xFC9E);

    if(g_FromLobby) {
        g_MySlot=g_LobbyPid-1;
        StartGame();
    } else if(g_LobbyOnline) {
        Lobby_RequestRooms();
        g_LobbyState=LOBBY_ST_LIST_WAIT;
    } else {
        g_LobbyState=LOBBY_ST_PLAYING;
        g_MySlot=0; g_LobbyActive=0x0F;
        StartGame();
    }

    // Game loop
    while(1) {
        Halt();
        VDP_WriteVRAM(g_NB,0x1800,0,768);

        Keyboard_Update();
        *((u16*)0xF3F8)=*((u16*)0xF3FA);

        if(Keyboard_IsKeyPressed(KEY_ESC)) break;

        // Lobby
        if(g_LobbyState < LOBBY_ST_PLAYING) {
            Lobby_Update();
            if(g_LobbyState==LOBBY_ST_PLAYING) {
                g_MySlot=g_LobbyPid-1;
                StartGame();
            }
        }
        // Playing
        else if(!g_GameOver) {
            DoInput();
            PlayerUpdate(&g_P[g_MySlot],g_MySlot);

            if(g_LobbyOnline){
                g_FrameCnt++;
                if((g_FrameCnt%5)==0) Net_SendFullSync();
            }

            if(!g_LobbyOnline)
                for(i=0;i<NP;i++) if(i!=g_MySlot) PlayerUpdate(&g_P[i],i);

            if(g_LobbyOnline) Lobby_Poll();

            for(i=0;i<NP;i++){DrawHeader(i);DrawBoard(i);}

            // Arrow sprite
            {u8 tgt=g_P[g_MySlot].target;VDP_SetSpriteExUniColor(0,tgt*8*8+28,0,0,g_SprColor[g_MySlot]);}

            // Winner check
            {u8 alive=0,last=0,pi2;
             for(pi2=0;pi2<NP;pi2++) if(!g_P[pi2].dead){alive++;last=pi2;}
             if(alive<=1){g_Winner=last;g_GameOver=1;VDP_SetSpriteExUniColor(0,0,209,0,0);}}
        }
        // Game over
        else {
            NB_Text(5,10,"GANA P"); NB_SET(11,10,T_F0+g_Winner+1);
            if(g_LobbyPid==1||!g_LobbyOnline) NB_Text(4,14,"S NUEVA PARTIDA");
            else NB_Text(4,14,"ESPERANDO HOST");

            if(g_LobbyOnline) Lobby_Poll();

            if(Keyboard_IsKeyPressed(KEY_S)&&(g_LobbyPid==1||!g_LobbyOnline)){
                if(g_LobbyOnline) Lobby_SendGameStart();
                StartGame();
            }
        }
    }

    Lobby_SendRoomLeave();
    for(i=0;i<32;i++) VDP_SetSpriteExUniColor(i,0,209,0,0);
    Bios_Exit(0);
}
