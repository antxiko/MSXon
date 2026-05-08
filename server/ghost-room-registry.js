'use strict';

// =============================================================================
// ghost-room-registry.js — Singleton para coordinar roomId compartido entre
// ghosts del mismo juego. Reemplaza los `let burdynRoomId/tetrisRoomId/parchisRoomId`
// globales que provocaban races cuando el host caía.
// =============================================================================

class GhostRoomRegistry {
    constructor() {
        this.rooms = new Map(); // gameKey -> { roomId, hostName }
    }

    // Reclama una sala para un gameKey. Si ya había una de otro host, la sobrescribe.
    claim(gameKey, roomId, hostName) {
        this.rooms.set(gameKey, { roomId, hostName });
    }

    // Libera la sala SOLO si el host actual coincide. Evita que un ghost que ya no
    // es host borre la entrada de otro.
    release(gameKey, hostName) {
        const cur = this.rooms.get(gameKey);
        if (cur && cur.hostName === hostName) {
            this.rooms.delete(gameKey);
        }
    }

    // Devuelve roomId actual o 0 si no hay.
    get(gameKey) {
        const cur = this.rooms.get(gameKey);
        return cur ? cur.roomId : 0;
    }

    has(gameKey) {
        return this.rooms.has(gameKey);
    }
}

module.exports = new GhostRoomRegistry();
