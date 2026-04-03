//=============================================================================
// msxgl_config.h — Burdyn · RPG Crawler MMO
//
// MSX2 · MSX-DOS 2 · Screen 4 (Graphic 3) · Sprites 16×16
//=============================================================================
#pragma once

//-----------------------------------------------------------------------------
// BIOS MODULE
//-----------------------------------------------------------------------------

#define BIOS_CALL_MAINROM			BIOS_CALL_DIRECT
#define BIOS_CALL_SUBROM			BIOS_CALL_INTERSLOT
#define BIOS_CALL_DISKROM			BIOS_CALL_INTERSLOT

#define BIOS_USE_MAINROM			TRUE
#define BIOS_USE_VDP				TRUE
#define BIOS_USE_PSG				FALSE
#define BIOS_USE_SUBROM				TRUE
#define BIOS_USE_DISKROM			TRUE

//-----------------------------------------------------------------------------
// VDP MODULE
//-----------------------------------------------------------------------------

#define VDP_VRAM_ADDR				VDP_VRAM_ADDR_17

#define VDP_UNIT					VDP_UNIT_U8

// Screen 4 = Graphic 3 (G3) — tiles + Sprite Mode 2
#define VDP_USE_MODE_T1				TRUE	// Screen 0 (para restaurar al salir)
#define VDP_USE_MODE_G1				FALSE
#define VDP_USE_MODE_G2				FALSE
#define VDP_USE_MODE_MC				FALSE
#define VDP_USE_MODE_T2				FALSE
#define VDP_USE_MODE_G3				TRUE	// Screen 4 — nuestro modo principal
#define VDP_USE_MODE_G4				FALSE
#define VDP_USE_MODE_G5				FALSE
#define VDP_USE_MODE_G6				FALSE
#define VDP_USE_MODE_G7				FALSE

#define VDP_USE_VRAM16K				FALSE
#define VDP_USE_SPRITE				TRUE	// Sprites Mode 2 (16×16, multicolor)
#define VDP_USE_COMMAND				TRUE	// Comandos VDP
#define VDP_USE_CUSTOM_CMD			FALSE
#define VDP_AUTO_INIT				TRUE
#define VDP_USE_UNDOCUMENTED		FALSE
#define VDP_USE_VALIDATOR			TRUE

//-----------------------------------------------------------------------------
// PRINT MODULE
//-----------------------------------------------------------------------------

#define PRINT_USE_TEXT				TRUE
#define PRINT_USE_BITMAP			FALSE	// No usamos bitmap (es Screen 4, tiles)
#define PRINT_USE_VRAM				FALSE
#define PRINT_USE_SPRITE			FALSE
#define PRINT_USE_FX_SHADOW			FALSE
#define PRINT_USE_FX_OUTLINE		FALSE
#define PRINT_USE_GRAPH				FALSE
#define PRINT_USE_VALIDATOR			TRUE
#define PRINT_WIDTH					32
#define PRINT_HEIGHT				24
#define PRINT_COLOR_NUM				16
#define PRINT_USE_UNIT				FALSE

//-----------------------------------------------------------------------------
// INPUT MODULE
//-----------------------------------------------------------------------------

#define INPUT_USE_KEYBOARD			TRUE
#define INPUT_USE_JOYSTICK			TRUE
#define INPUT_USE_MANAGER			FALSE
#define INPUT_JOY_UPDATE			FALSE
#define INPUT_KB_UPDATE				TRUE
#define INPUT_KB_UPDATE_MIN			0
#define INPUT_KB_UPDATE_MAX			8

//-----------------------------------------------------------------------------
// MEMORY MODULE
//-----------------------------------------------------------------------------

#define MEMORY_USE_VALIDATOR		FALSE

//-----------------------------------------------------------------------------
// SYSTEM MODULE
//-----------------------------------------------------------------------------

#define SYSTEM_USE_MSX_VERSION		TRUE
#define SYSTEM_USE_SLOT				TRUE

//-----------------------------------------------------------------------------
// DOS MODULE
//-----------------------------------------------------------------------------

#define DOS_USE_FCB					FALSE
#define DOS_USE_HANDLE				TRUE
#define DOS_USE_UTILITIES			TRUE
#define DOS_USE_VALIDATOR			FALSE
#define DOS_USE_ERROR_HANDLER		FALSE
#define DOS_USE_BIOSCALL			TRUE
