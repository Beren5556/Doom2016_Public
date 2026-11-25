# Documentación técnica del módulo

Este índice es la entrada para un desarrollador que no conozca AC7. Versión vigente 0.1.1; la base 0.1.0 deriva de AC7 0.8.3-r2. Consultar [cambios](../CHANGELOG.md) y [migración 0.1.1](MIGRATION_0.1.1.md). No confundir la versión del módulo, la versión del consumidor y las versiones de las estructuras de memoria.

Cierre aceptado por Carlos el 07/10/2026. Fuentes y kit en la
[release privada v0.1.1](https://github.com/Beren5556/ExternalOverlayXR/releases/tag/v0.1.1).
[Informe vigente](../ENTREGA_0.1.1_r2.md), [identidades de publicación](validation/publication-0.1.1.json),
[compatibilidad y aceptación](COMPATIBILITY.md) y [política de publicación](RELEASE_POLICY.md).

## Orden de lectura

1. [Arquitectura y responsabilidades](ARCHITECTURE.md): dónde vive cada función y flujo completo de un clic.
2. [Host y panel](HOST_AND_PANEL.md): qué son, procesos, dependencias y empaquetado.
3. [ABI de memoria](SURFACE_ABI.md): offsets, tamaños, mutex, eventos y caducidad.
4. [Contrato de adaptadores](ADAPTER_API.md): integración de parámetros y resolución, conflictos y guardado.
5. [Mandos, visores y ciclo OpenXR](DEVICES_AND_LIFECYCLE.md): acciones, foco, extensiones y recuperación.
6. [Integración por tipo de proyecto](INTEGRATING_A_CONSUMER.md): UEVR, mod independiente y PCVR nativo.
7. [Compilación, pruebas y releases](BUILD_TEST_RELEASE.md): reproducción y criterios para entregar.
8. [Diagnóstico y mantenimiento](TROUBLESHOOTING.md): síntomas, evidencias y soluciones sin destruir preferencias.
9. [Decisiones y límites](DECISIONS_AND_LIMITS.md): por qué se hizo así y qué no está implementado.
10. [Guía original AC7](AC7_ORIGIN_GUIDE.md): referencia extensa de la primera integración, incluidos todos los detalles del adaptador UEVR. Sus rutas pertenecen al repositorio AC7.
11. [Primera instalación y bootstrap](FIRST_INSTALL_AND_BOOTSTRAP.md): manifiesto/DLL antes de cargar, extracción tardía, prueba con perfil vacío y evidencia de AC7 0.8.4.
12. [Adopción 0.1.1](MIGRATION_0.1.1.md): retirada de proveedores en el consumidor y arranque automático del perfil AC7 0.8.5-r2.

## Archivos canónicos

| Área | Archivos |
|---|---|
| Composición y acciones OpenXR | `native/OpenXRLayer.cpp` |
| Superficie, cola y geometría | `native/Surface.h`, `host/surface.py` |
| Gesto | `native/HoldGesture.h` |
| Identidad de sesión | `native/XrInformation.h`, `host/web/information.js` |
| Dimensiones presentadas | `native/XrResolution.h`, `host/web/resolution.js` |
| Servidor autenticado | `host/host.py`, función `make_server` |
| Procesos hijos | `host/child_job.py` |
| Navegador fuera del juego | `host/render_surface.cjs` |
| Navegación de páginas | `host/web/navigation.js` |
| Ejemplo sin UEVR | `examples/demo_host.py`, `examples/web/` |
| Pruebas nativas independientes | `tests/XrSmoke.cpp` |
| Procedencia exacta | `provenance.json` y recibo de validación |

El código es la definición ejecutable; si cambia un contrato, actualizar simultáneamente estructura, consumidor, pruebas y documentación. No hacer que el siguiente proyecto dependa de conocimiento conservado únicamente en un chat.

[Recibo de validación de esta extracción](VALIDATION.md).

Para otro Codex y otro PC: [inicio desde un clon nuevo](HANDOFF_OTHER_PC.md).
