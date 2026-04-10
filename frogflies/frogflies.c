// Frog & Flies — MSX2 Screen 4 — MSXonLINE
// 4 players online, frogs catching flies
// GAME_ID=0x69, RELAY mode
#include "msxgl.h"
#include "vdp.h"
#include "input.h"
#include "bios.h"
#include "system.h"
#include "dos.h"
#include "lobby.h"
#include "lobby_client.h"
#include "tileset_data.h"
#include "screen_data.h"

// ── Layout ──────────────────────────────────────────────────────────
#define WATER_Y     180
#define LILY_W      20
#define NUM_FROGS   4

static const u8 g_LilyX[NUM_FROGS] = { 86, 35, 169, 220 };
static const u8 g_LilyY[NUM_FROGS] = { 138, 160, 139, 161 };
#define GRAVITY     1
#define MAX_FLIES   8

// Frog states
#define ST_IDLE     0
#define ST_JUMP     1
#define ST_TONGUE   2
#define ST_WATER    3

// Network packet types
#define PKT_FROG    1   // 7 bytes: frog state (player->server, relayed)
#define PKT_FLIES   2   // 25 bytes: all fly positions (server->client)
#define PKT_CATCH   3   // 2 bytes: caught fly index (player->server)
#define PKT_SCORE   4   // 5 bytes: all 4 scores (server->client)
#define PKT_WINNER  5   // 2 bytes: winner slot (server->client)

// ── Tiles ───────────────────────────────────────────────────────────
#define T_SKY       0
#define T_FA        200
#define T_F0        226

// ── Patterns ────────────────────────────────────────────────────────
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
};

static const u8 CL_FNT[8]  = {0xF4,0xF4,0xF4,0xF4,0xF4,0xF4,0xF4,0xF4};

// ── Sprites ─────────────────────────────────────────────────────────
static const u8 SPR_FROG_IDLE[32] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,
    0x07,0x1F,0x3F,0x7F,0xBF,0xBF,0x3F,0x1F,
    0x00,0x00,0x00,0x18,0x3C,0x66,0xE7,0xFF,
    0xFF,0xF0,0xF8,0xF8,0xFC,0xC4,0xE3,0xE3
};
static const u8 SPR_FROG_JUMP[32] = {
    0x00,0x00,0x01,0x07,0x1F,0x3F,0x7F,0x7F,
    0x7F,0x78,0x30,0x30,0x30,0x70,0xF0,0xC0,
    0x78,0xCC,0xCE,0xFF,0xFF,0xF8,0xEF,0x80,
    0xC0,0x60,0x38,0x00,0x00,0x00,0x00,0x00
};
static const u8 SPR_FROG_IDLE_L[32] = {
    0x00,0x00,0x00,0x18,0x3C,0x66,0xE7,0xFF,
    0xFF,0x0F,0x1F,0x1F,0x3F,0x23,0xC7,0xC7,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xC0,
    0xE0,0xF8,0xFC,0xFE,0xFD,0xFD,0xFC,0xF8
};
static const u8 SPR_FROG_JUMP_L[32] = {
    0x1E,0x33,0x73,0xFF,0xFF,0x1F,0xF7,0x01,
    0x03,0x06,0x1C,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x80,0xE0,0xF8,0xFC,0xFE,0xFE,
    0xFE,0x1E,0x0C,0x0C,0x0C,0x0E,0x0F,0x03
};
static const u8 SPR_FLY1[8] = { 0x24,0x18,0x3C,0x18,0x18,0x24,0x00,0x00 };
static const u8 SPR_FLY2[8] = { 0x18,0x18,0x3C,0x18,0x24,0x18,0x00,0x00 };
static const u8 SPR_TONGUE[32] = {
    0x00,0x00,0x00,0xFF,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0xFF,0xFF,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
};

// ── Player ──────────────────────────────────────────────────────────
typedef struct {
    i16 x, y;
    i8 vx, vy;
    u8 state;
    u8 dir;
    u8 tongueT;
    u8 score;
    u8 lilyX;
    u8 waterT;
    u8 charging;
    u8 chargeT;
} Frog;

static Frog g_Frog[NUM_FROGS];

typedef struct {
    i16 x, y;
    i8 vx, vy;
    u8 active;
    u8 anim;
} Fly;

static Fly g_Flies[MAX_FLIES];
static u16 g_FlySpawnT;
static u16 g_FrameCount;

// Name buffer
u8 g_NB[768];

// Online state
static u8 g_MySlot;      // 0-3, my frog index
static bool g_Online;
static u8 g_Winner;       // 255=no winner yet, 0-3=winner slot
#define WIN_SCORE 20

// Lobby config
static const u8 SERVER_IP[4] = {217, 154, 107, 144};
static LobbyConfig g_LobbyCfg = {
    "FROG FLIES", 0x69, 4, SERVER_IP, 9876
};


#define NB_SET(x,y,v) do{u16 _i=(u16)(y)*32+(x);g_NB[_i]=(v);}while(0)

static void NB_Text(u8 x, u8 y, const c8* s) {
    while(*s && x<32) {
        u8 ch=(u8)*s, t;
        if(ch>='A'&&ch<='Z') t=T_FA+ch-'A';
        else if(ch>='0'&&ch<='9') t=T_F0+ch-'0';
        else t=T_SKY;
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
    u8 bank, t;
    for(bank = 0; bank < 3; bank++) {
        u16 pb = bank * 2048;
        u16 cb = 0x2000 + pb;
        VDP_WriteVRAM(g_TilePatterns, pb, 0, 2048);
        VDP_WriteVRAM(g_TileColors, cb, 0, 2048);
        for(t = 0; t < 26; t++) {
            VDP_WriteVRAM(FONT[t], pb + (200+t)*8, 0, 8);
            VDP_WriteVRAM(CL_FNT, cb + (200+t)*8, 0, 8);
        }
        for(t = 0; t < 10; t++) {
            VDP_WriteVRAM(FONT[26+t], pb + (226+t)*8, 0, 8);
            VDP_WriteVRAM(CL_FNT, cb + (226+t)*8, 0, 8);
        }
    }
}

static void DrawBackground(void) {
    u16 i;
    for(i = 0; i < 768; i++) g_NB[i] = g_ScreenMap[i];
}

// ── Game logic ──────────────────────────────────────────────────────
static u16 g_Seed;
static u8 Rnd(void) { g_Seed = g_Seed * 25173 + 13849; return (u8)(g_Seed >> 8); }

static void InitFrog(Frog* f, u8 idx) {
    f->lilyX = g_LilyX[idx];
    f->x = f->lilyX;
    f->y = g_LilyY[idx] - 16;
    f->vx = 0; f->vy = 0;
    f->state = ST_IDLE;
    f->dir = (idx == 0) ? 1 : 0;
    f->tongueT = 0; f->score = 0;
    f->waterT = 0; f->charging = 0; f->chargeT = 0;
}

static void FrogJump(Frog* f, u8 horizontal, u8 chargeT) {
    u8 power;
    if(f->state != ST_IDLE) return;
    if(chargeT < 5) power = 0;
    else if(chargeT < 10) power = 1;
    else power = 2;
    f->state = ST_JUMP;
    if(horizontal) {
        static const i8 jvx[3] = { 2, 4, 6 };
        static const i8 jvy[3] = { -6, -8, -10 };
        f->vx = (f->dir ? 1 : -1) * jvx[power];
        f->vy = jvy[power];
    } else {
        static const i8 jvy[3] = { -8, -11, -14 };
        f->vx = (f->dir ? 1 : -1);
        f->vy = jvy[power];
    }
}

static void FrogTongue(Frog* f) {
    if(f->state != ST_JUMP) return;
    f->state = ST_TONGUE;
    f->tongueT = 8;
}

static void FrogUpdate(Frog* f) {
    u8 fi;
    u8 cp[2]; // for PKT_CATCH (declared at top per SDCC rule)
    if(f->state == ST_WATER) {
        f->waterT--;
        if(f->waterT == 0) {
            f->x = f->lilyX;
            {u8 li; for(li=0;li<NUM_FROGS;li++) if(g_LilyX[li]==f->lilyX){f->y=g_LilyY[li]-16;break;}}
            f->state = ST_IDLE; f->vx = 0; f->vy = 0;
        }
        return;
    }
    if(f->state == ST_TONGUE) {
        f->tongueT--;
        if(f->tongueT == 0) f->state = ST_JUMP;
        {
            i16 tx = f->x + (f->dir ? 16 : -8);
            i16 ty = f->y + 4;
            for(fi = 0; fi < MAX_FLIES; fi++) {
                if(!g_Flies[fi].active) continue;
                if(g_Flies[fi].x > tx - 8 && g_Flies[fi].x < tx + 16 &&
                   g_Flies[fi].y > ty - 4 && g_Flies[fi].y < ty + 4) {
                    if(g_Online) {
                        // Online: tell server, deactivate locally to avoid spam
                        cp[0] = PKT_CATCH;
                        cp[1] = fi;
                        Lobby_SendStateUpdate(cp, 2);
                        g_Flies[fi].active = 0; // prevent re-sending
                    } else {
                        g_Flies[fi].active = 0;
                        f->score++;
                    }
                }
            }
        }
    }
    if(f->state == ST_JUMP || f->state == ST_TONGUE) {
        f->vy += GRAVITY;
        f->x += f->vx;
        f->y += f->vy;
        if(f->x < 0) f->x = 0;
        if(f->x > 240) f->x = 240;
        {
            u8 landed = 0, li;
            for(li = 0; li < NUM_FROGS; li++) {
                i16 dx = f->x - (i16)g_LilyX[li];
                if(dx < 0) dx = -dx;
                if(dx < LILY_W && f->y >= (i16)g_LilyY[li] - 16) {
                    f->y = g_LilyY[li] - 16;
                    f->state = ST_IDLE; f->vx = 0; f->vy = 0;
                    f->lilyX = g_LilyX[li];
                    landed = 1; break;
                }
            }
            if(!landed && f->y >= WATER_Y) {
                f->state = ST_WATER; f->waterT = 60;
            }
        }
    }
}

static void SpawnFly(void) {
    u8 i;
    for(i = 0; i < MAX_FLIES; i++) {
        if(g_Flies[i].active) continue;
        g_Flies[i].active = 1;
        g_Flies[i].x = (Rnd() & 1) ? -8 : 256;
        g_Flies[i].y = 20 + (Rnd() % 80);
        g_Flies[i].vx = (g_Flies[i].x < 0) ? 1 + (Rnd() & 1) : -(1 + (Rnd() & 1));
        g_Flies[i].vy = 0; g_Flies[i].anim = 0;
        break;
    }
}

static void UpdateFlies(void) {
    u8 i;
    for(i = 0; i < MAX_FLIES; i++) {
        if(!g_Flies[i].active) continue;
        g_Flies[i].x += g_Flies[i].vx;
        g_Flies[i].vy = ((g_FrameCount + i * 7) & 0x0F) < 8 ? -1 : 1;
        g_Flies[i].y += g_Flies[i].vy;
        g_Flies[i].anim = (g_FrameCount >> 2) & 1;
        if(g_Flies[i].x < -16 || g_Flies[i].x > 270) g_Flies[i].active = 0;
    }
}

// ── Draw sprites ────────────────────────────────────────────────────
static const u8 FROG_COL[NUM_FROGS] = { 3, 8, 11, 13 };

static void DrawSprites(void) {
    u8 sprIdx = 0, i;
    for(i = 0; i < NUM_FROGS; i++) {
        Frog* f = &g_Frog[i];
        u8 pat, col;
        col = FROG_COL[i];
        if(f->state == ST_JUMP || f->state == ST_TONGUE)
            pat = f->dir ? 4 : 12;
        else
            pat = f->dir ? 0 : 8;
        if(f->state == ST_WATER) col = 4;
        VDP_SetSpriteExUniColor(sprIdx, (u8)f->x, (u8)f->y, pat, col);
        sprIdx++;
        if(f->state == ST_TONGUE) {
            i16 tx = f->x + (f->dir ? 16 : -16);
            VDP_SetSpriteExUniColor(sprIdx, (u8)tx, (u8)(f->y + 4), 24, 6);
            sprIdx++;
        }
    }
    for(i = 0; i < MAX_FLIES; i++) {
        u8 fpat;
        if(!g_Flies[i].active || sprIdx >= 31) continue;
        fpat = g_Flies[i].anim ? 20 : 16;
        VDP_SetSpriteExUniColor(sprIdx, (u8)g_Flies[i].x, (u8)g_Flies[i].y, fpat, 1);
        sprIdx++;
    }
    for(; sprIdx < 32; sprIdx++)
        VDP_SetSpriteExUniColor(sprIdx, 0, 209, 0, 0);
}

// ── AI ──────────────────────────────────────────────────────────────
static void FrogAI(Frog* f) {
    u8 fi;
    if(f->state == ST_IDLE) {
        for(fi = 0; fi < MAX_FLIES; fi++) {
            i16 dx, dy;
            if(!g_Flies[fi].active) continue;
            dx = g_Flies[fi].x - f->x;
            dy = g_Flies[fi].y - f->y;
            if(dy < -20 && dy > -100) {
                if(dx < 0) dx = -dx;
                if(dx < 80) {
                    f->dir = (g_Flies[fi].x > f->x) ? 1 : 0;
                    FrogJump(f, (dx > 30) ? 1 : 0, (u8)(10 + (Rnd() & 7)));
                    return;
                }
            }
        }
        if((Rnd() & 0x3F) == 0) {
            f->dir = Rnd() & 1;
            FrogJump(f, Rnd() & 1, 5 + (Rnd() % 10));
        }
    }
    else if(f->state == ST_JUMP) {
        for(fi = 0; fi < MAX_FLIES; fi++) {
            i16 dx, dy;
            if(!g_Flies[fi].active) continue;
            dx = g_Flies[fi].x - f->x;
            dy = g_Flies[fi].y - f->y;
            if(dy > -8 && dy < 8) {
                if(f->dir && dx > 0 && dx < 24) { FrogTongue(f); return; }
                if(!f->dir && dx < 0 && dx > -24) { FrogTongue(f); return; }
            }
        }
    }
}

// ── Network ─────────────────────────────────────────────────────────

// Send my frog state (7 bytes payload)
static void Net_SendFrog(void) {
    u8 pl[7];
    Frog* f = &g_Frog[g_MySlot];
    pl[0] = PKT_FROG;
    pl[1] = (u8)(f->x >> 8);
    pl[2] = (u8)(f->x & 0xFF);
    pl[3] = (u8)(f->y >> 8);
    pl[4] = (u8)(f->y & 0xFF);
    pl[5] = f->state;
    pl[6] = f->dir; // score comes from server via PKT_SCORE
    Lobby_SendStateUpdate(pl, 7);
}

// (flies are managed by the server — no client-side fly sending)

// Receive packets
static void Game_OnPacket(u8 cmd, u8 senderPid, u8* pl, u8 len) {
    u8 slot, i;
    if(cmd != CMD_STATE_UPDATE || len < 1) return;
    slot = senderPid - 1;

    if(pl[0] == PKT_FROG && len >= 7 && slot < NUM_FROGS && slot != g_MySlot) {
        Frog* f = &g_Frog[slot];
        f->x = ((i16)pl[1] << 8) | pl[2];
        f->y = ((i16)pl[3] << 8) | pl[4];
        f->state = pl[5];
        f->dir = pl[6] & 0x01;
        // score not in frog packet — comes from PKT_SCORE
    }
    else if(pl[0] == PKT_FLIES && len >= 25) {
        // Fly positions from server
        u8 idx = 1;
        for(i = 0; i < MAX_FLIES; i++) {
            u8 xl = pl[idx++];
            u8 yl = pl[idx++];
            u8 flags = pl[idx++];
            g_Flies[i].x = (i16)xl;
            if(xl > 240) g_Flies[i].x = (i16)xl - 256;
            g_Flies[i].y = (i16)yl;
            g_Flies[i].active = flags & 0x01;
            g_Flies[i].anim = (g_FrameCount >> 2) & 1;
        }
    }
    else if(pl[0] == PKT_SCORE && len >= 5) {
        // Score update from server
        for(i = 0; i < 4; i++) g_Frog[i].score = pl[1 + i];
    }
    else if(pl[0] == PKT_WINNER && len >= 2) {
        g_Winner = pl[1];
    }
}

// ── Main ────────────────────────────────────────────────────────────

void main(void)
{
    u8 i, w;
    u8 prevRow8 = 0xFF;

    *((u8*)0xF3DB) = 0;

    // Online check
    g_Online = FALSE;
    g_MySlot = 0;

    if(LobbyClient_Load()) {
        // From LOBBY.COM
        Lobby_Init(&g_LobbyCfg, Game_OnPacket);
        g_LobbyConn = (NetConn)(int)g_LobbyData.conn;
        g_LobbyPid = g_LobbyData.pid;
        g_LobbyRoomId = g_LobbyData.roomId;
        g_LobbyActive = g_LobbyData.active;
        g_LobbyOnline = TRUE;
        g_LobbyState = LOBBY_ST_PLAYING;
        g_Online = TRUE;
        g_MySlot = g_LobbyPid - 1;
        Net_Init();
    } else {
        // Direct launch: try lobby
        Lobby_Init(&g_LobbyCfg, Game_OnPacket);
        Lobby_SetTileOffsets(T_FA, T_F0);
        Lobby_Diag();
        if(Lobby_Connect()) {
            g_Online = TRUE;
        }
    }

    VDP_SetMode(VDP_MODE_SCREEN4);
    VDP_SetColor(0x41);
    VDP_SetSpriteFlag(VDP_SPRITE_SIZE_16 | VDP_SPRITE_SCALE_1);

    VDP_LoadSpritePattern(SPR_FROG_IDLE, 0, 4);
    VDP_LoadSpritePattern(SPR_FROG_JUMP, 4, 4);
    VDP_LoadSpritePattern(SPR_FROG_IDLE_L, 8, 4);
    VDP_LoadSpritePattern(SPR_FROG_JUMP_L, 12, 4);
    {
        u8 flyPat[32];
        u8 j;
        for(j=0;j<32;j++) flyPat[j]=0;
        for(j=0;j<8;j++) flyPat[j]=SPR_FLY1[j];
        VDP_LoadSpritePattern(flyPat, 16, 4);
        for(j=0;j<32;j++) flyPat[j]=0;
        for(j=0;j<8;j++) flyPat[j]=SPR_FLY2[j];
        VDP_LoadSpritePattern(flyPat, 20, 4);
    }
    VDP_LoadSpritePattern(SPR_TONGUE, 24, 4);
    for(i = 0; i < 32; i++)
        VDP_SetSpriteExUniColor(i, 0, 209, 0, 0);

    LoadTiles();
    g_Seed = *((u16*)0xFC9E);

    {u8 fi; for(fi=0;fi<NUM_FROGS;fi++) InitFrog(&g_Frog[fi], fi);}
    for(i = 0; i < MAX_FLIES; i++) g_Flies[i].active = 0;
    g_FlySpawnT = 0;
    g_FrameCount = 0;
    g_Winner = 255;

    DrawBackground();

    // Lobby phase (if not from LOBBY.COM)
    if(g_Online && !g_FromLobby) {
        Lobby_RequestRooms();
        g_LobbyState = LOBBY_ST_LIST_WAIT;
        while(g_LobbyState < LOBBY_ST_PLAYING) {
            Halt();
            VDP_WriteVRAM(g_NB, 0x1800, 0, 768);
            Keyboard_Update();
            *((u16*)0xF3F8) = *((u16*)0xF3FA);
            if(Keyboard_IsKeyPressed(KEY_ESC)) { Bios_Exit(0); return; }
            Lobby_Update();
        }
        g_MySlot = g_LobbyPid - 1;
        DrawBackground();
    }

    Halt();
    VDP_WriteVRAM(g_NB, 0x1800, 0, 768);

    // Game loop
    while(1)
    {
        Halt();
        VDP_WriteVRAM(g_NB, 0x1800, 0, 768);

        Keyboard_Update();
        *((u16*)0xF3F8) = *((u16*)0xF3FA);

        // Network poll
        if(g_Online) Lobby_Poll();

        // Input — only for MY frog
        {
            u8 row8 = Keyboard_Read(8);
            u8 l = !(row8 & 0x10);
            u8 r = !(row8 & 0x80);
            u8 sp = !(row8 & 0x01);
            Frog* me = &g_Frog[g_MySlot];

            if(me->state == ST_IDLE) {
                if(l) me->dir = 0;
                if(r) me->dir = 1;
                if(sp) {
                    me->charging = (l || r) ? 2 : 1;
                    if(me->chargeT < 255) me->chargeT++;
                }
                else if(me->charging) {
                    FrogJump(me, (me->charging == 2) ? 1 : 0, me->chargeT);
                    me->charging = 0; me->chargeT = 0;
                }
            }
            else if(me->state == ST_JUMP) {
                if(sp && (prevRow8 & 0x01)) FrogTongue(me);
            }
            prevRow8 = row8;
        }

        if(Keyboard_IsKeyPressed(KEY_ESC)) break;

        // AI + update: only offline (online AI is desynced between clients)
        if(!g_Online) {
            u8 ai;
            for(ai = 0; ai < NUM_FROGS; ai++) {
                if(ai == g_MySlot) continue;
                FrogAI(&g_Frog[ai]);
            }
        }

        // Update my frog (always)
        FrogUpdate(&g_Frog[g_MySlot]);

        // Update AI frogs (offline only)
        if(!g_Online) {
            u8 fi;
            for(fi = 0; fi < NUM_FROGS; fi++) {
                if(fi == g_MySlot) continue;
                FrogUpdate(&g_Frog[fi]);
            }
        }

        // Flies: server generates when online, local when offline
        g_FrameCount++;
        if(!g_Online) {
            g_FlySpawnT++;
            if(g_FlySpawnT >= 90) { g_FlySpawnT = 0; SpawnFly(); }
            UpdateFlies();
        }

        // Network send: my frog state every 3 frames
        if(g_Online && (g_FrameCount % 3) == 0) {
            Net_SendFrog();
        }

        // Offline win check
        if(!g_Online) {
            for(w = 0; w < NUM_FROGS; w++) {
                if(g_Frog[w].score >= WIN_SCORE) { g_Winner = w; break; }
            }
        }

        // HUD
        NB_Text(0, 0, "P1");  NB_Num(3, 0, g_Frog[0].score);
        NB_Text(8, 0, "P2");  NB_Num(11, 0, g_Frog[1].score);
        NB_Text(16, 0, "P3"); NB_Num(19, 0, g_Frog[2].score);
        NB_Text(24, 0, "P4"); NB_Num(27, 0, g_Frog[3].score);

        DrawSprites();

        // Win screen
        if(g_Winner != 255) {
            u16 ci;
            // Clear screen
            for(ci = 0; ci < 768; ci++) g_NB[ci] = T_SKY;
            // Scores
            NB_Text(12, 4, "FIN DE PARTIDA");
            NB_Text(10, 7, "P1");  NB_Num(13, 7, g_Frog[0].score);
            NB_Text(10, 9, "P2");  NB_Num(13, 9, g_Frog[1].score);
            NB_Text(10, 11, "P3"); NB_Num(13, 11, g_Frog[2].score);
            NB_Text(10, 13, "P4"); NB_Num(13, 13, g_Frog[3].score);
            // Winner
            NB_Text(10, 16, "GANA P");
            NB_Num(16, 16, g_Winner + 1);
            NB_Text(6, 20, "ESPACIO PARA SALIR");
            // Hide all sprites
            for(i = 0; i < 32; i++)
                VDP_SetSpriteExUniColor(i, 0, 209, 0, 0);
            Halt();
            VDP_WriteVRAM(g_NB, 0x1800, 0, 768);
            while(1) {
                Halt();
                Keyboard_Update();
                if(Keyboard_IsKeyPressed(KEY_SPACE)) break;
                if(Keyboard_IsKeyPressed(KEY_ESC)) break;
                if(g_Online) Lobby_Poll();
            }
            break;
        }
    }

    if(g_Online) Lobby_SendRoomLeave();
    for(i = 0; i < 32; i++)
        VDP_SetSpriteExUniColor(i, 0, 209, 0, 0);
    Bios_Exit(0);
}
