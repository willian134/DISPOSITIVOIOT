import Database from 'better-sqlite3';
import path from 'node:path';

function crearConexion() {
  const db = new Database(path.join(process.cwd(), 'estacion.db'));
  db.pragma('journal_mode = WAL');
  db.exec(`
    CREATE TABLE IF NOT EXISTS lecturas (
      id          INTEGER PRIMARY KEY AUTOINCREMENT,
      dispositivo TEXT    NOT NULL,
      temperatura REAL,
      humedad     REAL,
      luz         INTEGER,
      distancia   REAL,
      servo       INTEGER,
      creado_en   TEXT    NOT NULL DEFAULT (datetime('now', 'localtime'))
    )
  `);
  return db;
}

const db = globalThis.__db ?? crearConexion();
if (process.env.NODE_ENV !== 'production') globalThis.__db = db;

export default db;