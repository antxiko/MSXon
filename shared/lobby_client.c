// lobby_client.c — MSXon Lobby Client
#include "lobby_client.h"

LobbyData g_LobbyData;
bool g_FromLobby = FALSE;

bool LobbyClient_Load(void)
{
    u8 fh;

    fh = DOS_OpenHandle(LOBBY_DAT_FILE, O_RDONLY);
    if(fh >= 0xFE) return FALSE; // file not found = launched directly

    DOS_ReadHandle(fh, (void*)&g_LobbyData, 8);
    DOS_CloseHandle(fh);

    // LOBBY.DAT is deleted by AUTOEXEC.BAT after game exits

    if(g_LobbyData.magic != LOBBY_MAGIC) return FALSE;

    g_FromLobby = TRUE;
    return TRUE;
}
