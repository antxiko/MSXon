// Parchis — datos del tablero (auto-generado desde path_editor.html)
// NO EDITAR A MANO

#define PATH_LENGTH 68

const u8 g_PathX[68] = { 17,17,17,17,17,17,17,17,17,19,20,21,22,23,24,25,25,25,24,23,22,21,20,19,17,17,17,17,17,17,17,17,17,15,13,13,13,13,13,13,13,14,13,12,11,10,9,8,7,6,6,6,7,8,9,10,11,12,13,13,13,13,13,13,13,13,13,15 };
const u8 g_PathY[68] = { 21,20,19,18,17,16,15,14,13,13,13,13,13,13,13,13,11,9,9,9,9,9,9,9,10,9,8,7,6,5,4,3,2,2,2,3,4,5,6,7,8,9,9,9,9,9,9,9,9,9,11,13,13,13,13,13,13,13,13,14,15,16,17,18,19,20,21,21 };
const u8 g_PathDir[68] = { 0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0 };

// Casillas seguras (coordenadas tile)
#define SAFE_COUNT 12
const u8 g_SafeX[12] = { 21,25,21,17,15,13,10,10,6,13,15,17 };
const u8 g_SafeY[12] = { 13,11,9,6,2,6,13,9,11,17,21,17 };

// Casilla de salida en el recorrido (1-68, donde aparece al sacar 5)
#define EXIT_Y  5
#define EXIT_B  22
#define EXIT_R  39
#define EXIT_G  56

// Ultima casilla del recorrido antes de entrar al pasillo
#define ENTER_PASS_Y  68
#define ENTER_PASS_B  17
#define ENTER_PASS_R  34
#define ENTER_PASS_G  51

// Pasillo (8 casillas cada uno, la ultima es la meta)
#define PASSAGE_LENGTH 8

const u8 g_PassRX[8] = { 15,15,15,15,15,15,15,15 };
const u8 g_PassRY[8] = { 3,4,5,6,7,8,9,10 };
const u8 g_PassRDir[8] = { 0,0,0,0,0,0,0,0 };

const u8 g_PassGX[8] = { 7,8,9,10,11,12,13,14 };
const u8 g_PassGY[8] = { 11,11,11,11,11,11,11,11 };
const u8 g_PassGDir[8] = { 1,1,1,1,1,1,1,1 };

const u8 g_PassYX[8] = { 15,15,15,15,15,15,15,15 };
const u8 g_PassYY[8] = { 20,19,18,17,16,15,14,13 };
const u8 g_PassYDir[8] = { 0,0,0,0,0,0,0,0 };

const u8 g_PassBX[8] = { 24,23,22,21,20,19,18,17 };
const u8 g_PassBY[8] = { 11,11,11,11,11,11,11,11 };
const u8 g_PassBDir[8] = { 1,1,1,1,1,1,1,1 };

// Casa (4 fichas cada uno)
const u8 g_HomeRX[4] = { 8,9,8,9 };
const u8 g_HomeRY[4] = { 4,4,5,5 };

const u8 g_HomeGX[4] = { 8,9,8,9 };
const u8 g_HomeGY[4] = { 18,18,19,19 };

const u8 g_HomeYX[4] = { 22,23,22,23 };
const u8 g_HomeYY[4] = { 18,18,19,19 };

const u8 g_HomeBX[4] = { 22,23,22,23 };
const u8 g_HomeBY[4] = { 4,4,5,5 };
