import './globals.css';

export const metadata = {
  title: 'Estación IoT – Práctica 2',
  description: 'Dashboard de la estación IoT bidireccional con ESP32',
};

export default function RootLayout({ children }) {
  return (
    <html lang="es">
      <body>{children}</body>
    </html>
  );
}