//=============================================================================
// log.h — MSXon · Log a fichero de texto
//
// Escribe eventos de red en MSXONLIN.LOG para depurar en hardware real.
// Usa MSX-DOS 2 handle API (dos.h con DOS_USE_HANDLE).
//
// Uso:
//   Log_Init()          — crea/trunca el fichero
//   Log_Write("texto")  — escribe una linea + CR/LF
//   Log_Hex8(val)       — escribe un byte en hex (2 chars)
//   Log_Close()         — cierra el fichero
//=============================================================================
#ifndef LOG_H
#define LOG_H

#include "msxgl.h"
#include "dos.h"

// Nombre del fichero (8.3, directorio actual)
#define LOG_FILENAME    "DAMAS.LOG"

// Handle del fichero (0xFF = cerrado)
#define LOG_CLOSED      0xFF
static u8 g_LogHandle = LOG_CLOSED;

// Tabla hex para conversion rapida
static const c8 g_HexChars[16] = "0123456789ABCDEF";

//─────────────────────────────────────────────────────────────────
// Log_Init — Crea el fichero (trunca si existe)
//─────────────────────────────────────────────────────────────────
static void Log_Init(void)
{
    g_LogHandle = DOS_CreateHandle(LOG_FILENAME, O_WRONLY, 0x00);
    // Si fallo, queda como LOG_CLOSED (0xFF no es handle valido)
}

//─────────────────────────────────────────────────────────────────
// Log_StrLen — Longitud de string (no tenemos strlen)
//─────────────────────────────────────────────────────────────────
static u16 Log_StrLen(const c8* s)
{
    u16 len = 0;
    while(s[len]) len++;
    return len;
}

//─────────────────────────────────────────────────────────────────
// Log_Write — Escribe una linea de texto + CR/LF
//─────────────────────────────────────────────────────────────────
static void Log_Write(const c8* msg)
{
    u16 len;
    static const c8 crlf[2] = { 0x0D, 0x0A };

    if(g_LogHandle == LOG_CLOSED) return;

    len = Log_StrLen(msg);
    if(len > 0)
        DOS_WriteHandle(g_LogHandle, msg, len);

    DOS_WriteHandle(g_LogHandle, crlf, 2);
    DOS_EnsureHandle(g_LogHandle); // Flush inmediato
}

//─────────────────────────────────────────────────────────────────
// Log_Hex8 — Escribe un byte como 2 caracteres hex
//─────────────────────────────────────────────────────────────────
static void Log_Hex8(u8 val)
{
    c8 hex[2];
    if(g_LogHandle == LOG_CLOSED) return;
    hex[0] = g_HexChars[(val >> 4) & 0x0F];
    hex[1] = g_HexChars[val & 0x0F];
    DOS_WriteHandle(g_LogHandle, hex, 2);
}

//─────────────────────────────────────────────────────────────────
// Log_WriteHex — Escribe texto + valor hex en una sola linea
// Ej: Log_WriteHex("Handle=", 3)  =>  "Handle=03\r\n"
//─────────────────────────────────────────────────────────────────
static void Log_WriteHex(const c8* msg, u8 val)
{
    static const c8 crlf[2] = { 0x0D, 0x0A };
    u16 len;

    if(g_LogHandle == LOG_CLOSED) return;

    len = Log_StrLen(msg);
    if(len > 0)
        DOS_WriteHandle(g_LogHandle, msg, len);

    Log_Hex8(val);
    DOS_WriteHandle(g_LogHandle, crlf, 2);
}

//─────────────────────────────────────────────────────────────────
// Log_Close — Cierra el fichero
//─────────────────────────────────────────────────────────────────
static void Log_Close(void)
{
    if(g_LogHandle == LOG_CLOSED) return;
    DOS_CloseHandle(g_LogHandle);
    g_LogHandle = LOG_CLOSED;
}

#endif // LOG_H
