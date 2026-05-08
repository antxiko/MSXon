'use strict';

// =============================================================================
// ghost-service.js — MSXon Ghost Service (v2.0, modular)
//
// Entry point que importa los 5 ghosts modulares y los arranca con stagger.
// La lógica de cada ghost vive en su propio archivo:
//   - ghost-damas.js
//   - ghost-burdyn.js
//   - ghost-tetris.js
//   - ghost-poker.js
//   - ghost-parchis.js
// El plumbing común (socket, parser, ping, reconexión con backoff, cleanup,
// SIGTERM) está en ghost-base.js. El roomId compartido entre ghosts del mismo
// juego se gestiona en ghost-room-registry.js.
//
// Uso:
//   node ghost-service.js [N_BURDYN] [N_TETRIS] [N_PARCHIS]
//
// Servicio systemd:
//   sudo systemctl start msx-ghost
// =============================================================================

const DamasGhost   = require('./ghost-damas');
const BurdynGhost  = require('./ghost-burdyn');
const TetrisGhost  = require('./ghost-tetris');
const PokerGhost   = require('./ghost-poker');
const ParchisGhost = require('./ghost-parchis');

// Global error handlers — último recurso. Las clases ya capturan errores en sus hooks.
process.on('uncaughtException',  (err) => console.error('[FATAL] uncaughtException:',  err && err.stack || err));
process.on('unhandledRejection', (err) => console.error('[FATAL] unhandledRejection:', err && err.stack || err));

const NUM_BURDYN  = parseInt(process.argv[2] || '2', 10);
const NUM_TETRIS  = parseInt(process.argv[3] || '3', 10);
const NUM_PARCHIS = parseInt(process.argv[4] || '3', 10);

console.log('MSXon Ghost Service v2.0 (modular)');
console.log(`Iniciando: 1 damas + ${NUM_BURDYN} burdyn + ${NUM_TETRIS} tetris + ${NUM_PARCHIS} parchis + 1 poker\n`);

// Damas — 1 instancia, host=self, arranca inmediato
new DamasGhost().start();

// Burdyn — host inmediato, joiners staggered
new BurdynGhost(0).start();
for (let i = 1; i < NUM_BURDYN; i++) {
    setTimeout(() => new BurdynGhost(i).start(), 5000 + i * 1500);
}

// Tetris — empieza a los 8s para no saturar el server al boot
setTimeout(() => {
    new TetrisGhost(0).start();
    for (let i = 1; i < NUM_TETRIS; i++) {
        setTimeout(() => new TetrisGhost(i).start(), 3000 + i * 1500);
    }
}, 8000);

// Poker — instancia única, 12s
setTimeout(() => new PokerGhost().start(), 12000);

// Parchis — host a los 15s, joiners staggered
setTimeout(() => {
    new ParchisGhost(0).start();
    for (let i = 1; i < NUM_PARCHIS; i++) {
        setTimeout(() => new ParchisGhost(i).start(), 3000 + i * 1500);
    }
}, 15000);
