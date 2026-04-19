const http = require('http');
const fs = require('fs');
const path = require('path');

const PORT = 8080;
const BASE = __dirname;

http.createServer((req, res) => {
    const url = decodeURIComponent(req.url).replace(/^\//, '');
    const file = path.join(BASE, url);

    if (!url || url === '/') {
        // Listar archivos de bin/ y dsk/
        let html = '<h1>MSX Builds</h1><h2>bin/</h2><ul>';
        try {
            fs.readdirSync(path.join(BASE, 'bin')).forEach(f => {
                html += `<li><a href="/bin/${f}">${f}</a> (${fs.statSync(path.join(BASE, 'bin', f)).size} bytes)</li>`;
            });
        } catch(e) {}
        html += '</ul><h2>dsk/</h2><ul>';
        try {
            fs.readdirSync(path.join(BASE, 'dsk')).forEach(f => {
                html += `<li><a href="/dsk/${f}">${f}</a> (${fs.statSync(path.join(BASE, 'dsk', f)).size} bytes)</li>`;
            });
        } catch(e) {}
        html += '</ul>';
        res.writeHead(200, {'Content-Type': 'text/html'});
        res.end(html);
        return;
    }

    if (!fs.existsSync(file)) {
        res.writeHead(404);
        res.end('Not found');
        return;
    }

    const data = fs.readFileSync(file);
    res.writeHead(200, {
        'Content-Type': 'application/octet-stream',
        'Content-Length': data.length
    });
    res.end(data);
}).listen(PORT, '0.0.0.0', () => {
    console.log(`Servidor de builds en http://localhost:${PORT}`);
    console.log(`Directorio: ${BASE}`);
});
