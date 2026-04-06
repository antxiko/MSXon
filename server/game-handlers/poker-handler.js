'use strict';

// =============================================================================
// poker-handler.js — Texas Hold'em Dealer (server-side)
//
// El servidor es el dealer. Los clientes MSX solo envían acciones
// y reciben cartas/resultados.
// =============================================================================

// Card encoding: bits 0-3 = value (1=A,2-10,11=J,12=Q,13=K), bits 4-5 = suit
function makeCard(val, suit) { return (suit << 4) | val; }
function cardVal(c) { return c & 0x0F; }
function cardSuit(c) { return (c >> 4) & 0x03; }

// Packet types (first byte of STATE_UPDATE payload)
const PKT = {
    HAND_START:     0x01,  // [type, dealerSeat, sbSeat, bbSeat, sbHi, sbLo, bbHi, bbLo, handHi, handLo]
    DEAL_HOLE:      0x02,  // [type, card1, card2] — sent individually per player
    COMMUNITY:      0x03,  // [type, phase, c1, c2, c3, c4, c5]
    ACTION_PROMPT:  0x04,  // [type, seat, betHi, betLo, minRaiseHi, minRaiseLo, potHi, potLo]
    ACTION_RESULT:  0x05,  // [type, seat, action, amtHi, amtLo, chipsHi, chipsLo]
    CHIP_UPDATE:    0x06,  // [type, s0Hi,s0Lo, s1Hi,s1Lo, ...]
    SHOWDOWN:       0x07,  // [type, winnerSeat, handRank, potHi, potLo, card1, card2]
    ELIMINATED:     0x08,  // [type, seat, playersLeft]
    GAME_OVER:      0x09,  // [type, winnerSeat]
};

// Player actions
const ACT_FOLD = 0, ACT_CHECK = 1, ACT_CALL = 2, ACT_RAISE = 3, ACT_ALLIN = 4;

// ── Deck ────────────────────────────────────────────────────────────

function createDeck() {
    const deck = [];
    for (let s = 0; s < 4; s++)
        for (let v = 1; v <= 13; v++)
            deck.push(makeCard(v, s));
    // Fisher-Yates shuffle
    for (let i = 51; i > 0; i--) {
        const j = Math.floor(Math.random() * (i + 1));
        [deck[i], deck[j]] = [deck[j], deck[i]];
    }
    return deck;
}

// ── Hand evaluator ──────────────────────────────────────────────────

function evaluateHand(cards) {
    const valCount = new Array(14).fill(0);
    const suitCount = new Array(4).fill(0);

    for (const c of cards) {
        valCount[cardVal(c)]++;
        suitCount[cardSuit(c)]++;
    }

    // Flush
    let hasFlush = false;
    for (let i = 0; i < 4; i++) if (suitCount[i] >= 5) hasFlush = true;

    // Straight
    let hasStraight = false, straightHigh = 0;
    // A-high: 10,J,Q,K,A
    if (valCount[10] && valCount[11] && valCount[12] && valCount[13] && valCount[1]) {
        hasStraight = true; straightHigh = 14;
    }
    let run = 0;
    for (let i = 1; i <= 13; i++) {
        if (valCount[i]) { run++; if (run >= 5) { hasStraight = true; straightHigh = i; } }
        else run = 0;
    }

    // Groups
    let pairs = 0, trips = 0, quads = 0;
    let pairVal1 = 0, pairVal2 = 0, tripVal = 0, quadVal = 0;
    for (let i = 13; i >= 1; i--) {
        if (valCount[i] === 4) { quads++; quadVal = i; }
        else if (valCount[i] === 3) { trips++; tripVal = i; }
        else if (valCount[i] === 2) { pairs++; if (!pairVal1) pairVal1 = i; else pairVal2 = i; }
    }

    // Highest card (Ace = 14)
    let highCard = 0;
    for (let i = 13; i >= 1; i--) if (valCount[i]) { highCard = i; break; }
    if (valCount[1]) highCard = 14;

    // Rank (1=best, 9=worst)
    let rank, kicker;
    if (hasFlush && hasStraight) { rank = 1; kicker = straightHigh; }
    else if (quads) { rank = 2; kicker = quadVal; }
    else if (trips && pairs) { rank = 3; kicker = tripVal; }
    else if (hasFlush) { rank = 4; kicker = highCard; }
    else if (hasStraight) { rank = 5; kicker = straightHigh; }
    else if (trips) { rank = 6; kicker = tripVal; }
    else if (pairs >= 2) { rank = 7; kicker = pairVal1; }
    else if (pairs === 1) { rank = 8; kicker = pairVal1; }
    else { rank = 9; kicker = highCard; }

    return { rank, kicker };
}

// ── Helper: send packet to room ─────────────────────────────────────

function sendToAll(room, type, data) {
    const payload = Buffer.from([type, ...data]);
    room.api.serverBroadcast(room, 0x40, payload); // CMD_STATE_UPDATE = 0x40
}

function sendToOne(room, pid, type, data) {
    const payload = Buffer.from([type, ...data]);
    const packet = room.api.buildPacket(0x40, room.id, 0, payload);
    room.api.sendToPlayer(room, pid, packet);
}

// ── Dealer logic ────────────────────────────────────────────────────

function nextActiveSeat(gs, from) {
    let next = (from + 1) % gs.maxSeats;
    let tries = 0;
    while (!gs.seats[next] && tries < gs.maxSeats) {
        next = (next + 1) % gs.maxSeats;
        tries++;
    }
    return next;
}

function nextInHandSeat(gs, from) {
    let next = (from + 1) % gs.maxSeats;
    let tries = 0;
    while (tries < gs.maxSeats) {
        if (gs.seats[next] && gs.seats[next].inHand && !gs.seats[next].allIn) return next;
        next = (next + 1) % gs.maxSeats;
        tries++;
    }
    return -1;
}

function countInHand(gs) {
    let c = 0;
    for (let i = 0; i < gs.maxSeats; i++) if (gs.seats[i] && gs.seats[i].inHand) c++;
    return c;
}

function countCanAct(gs) {
    let c = 0;
    for (let i = 0; i < gs.maxSeats; i++)
        if (gs.seats[i] && gs.seats[i].inHand && !gs.seats[i].allIn) c++;
    return c;
}

function startNewHand(room) {
    const gs = room.gameState;
    gs.handNum++;
    gs.deck = createDeck();
    gs.deckIdx = 0;
    gs.community = [];
    gs.pot = 0;
    gs.currentBet = 0;
    gs.phase = 'preflop';
    gs.actionsThisRound = 0;

    // Reset seats
    for (let i = 0; i < gs.maxSeats; i++) {
        if (!gs.seats[i]) continue;
        gs.seats[i].cards = [0, 0];
        gs.seats[i].currentBet = 0;
        gs.seats[i].allIn = false;
        gs.seats[i].folded = false;
        gs.seats[i].inHand = gs.seats[i].chips > 0;
    }

    // Need at least 2 active players
    if (countInHand(gs) < 2) {
        console.log(`[POKER] Sala ${room.id}: no hay suficientes jugadores`);
        gs.phase = 'waiting';
        return;
    }

    // Dealer rotation
    gs.dealerSeat = nextActiveSeat(gs, gs.dealerSeat);
    let sbSeat = nextActiveSeat(gs, gs.dealerSeat);
    let bbSeat = nextActiveSeat(gs, sbSeat);

    if (!gs.seats[gs.dealerSeat] || !gs.seats[sbSeat] || !gs.seats[bbSeat]) {
        console.log(`[POKER] Sala ${room.id}: seats invalidos`);
        gs.phase = 'waiting';
        return;
    }

    // Heads-up: dealer = SB
    if (countInHand(gs) === 2) {
        sbSeat = gs.dealerSeat;
        bbSeat = nextActiveSeat(gs, gs.dealerSeat);
    }

    // Post blinds
    const sb = Math.min(gs.sb, gs.seats[sbSeat].chips);
    gs.seats[sbSeat].chips -= sb;
    gs.seats[sbSeat].currentBet = sb;
    gs.pot += sb;

    const bb = Math.min(gs.bb, gs.seats[bbSeat].chips);
    gs.seats[bbSeat].chips -= bb;
    gs.seats[bbSeat].currentBet = bb;
    gs.pot += bb;
    gs.currentBet = bb;

    // Send HAND_START
    sendToAll(room, PKT.HAND_START, [
        gs.dealerSeat, sbSeat, bbSeat,
        (gs.sb >> 8) & 0xFF, gs.sb & 0xFF,
        (gs.bb >> 8) & 0xFF, gs.bb & 0xFF,
        (gs.handNum >> 8) & 0xFF, gs.handNum & 0xFF,
    ]);

    // Deal hole cards — PRIVATE per player
    for (let i = 0; i < gs.maxSeats; i++) {
        if (!gs.seats[i] || !gs.seats[i].inHand) continue;
        gs.seats[i].cards[0] = gs.deck[gs.deckIdx++];
        gs.seats[i].cards[1] = gs.deck[gs.deckIdx++];
        sendToOne(room, i + 1, PKT.DEAL_HOLE, [gs.seats[i].cards[0], gs.seats[i].cards[1]]);
    }

    // Send chip update
    sendChipUpdate(room);

    // First to act: after BB
    gs.actionSeat = nextInHandSeat(gs, bbSeat);
    gs.lastRaiser = gs.actionSeat;
    gs.actionsThisRound = 0;

    // Send action prompt
    setTimeout(() => sendActionPrompt(room), 500);
}

function sendChipUpdate(room) {
    const gs = room.gameState;
    const data = [];
    for (let i = 0; i < gs.maxSeats; i++) {
        const chips = gs.seats[i] ? gs.seats[i].chips : 0;
        data.push((chips >> 8) & 0xFF, chips & 0xFF);
    }
    sendToAll(room, PKT.CHIP_UPDATE, data);
}

function sendActionPrompt(room) {
    const gs = room.gameState;
    if (gs.actionSeat < 0) return;
    const minRaise = gs.bb;
    sendToAll(room, PKT.ACTION_PROMPT, [
        gs.actionSeat,
        (gs.currentBet >> 8) & 0xFF, gs.currentBet & 0xFF,
        (minRaise >> 8) & 0xFF, minRaise & 0xFF,
        (gs.pot >> 8) & 0xFF, gs.pot & 0xFF,
    ]);

    // 30 second timeout: auto-fold/check
    if (gs.actionTimeout) clearTimeout(gs.actionTimeout);
    gs.actionTimeout = setTimeout(() => {
        const seat = gs.seats[gs.actionSeat];
        if (!seat) return;
        const autoAction = (gs.currentBet <= seat.currentBet) ? ACT_CHECK : ACT_FOLD;
        processAction(room, gs.actionSeat + 1, autoAction, 0);
    }, 30000);
}

function processAction(room, senderPid, action, amount) {
    const gs = room.gameState;
    const seat = senderPid - 1;
    if (seat !== gs.actionSeat) return; // wrong player
    if (!gs.seats[seat] || !gs.seats[seat].inHand) return;

    if (gs.actionTimeout) { clearTimeout(gs.actionTimeout); gs.actionTimeout = null; }

    const s = gs.seats[seat];

    switch (action) {
        case ACT_FOLD:
            s.inHand = false;
            s.folded = true;
            break;
        case ACT_CHECK:
            break;
        case ACT_CALL: {
            const callAmt = Math.min(gs.currentBet - s.currentBet, s.chips);
            s.chips -= callAmt;
            s.currentBet += callAmt;
            gs.pot += callAmt;
            if (s.chips === 0) s.allIn = true;
            break;
        }
        case ACT_RAISE: {
            const callFirst = gs.currentBet - s.currentBet;
            let total = callFirst + amount;
            if (total >= s.chips) { total = s.chips; s.allIn = true; }
            s.chips -= total;
            s.currentBet += total;
            gs.pot += total;
            gs.currentBet = s.currentBet;
            gs.lastRaiser = seat;
            break;
        }
        case ACT_ALLIN: {
            const allInAmt = s.chips;
            s.currentBet += allInAmt;
            gs.pot += allInAmt;
            s.chips = 0;
            s.allIn = true;
            if (s.currentBet > gs.currentBet) {
                gs.currentBet = s.currentBet;
                gs.lastRaiser = seat;
            }
            break;
        }
    }

    gs.actionsThisRound++;

    // Send action result to all
    sendToAll(room, PKT.ACTION_RESULT, [
        seat, action,
        (amount >> 8) & 0xFF, amount & 0xFF,
        (s.chips >> 8) & 0xFF, s.chips & 0xFF,
    ]);

    advanceAction(room);
}

function advanceAction(room) {
    const gs = room.gameState;

    // Solo 1 jugador en mano → gana bote
    if (countInHand(gs) <= 1) {
        for (let i = 0; i < gs.maxSeats; i++) {
            if (gs.seats[i] && gs.seats[i].inHand) {
                gs.seats[i].chips += gs.pot;
                sendToAll(room, PKT.SHOWDOWN, [i, 0, (gs.pot >> 8) & 0xFF, gs.pot & 0xFF, 0, 0]);
                gs.pot = 0;
                break;
            }
        }
        sendChipUpdate(room);
        checkEliminated(room);
        setTimeout(() => { if (countActive(gs) >= 2) startNewHand(room); else endGame(room); }, 3000);
        return;
    }

    // Nadie puede actuar (todos all-in)
    if (countCanAct(gs) === 0) {
        // Deal remaining community + showdown
        while (gs.community.length < 5) gs.community.push(gs.deck[gs.deckIdx++]);
        sendToAll(room, PKT.COMMUNITY, [3, ...gs.community]);
        setTimeout(() => doShowdown(room), 1000);
        return;
    }

    // All bets matched and everyone acted?
    let allMatched = true;
    for (let i = 0; i < gs.maxSeats; i++) {
        if (!gs.seats[i] || !gs.seats[i].inHand || gs.seats[i].allIn) continue;
        if (gs.seats[i].currentBet < gs.currentBet) { allMatched = false; break; }
    }

    if (gs.actionsThisRound >= countCanAct(gs) && allMatched) {
        // Advance phase
        advancePhase(room);
        return;
    }

    // Next player
    gs.actionSeat = nextInHandSeat(gs, gs.actionSeat);
    sendActionPrompt(room);
}

function advancePhase(room) {
    const gs = room.gameState;

    // Reset round bets
    for (let i = 0; i < gs.maxSeats; i++) {
        if (gs.seats[i]) gs.seats[i].currentBet = 0;
    }
    gs.currentBet = 0;
    gs.actionsThisRound = 0;

    if (gs.phase === 'preflop') {
        // Flop
        gs.phase = 'flop';
        gs.community.push(gs.deck[gs.deckIdx++], gs.deck[gs.deckIdx++], gs.deck[gs.deckIdx++]);
        sendToAll(room, PKT.COMMUNITY, [1, ...padCommunity(gs.community)]);
    } else if (gs.phase === 'flop') {
        // Turn
        gs.phase = 'turn';
        gs.community.push(gs.deck[gs.deckIdx++]);
        sendToAll(room, PKT.COMMUNITY, [2, ...padCommunity(gs.community)]);
    } else if (gs.phase === 'turn') {
        // River
        gs.phase = 'river';
        gs.community.push(gs.deck[gs.deckIdx++]);
        sendToAll(room, PKT.COMMUNITY, [3, ...padCommunity(gs.community)]);
    } else if (gs.phase === 'river') {
        // Showdown
        doShowdown(room);
        return;
    }

    // First to act post-flop: after dealer
    gs.actionSeat = nextInHandSeat(gs, gs.dealerSeat);
    gs.lastRaiser = gs.actionSeat;

    setTimeout(() => sendActionPrompt(room), 500);
}

function padCommunity(comm) {
    const r = [...comm];
    while (r.length < 5) r.push(0);
    return r;
}

function doShowdown(room) {
    const gs = room.gameState;
    let bestSeat = -1, bestRank = 99, bestKicker = 0;

    for (let i = 0; i < gs.maxSeats; i++) {
        if (!gs.seats[i] || !gs.seats[i].inHand) continue;
        const allCards = [...gs.seats[i].cards, ...gs.community];
        const { rank, kicker } = evaluateHand(allCards);
        if (rank < bestRank || (rank === bestRank && kicker > bestKicker)) {
            bestRank = rank;
            bestKicker = kicker;
            bestSeat = i;
        }
    }

    if (bestSeat >= 0) {
        gs.seats[bestSeat].chips += gs.pot;
        sendToAll(room, PKT.SHOWDOWN, [
            bestSeat, bestRank,
            (gs.pot >> 8) & 0xFF, gs.pot & 0xFF,
            gs.seats[bestSeat].cards[0], gs.seats[bestSeat].cards[1],
        ]);
        gs.pot = 0;
    }

    sendChipUpdate(room);
    checkEliminated(room);

    setTimeout(() => {
        if (countActive(gs) >= 2) startNewHand(room);
        else endGame(room);
    }, 5000);
}

function countActive(gs) {
    let c = 0;
    for (let i = 0; i < gs.maxSeats; i++)
        if (gs.seats[i] && gs.seats[i].chips > 0) c++;
    return c;
}

function checkEliminated(room) {
    const gs = room.gameState;
    let alive = 0;
    for (let i = 0; i < gs.maxSeats; i++) {
        if (!gs.seats[i]) continue;
        if (gs.seats[i].chips <= 0) {
            sendToAll(room, PKT.ELIMINATED, [i, countActive(gs)]);
        } else {
            alive++;
        }
    }
}

function endGame(room) {
    const gs = room.gameState;
    for (let i = 0; i < gs.maxSeats; i++) {
        if (gs.seats[i] && gs.seats[i].chips > 0) {
            sendToAll(room, PKT.GAME_OVER, [i]);
            break;
        }
    }
    gs.phase = 'ended';
}

// ── Handler exports ─────────────────────────────────────────────────

module.exports = {
    onRoomCreated(room, api) {
        room.api = api;
        room.gameState = {
            maxSeats: room.maxPlayers,
            deck: [], deckIdx: 0,
            community: [], pot: 0,
            seats: new Array(room.maxPlayers).fill(null),
            dealerSeat: 0, phase: 'waiting',
            actionSeat: -1, currentBet: 0,
            lastRaiser: -1, actionsThisRound: 0,
            handNum: 0, sb: 10, bb: 20,
            actionTimeout: null,
        };
        console.log(`[POKER] Handler creado para sala ${room.id}`);
    },

    onPlayerJoined(room, pid) {
        const gs = room.gameState;
        gs.seats[pid - 1] = {
            chips: 1000, cards: [0, 0], inHand: false,
            currentBet: 0, allIn: false, folded: false,
        };
        console.log(`[POKER] Sala ${room.id}: P${pid} se sienta (${gs.seats.filter(s => s).length} jugadores)`);
    },

    onGameStart(room) {
        console.log(`[POKER] Sala ${room.id}: partida iniciada`);
        setTimeout(() => startNewHand(room), 1000);
    },

    onStateUpdate(room, senderPid, payload) {
        // Player action: [action, amountHi, amountLo]
        if (payload.length >= 3) {
            const action = payload[0];
            const amount = (payload[1] << 8) | payload[2];
            processAction(room, senderPid, action, amount);
        }
        return true; // consumido, no reenviar como relay
    },

    onPlayerLeft(room, pid) {
        const gs = room.gameState;
        if (gs.seats[pid - 1]) {
            gs.seats[pid - 1].inHand = false;
            gs.seats[pid - 1].chips = 0;
            console.log(`[POKER] Sala ${room.id}: P${pid} se va`);
        }
        if (gs.actionTimeout) clearTimeout(gs.actionTimeout);
    },
};
