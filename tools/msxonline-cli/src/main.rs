use crossterm::{
    event::{self, Event, KeyCode, KeyEventKind},
    terminal::{disable_raw_mode, enable_raw_mode, EnterAlternateScreen, LeaveAlternateScreen},
    ExecutableCommand,
};
use ratatui::{
    backend::CrosstermBackend,
    layout::{Constraint, Direction, Layout, Rect},
    style::{Color, Modifier, Style},
    text::{Line, Span},
    widgets::{Block, Borders, List, ListItem, Paragraph, Tabs},
    Frame, Terminal,
};
use std::io::{self, stdout, Read, Write};
use std::net::TcpStream;
use std::time::{Duration, Instant};

const SERVER_IP: &str = "217.154.107.144";
const SERVER_PORT: u16 = 9876;
const AUTH_TOKEN: [u8; 4] = [0xDE, 0xAD, 0xBE, 0xEF];

const CMD_PING: u8 = 0x01;
const CMD_PONG: u8 = 0x02;
const CMD_AUTH: u8 = 0x10;
const CMD_AUTH_OK: u8 = 0x11;
const CMD_ROOM_LIST: u8 = 0x26;

fn game_name(id: u8) -> &'static str {
    match id {
        0x01 => "Ball Demo",
        0x02 => "Damas",
        0x03 => "Burdyn",
        _ => "???",
    }
}

fn build_packet(cmd: u8, room: u8, pid: u8, payload: &[u8]) -> Vec<u8> {
    let mut pkt = vec![0x46, 0x4D, cmd, room, pid, payload.len() as u8];
    pkt.extend_from_slice(payload);
    pkt
}

#[derive(Clone)]
struct RoomInfo {
    id: u8,
    game_id: u8,
    players: u8,
}

struct App {
    tab: usize,
    rooms: Vec<RoomInfo>,
    logs: Vec<String>,
    connected: bool,
    last_ping: Option<u64>,
    last_refresh: Instant,
    stream: Option<TcpStream>,
    selected_room: usize,
}

impl App {
    fn new() -> Self {
        Self {
            tab: 0,
            rooms: vec![],
            logs: vec![],
            connected: false,
            last_ping: None,
            last_refresh: Instant::now(),
            stream: None,
            selected_room: 0,
        }
    }

    fn log(&mut self, msg: String) {
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap_or_default()
            .as_secs();
        let h = (now / 3600) % 24;
        let m = (now / 60) % 60;
        let s = now % 60;
        self.logs.push(format!("[{:02}:{:02}:{:02}] {}", h, m, s, msg));
        if self.logs.len() > 200 { self.logs.remove(0); }
    }

    fn connect(&mut self) {
        self.log("Conectando...".into());
        match TcpStream::connect_timeout(
            &format!("{}:{}", SERVER_IP, SERVER_PORT).parse().unwrap(),
            Duration::from_secs(5),
        ) {
            Ok(mut s) => {
                s.write_all(&build_packet(CMD_AUTH, 0, 0, &AUTH_TOKEN)).ok();
                s.set_read_timeout(Some(Duration::from_secs(3))).ok();
                let mut buf = [0u8; 64];
                match s.read(&mut buf) {
                    Ok(n) if n >= 6 && buf[2] == CMD_AUTH_OK => {
                        self.connected = true;
                        self.log("Conectado y autenticado".into());
                        s.set_nonblocking(true).ok();
                        self.stream = Some(s);
                    }
                    _ => {
                        self.log("Auth fallida".into());
                    }
                }
            }
            Err(e) => self.log(format!("Error: {}", e)),
        }
    }

    fn disconnect(&mut self) {
        self.stream = None;
        self.connected = false;
        self.rooms.clear();
        self.log("Desconectado".into());
    }

    fn refresh_rooms(&mut self) {
        let mut msg = String::new();
        let mut new_rooms = vec![];

        if let Some(ref mut s) = self.stream {
            s.write_all(&build_packet(CMD_ROOM_LIST, 0, 0, &[])).ok();
            s.set_read_timeout(Some(Duration::from_secs(2))).ok();
            s.set_nonblocking(false).ok();
            let mut buf = [0u8; 512];
            match s.read(&mut buf) {
                Ok(n) if n >= 7 && buf[2] == CMD_ROOM_LIST => {
                    let count = buf[6] as usize;
                    for i in 0..count {
                        let off = 7 + i * 3;
                        if off + 2 < n {
                            new_rooms.push(RoomInfo {
                                id: buf[off],
                                game_id: buf[off + 1],
                                players: buf[off + 2],
                            });
                        }
                    }
                    msg = format!("{} sala(s)", new_rooms.len());
                }
                _ => { msg = "Sin respuesta".into(); }
            }
            s.set_nonblocking(true).ok();
        }

        self.rooms = new_rooms;
        self.last_refresh = Instant::now();
        if !msg.is_empty() { self.log(msg); }
    }

    fn ping(&mut self) {
        let mut msg = String::new();
        let mut ping_val: Option<u64> = None;

        if let Some(ref mut s) = self.stream {
            let start = Instant::now();
            s.write_all(&build_packet(CMD_PING, 0, 0, &[])).ok();
            s.set_read_timeout(Some(Duration::from_secs(3))).ok();
            s.set_nonblocking(false).ok();
            let mut buf = [0u8; 64];
            match s.read(&mut buf) {
                Ok(n) if n >= 6 && buf[2] == CMD_PONG => {
                    let ms = start.elapsed().as_millis() as u64;
                    ping_val = Some(ms);
                    msg = format!("Ping: {}ms", ms);
                }
                _ => { msg = "Ping timeout".into(); }
            }
            s.set_nonblocking(true).ok();
        }

        self.last_ping = ping_val;
        if !msg.is_empty() { self.log(msg); }
    }
}

fn ui(f: &mut Frame, app: &App) {
    let chunks = Layout::default()
        .direction(Direction::Vertical)
        .constraints([
            Constraint::Length(3),
            Constraint::Min(10),
            Constraint::Length(1),
        ])
        .split(f.area());

    let titles = vec![" Salas [1] ", " Logs [2] ", " Ayuda [3] "];
    let tabs = Tabs::new(titles)
        .block(Block::default().borders(Borders::ALL).title(" ◆ MSX ONLINE MANAGER ◆ "))
        .select(app.tab)
        .style(Style::default().fg(Color::White))
        .highlight_style(Style::default().fg(Color::Yellow).add_modifier(Modifier::BOLD));
    f.render_widget(tabs, chunks[0]);

    match app.tab {
        0 => draw_rooms(f, app, chunks[1]),
        1 => draw_logs(f, app, chunks[1]),
        2 => draw_help(f, chunks[1]),
        _ => {}
    }

    let status = if app.connected {
        let ping = app.last_ping.map(|p| format!(" │ {}ms", p)).unwrap_or_default();
        format!(" ● ONLINE │ {}:{} │ {} sala(s){}", SERVER_IP, SERVER_PORT, app.rooms.len(), ping)
    } else {
        " ○ OFFLINE │ Pulsa 'c' para conectar".into()
    };
    let color = if app.connected { Color::Green } else { Color::Red };
    f.render_widget(
        Paragraph::new(status).style(Style::default().fg(Color::Black).bg(color)),
        chunks[2],
    );
}

fn draw_rooms(f: &mut Frame, app: &App, area: Rect) {
    let chunks = Layout::default()
        .direction(Direction::Horizontal)
        .constraints([Constraint::Percentage(65), Constraint::Percentage(35)])
        .split(area);

    let items: Vec<ListItem> = if app.rooms.is_empty() {
        vec![ListItem::new("  (sin salas)").style(Style::default().fg(Color::DarkGray))]
    } else {
        app.rooms.iter().enumerate().map(|(i, r)| {
            let style = if i == app.selected_room {
                Style::default().fg(Color::Black).bg(Color::Yellow)
            } else {
                Style::default().fg(Color::White)
            };
            ListItem::new(format!(
                "  #{:02X}  {:12}  {}/{} jugadores",
                r.id, game_name(r.game_id), r.players,
                if r.game_id == 0x02 { 2 } else if r.game_id == 0x03 { 14 } else { 4 }
            )).style(style)
        }).collect()
    };

    f.render_widget(
        List::new(items).block(Block::default().borders(Borders::ALL).title(" Salas ")),
        chunks[0],
    );

    let info = vec![
        Line::from(Span::styled("CONTROLES", Style::default().fg(Color::Yellow).add_modifier(Modifier::BOLD))),
        Line::from(""),
        Line::from("  c  Conectar"),
        Line::from("  d  Desconectar"),
        Line::from("  r  Refrescar salas"),
        Line::from("  p  Ping"),
        Line::from(""),
        Line::from("  ↑↓ Seleccionar"),
        Line::from(""),
        Line::from("  1-3 Pestanas"),
        Line::from("  q   Salir"),
    ];
    f.render_widget(
        Paragraph::new(info).block(Block::default().borders(Borders::ALL).title(" Atajos ")),
        chunks[1],
    );
}

fn draw_logs(f: &mut Frame, app: &App, area: Rect) {
    let items: Vec<ListItem> = app.logs.iter().rev().take(50).rev().map(|l| {
        let color = if l.contains("Error") || l.contains("fallida") || l.contains("timeout") {
            Color::Red
        } else if l.contains("OK") || l.contains("Conectado") || l.contains("autenticado") {
            Color::Green
        } else {
            Color::White
        };
        ListItem::new(l.as_str()).style(Style::default().fg(color))
    }).collect();

    f.render_widget(
        List::new(items).block(Block::default().borders(Borders::ALL).title(" Logs ")),
        area,
    );
}

fn draw_help(f: &mut Frame, area: Rect) {
    let help = vec![
        Line::from(Span::styled("MSX ONLINE MANAGER v1.0", Style::default().fg(Color::Yellow).add_modifier(Modifier::BOLD))),
        Line::from(""),
        Line::from("Herramienta de gestion del servidor MSX Online."),
        Line::from(""),
        Line::from(Span::styled("Servidor", Style::default().fg(Color::Cyan))),
        Line::from(format!("  {}:{}", SERVER_IP, SERVER_PORT)),
        Line::from(""),
        Line::from(Span::styled("Juegos", Style::default().fg(Color::Cyan))),
        Line::from("  0x01 Ball Demo (4 jugadores, RELAY)"),
        Line::from("  0x02 Damas (2 jugadores, RELAY)"),
        Line::from("  0x03 Burdyn RPG (14 jugadores, AGGREGATE)"),
        Line::from(""),
        Line::from(Span::styled("SSH", Style::default().fg(Color::Cyan))),
        Line::from("  Logs:    ssh root@217.154.107.144 journalctl -u msx-server -f"),
        Line::from("  Restart: ssh root@217.154.107.144 systemctl restart msx-server"),
        Line::from(""),
        Line::from(Span::styled("Proyecto de Antxiko", Style::default().fg(Color::DarkGray))),
    ];
    f.render_widget(
        Paragraph::new(help).block(Block::default().borders(Borders::ALL).title(" Ayuda ")),
        area,
    );
}

fn main() -> io::Result<()> {
    enable_raw_mode()?;
    stdout().execute(EnterAlternateScreen)?;
    let mut terminal = Terminal::new(CrosstermBackend::new(stdout()))?;

    let mut app = App::new();
    app.log("MSX Online Manager arrancado".into());

    loop {
        terminal.draw(|f| ui(f, &app))?;

        if event::poll(Duration::from_millis(100))? {
            if let Event::Key(key) = event::read()? {
                if key.kind != KeyEventKind::Press { continue; }
                match key.code {
                    KeyCode::Char('q') | KeyCode::Esc => break,
                    KeyCode::Char('1') => app.tab = 0,
                    KeyCode::Char('2') => app.tab = 1,
                    KeyCode::Char('3') => app.tab = 2,
                    KeyCode::Char('c') => {
                        app.disconnect();
                        app.connect();
                        if app.connected { app.refresh_rooms(); }
                    }
                    KeyCode::Char('r') if app.connected => app.refresh_rooms(),
                    KeyCode::Char('p') if app.connected => app.ping(),
                    KeyCode::Char('d') => app.disconnect(),
                    KeyCode::Up if app.selected_room > 0 => app.selected_room -= 1,
                    KeyCode::Down if app.selected_room + 1 < app.rooms.len() => app.selected_room += 1,
                    _ => {}
                }
            }
        }

        if app.connected && app.last_refresh.elapsed() > Duration::from_secs(10) {
            app.refresh_rooms();
        }
    }

    disable_raw_mode()?;
    stdout().execute(LeaveAlternateScreen)?;
    Ok(())
}
