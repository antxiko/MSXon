//=============================================================================
// msxgl_config.h — MSX Online · Configuración de MSXgl
//
// MSX2 · Screen 5 · Sprites 16×16 · UNAPI TCP/IP
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

// MSX2: 128K VRAM con direccionamiento 17-bit
#define VDP_VRAM_ADDR				VDP_VRAM_ADDR_17

// Coordenadas 8-bit (suficiente para Screen 5: 256×212)
#define VDP_UNIT					VDP_UNIT_U8

// Solo activamos los modos que usamos
#define VDP_USE_MODE_T1				TRUE	// Screen 0 (para restaurar al salir)
#define VDP_USE_MODE_G1				FALSE
#define VDP_USE_MODE_G2				FALSE
#define VDP_USE_MODE_MC				FALSE
#define VDP_USE_MODE_T2				FALSE
#define VDP_USE_MODE_G3				FALSE
#define VDP_USE_MODE_G4				TRUE	// Screen 5 — nuestro modo principal
#define VDP_USE_MODE_G5				FALSE
#define VDP_USE_MODE_G6				FALSE
#define VDP_USE_MODE_G7				FALSE

#define VDP_USE_VRAM16K				FALSE
#define VDP_USE_SPRITE				TRUE	// Sprites activados
#define VDP_USE_COMMAND				TRUE	// Comandos VDP (FillVRAM, DrawLine, etc.)
#define VDP_USE_CUSTOM_CMD			FALSE
#define VDP_AUTO_INIT				TRUE
#define VDP_USE_UNDOCUMENTED		FALSE
#define VDP_USE_VALIDATOR			TRUE
#define VDP_USE_DEFAULT_PALETTE		FALSE
#define VDP_USE_MSX1_PALETTE		FALSE
#define VDP_USE_DEFAULT_SETTINGS	TRUE
#define VDP_USE_16X16_SPRITE		TRUE	// Sprites 16×16
#define VDP_USE_RESTORE_S0			TRUE
#define VDP_USE_PALETTE16			FALSE

#define VDP_ISR_SAFE_MODE			VDP_ISR_SAFE_DEFAULT

#define VDP_INIT_50HZ				VDP_INIT_DEFAULT

//-----------------------------------------------------------------------------
// INPUT MODULE
//-----------------------------------------------------------------------------

#define INPUT_USE_JOYSTICK			TRUE	// Joystick para mover
#define INPUT_USE_KEYBOARD			TRUE	// Teclado para ESC
#define INPUT_USE_MOUSE				FALSE
#define INPUT_USE_DETECT			FALSE
#define INPUT_USE_ISR_PROTECTION	TRUE
#define INPUT_JOY_UPDATE			FALSE
#define INPUT_HOLD_SIGNAL			FALSE
#define INPUT_KB_UPDATE				TRUE
#define INPUT_KB_UPDATE_MIN			0
#define INPUT_KB_UPDATE_MAX			8

//-----------------------------------------------------------------------------
// MEMORY MODULE
//-----------------------------------------------------------------------------

#define MEM_USE_VALIDATOR			FALSE
#define MEM_USE_FASTCOPY			FALSE
#define MEM_USE_FASTSET				FALSE
#define MEM_USE_DYNAMIC				FALSE	// No usamos malloc
#define MEM_USE_BUILTIN				TRUE

//-----------------------------------------------------------------------------
// PRINT MODULE
//-----------------------------------------------------------------------------

#define PRINT_USE_TEXT				TRUE	// Para restaurar Screen 0 al salir
#define PRINT_USE_BITMAP			TRUE	// Para imprimir texto en Screen 5 (HUD)
#define PRINT_USE_VRAM				FALSE
#define PRINT_USE_SPRITE			FALSE
#define PRINT_USE_FX_SHADOW			FALSE
#define PRINT_USE_FX_OUTLINE		FALSE
#define PRINT_USE_2_PASS_FX			FALSE
#define PRINT_USE_GRAPH				FALSE
#define PRINT_USE_VALIDATOR			FALSE
#define PRINT_USE_UNIT				FALSE
#define PRINT_USE_FORMAT			FALSE
#define PRINT_USE_32B				FALSE
#define PRINT_SKIP_SPACE			TRUE
#define PRINT_COLOR_NUM				12		// Colores por línea en bitmap
#define PRINT_WIDTH					PRINT_WIDTH_8
#define PRINT_HEIGHT				PRINT_HEIGHT_8

//-----------------------------------------------------------------------------
// MSX-DOS MODULE
//-----------------------------------------------------------------------------

#define DOS_USE_FCB					FALSE
#define DOS_USE_HANDLE				FALSE
#define DOS_USE_UTILITIES			FALSE
#define DOS_USE_VALIDATOR			FALSE
#define DOS_USE_ERROR_HANDLER		FALSE
#define DOS_USE_BIOSCALL			TRUE

//-----------------------------------------------------------------------------
// MATH MODULE
//-----------------------------------------------------------------------------

#define RANDOM_8_METHOD				RANDOM_8_REGISTER
#define RANDOM_16_METHOD			RANDOM_16_NONE

//-----------------------------------------------------------------------------
// STRING MODULE
//-----------------------------------------------------------------------------

#define STRING_USE_FROM_INT8		FALSE
#define STRING_USE_FROM_UINT8		TRUE	// Para Print_PrintByte en el HUD
#define STRING_USE_FROM_INT16		FALSE
#define STRING_USE_FROM_UINT16		FALSE
#define STRING_USE_FORMAT			FALSE
#define STRING_USE_INT32			FALSE

//-----------------------------------------------------------------------------
// DEBUG & PROFILE
//-----------------------------------------------------------------------------

#define DEBUG_TOOL					DEBUG_DISABLE
#define PROFILE_TOOL				PROFILE_DISABLE
#define PROFILE_LEVEL				10
