export async function llamarEsp32(ruta, opciones = {}) {
  const url = `${process.env.ESP32_URL}${ruta}`;
  try {
    const respuesta = await fetch(url, {
      ...opciones,
      headers: {
        'Content-Type': 'application/json',
        'X-API-Key': process.env.DEVICE_API_KEY,
        ...opciones.headers,
      },
      cache: 'no-store',
      signal: AbortSignal.timeout(4000),
    });
    const datos = await respuesta.json();
    return { status: respuesta.status, datos };
  } catch (error) {
    return { status: 504, datos: { ok: false, error: 'El ESP32 no responde', detalle: error.message } };
  }
}