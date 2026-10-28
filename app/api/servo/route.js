import { llamarEsp32 } from '@/lib/esp32';

export async function POST(request) {
  let body;
  try {
    body = await request.json();
  } catch {
    return Response.json({ ok: false, error: 'JSON inválido' }, { status: 400 });
  }
  const angulo = Number(body.angulo);
  if (!Number.isInteger(angulo) || angulo < 0 || angulo > 180) {
    return Response.json({ ok: false, error: 'El ángulo debe estar entre 0 y 180' }, { status: 422 });
  }
  const { status, datos } = await llamarEsp32('/api/servo', {
    method: 'POST',
    body: JSON.stringify({ angulo }),
  });
  return Response.json(datos, { status });
}