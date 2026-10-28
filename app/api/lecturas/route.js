import db from '@/lib/db';
import { validarLectura } from '@/lib/validar';

export const dynamic = 'force-dynamic';

export async function POST(request) {
  if (request.headers.get('x-api-key') !== process.env.DEVICE_API_KEY) {
    return Response.json({ ok: false, error: 'API key inválida' }, { status: 401 });
  }
  let body;
  try {
    body = await request.json();
  } catch {
    return Response.json({ ok: false, error: 'JSON inválido' }, { status: 400 });
  }
  const { ok, errores, datos } = validarLectura(body);
  if (!ok) return Response.json({ ok: false, errores }, { status: 422 });

  const info = db
    .prepare(`INSERT INTO lecturas (dispositivo, temperatura, humedad, luz, distancia, servo)
              VALUES (@dispositivo, @temperatura, @humedad, @luz, @distancia, @servo)`)
    .run(datos);
  return Response.json({ ok: true, id: Number(info.lastInsertRowid) }, { status: 201 });
}

export async function GET(request) {
  const { searchParams } = new URL(request.url);
  const limite = Math.min(Number(searchParams.get('limite')) || 50, 500);
  const filas = db.prepare('SELECT * FROM lecturas ORDER BY id DESC LIMIT ?').all(limite);
  return Response.json(filas.reverse());
}