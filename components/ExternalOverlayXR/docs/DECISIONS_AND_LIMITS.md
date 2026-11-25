# Decisiones registradas y límites de 0.1.1

| Decisión | Motivo | Consecuencia |
|---|---|---|
| Capa OpenXR común | Compartir visores, input y composición entre motores | Cada consumidor conserva su sesión y aporta inicialización |
| Host fuera del juego | Ejecutar UI sin introducir Edge/Node en render | Deben empaquetarse dependencias y gestionar procesos hijos |
| Autoridad de parámetros por adaptador | No duplicar estado ni inventar setters universales | Integración de escritura por motor; misma UI/contrato |
| Lectura de subrect por vista | Swapchain puede ser atlas, array o contener márgenes | No dividir su ancho entre dos ni confundir recomendación |
| Borrador y Apply común | Evitar recrear recursos por cada movimiento | Preflight global antes de mutar grupos; errores parciales explícitos |
| Sin replay tras timeout | Una orden puede haberse aplicado aunque se pierda respuesta | Reconciliar mediante estado, no volver a enviar automáticamente |
| Versiones/huellas fijadas | Mantener resultados ligados a lo probado | Actualizaciones explícitas por consumidor |
| Adaptador UEVR fuera del núcleo | Sus offsets son de un único backend | No contaminar otros proyectos ni heredar compatibilidad interna |

## Soporte real

Windows x64, OpenXR y D3D11/D3D12; una sesión/instancia activa por proceso. Interacción derecha para aim/clic/scroll y gesto de ambas manos. No implementados: OpenVR directo, Vulkan/OpenGL, múltiples sesiones simultáneas, detección física infalible del modelo, generación automática de interfaces ni control de resolución universal sin adaptador.

La extracción incluye una factory de host y widgets. No contiene un bootstrap listo para todo motor ni sustituye las restricciones de carga de cada aplicación. Un juego que crea OpenXR antes de permitir cargar plugins necesita resolver ese punto de integración; no prometer carga tardía por cambiar un manifiesto.

## Qué se validó

La evidencia del nuevo artefacto está en [validación 0.1.1](validation/release-0.1.1.json),
el [cierre de publicación](validation/publication-0.1.1.json) y la
[matriz de compatibilidad](COMPATIBILITY.md). Carlos aceptó la integración
AC7 0.8.5-r2 y el cierre del módulo el 07/10/2026. Los resultados que siguen
identifican exclusivamente el origen histórico.

Origen AC7 0.8.3-r2: 30 casos offline y aceptación física del usuario tras vuelo el 05/10/2026. Referencia Pimax/mandos propios y Pico 4 validada en Rogue Trader según Carlos; nueva integración Pimax AC7 sin dispositivo disponible. Extraer el núcleo y probar WARP verifica separación técnica, no certifica otro juego, visor, conexión ni rendimiento.

La funcionalidad de resolución en caliente de AC7 se conecta al mismo escalar de la UI UEVR, con identidad estricta de binario/PDB y compare/exchange. No es una API pública de UEVR ni un permiso para adaptar otras nightly mediante offsets aproximados. El módulo común no sabe que ese acceso existe.

## Trabajo posterior deliberado

Incorporar consumidores adicionales con adaptadores y matrices de prueba; automatizar comparación de ABI entre versiones; ampliar APIs gráficas sólo con alcance y pruebas; mantener portabilidad del host y métricas de coste separadas del FPS del juego. No trasladar problemas de render de un consumidor al núcleo sin una reproducción que señale esa responsabilidad.
