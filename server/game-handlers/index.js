'use strict';

const pokerHandler = require('./poker-handler');
const frogfliesHandler = require('./frogflies-handler');
const bombermanHandler = require('./bomberman-handler');
// const amongHandler = require('./among-handler'); // futuro

const handlers = new Map();
handlers.set(0x05, pokerHandler);       // Texas Hold'em
handlers.set(0x08, bombermanHandler);   // Bomberman
handlers.set(0x69, frogfliesHandler);   // Frog & Flies
// handlers.set(0x07, amongHandler);    // Among MSX (futuro)

module.exports = handlers;
