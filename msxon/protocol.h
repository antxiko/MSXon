//=============================================================================
// protocol.h — MSX Online Game Server · Protocolo binario v1.0
// Compartido entre cliente MSX y servidor Node.js
//
// Estructura de paquete (6 bytes header + payload):
//  [0x46][0x4D][CMD][ROOM][PID][LEN][...PAYLOAD...]
//   'F'   'M'
//=============================================================================
#ifndef PROTOCOL_H
#define PROTOCOL_H

// ── Magic bytes (FM = FX-Media) ───────────────────────────────
#define PROTO_MAGIC_0       0x46    // 'F'
#define PROTO_MAGIC_1       0x4D    // 'M'
#define PROTO_HEADER_SZ     6

// ── Comandos ─────────────────────────────────────────────────
// Conexión
#define CMD_PING            0x01
#define CMD_PONG            0x02
// Autenticación legacy (token DEADBEEF, usado por ghost-service y server-status)
#define CMD_AUTH            0x10
#define CMD_AUTH_OK         0x11
#define CMD_AUTH_FAIL       0x12
// Autenticación de usuario (LOGIN/REGISTER, sesion-aware)
#define CMD_LOGIN           0x13    // C->S [ULEN][user][PLEN][pass]
#define CMD_LOGIN_OK        0x14    // S->C [role][nickLen][nick][session_id 4B]
#define CMD_LOGIN_FAIL      0x15    // S->C [reason] 1=bad_creds 2=not_found 3=banned 4=rate 5=pending_setup
#define CMD_REGISTER        0x16    // C->S [ULEN][user][NLEN][nick]
#define CMD_REG_PENDING     0x17    // S->C [TLEN][token]  (token 8 chars hex, ttl 10min)
#define CMD_REG_FAIL        0x18    // S->C [reason] 1=user_exists 2=invalid_chars 3=disabled
#define CMD_LOGOUT          0x19    // C->S
#define CMD_SESSION_RESUME  0x1A    // C->S [session_id 4B] (lo usan los juegos al arrancar)
// Roles (byte de respuesta de LOGIN_OK)
#define ROLE_USER           0x01
#define ROLE_ADMIN          0x02
#define ROLE_SUPERADMIN     0x03
// Salas
#define CMD_ROOM_CREATE     0x20
#define CMD_ROOM_JOIN       0x21
#define CMD_ROOM_LEAVE      0x22
#define CMD_ROOM_INFO       0x23
#define CMD_ROOM_FULL       0x24
#define CMD_ROOM_NOT_FOUND  0x25
#define CMD_ROOM_LIST       0x26
// Eventos de sala (broadcasts del servidor)
#define CMD_PLAYER_JOINED   0x30
#define CMD_PLAYER_LEFT     0x31
#define CMD_GAME_START      0x32
#define CMD_GAME_END        0x33
// Datos de juego
#define CMD_STATE_UPDATE    0x40
// Catalogo dinamico de juegos
#define CMD_GAME_LIST       0x27    // S->C [N][gameId,flags,nameLen,name] x N
// Lista de jugadores de una sala (con nicks)
#define CMD_PLAYER_LIST     0x28    // C->S sin payload; S->C [N][PID,NLEN,nick] x N
// Chat global (solo usuarios loggeados)
#define CMD_CHAT_SEND       0x50    // C->S [MLEN][texto]
#define CMD_CHAT_RECV       0x51    // S->C [NLEN][nick][MLEN][texto]
#define CMD_CHAT_HISTORY    0x52    // C->S sin payload; S->C envia CHAT_RECV[]
// Error
#define CMD_ERROR           0xFF

// ── PIDs ──────────────────────────────────────────────────────
#define PID_SERVER          0x00    // Paquete viene del servidor
#define PID_P1              0x01
#define PID_P2              0x02
#define PID_P3              0x03
#define PID_P4              0x04
#define MAX_PLAYERS         4

// ── Token de autenticación (cambiar en producción) ────────────
// Debe coincidir exactamente con AUTH_TOKEN en msx-gameserver.js
#define AUTH_TOKEN_0        0xDE
#define AUTH_TOKEN_1        0xAD
#define AUTH_TOKEN_2        0xBE
#define AUTH_TOKEN_3        0xEF

// ── Game ID de este juego ─────────────────────────────────────
#define GAME_ID_BALL        0x01

// ── Payload STATE_UPDATE — 8 bytes ───────────────────────────
// Byte 0: X high byte
// Byte 1: X low byte   → X = (payload[0]<<8)|payload[1], rango 0..255
// Byte 2: Y high byte
// Byte 3: Y low byte   → Y = (payload[2]<<8)|payload[3], rango 0..211
// Byte 4: frame de animación del sprite
// Byte 5: flags de estado (ver máscaras abajo)
// Byte 6: data extra 0 (reservado)
// Byte 7: data extra 1 (reservado)
#define STATE_PAYLOAD_SZ    8
#define STATE_FLAG_DIR_R    0x01    // Mirando derecha
#define STATE_FLAG_DIR_D    0x02    // Moviéndose abajo
#define STATE_FLAG_ACTION_A 0x04    // Botón A pulsado
#define STATE_FLAG_ACTION_B 0x08    // Botón B pulsado

#endif // PROTOCOL_H
