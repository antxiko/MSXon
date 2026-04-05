const fs = require('fs');
const zlib = require('zlib');

const data = fs.readFileSync('tileset_current.png');

// Parse PNG chunks
let pos = 8;
let width, height, bitDepth, colorType;
let palette = [];
let idatChunks = [];

while(pos < data.length) {
    const len = data.readUInt32BE(pos);
    const type = data.toString('ascii', pos+4, pos+8);
    const chunkData = data.slice(pos+8, pos+8+len);

    if(type === 'IHDR') {
        width = chunkData.readUInt32BE(0);
        height = chunkData.readUInt32BE(4);
        bitDepth = chunkData[8];
        colorType = chunkData[9];
        console.log('PNG: ' + width + 'x' + height + ' bitDepth=' + bitDepth + ' colorType=' + colorType);
    } else if(type === 'PLTE') {
        for(let i = 0; i < chunkData.length; i += 3)
            palette.push([chunkData[i], chunkData[i+1], chunkData[i+2]]);
    } else if(type === 'IDAT') {
        idatChunks.push(chunkData);
    }
    pos += 12 + len;
}

const compressed = Buffer.concat(idatChunks);
const raw = zlib.inflateSync(compressed);

// MSX palette
const msxPal = [
    [0,0,0],[0,0,0],[33,200,66],[94,220,120],
    [84,85,237],[125,118,252],[212,82,77],[66,235,245],
    [252,85,84],[255,121,120],[212,193,84],[230,206,128],
    [33,176,59],[201,91,186],[204,204,204],[255,255,255]
];

function matchMSX(r, g, b) {
    let best = 0, bestD = 999999;
    for(let i = 0; i < 16; i++) {
        const dr = r-msxPal[i][0], dg = g-msxPal[i][1], db = b-msxPal[i][2];
        const d = dr*dr + dg*dg + db*db;
        if(d < bestD) { bestD = d; best = i; }
    }
    return best;
}

// Calculate bytes per pixel and stride
let bpp;
if(colorType === 3) bpp = 1;
else if(colorType === 2) bpp = 3;
else if(colorType === 6) bpp = 4;
else bpp = 1;

let pixelBytes;
if(colorType === 3 && bitDepth < 8) pixelBytes = Math.ceil(width * bitDepth / 8);
else if(colorType === 3) pixelBytes = width;
else if(colorType === 2) pixelBytes = width * 3;
else if(colorType === 6) pixelBytes = width * 4;
else pixelBytes = width;

const scanline = 1 + pixelBytes;

// Reconstruct PNG filters
const recon = Buffer.alloc(raw.length);
for(let y = 0; y < height; y++) {
    const rowOff = y * scanline;
    const filter = raw[rowOff];
    recon[rowOff] = 0;

    for(let x = 0; x < pixelBytes; x++) {
        const val = raw[rowOff + 1 + x];
        const a = (x >= bpp) ? recon[rowOff + 1 + x - bpp] : 0;
        const b2 = (y > 0) ? recon[(y-1) * scanline + 1 + x] : 0;
        const c2 = (x >= bpp && y > 0) ? recon[(y-1) * scanline + 1 + x - bpp] : 0;

        switch(filter) {
            case 0: recon[rowOff + 1 + x] = val; break;
            case 1: recon[rowOff + 1 + x] = (val + a) & 0xFF; break;
            case 2: recon[rowOff + 1 + x] = (val + b2) & 0xFF; break;
            case 3: recon[rowOff + 1 + x] = (val + Math.floor((a + b2) / 2)) & 0xFF; break;
            case 4: {
                const p = a + b2 - c2;
                const pa = Math.abs(p - a), pb = Math.abs(p - b2), pc = Math.abs(p - c2);
                const pr = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b2 : c2);
                recon[rowOff + 1 + x] = (val + pr) & 0xFF;
                break;
            }
        }
    }
}

function getPixel(x, y) {
    const rowOff = y * scanline + 1;
    if(colorType === 3) {
        let idx;
        if(bitDepth === 8) idx = recon[rowOff + x];
        else if(bitDepth === 4) {
            const b = recon[rowOff + Math.floor(x/2)];
            idx = (x % 2 === 0) ? (b >> 4) : (b & 0xF);
        } else if(bitDepth === 2) {
            const b = recon[rowOff + Math.floor(x/4)];
            const shift = 6 - (x % 4) * 2;
            idx = (b >> shift) & 0x3;
        } else {
            const b = recon[rowOff + Math.floor(x/8)];
            idx = (b >> (7 - x % 8)) & 1;
        }
        const c = palette[idx] || [0,0,0];
        return matchMSX(c[0], c[1], c[2]);
    } else if(colorType === 2) {
        return matchMSX(recon[rowOff+x*3], recon[rowOff+x*3+1], recon[rowOff+x*3+2]);
    } else if(colorType === 6) {
        return matchMSX(recon[rowOff+x*4], recon[rowOff+x*4+1], recon[rowOff+x*4+2]);
    }
    return 0;
}

// Convert 256 tiles
const patterns = new Uint8Array(2048);
const colors = new Uint8Array(2048);

for(let t = 0; t < 256; t++) {
    const tx = (t % 32) * 8;
    const ty = Math.floor(t / 32) * 8;

    for(let row = 0; row < 8; row++) {
        const rowPixels = [];
        for(let bit = 0; bit < 8; bit++)
            rowPixels.push(getPixel(tx + bit, ty + row));

        const unique = [...new Set(rowPixels)];
        let fg, bg;
        if(unique.length <= 1) { fg = unique[0] || 0; bg = fg; }
        else { fg = unique[0]; bg = unique[1]; }

        let pat = 0;
        for(let bit = 0; bit < 8; bit++) {
            if(rowPixels[bit] === fg) pat |= (1 << (7 - bit));
        }

        patterns[t * 8 + row] = pat;
        colors[t * 8 + row] = (fg << 4) | bg;
    }
}

// Verify
console.log('Tile 0:', Array.from(patterns.slice(0,8)).map(v => '0x'+v.toString(16).padStart(2,'0')));
console.log('Tile 5:', Array.from(patterns.slice(40,48)).map(v => '0x'+v.toString(16).padStart(2,'0')));
console.log('Tile 52 (A):', Array.from(patterns.slice(416,424)).map(v => '0x'+v.toString(16).padStart(2,'0')));

// Write tileset_data.h
let out = '// Auto-generated from tileset_current.png\n#define TILE_COUNT 256\n\n';
out += 'const u8 g_TilePatterns[2048] = {\n';
for(let t = 0; t < 256; t++) {
    const hex = [];
    for(let i = 0; i < 8; i++) hex.push('0x' + patterns[t*8+i].toString(16).toUpperCase().padStart(2, '0'));
    out += '    ' + hex.join(',') + ', // Tile ' + t + '\n';
}
out += '};\n\n';
out += 'const u8 g_TileColors[2048] = {\n';
for(let t = 0; t < 256; t++) {
    const hex = [];
    for(let i = 0; i < 8; i++) hex.push('0x' + colors[t*8+i].toString(16).toUpperCase().padStart(2, '0'));
    out += '    ' + hex.join(',') + ', // Tile ' + t + '\n';
}
out += '};\n';

fs.writeFileSync('../../MSXgl/projects/texas/tileset_data.h', out);
console.log('tileset_data.h actualizado! (' + out.length + ' chars)');
