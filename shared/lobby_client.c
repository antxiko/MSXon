// lobby_client.c — MSXon Lobby Client
#include "lobby_client.h"

LobbyData g_LobbyData;
bool g_FromLobby = FALSE;
c8   g_LobbyNicks[LOBBY_MAX_NICKS][LOBBY_NICK_LEN + 1];

bool LobbyClient_Load(void)
{
    u8 fh;

    fh = DOS_OpenHandle(LOBBY_DAT_FILE, O_RDONLY);
    if(fh >= 0xFE) return FALSE;

    DOS_ReadHandle(fh, (void*)&g_LobbyData, 8);

    if(g_LobbyData.magic != LOBBY_MAGIC && g_LobbyData.magic != LOBBY_MAGIC_V2) {
        DOS_CloseHandle(fh);
        return FALSE;
    }

    // v2: leer tabla de nicks. 16 slots de [NLEN(1)][nick(16)].
    {
        u8 i;
        for(i = 0; i < LOBBY_MAX_NICKS; i++) g_LobbyNicks[i][0] = 0;
    }
    if(g_LobbyData.magic == LOBBY_MAGIC_V2) {
        u8 i, j;
        u8 entry[1 + LOBBY_NICK_LEN];
        for(i = 0; i < LOBBY_MAX_NICKS; i++) {
            DOS_ReadHandle(fh, entry, 1 + LOBBY_NICK_LEN);
            u8 nl = entry[0];
            if(nl > LOBBY_NICK_LEN) nl = LOBBY_NICK_LEN;
            for(j = 0; j < nl; j++) g_LobbyNicks[i][j] = entry[1 + j];
            g_LobbyNicks[i][nl] = 0;
        }
    }
    DOS_CloseHandle(fh);

    g_FromLobby = TRUE;
    return TRUE;
}
