// ============================================================================
// secrets.h – Credenciales del dispositivo
// Este archivo NO se sube al repositorio (está en .gitignore).
// Cada grupo crea su copia a partir de secrets.example.h
// ============================================================================
#pragma once

#define WIFI_SSID    "NOMBRE_DE_LA_RED"
#define WIFI_PASS    "CLAVE_DE_LA_RED"

// Debe ser IGUAL a DEVICE_API_KEY del archivo .env.local del backend
#define API_KEY      "grupo1-clave-2026"

// IP de la laptop donde corre Next.js (ver con ipconfig / ifconfig) y el puerto
#define BACKEND_URL  "http://192.168.1.50:3000"
