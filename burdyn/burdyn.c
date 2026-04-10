//=============================================================================
// burdyn.c — Burdyn RPG Crawler MMO · Fase 2: multiplayer
//
// MSX2 · MSX-DOS 2 · Screen 4 (Graphic 3)
// Tiles 8×8, mapa 64×64, viewport 24×24 + HUD sidebar
//=============================================================================

#include "msxgl.h"
#include "vdp.h"
#include "input.h"
#include "bios.h"
#include "system.h"
#include "dos.h"
#include "protocol.h"
#include "network.h"
#include "log.h"
#include "lobby_client.h"

//=============================================================================
// CONSTANTES
//=============================================================================

#define MAP_W           64
#define MAP_H           64
#define VIEW_W          24      // 24 tiles de mapa visibles (ancho)
#define VIEW_H          24      // 24 tiles de mapa visibles (alto) = pantalla completa
#define HUD_COL         24      // Columna donde empieza el HUD (derecha, 8 tiles)

// Tiles
#define TILE_FLOOR      0
#define TILE_WALL       1
#define TILE_GRASS      2
#define TILE_WATER      3
#define TILE_DOOR       4
#define TILE_STAIRS     5
#define TILE_TREE       6
#define TILE_CHEST      7

// Items (tiles 8-39) — 32 tipos
#define ITEM_DAGGER      8   // Armas
#define ITEM_SWORD       9
#define ITEM_AXE        10
#define ITEM_MACE       11
#define ITEM_BOW        12
#define ITEM_STAFF      13
#define ITEM_SHIELD_W   14   // Escudos
#define ITEM_SHIELD_I   15
#define ITEM_HELMET_L   16   // Cascos
#define ITEM_HELMET_H   17
#define ITEM_ARMOR_L    18   // Armaduras
#define ITEM_ARMOR_H    19
#define ITEM_BOOTS      20   // Botas
#define ITEM_CLOAK      21   // Capa
#define ITEM_RING       22   // Anillo
#define ITEM_AMULET     23   // Amuleto
#define ITEM_POTION_HP  24   // Pociones
#define ITEM_POTION_MP  25
#define ITEM_POTION_SPD 26
#define ITEM_BREAD      27   // Comida
#define ITEM_MEAT       28
#define ITEM_APPLE      29
#define ITEM_KEY_GOLD   30   // Llaves
#define ITEM_KEY_SILVER 31
#define ITEM_GOLD_COIN  32   // Tesoros
#define ITEM_GEM_RED    33
#define ITEM_GEM_BLUE   34
#define ITEM_SCROLL     35   // Objetos magicos
#define ITEM_BOMB       36
#define ITEM_TORCH      37
#define ITEM_ROPE       38
#define ITEM_MAP        39

// Fuente HUD (tiles 40-82)
#define TILE_FONT_BASE  40
#define TILE_SPC        40
// A=41, B=42, ..., Z=66
// 0=67, 1=68, ..., 9=76
#define TILE_COLON      77
#define TILE_SLASH      78
#define TILE_BAR_FULL   79
#define TILE_BAR_EMPTY  80
#define TILE_FRAME_TL   81
#define TILE_FRAME_V    82

#define TILE_CURSOR     83   // >
#define TILE_COUNT      84   // Total de tiles definidos

// Sprite del jugador
#define SPR_PLAYER      0

// Red
static const u8 SERVER_IP[4] = { 217, 154, 107, 144 };
#define SERVER_PORT     9876
#define GAME_ID_CRAWLER 0x03
#define MAX_ENTITIES    16
#define PING_INTERVAL   250

// WORLD_STATE CMD
#define CMD_WORLD_STATE 0x41

// Proto version para AGGREGATE mode
#define PROTO_VERSION_AGGREGATE 0x02

//=============================================================================
// TILESET — 8 tiles basicos, 8 bytes cada uno (patterns)
//=============================================================================

const u8 g_TilePatterns[] = {
    // Tile 0: suelo (puntos dispersos)
    0x00, 0x00, 0x20, 0x00, 0x00, 0x04, 0x00, 0x00,
    // Tile 1: muro (ladrillo solido)
    0xFF, 0x81, 0x81, 0xFF, 0xFF, 0x91, 0x91, 0xFF,
    // Tile 2: hierba (rayitas)
    0x44, 0x00, 0x11, 0x00, 0x44, 0x00, 0x11, 0x00,
    // Tile 3: agua (ondas)
    0x00, 0x00, 0x66, 0x99, 0x00, 0x00, 0x99, 0x66,
    // Tile 4: puerta (arco)
    0xFF, 0xC3, 0xA5, 0x81, 0x81, 0x81, 0x81, 0x81,
    // Tile 5: escaleras (lineas horizontales)
    0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00,
    // Tile 6: arbol (copa + tronco)
    0x18, 0x3C, 0x7E, 0xFF, 0x7E, 0x18, 0x18, 0x18,
    // Tile 7: cofre (caja)
    0x00, 0x7E, 0x7E, 0x7E, 0xFF, 0x7E, 0x7E, 0x00,
    // --- ITEMS (tiles 8-39) ---
    // 8: daga (linea diagonal corta)
    0x00, 0x00, 0x04, 0x08, 0x10, 0x20, 0x10, 0x00,
    // 9: espada (linea vertical + guarda)
    0x08, 0x08, 0x08, 0x08, 0x3E, 0x08, 0x1C, 0x08,
    // 10: hacha (hoja + mango)
    0x00, 0x1C, 0x3C, 0x7C, 0x08, 0x08, 0x08, 0x08,
    // 11: maza (bola + mango)
    0x1C, 0x3E, 0x3E, 0x1C, 0x08, 0x08, 0x08, 0x08,
    // 12: arco (curva + cuerda)
    0x02, 0x04, 0x0C, 0x14, 0x14, 0x0C, 0x04, 0x02,
    // 13: baston (vara con punta)
    0x1C, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
    // 14: escudo madera (cuadrado)
    0x3C, 0x7E, 0x7E, 0x7E, 0x7E, 0x3C, 0x18, 0x00,
    // 15: escudo hierro (puntiagudo)
    0x3C, 0x7E, 0xFF, 0xFF, 0x7E, 0x3C, 0x18, 0x00,
    // 16: casco ligero (semicirculo)
    0x00, 0x3C, 0x7E, 0x7E, 0x42, 0x00, 0x00, 0x00,
    // 17: casco pesado (con visera)
    0x3C, 0x7E, 0xFF, 0xFF, 0xC3, 0x42, 0x00, 0x00,
    // 18: armadura ligera (pechera)
    0x42, 0x7E, 0x3C, 0x3C, 0x3C, 0x3C, 0x24, 0x00,
    // 19: armadura pesada (pechera gruesa)
    0xE7, 0xFF, 0x7E, 0x7E, 0x7E, 0x7E, 0x66, 0x00,
    // 20: botas (par)
    0x00, 0x00, 0x24, 0x24, 0x24, 0x3C, 0x7E, 0x00,
    // 21: capa (tela triangular)
    0x18, 0x18, 0x3C, 0x3C, 0x7E, 0x7E, 0xFF, 0x00,
    // 22: anillo (circulo pequeno)
    0x00, 0x00, 0x18, 0x24, 0x24, 0x18, 0x00, 0x00,
    // 23: amuleto (circulo con cadena)
    0x08, 0x08, 0x1C, 0x3E, 0x3E, 0x1C, 0x08, 0x00,
    // 24: pocion HP (botella roja)
    0x08, 0x08, 0x1C, 0x3E, 0x3E, 0x3E, 0x1C, 0x00,
    // 25: pocion MP (botella azul)
    0x08, 0x08, 0x1C, 0x3E, 0x3E, 0x3E, 0x1C, 0x00,
    // 26: pocion velocidad (botella verde)
    0x08, 0x08, 0x1C, 0x3E, 0x3E, 0x3E, 0x1C, 0x00,
    // 27: pan (hogaza)
    0x00, 0x00, 0x1C, 0x3E, 0x3E, 0x1C, 0x00, 0x00,
    // 28: carne (muslo)
    0x00, 0x30, 0x78, 0x7C, 0x3C, 0x1C, 0x08, 0x00,
    // 29: manzana (circulo con tallo)
    0x04, 0x0C, 0x1E, 0x3F, 0x3F, 0x1E, 0x00, 0x00,
    // 30: llave dorada
    0x1C, 0x14, 0x1C, 0x08, 0x08, 0x0E, 0x0A, 0x00,
    // 31: llave plateada
    0x1C, 0x14, 0x1C, 0x08, 0x08, 0x0E, 0x0A, 0x00,
    // 32: moneda de oro
    0x00, 0x1C, 0x3E, 0x2A, 0x3E, 0x1C, 0x00, 0x00,
    // 33: gema roja (diamante)
    0x00, 0x08, 0x1C, 0x3E, 0x1C, 0x08, 0x00, 0x00,
    // 34: gema azul
    0x00, 0x08, 0x1C, 0x3E, 0x1C, 0x08, 0x00, 0x00,
    // 35: pergamino (rollo)
    0x3E, 0x22, 0x22, 0x22, 0x22, 0x22, 0x3E, 0x00,
    // 36: bomba (circulo con mecha)
    0x02, 0x04, 0x1C, 0x3E, 0x3E, 0x3E, 0x1C, 0x00,
    // 37: antorcha (fuego + palo)
    0x08, 0x1C, 0x1C, 0x08, 0x08, 0x08, 0x08, 0x08,
    // 38: cuerda (espiral)
    0x3C, 0x42, 0x02, 0x3C, 0x40, 0x42, 0x3C, 0x00,
    // 39: mapa (papel doblado)
    0x7E, 0x42, 0x5A, 0x42, 0x5A, 0x42, 0x7E, 0x00,
    // --- FUENTE HUD (tiles 40-82) ---
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // 40: espacio
    0x1C,0x22,0x22,0x3E,0x22,0x22,0x22,0x00, // 41: A
    0x3C,0x22,0x3C,0x22,0x22,0x3C,0x00,0x00, // 42: B
    0x1C,0x22,0x20,0x20,0x22,0x1C,0x00,0x00, // 43: C
    0x3C,0x22,0x22,0x22,0x22,0x3C,0x00,0x00, // 44: D
    0x3E,0x20,0x3C,0x20,0x20,0x3E,0x00,0x00, // 45: E
    0x3E,0x20,0x3C,0x20,0x20,0x20,0x00,0x00, // 46: F
    0x1C,0x22,0x20,0x2E,0x22,0x1C,0x00,0x00, // 47: G
    0x22,0x22,0x3E,0x22,0x22,0x22,0x00,0x00, // 48: H
    0x1C,0x08,0x08,0x08,0x08,0x1C,0x00,0x00, // 49: I
    0x0E,0x04,0x04,0x04,0x24,0x18,0x00,0x00, // 50: J
    0x22,0x24,0x38,0x24,0x22,0x22,0x00,0x00, // 51: K
    0x20,0x20,0x20,0x20,0x20,0x3E,0x00,0x00, // 52: L
    0x22,0x36,0x2A,0x22,0x22,0x22,0x00,0x00, // 53: M
    0x22,0x32,0x2A,0x26,0x22,0x22,0x00,0x00, // 54: N
    0x1C,0x22,0x22,0x22,0x22,0x1C,0x00,0x00, // 55: O
    0x3C,0x22,0x3C,0x20,0x20,0x20,0x00,0x00, // 56: P
    0x1C,0x22,0x22,0x2A,0x24,0x1A,0x00,0x00, // 57: Q
    0x3C,0x22,0x3C,0x24,0x22,0x22,0x00,0x00, // 58: R
    0x1C,0x20,0x1C,0x02,0x22,0x1C,0x00,0x00, // 59: S
    0x3E,0x08,0x08,0x08,0x08,0x08,0x00,0x00, // 60: T
    0x22,0x22,0x22,0x22,0x22,0x1C,0x00,0x00, // 61: U
    0x22,0x22,0x22,0x14,0x14,0x08,0x00,0x00, // 62: V
    0x22,0x22,0x22,0x2A,0x36,0x22,0x00,0x00, // 63: W
    0x22,0x14,0x08,0x14,0x22,0x22,0x00,0x00, // 64: X
    0x22,0x14,0x08,0x08,0x08,0x08,0x00,0x00, // 65: Y
    0x3E,0x04,0x08,0x10,0x20,0x3E,0x00,0x00, // 66: Z
    0x1C,0x22,0x26,0x2A,0x32,0x1C,0x00,0x00, // 67: 0
    0x08,0x18,0x08,0x08,0x08,0x1C,0x00,0x00, // 68: 1
    0x1C,0x22,0x04,0x08,0x10,0x3E,0x00,0x00, // 69: 2
    0x1C,0x02,0x0C,0x02,0x22,0x1C,0x00,0x00, // 70: 3
    0x04,0x0C,0x14,0x3E,0x04,0x04,0x00,0x00, // 71: 4
    0x3E,0x20,0x3C,0x02,0x22,0x1C,0x00,0x00, // 72: 5
    0x1C,0x20,0x3C,0x22,0x22,0x1C,0x00,0x00, // 73: 6
    0x3E,0x02,0x04,0x08,0x10,0x10,0x00,0x00, // 74: 7
    0x1C,0x22,0x1C,0x22,0x22,0x1C,0x00,0x00, // 75: 8
    0x1C,0x22,0x1E,0x02,0x04,0x18,0x00,0x00, // 76: 9
    0x00,0x08,0x00,0x00,0x08,0x00,0x00,0x00, // 77: :
    0x02,0x04,0x08,0x10,0x20,0x00,0x00,0x00, // 78: /
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, // 79: barra llena
    0x81,0x81,0x81,0x81,0x81,0x81,0x81,0x81, // 80: barra vacia
    0xFF,0x80,0x80,0x80,0x80,0x80,0x80,0x80, // 81: marco esquina
    0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80, // 82: marco vertical
    0x10,0x18,0x1C,0x1E,0x1C,0x18,0x10,0x00, // 83: cursor >
};

// Colores de cada tile (8 bytes por tile, 1 por fila: [FG<<4 | BG])
const u8 g_TileColors[] = {
    // Tile 0: suelo — gris oscuro sobre negro
    0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0,
    // Tile 1: muro — marron sobre negro
    0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
    // Tile 2: hierba — verde sobre negro
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    // Tile 3: agua — azul oscuro sobre azul claro
    0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47,
    // Tile 4: puerta — marron claro sobre negro
    0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0,
    // Tile 5: escaleras — blanco sobre gris
    0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE, 0xFE,
    // Tile 6: arbol — verde oscuro sobre negro
    0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0x90, 0x90,
    // Tile 7: cofre — amarillo sobre negro
    0xA0, 0xB0, 0xB0, 0xB0, 0xF0, 0xB0, 0xB0, 0xA0,
    // --- ITEMS (colores) ---
    // 8: daga — blanco/negro
    0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
    // 9: espada — blanco/negro
    0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
    // 10: hacha — gris/negro
    0xE0, 0xE0, 0xE0, 0xE0, 0x90, 0x90, 0x90, 0x90,
    // 11: maza — gris/negro
    0xE0, 0xE0, 0xE0, 0xE0, 0x90, 0x90, 0x90, 0x90,
    // 12: arco — marron/negro
    0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0,
    // 13: baston — morado/negro
    0xD0, 0xD0, 0xD0, 0xD0, 0x90, 0x90, 0x90, 0x90,
    // 14: escudo madera — marron/negro
    0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0,
    // 15: escudo hierro — cian/negro
    0x70, 0x70, 0x70, 0x70, 0x70, 0x70, 0x70, 0x70,
    // 16: casco ligero — marron claro/negro
    0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0,
    // 17: casco pesado — gris/negro
    0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0,
    // 18: armadura ligera — marron/negro
    0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0,
    // 19: armadura pesada — cian/negro
    0x70, 0x70, 0x70, 0x70, 0x70, 0x70, 0x70, 0x70,
    // 20: botas — marron/negro
    0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
    // 21: capa — morado/negro
    0xD0, 0xD0, 0xD0, 0xD0, 0xD0, 0xD0, 0xD0, 0xD0,
    // 22: anillo — amarillo/negro
    0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0,
    // 23: amuleto — amarillo/negro
    0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0,
    // 24: pocion HP — rojo/negro
    0xF0, 0xF0, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    // 25: pocion MP — azul/negro
    0xF0, 0xF0, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    // 26: pocion velocidad — verde/negro
    0xF0, 0xF0, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    // 27: pan — marron claro/negro
    0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0,
    // 28: carne — rosa/negro
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    // 29: manzana — rojo/negro
    0x20, 0x20, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    // 30: llave dorada — amarillo/negro
    0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0,
    // 31: llave plateada — blanco/negro
    0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
    // 32: moneda oro — amarillo/negro
    0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0,
    // 33: gema roja — rojo/negro
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    // 34: gema azul — azul claro/negro
    0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50,
    // 35: pergamino — blanco/negro
    0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0,
    // 36: bomba — rojo+negro
    0x80, 0x80, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0,
    // 37: antorcha — naranja+marron
    0xB0, 0x80, 0x80, 0xB0, 0x90, 0x90, 0x90, 0x90,
    // 38: cuerda — marron/negro
    0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0,
    // 39: mapa — blanco/marron
    0xFA, 0xFA, 0x9A, 0xFA, 0x9A, 0xFA, 0xFA, 0xFA,
    // --- FUENTE HUD colores (43 tiles × 8 bytes, blanco sobre negro) ---
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // espacio
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // A
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // B
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // C
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // D
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // E
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // F
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // G
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // H
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // I
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // J
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // K
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // L
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // M
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // N
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // O
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // P
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // Q
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // R
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // S
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // T
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // U
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // V
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // W
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // X
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // Y
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // Z
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // 0
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // 1
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // 2
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // 3
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // 4
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // 5
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // 6
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // 7
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // 8
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // 9
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // :
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // /
    0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80, // barra llena (rojo)
    0xE0,0xE0,0xE0,0xE0,0xE0,0xE0,0xE0,0xE0, // barra vacia (gris)
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // marco esquina
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // marco vertical
    0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0,0xF0, // cursor >
};

//=============================================================================
// SPRITE JUGADOR — 16×16, formato MSX: LT, LB, RT, RB
//=============================================================================

const u8 g_PlayerSprite[32] = {
    // Left top (filas 0-7)
    0x03, 0x07, 0x07, 0x0F, 0x0F, 0x07, 0x03, 0x07,
    // Left bottom (filas 8-15)
    0x0F, 0x0F, 0x07, 0x07, 0x03, 0x03, 0x05, 0x04,
    // Right top (filas 0-7)
    0xC0, 0xE0, 0xE0, 0xF0, 0xF0, 0xE0, 0xC0, 0xE0,
    // Right bottom (filas 8-15)
    0xF0, 0xF0, 0xE0, 0xE0, 0xC0, 0xC0, 0xA0, 0x20,
};

//=============================================================================
// VARIABLES GLOBALES
//=============================================================================

static u8 g_Map[MAP_W * MAP_H];

// Jugador
static u8 g_PlayerX = 5;
static u8 g_PlayerY = 5;
static u8 g_ScrollX = 0;
static u8 g_ScrollY = 0;
static bool g_MapDirty = TRUE;
static bool g_HUDDirty = TRUE;
static u8 g_MoveDelay = 0;
#define MOVE_RATE   2

// Inventario
#define INV_SLOTS   8
static u8 g_Inventory[INV_SLOTS];
static u8 g_InvCount = 0;

// Stats
static u8 g_HP = 100;
static u8 g_MaxHP = 100;
static u8 g_MP = 50;
static u8 g_MaxMP = 50;
static u8 g_Level = 1;
static u8 g_Gold = 0;

// Name table: buffer de trabajo + dirty tracking
static u8 g_NameBuf[768];            // Buffer de trabajo (RAM)
#define NB_DIRTY_MAX 128              // Max tiles cambiados por frame
static u16 g_NbDirtyIdx[NB_DIRTY_MAX];
static u8 g_NbDirtyCount = 0;
static bool g_FullFlush = TRUE;       // TRUE = volcar todo (primera vez)

// Lobby
#define LOBBY_MAX_ROOMS 10
typedef struct {
    u8 roomId;
    u8 gameId;
    u8 players;
} LobbyRoom;
static LobbyRoom g_LobbyRooms[LOBBY_MAX_ROOMS];
static u8 g_LobbyCount = 0;
static u8 g_LobbyCursor = 0;

// Game state
#define STATE_LOBBY_WAIT 0
#define STATE_LOBBY      1
#define STATE_PLAYING    2
static u8 g_GameState = STATE_LOBBY_WAIT;

// Buffer de teclas: se capturan en un frame, se procesan en el siguiente
static u8 g_KeyUp = 0;
static u8 g_KeyDown = 0;
static u8 g_KeyRet = 0;
static u8 g_KeyC = 0;
static u8 g_KeyR = 0;
static u8 g_KeyEsc = 0;

// Red
static NetConn g_Conn = NET_INVALID_CONN;
static u8 g_MyPid = 0;
static u8 g_RoomId = 0;
static u16 g_PingTimer = 0;
static u8 g_SendBuf[20];

// Entidades remotas (otros jugadores)
typedef struct {
    u8 active;
    u8 x;
    u8 y;
    u8 dir;
    u8 hp;
    u8 classType;
} Entity;
static Entity g_Entities[MAX_ENTITIES + 1];

const u8 g_EntityColors[17] = {
    0, 15, 7, 9, 11, 3, 5, 8, 10, 12, 13, 14, 6, 4, 2, 1, 15
};

//=============================================================================
// RED — CONEXION Y PROTOCOLO
//=============================================================================

//=============================================================================
// DIAGNOSTICO DE RED (Screen 0 — modo texto MSX-DOS)
//=============================================================================

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
    DOS_StringOutput("   BURDYN - NET DIAGNOSTICS\r\n$");
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

//=============================================================================
// RED — CONEXION Y PROTOCOLO
//=============================================================================

void Net_Wait50(void)
{
    u8 w;
    for(w = 0; w < 25; w++) Halt();
}

bool Net_ConnectToServer(void)
{
    u8 tcpState;
    u16 timeout;

    Log_Init();
    Log_Write("[INIT] Burdyn arrancando");

    // Buscar UNAPI
    if(Net_Init() != NET_OK)
    {
        Log_Write("[CONN] UNAPI no hallado");
        return FALSE;
    }
    Log_WriteHex("[CONN] UNAPI OK impl=", g_NetImplCount);

    Net_Wait50();

    // IP local
    tcpip_get_ipinfo(&g_IpInfo);
    Log_WriteHex("[CONN] IP=", (u8)g_IpInfo.local_ip[0]);

    Net_Wait50();

    // Abrir TCP
    Log_Write("[CONN] Abriendo TCP...");
    g_Conn = Net_Open(SERVER_IP, SERVER_PORT);
    if(g_Conn == NET_INVALID_CONN)
    {
        Log_WriteHex("[CONN] Fallo err=", g_NetLastError);
        return FALSE;
    }
    Log_WriteHex("[CONN] Handle=", (u8)g_Conn);

    // Esperar ESTABLISHED
    timeout = 0;
    while(timeout < 500)
    {
        Halt();
        tcpState = Net_GetConnState(g_Conn);
        if(tcpState == TCP_STATE_ESTABLISHED) break;
        if(tcpState == 0xFF) { Log_Write("[CONN] Fallo TCP"); return FALSE; }
        timeout++;
    }
    if(timeout >= 500) { Log_Write("[CONN] Timeout"); return FALSE; }
    Log_Write("[CONN] ESTABLISHED");

    Net_Wait50();

    // Auth
    {
        u8 token[4];
        u8 len;
        token[0] = AUTH_TOKEN_0;
        token[1] = AUTH_TOKEN_1;
        token[2] = AUTH_TOKEN_2;
        token[3] = AUTH_TOKEN_3;
        g_SendBuf[0] = PROTO_MAGIC_0;
        g_SendBuf[1] = PROTO_MAGIC_1;
        g_SendBuf[2] = CMD_AUTH;
        g_SendBuf[3] = 0;
        g_SendBuf[4] = 0;
        g_SendBuf[5] = 4;
        g_SendBuf[6] = token[0];
        g_SendBuf[7] = token[1];
        g_SendBuf[8] = token[2];
        g_SendBuf[9] = token[3];
        Net_Send(g_Conn, g_SendBuf, 10);
        Log_Write("[AUTH] Enviado");
    }

    // Esperar AUTH_OK
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
            if(hdr[2] == CMD_AUTH_FAIL) { Log_Write("[AUTH] FAIL"); return FALSE; }
        }
        timeout++;
    }
    if(timeout >= 250) { Log_Write("[AUTH] Timeout"); return FALSE; }

    return TRUE;
}

// Pedir lista de salas al servidor
void Net_RequestRoomList(void)
{
    if(g_Conn == NET_INVALID_CONN) return;
    g_SendBuf[0] = PROTO_MAGIC_0;
    g_SendBuf[1] = PROTO_MAGIC_1;
    g_SendBuf[2] = CMD_ROOM_LIST;
    g_SendBuf[3] = 0;
    g_SendBuf[4] = 0;
    g_SendBuf[5] = 0;
    Net_Send(g_Conn, g_SendBuf, 6);
}

// Crear sala nueva
void Net_CreateRoom(void)
{
    if(g_Conn == NET_INVALID_CONN) return;
    g_SendBuf[0] = PROTO_MAGIC_0;
    g_SendBuf[1] = PROTO_MAGIC_1;
    g_SendBuf[2] = CMD_ROOM_CREATE;
    g_SendBuf[3] = 0;
    g_SendBuf[4] = 0;
    g_SendBuf[5] = 3;
    g_SendBuf[6] = GAME_ID_CRAWLER;
    g_SendBuf[7] = 14;  // max 14 jugadores
    g_SendBuf[8] = PROTO_VERSION_AGGREGATE;
    Net_Send(g_Conn, g_SendBuf, 9);
    Log_Write("[ROOM] CREATE enviado");
}

// Unirse a sala existente
void Net_JoinRoom(u8 roomId)
{
    if(g_Conn == NET_INVALID_CONN) return;
    g_SendBuf[0] = PROTO_MAGIC_0;
    g_SendBuf[1] = PROTO_MAGIC_1;
    g_SendBuf[2] = CMD_ROOM_JOIN;
    g_SendBuf[3] = 0;
    g_SendBuf[4] = 0;
    g_SendBuf[5] = 1;
    g_SendBuf[6] = roomId;
    Net_Send(g_Conn, g_SendBuf, 7);
    Log_WriteHex("[ROOM] JOIN enviado room=", roomId);
}

// Enviar GAME_START
void Net_SendGameStart(void)
{
    if(g_Conn == NET_INVALID_CONN) return;
    g_SendBuf[0] = PROTO_MAGIC_0;
    g_SendBuf[1] = PROTO_MAGIC_1;
    g_SendBuf[2] = CMD_GAME_START;
    g_SendBuf[3] = g_RoomId;
    g_SendBuf[4] = g_MyPid;
    g_SendBuf[5] = 0;
    Net_Send(g_Conn, g_SendBuf, 6);
}

// Procesar paquete recibido (lobby + juego)
void Net_ProcessPacket(u8 cmd, u8* payload, u8 len)
{
    if(cmd == CMD_ROOM_LIST && len >= 1)
    {
        u8 count = payload[0];
        u8 i;
        g_LobbyCount = 0;
        for(i = 0; i < count && i < LOBBY_MAX_ROOMS; i++)
        {
            u8 off = 1 + i * 3;
            u8 gid = payload[off + 1];
            // Solo mostrar salas de Burdyn
            if(gid == GAME_ID_CRAWLER)
            {
                g_LobbyRooms[g_LobbyCount].roomId = payload[off];
                g_LobbyRooms[g_LobbyCount].gameId = gid;
                g_LobbyRooms[g_LobbyCount].players = payload[off + 2];
                g_LobbyCount++;
            }
        }
        g_LobbyCursor = 0;
        Log_WriteHex("[LOBBY] Salas Burdyn=", g_LobbyCount);
        g_GameState = STATE_LOBBY;
        Lobby_Draw();
    }
    else if(cmd == CMD_ROOM_INFO && len >= 4)
    {
        g_RoomId = payload[0];
        g_MyPid = payload[3];
        Log_WriteHex("[ROOM] Room=", g_RoomId);
        Log_WriteHex("[ROOM] PID=", g_MyPid);
        // Si somos P1, enviar GAME_START para activar tick AGGREGATE
        if(g_MyPid == 1)
            Net_SendGameStart();
        g_GameState = STATE_PLAYING;
        Scroll_Update();
        g_MapDirty = TRUE;
        g_HUDDirty = TRUE;
        g_FullFlush = TRUE;
    }
    else if(cmd == CMD_WORLD_STATE && len >= 3)
    {
        u8 nPlayers = payload[1];
        u8 off = 3;
        u8 i;
        for(i = 1; i <= MAX_ENTITIES; i++) g_Entities[i].active = 0;
        for(i = 0; i < nPlayers && off + 10 <= len; i++)
        {
            u8 pid = payload[off];
            u8 pflags = payload[off + 1];
            if(pid >= 1 && pid <= MAX_ENTITIES && pid != g_MyPid)
            {
                g_Entities[pid].active = (pflags & 0x01) ? 1 : 0;
                g_Entities[pid].x = payload[off + 2];
                g_Entities[pid].y = payload[off + 3];
                g_Entities[pid].dir = payload[off + 4];
                g_Entities[pid].hp = payload[off + 5];
                g_Entities[pid].classType = payload[off + 6];
            }
            off += 10;
        }
    }
}

void Net_SendPosition(void)
{
    if(g_Conn == NET_INVALID_CONN) return;

    g_SendBuf[0] = PROTO_MAGIC_0;
    g_SendBuf[1] = PROTO_MAGIC_1;
    g_SendBuf[2] = CMD_STATE_UPDATE;
    g_SendBuf[3] = g_RoomId;
    g_SendBuf[4] = g_MyPid;
    g_SendBuf[5] = 8; // payload
    g_SendBuf[6] = g_PlayerX;
    g_SendBuf[7] = g_PlayerY;
    g_SendBuf[8] = 0; // direction
    g_SendBuf[9] = g_HP;
    g_SendBuf[10] = 0; // item
    g_SendBuf[11] = 0; // class
    g_SendBuf[12] = g_Level;
    g_SendBuf[13] = 0; // flags
    Net_Send(g_Conn, g_SendBuf, 14);
}

void Net_SendPing(void)
{
    if(g_Conn == NET_INVALID_CONN) return;
    g_SendBuf[0] = PROTO_MAGIC_0;
    g_SendBuf[1] = PROTO_MAGIC_1;
    g_SendBuf[2] = CMD_PING;
    g_SendBuf[3] = g_RoomId;
    g_SendBuf[4] = g_MyPid;
    g_SendBuf[5] = 0;
    Net_Send(g_Conn, g_SendBuf, 6);
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

        Net_ProcessPacket(hdr[2], payload, hdr[5]);
    }

    // Ping periodico
    g_PingTimer++;
    if(g_PingTimer >= PING_INTERVAL)
    {
        g_PingTimer = 0;
        Net_SendPing();
    }
}

void Entities_DrawSprites(void)
{
    u8 i;
    u8 sprIdx = 1; // Sprite 0 = jugador local

    for(i = 1; i <= MAX_ENTITIES && sprIdx < 15; i++)
    {
        if(g_Entities[i].active && i != g_MyPid)
        {
            // Solo dibujar si esta dentro del viewport
            u8 ex = g_Entities[i].x;
            u8 ey = g_Entities[i].y;

            if(ex >= g_ScrollX && ex < g_ScrollX + VIEW_W &&
               ey >= g_ScrollY && ey < g_ScrollY + VIEW_H)
            {
                u8 px = (ex - g_ScrollX) * 8;
                u8 py = (ey - g_ScrollY) * 8;
                VDP_SetSpriteExUniColor(sprIdx, px - 4, py - 4, 0, g_EntityColors[i]);
                sprIdx++;
            }
        }
    }

    // Ocultar sprites no usados
    for(; sprIdx < 15; sprIdx++)
        VDP_SetSpriteExUniColor(sprIdx, 0, 209, 0, 0);
}

//=============================================================================
// CARGA DE MAPA DESDE DISCO
//=============================================================================

bool Map_LoadFromDisk(void)
{
    u8 handle;
    u16 remaining;
    u16 offset;
    u16 read;
    u8 buf[128];

    handle = DOS_FOpen("BURDYN.MAP", O_RDONLY);
    if(handle == 0xFF) return FALSE;

    remaining = MAP_W * MAP_H; // 4096
    offset = 0;
    while(remaining > 0)
    {
        u16 chunk = (remaining > 128) ? 128 : remaining;
        read = DOS_FRead(handle, buf, chunk);
        if(read == 0) break;
        {
            u16 i;
            for(i = 0; i < read; i++)
                g_Map[offset + i] = buf[i];
        }
        offset += read;
        remaining -= read;
    }

    DOS_FClose(handle);
    Log_WriteHex("[MAP] Cargado bytes=", (u8)(offset >> 8));
    return (offset >= MAP_W * MAP_H) ? TRUE : FALSE;
}

//=============================================================================
// GENERADOR DE MAPA (fallback si no hay fichero)
//=============================================================================

void Map_Generate(void)
{
    u8 x, y;
    u16 idx;

    for(y = 0; y < MAP_H; y++)
    {
        for(x = 0; x < MAP_W; x++)
        {
            idx = (u16)y * MAP_W + x;

            // Bordes del mapa = muro
            if(x == 0 || y == 0 || x == MAP_W - 1 || y == MAP_H - 1)
            {
                g_Map[idx] = TILE_WALL;
            }
            // Muros internos: cuadricula cada 8 tiles (con huecos)
            else if((x & 7) == 0 && (y & 3) != 0)
            {
                g_Map[idx] = TILE_WALL;
            }
            else if((y & 7) == 0 && (x & 3) != 0)
            {
                g_Map[idx] = TILE_WALL;
            }
            // Agua en zona central
            else if(x >= 28 && x <= 35 && y >= 28 && y <= 35)
            {
                g_Map[idx] = TILE_WATER;
            }
            // Arboles dispersos
            else if(((x * 7 + y * 13) & 0x3F) == 0)
            {
                g_Map[idx] = TILE_TREE;
            }
            // Cofres raros
            else if(((x * 11 + y * 23) & 0xFF) == 0 && x > 2 && y > 2)
            {
                g_Map[idx] = TILE_CHEST;
            }
            // Hierba en algunos sitios
            else if(((x + y) & 3) == 0)
            {
                g_Map[idx] = TILE_GRASS;
            }
            else
            {
                g_Map[idx] = TILE_FLOOR;
            }
        }
    }

    // Puertas entre secciones
    g_Map[32 + 12 * MAP_W] = TILE_DOOR;  // Puerta horizontal
    g_Map[12 + 32 * MAP_W] = TILE_DOOR;  // Puerta vertical
    g_Map[32 + 44 * MAP_W] = TILE_DOOR;
    g_Map[44 + 32 * MAP_W] = TILE_DOOR;

    // Escaleras
    g_Map[3 + 3 * MAP_W] = TILE_STAIRS;
    g_Map[60 + 60 * MAP_W] = TILE_STAIRS;

    // Items dispersos por el mapa
    g_Map[5  + 3  * MAP_W] = ITEM_SWORD;
    g_Map[10 + 5  * MAP_W] = ITEM_SHIELD_W;
    g_Map[15 + 3  * MAP_W] = ITEM_POTION_HP;
    g_Map[20 + 7  * MAP_W] = ITEM_GOLD_COIN;
    g_Map[3  + 10 * MAP_W] = ITEM_DAGGER;
    g_Map[25 + 12 * MAP_W] = ITEM_HELMET_L;
    g_Map[6  + 15 * MAP_W] = ITEM_BREAD;
    g_Map[18 + 18 * MAP_W] = ITEM_KEY_GOLD;
    g_Map[40 + 5  * MAP_W] = ITEM_AXE;
    g_Map[45 + 10 * MAP_W] = ITEM_ARMOR_L;
    g_Map[50 + 3  * MAP_W] = ITEM_BOW;
    g_Map[55 + 8  * MAP_W] = ITEM_POTION_MP;
    g_Map[38 + 20 * MAP_W] = ITEM_TORCH;
    g_Map[42 + 15 * MAP_W] = ITEM_BOMB;
    g_Map[10 + 40 * MAP_W] = ITEM_GEM_RED;
    g_Map[20 + 45 * MAP_W] = ITEM_GEM_BLUE;
    g_Map[35 + 50 * MAP_W] = ITEM_SCROLL;
    g_Map[50 + 55 * MAP_W] = ITEM_MAP;
    g_Map[15 + 35 * MAP_W] = ITEM_MEAT;
    g_Map[55 + 40 * MAP_W] = ITEM_ROPE;
    g_Map[5  + 55 * MAP_W] = ITEM_CLOAK;
    g_Map[45 + 45 * MAP_W] = ITEM_RING;
    g_Map[30 + 10 * MAP_W] = ITEM_STAFF;
    g_Map[22 + 22 * MAP_W] = ITEM_AMULET;
    g_Map[8  + 25 * MAP_W] = ITEM_APPLE;
    g_Map[48 + 30 * MAP_W] = ITEM_MACE;
    g_Map[12 + 50 * MAP_W] = ITEM_BOOTS;
    g_Map[58 + 15 * MAP_W] = ITEM_SHIELD_I;
    g_Map[35 + 35 * MAP_W] = ITEM_ARMOR_H;
    g_Map[52 + 48 * MAP_W] = ITEM_HELMET_H;
    g_Map[28 + 55 * MAP_W] = ITEM_KEY_SILVER;
    g_Map[60 + 3  * MAP_W] = ITEM_POTION_SPD;
}

//=============================================================================
// VDP: CARGAR TILESET EN VRAM
//=============================================================================

void VDP_LoadTileset(void)
{
    u8 tile, bank;

    // Screen 4 (Graphic 3) tiene 3 bancos de patterns/colors
    // como Screen 2. Cada banco cubre 1/3 de la pantalla (8 filas de tiles).
    // Cargamos los mismos tiles en los 3 bancos.

    for(bank = 0; bank < 3; bank++)
    {
        u16 patBase = bank * 2048;   // 0x0000, 0x0800, 0x1000
        u16 colBase = 0x2000 + bank * 2048;

        for(tile = 0; tile < TILE_COUNT; tile++)
        {
            VDP_WriteVRAM(g_TilePatterns + tile * 8, patBase + tile * 8, 0, 8);
            VDP_WriteVRAM(g_TileColors + tile * 8, colBase + tile * 8, 0, 8);
        }
    }
}

//=============================================================================
// SCROLL: centrar viewport en el jugador
//=============================================================================

void Scroll_Update(void)
{
    // Centrar jugador en el viewport
    u8 halfW = VIEW_W / 2;
    u8 halfH = VIEW_H / 2;

    if(g_PlayerX >= halfW)
        g_ScrollX = g_PlayerX - halfW;
    else
        g_ScrollX = 0;

    if(g_PlayerY >= halfH)
        g_ScrollY = g_PlayerY - halfH;
    else
        g_ScrollY = 0;

    // Limitar para no salirse del mapa
    if(g_ScrollX + VIEW_W > MAP_W) g_ScrollX = MAP_W - VIEW_W;
    if(g_ScrollY + VIEW_H > MAP_H) g_ScrollY = MAP_H - VIEW_H;
}

//=============================================================================
// DIBUJAR VIEWPORT (24×24 tiles) + HUD (8 columnas derecha)
//=============================================================================

// Tile especial para el HUD (fondo oscuro)
#define HUD_BG  TILE_WALL

// Escribir un tile al buffer con dirty tracking
void Buf_PutTile(u8 col, u8 row, u8 tile)
{
    u16 idx = (u16)row * 32 + col;
    if(g_NameBuf[idx] != tile)
    {
        g_NameBuf[idx] = tile;
        if(g_NbDirtyCount < NB_DIRTY_MAX)
            g_NbDirtyIdx[g_NbDirtyCount++] = idx;
        else
            g_FullFlush = TRUE; // Desbordado: volcar todo
    }
}

// Escribir texto al buffer
void Buf_PutText(u8 col, u8 row, const c8* text)
{
    while(*text && col < 32)
    {
        u8 ch = (u8)*text;
        u8 tile;
        if(ch >= 'A' && ch <= 'Z')
            tile = 41 + (ch - 'A');
        else if(ch >= 'a' && ch <= 'z')
            tile = 41 + (ch - 'a');
        else if(ch >= '0' && ch <= '9')
            tile = 67 + (ch - '0');
        else if(ch == ':')
            tile = TILE_COLON;
        else if(ch == '/')
            tile = TILE_SLASH;
        else
            tile = TILE_SPC;
        Buf_PutTile(col, row, tile);
        col++;
        text++;
    }
}

// Escribir numero al buffer
void Buf_PutNum(u8 col, u8 row, u8 val)
{
    u8 h = val / 100;
    u8 t = (val % 100) / 10;
    u8 u = val % 10;
    if(h > 0)
    {
        Buf_PutTile(col++, row, 67 + h);
        Buf_PutTile(col++, row, 67 + t);
        Buf_PutTile(col, row, 67 + u);
    }
    else if(t > 0)
    {
        Buf_PutTile(col++, row, 67 + t);
        Buf_PutTile(col, row, 67 + u);
    }
    else
    {
        Buf_PutTile(col, row, 67 + u);
    }
}

void Map_DrawViewport(void)
{
    u8 tx, ty, mapX, mapY, tile;

    for(ty = 0; ty < VIEW_H; ty++)
    {
        mapY = g_ScrollY + ty;
        for(tx = 0; tx < VIEW_W; tx++)
        {
            mapX = g_ScrollX + tx;
            if(mapX < MAP_W && mapY < MAP_H)
                tile = g_Map[(u16)mapY * MAP_W + mapX];
            else
                tile = TILE_WALL;
            Buf_PutTile(tx, ty, tile);
        }
    }

    g_MapDirty = FALSE;
}

//=============================================================================
// HUD — sidebar derecha (columnas 24-31)
//=============================================================================

void HUD_Draw(void)
{
    u8 i;
    u8 barLen;

    if(!g_HUDDirty) return;

    // Limpiar zona HUD en buffer (columnas 24-31)
    for(i = 0; i < 24; i++)
    {
        u8 c;
        for(c = HUD_COL; c < 32; c++)
            Buf_PutTile(c, i, TILE_SPC);
    }

    // Separador vertical (columna 24)
    for(i = 0; i < 24; i++)
        Buf_PutTile(HUD_COL, i, TILE_FRAME_V);

    // -- HP --
    Buf_PutText(25, 0, "HP");
    barLen = (g_HP * 6) / g_MaxHP;
    for(i = 0; i < 6; i++)
        Buf_PutTile(25 + i, 1, (i < barLen) ? TILE_BAR_FULL : TILE_BAR_EMPTY);
    Buf_PutNum(25, 2, g_HP);
    Buf_PutTile(28, 2, TILE_SLASH);
    Buf_PutNum(29, 2, g_MaxHP);

    // -- MP --
    Buf_PutText(25, 4, "MP");
    barLen = (g_MP * 6) / g_MaxMP;
    for(i = 0; i < 6; i++)
        Buf_PutTile(25 + i, 5, (i < barLen) ? TILE_BAR_FULL : TILE_BAR_EMPTY);
    Buf_PutNum(25, 6, g_MP);
    Buf_PutTile(28, 6, TILE_SLASH);
    Buf_PutNum(29, 6, g_MaxMP);

    // -- LEVEL --
    Buf_PutText(25, 8, "LV");
    Buf_PutNum(28, 8, g_Level);

    // -- GOLD --
    Buf_PutText(25, 10, "GOLD");
    Buf_PutNum(25, 11, g_Gold);

    // -- ONLINE/OFFLINE --
    if(g_Conn != NET_INVALID_CONN)
        Buf_PutText(25, 22, "ONLINE");
    else
        Buf_PutText(25, 22, "OFLINE");

    // -- ITEMS --
    Buf_PutText(25, 13, "ITEMS");
    for(i = 0; i < INV_SLOTS; i++)
    {
        u8 r = 14 + i;
        if(i < g_InvCount && g_Inventory[i] != 0)
            Buf_PutTile(26, r, g_Inventory[i]);
        else
            Buf_PutTile(26, r, TILE_SPC);
    }

    g_HUDDirty = FALSE;
}

//=============================================================================
// SPRITE DEL JUGADOR
//=============================================================================

void Player_UpdateSprite(void)
{
    // Posicion en pixeles relativa al scroll
    u8 px = (g_PlayerX - g_ScrollX) * 8;
    u8 py = (g_PlayerY - g_ScrollY) * 8;

    // Centrar sprite 16×16 en tile 8×8: offset -4
    VDP_SetSpriteExUniColor(SPR_PLAYER, px - 4, py - 4, 0, g_EntityColors[g_MyPid > 0 ? g_MyPid : 1]);
}

//=============================================================================
// INPUT Y MOVIMIENTO
//=============================================================================

// Scroll incremental: desplazar buffer y rellenar columna nueva
void Map_ScrollColumn(i8 dx)
{
    u8 ty, tx;
    u8 newCol;
    u8 mapX;

    if(dx > 0)
    {
        // Scroll derecha: desplazar buffer a la izquierda, rellenar columna derecha
        for(ty = 0; ty < VIEW_H; ty++)
        {
            u16 base = (u16)ty * 32;
            for(tx = 0; tx < VIEW_W - 1; tx++)
                Buf_PutTile(tx, ty, g_NameBuf[base + tx + 1]);
            // Columna nueva (la de la derecha)
            mapX = g_ScrollX + VIEW_W - 1;
            Buf_PutTile(VIEW_W - 1, ty, (mapX < MAP_W) ? g_Map[(u16)(g_ScrollY + ty) * MAP_W + mapX] : TILE_WALL);
        }
    }
    else
    {
        // Scroll izquierda: desplazar buffer a la derecha, rellenar columna izquierda
        for(ty = 0; ty < VIEW_H; ty++)
        {
            u16 base = (u16)ty * 32;
            for(tx = VIEW_W - 1; tx > 0; tx--)
                Buf_PutTile(tx, ty, g_NameBuf[base + tx - 1]);
            // Columna nueva (la de la izquierda)
            mapX = g_ScrollX;
            Buf_PutTile(0, ty, (mapX < MAP_W) ? g_Map[(u16)(g_ScrollY + ty) * MAP_W + mapX] : TILE_WALL);
        }
    }
}

// Scroll incremental: desplazar buffer y rellenar fila nueva
void Map_ScrollRow(i8 dy)
{
    u8 tx, ty;
    u8 mapY;

    if(dy > 0)
    {
        // Scroll abajo: desplazar buffer arriba, rellenar fila inferior
        for(ty = 0; ty < VIEW_H - 1; ty++)
        {
            u16 src = (u16)(ty + 1) * 32;
            for(tx = 0; tx < VIEW_W; tx++)
                Buf_PutTile(tx, ty, g_NameBuf[src + tx]);
        }
        // Fila nueva (la de abajo)
        mapY = g_ScrollY + VIEW_H - 1;
        for(tx = 0; tx < VIEW_W; tx++)
        {
            u8 mapX = g_ScrollX + tx;
            Buf_PutTile(tx, VIEW_H - 1, (mapX < MAP_W && mapY < MAP_H) ? g_Map[(u16)mapY * MAP_W + mapX] : TILE_WALL);
        }
    }
    else
    {
        // Scroll arriba: desplazar buffer abajo, rellenar fila superior
        for(ty = VIEW_H - 1; ty > 0; ty--)
        {
            u16 src = (u16)(ty - 1) * 32;
            for(tx = 0; tx < VIEW_W; tx++)
                Buf_PutTile(tx, ty, g_NameBuf[src + tx]);
        }
        // Fila nueva (la de arriba)
        mapY = g_ScrollY;
        for(tx = 0; tx < VIEW_W; tx++)
        {
            u8 mapX = g_ScrollX + tx;
            Buf_PutTile(tx, 0, (mapX < MAP_W && mapY < MAP_H) ? g_Map[(u16)mapY * MAP_W + mapX] : TILE_WALL);
        }
    }
}

bool Map_IsWalkable(u8 x, u8 y)
{
    u8 tile;
    if(x >= MAP_W || y >= MAP_H) return FALSE;
    tile = g_Map[(u16)y * MAP_W + x];
    return (tile != TILE_WALL && tile != TILE_WATER && tile != TILE_TREE);
}

void Player_Move(void)
{
    u8 newX = g_PlayerX;
    u8 newY = g_PlayerY;

    // Rate limit: solo mover cada MOVE_RATE frames
    if(g_MoveDelay > 0) { g_MoveDelay--; return; }

    if(Keyboard_IsKeyPressed(KEY_UP))    { if(newY > 0) newY--; }
    if(Keyboard_IsKeyPressed(KEY_DOWN))  { if(newY < MAP_H - 1) newY++; }
    if(Keyboard_IsKeyPressed(KEY_LEFT))  { if(newX > 0) newX--; }
    if(Keyboard_IsKeyPressed(KEY_RIGHT)) { if(newX < MAP_W - 1) newX++; }

    // Colision
    if(!Map_IsWalkable(newX, newY))
        return;

    if(newX == g_PlayerX && newY == g_PlayerY)
        return;

    g_PlayerX = newX;
    g_PlayerY = newY;
    g_MoveDelay = MOVE_RATE;

    // Recoger item si pisamos uno
    {
        u16 idx = (u16)g_PlayerY * MAP_W + g_PlayerX;
        u8 tile = g_Map[idx];
        if(tile >= 8 && tile < TILE_COUNT && g_InvCount < INV_SLOTS)
        {
            // Monedas de oro suman directo
            if(tile == ITEM_GOLD_COIN)
            {
                g_Gold++;
            }
            else
            {
                g_Inventory[g_InvCount] = tile;
                g_InvCount++;
            }
            g_Map[idx] = TILE_FLOOR; // quitar item del mapa
            g_HUDDirty = TRUE;
        }
    }

    // Solo redibujar mapa si el scroll cambio
    {
        u8 oldSX = g_ScrollX;
        u8 oldSY = g_ScrollY;
        Scroll_Update();
        if(g_ScrollX != oldSX || g_ScrollY != oldSY)
        {
            // Scroll cambio: volcar mapa completo (576 tiles)
            // Escribir directo al buffer sin dirty tracking (mas rapido)
            // y marcar full flush
            u8 tx, ty, mapX, mapY;
            for(ty = 0; ty < VIEW_H; ty++)
            {
                u16 bufBase = (u16)ty * 32;
                mapY = g_ScrollY + ty;
                for(tx = 0; tx < VIEW_W; tx++)
                {
                    mapX = g_ScrollX + tx;
                    g_NameBuf[bufBase + tx] = (mapX < MAP_W && mapY < MAP_H) ? g_Map[(u16)mapY * MAP_W + mapX] : TILE_WALL;
                }
            }
            g_FullFlush = TRUE;
        }
    }
}

//=============================================================================
// LOBBY GRAFICO (Screen 4)
//=============================================================================

void Lobby_Draw(void)
{
    u8 i;
    u16 idx;

    // Limpiar buffer completo (forzar full flush)
    for(idx = 0; idx < 768; idx++) g_NameBuf[idx] = TILE_SPC;
    g_FullFlush = TRUE;

    Buf_PutText(5, 1, "BURDYN");
    Buf_PutText(2, 3, "SALAS DISPONIBLES");

    if(g_LobbyCount == 0)
    {
        Buf_PutText(2, 6, "NO HAY SALAS");
        Buf_PutText(2, 8, "PULSA C PARA CREAR");
    }
    else
    {
        Buf_PutText(3, 5, "SALA  JUGADORES");

        for(i = 0; i < g_LobbyCount; i++)
        {
            u8 row = 7 + i * 2;
            if(row > 20) break;

            if(i == g_LobbyCursor)
                Buf_PutTile(1, row, TILE_CURSOR);

            Buf_PutNum(3, row, g_LobbyRooms[i].roomId);
            Buf_PutNum(10, row, g_LobbyRooms[i].players);
            Buf_PutTile(12, row, TILE_SLASH);
            Buf_PutNum(13, row, 4);
        }
    }

    Buf_PutText(2, 22, "C CREAR  ENTER UNIR");
    Buf_PutText(2, 23, "R REFRESCAR  ESC SALIR");
}

void Lobby_MoveCursor(u8 oldPos, u8 newPos)
{
    // Borrar cursor viejo
    u8 oldRow = 7 + oldPos * 2;
    Buf_PutTile(1, oldRow, TILE_SPC);
    // Poner cursor nuevo
    u8 newRow = 7 + newPos * 2;
    Buf_PutTile(1, newRow, TILE_CURSOR);
}

void Lobby_ProcessInput(void)
{
    if(g_KeyUp)
    {
        g_KeyUp = 0;
        if(g_LobbyCursor > 0)
        {
            u8 old = g_LobbyCursor;
            g_LobbyCursor--;
            Lobby_MoveCursor(old, g_LobbyCursor);
        }
    }
    if(g_KeyDown)
    {
        g_KeyDown = 0;
        if(g_LobbyCount > 0 && g_LobbyCursor < g_LobbyCount - 1)
        {
            u8 old = g_LobbyCursor;
            g_LobbyCursor++;
            Lobby_MoveCursor(old, g_LobbyCursor);
        }
    }
    if(g_KeyRet)
    {
        g_KeyRet = 0;
        if(g_LobbyCount > 0)
            Net_JoinRoom(g_LobbyRooms[g_LobbyCursor].roomId);
    }
    if(g_KeyC)
    {
        g_KeyC = 0;
        Net_CreateRoom();
    }
    if(g_KeyR)
    {
        g_KeyR = 0;
        g_GameState = STATE_LOBBY_WAIT;
        Net_RequestRoomList();
    }
}

//=============================================================================
// MAIN
//=============================================================================

void main(void)
{
    u8 i;
    bool online;

    // Check if launched from LOBBY.COM
    if(LobbyClient_Load()) {
        g_Conn = (NetConn)(int)g_LobbyData.conn;
        g_MyPid = g_LobbyData.pid;
        g_RoomId = g_LobbyData.roomId;
        online = TRUE;
        Net_Init();
    } else {
        Diag_ShowNetInfo();
        online = Net_ConnectToServer();
    }

    // Screen 4 (Graphic 3)
    VDP_SetMode(VDP_MODE_SCREEN4);
    VDP_SetColor(1);

    // Sprites 16x16
    VDP_SetSpriteFlag(VDP_SPRITE_SIZE_16 | VDP_SPRITE_SCALE_1);
    VDP_LoadSpritePattern(g_PlayerSprite, 0, 4);

    // Ocultar todos los sprites al inicio
    {
        u8 s;
        for(s = 0; s < 32; s++)
            VDP_SetSpriteExUniColor(s, 0, 209, 0, 0);
    }

    // Inicializar
    for(i = 0; i < INV_SLOTS; i++) g_Inventory[i] = 0;
    for(i = 0; i <= MAX_ENTITIES; i++) g_Entities[i].active = 0;
    if(!Map_LoadFromDisk())
        Map_Generate();
    VDP_LoadTileset();

    // Primera vez: forzar volcado completo
    g_FullFlush = TRUE;

    if(g_FromLobby)
    {
        // From LOBBY.COM: straight to playing
        g_GameState = STATE_PLAYING;
        Scroll_Update();
        Map_DrawViewport();
        g_HUDDirty = TRUE;
        HUD_Draw();
        Player_UpdateSprite();
    }
    else if(online)
    {
        g_GameState = STATE_LOBBY_WAIT;
        Net_RequestRoomList();
    }
    else
    {
        g_GameState = STATE_PLAYING;
        Scroll_Update();
        Map_DrawViewport();
        g_HUDDirty = TRUE;
        HUD_Draw();
        Player_UpdateSprite();
    }

    // Game loop
    while(1)
    {
        Halt();

        // Volcar a VRAM: full flush o solo dirty tiles
        if(g_FullFlush)
        {
            VDP_WriteVRAM(g_NameBuf, 0x1800, 0, 768);
            g_FullFlush = FALSE;
            g_NbDirtyCount = 0;
        }
        else if(g_NbDirtyCount > 0)
        {
            u8 d;
            for(d = 0; d < g_NbDirtyCount; d++)
            {
                u16 idx = g_NbDirtyIdx[d];
                VDP_WriteVRAM(g_NameBuf + idx, 0x1800 + idx, 0, 1);
            }
            g_NbDirtyCount = 0;
        }

        Keyboard_Update();

        // Vaciar buffer teclado BIOS (evitar acumulacion para DOS)
        *((u16*)0xF3F8) = *((u16*)0xF3FA);

        // Capturar teclas
        if(g_GameState == STATE_LOBBY || g_GameState == STATE_LOBBY_WAIT)
        {
            // Lobby: anti-rebote para evitar repeticion
            if(g_MoveDelay > 0)
            {
                g_MoveDelay--;
            }
            else
            {
                if(Keyboard_IsKeyPressed(KEY_UP))    { g_KeyUp = 1; g_MoveDelay = 8; }
                if(Keyboard_IsKeyPressed(KEY_DOWN))  { g_KeyDown = 1; g_MoveDelay = 8; }
                if(Keyboard_IsKeyPressed(KEY_RET))   { g_KeyRet = 1; g_MoveDelay = 15; }
                if(Keyboard_IsKeyPressed(KEY_C))     { g_KeyC = 1; g_MoveDelay = 15; }
                if(Keyboard_IsKeyPressed(KEY_R))     { g_KeyR = 1; g_MoveDelay = 15; }
                if(Keyboard_IsKeyPressed(KEY_ESC))   { g_KeyEsc = 1; }
            }
        }
        else
        {
            // Juego: sin anti-rebote, MOVE_RATE se aplica en Player_Move
            if(Keyboard_IsKeyPressed(KEY_ESC))   g_KeyEsc = 1;
        }

        // ESC = salir
        if(g_KeyEsc) break;

        if(g_GameState == STATE_LOBBY_WAIT)
        {
            // Esperando ROOM_LIST del servidor
            Net_Poll();
        }
        else if(g_GameState == STATE_LOBBY)
        {
            Lobby_ProcessInput();
            // Poll ligero: 1 paquete max (para ROOM_INFO response)
            if(online)
            {
                u16 avail = Net_Available(g_Conn);
                if(avail >= 6)
                {
                    u8 hdr[6];
                    u8 pl[255];
                    Net_Recv(g_Conn, hdr, 6);
                    if(hdr[0] == PROTO_MAGIC_0 && hdr[1] == PROTO_MAGIC_1 && hdr[5] > 0)
                    {
                        while(Net_Available(g_Conn) < hdr[5]) Halt();
                        Net_Recv(g_Conn, pl, hdr[5]);
                    }
                    if(hdr[0] == PROTO_MAGIC_0 && hdr[1] == PROTO_MAGIC_1)
                        Net_ProcessPacket(hdr[2], pl, hdr[5]);
                }
            }
        }
        else if(g_GameState == STATE_PLAYING)
        {
            Player_Move();

            if(online)
            {
                Net_SendPosition();
                Net_Poll();
                Entities_DrawSprites();
            }

            if(g_MapDirty)
                Map_DrawViewport();
            HUD_Draw();
            Player_UpdateSprite();
        }
    }

    // Cerrar
    if(g_Conn != NET_INVALID_CONN)
    {
        Net_Close(g_Conn);
        g_Conn = NET_INVALID_CONN;
    }
    Log_Write("[EXIT] Fin");
    Log_Close();

    for(i = 0; i < 8; i++)
        VDP_SetSpriteExUniColor(i, 0, 209, 0, 0);

    // Limpiar buffer teclado antes de volver a DOS
    // KILBUF + forzar PUTPNT = GETPNT
    Bios_MainCall(0x0156);
    *((u16*)0xF3F8) = *((u16*)0xF3FA);
    Bios_Exit(0);
}
