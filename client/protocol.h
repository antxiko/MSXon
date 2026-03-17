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
// Autenticación
#define CMD_AUTH            0x10
#define CMD_AUTH_OK         0x11
#define CMD_AUTH_FAIL       0x12
// Salas
#define CMD_ROOM_CREATE     0x20
#define CMD_ROOM_JOIN       0x21
#define CMD_ROOM_LEAVE      0x22
#define CMD_ROOM_INFO       0x23
#define CMD_ROOM_FULL       0x24
#define CMD_ROOM_NOT_FOUND  0x25
// Eventos de sala (broadcasts del servidor)
#define CMD_PLAYER_JOINED   0x30
#define CMD_PLAYER_LEFT     0x31
#define CMD_GAME_START      0x32
#define CMD_GAME_END        0x33
// Datos de juego
#define CMD_STATE_UPDATE    0x40
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
