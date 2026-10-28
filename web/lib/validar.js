const REGLAS = {
  temperatura: { min: 0, max: 50, nulo: true },
  humedad:     { min: 0, max: 100, nulo: true },
  luz:         { min: 0, max: 100, nulo: false },
  distancia:   { min: 0, max: 400, nulo: true },
  servo:       { min: 0, max: 180, nulo: false },
};

export function validarLectura(body) {
  const errores = [];
  if (!body || typeof body !== 'object') {
    return { ok: false, errores: ['El cuerpo debe ser un objeto JSON'] };
  }
  if (typeof body.dispositivo !== 'string' || body.dispositivo.length === 0) {
    errores.push("'dispositivo' es obligatorio");
  }
  const datos = { dispositivo: body.dispositivo };
  for (const [campo, regla] of Object.entries(REGLAS)) {
    const valor = body[campo];
    if (valor === null || valor === undefined) {
      if (!regla.nulo) errores.push(`'${campo}' es obligatorio`);
      datos[campo] = null;
    } else if (typeof valor !== 'number' || valor < regla.min || valor > regla.max) {
      errores.push(`'${campo}' fuera de rango (${regla.min} a ${regla.max})`);
    } else {
      datos[campo] = valor;
    }
  }
  return { ok: errores.length === 0, errores, datos };
}