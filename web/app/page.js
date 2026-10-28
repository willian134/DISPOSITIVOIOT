'use client';

import { useEffect, useState } from 'react';
import {
  LineChart, Line, XAxis, YAxis, Tooltip, Legend, CartesianGrid, ResponsiveContainer,
} from 'recharts';

const INTERVALO_MS = 3000;

function Tarjeta({ etiqueta, valor, unidad }) {
  return (
    <div className="tarjeta">
      <div className="etiqueta">{etiqueta}</div>
      <div className="valor">
        {valor ?? '--'}
        <span className="unidad">{unidad}</span>
      </div>
    </div>
  );
}

export default function Dashboard() {
  const [historial, setHistorial] = useState([]);
  const [dispositivo, setDispositivo] = useState({ online: false });
  const [angulo, setAngulo] = useState(90);
  const [enviando, setEnviando] = useState(false);
  const [mensaje, setMensaje] = useState({ texto: '', error: false });

  useEffect(() => {
    async function cargar() {
      try {
        const [rHist, rDisp] = await Promise.all([
          fetch('/api/lecturas?limite=30'),
          fetch('/api/dispositivo'),
        ]);
        setHistorial(await rHist.json());
        setDispositivo(await rDisp.json());
      } catch {
        setDispositivo({ online: false });
      }
    }
    cargar();
    const id = setInterval(cargar, INTERVALO_MS);
    return () => clearInterval(id);
  }, []);

  async function moverServo(valor) {
    setEnviando(true);
    setMensaje({ texto: 'Enviando orden…', error: false });
    try {
      const r = await fetch('/api/servo', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ angulo: valor }),
      });
      const datos = await r.json();
      if (r.ok) {
        setMensaje({ texto: `Servo movido a ${datos.angulo}°`, error: false });
        setDispositivo((d) => ({ ...d, servo: datos.angulo }));
      } else setMensaje({ texto: `Error ${r.status}: ${datos.error}`, error: true });
    } catch {
      setMensaje({ texto: 'No se pudo contactar al backend', error: true });
    } finally {
      setEnviando(false);
    }
  }

  const ultima = historial.at(-1) ?? {};
  const actual = dispositivo.online ? dispositivo : ultima;
  const datosGrafica = historial.map((l) => ({
    hora: l.creado_en?.slice(11, 19),
    Temperatura: l.temperatura,
    Humedad: l.humedad,
    Luz: l.luz,
  }));

  return (
    <main>
      <h1>Estación IoT</h1>
      <p className="subtitulo">
        <span className="estado">
          <span className={`punto ${dispositivo.online ? 'online' : ''}`} />
          {dispositivo.online
            ? `ESP32 en línea · ${dispositivo.ip} · señal ${dispositivo.rssi} dBm`
            : 'ESP32 sin conexión (mostrando la última lectura guardada)'}
        </span>
      </p>
      <div className="grid">
        <Tarjeta etiqueta="Temperatura" valor={actual.temperatura} unidad="°C" />
        <Tarjeta etiqueta="Humedad" valor={actual.humedad} unidad="%" />
        <Tarjeta etiqueta="Luz" valor={actual.luz} unidad="%" />
        <Tarjeta etiqueta="Distancia" valor={actual.distancia} unidad="cm" />
        <Tarjeta etiqueta="Servo" valor={actual.servo} unidad="°" />
      </div>
      <section className="seccion tarjeta">
        <h2>Control del servomotor</h2>
        <div className="control">
          <input
            type="range" min="0" max="180" value={angulo}
            onChange={(e) => setAngulo(Number(e.target.value))}
          />
          <strong>{angulo}°</strong>
          <button disabled={enviando} onClick={() => moverServo(angulo)}>Mover</button>
          {[0, 90, 180].map((v) => (
            <button key={v} className="secundario" disabled={enviando}
              onClick={() => { setAngulo(v); moverServo(v); }}>
              {v}°
            </button>
          ))}
        </div>
        <p className={`mensaje ${mensaje.error ? 'error' : ''}`}>{mensaje.texto}</p>
      </section>
      <section className="seccion tarjeta">
        <h2>Historial (últimas 30 lecturas)</h2>
        <div style={{ width: '100%', height: 280 }}>
          <ResponsiveContainer>
            <LineChart data={datosGrafica}>
              <CartesianGrid strokeDasharray="3 3" stroke="#e4e0d6" />
              <XAxis dataKey="hora" fontSize={12} />
              <YAxis fontSize={12} />
              <Tooltip />
              <Legend />
              <Line type="monotone" dataKey="Temperatura" stroke="#1e5c3a" dot={false} />
              <Line type="monotone" dataKey="Humedad" stroke="#2e86c1" dot={false} />
              <Line type="monotone" dataKey="Luz" stroke="#c9a227" dot={false} />
            </LineChart>
          </ResponsiveContainer>
        </div>
      </section>
      <section className="seccion tarjeta">
        <h2>Últimas lecturas</h2>
        <div className="tabla-scroll">
          <table>
            <thead>
              <tr>
                <th>Hora</th><th>Temp. (°C)</th><th>Hum. (%)</th>
                <th>Luz (%)</th><th>Dist. (cm)</th><th>Servo (°)</th>
              </tr>
            </thead>
            <tbody>
              {[...historial].reverse().slice(0, 10).map((l) => (
                <tr key={l.id}>
                  <td>{l.creado_en}</td><td>{l.temperatura ?? '--'}</td>
                  <td>{l.humedad ?? '--'}</td><td>{l.luz}</td>
                  <td>{l.distancia ?? '--'}</td><td>{l.servo}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      </section>
    </main>
  );
}