# Validaciones por versión

La entrega 0.1.1 tiene un [recibo independiente](validation/release-0.1.1.json)
y su [informe inicial](../ENTREGA_0.1.1.md). El cierre privado conserva la DLL
probada y actualiza la documentación del kit: [informe vigente](../ENTREGA_0.1.1_r2.md),
[identidades de publicación](validation/publication-0.1.1.json) y
[compatibilidad](COMPATIBILITY.md). La evidencia histórica siguiente permanece
ligada a sus artefactos originales.

## Cierre privado 0.1.1 — 07/10/2026

Los 9/9 casos nativos, host/Edge y siete casos negativos originales conservan
su validez para la DLL y fuentes operativas idénticas. El paquete de publicación
actualiza sólo documentación; se comprueban hashes, contenido y una extracción
nueva. Carlos aceptó el cierre y ordenó subirlo a GitHub privado. Su aceptación
de AC7 0.8.5-r2 se documenta separadamente de las pruebas offline, sin deducir
rendimiento ni compatibilidad con otras combinaciones de juego/visor.

## Extracción 0.1.0 — 05/10/2026

Compilación independiente desde la raíz de este repositorio con MSVC 2022 x64 y CMake: PASS. CTest: 9/9 PASS (D3D11, D3D12, Index, Vive, Pico, Plus, Motion, no-ext y core 1.1). Son fixtures OpenXR con GPU WARP, sin arranque de juego ni headset. Se ejecutaron contra la DLL compilada en este repositorio, separada del artefacto aceptado de AC7.

`python tests/test_demo.py`: PASS con Python 3.12.14, Pillow 12.3.0, Node 24.19.0, Playwright 1.62.1 y Edge instalado. El ejemplo usa el host extraído y un adaptador en memoria sin UEVR; se verifican autenticación, UI real, pasos de 50, navegación, Apply/Discard. Instalar requirements.txt y package.json; el test encuentra node en PATH o mediante NODE_EXE. El ejemplo no modifica un juego ni implementa guardado físico.

La aceptación física de origen es la sesión AC7 0.8.3-r2 de Carlos. La DLL conservada en AC7 tiene hash 4351d18574e1eac52dc352d45296c311d936a19c78b6b628824ab1bdf724ecb7. No se reemplazó por la nueva compilación. La nueva compilación y demo no certifican otro juego, Pimax/Pico en AC7 ni FPS.

Las fuentes exactas y transformaciones están en provenance.json. Para una nueva release volver a ejecutar los casos afectados y comprobar su propio artefacto final; no copiar este PASS a un binario modificado.

## Comprobación posterior de primera instalación — 05/10/2026

Se comprobó el consumidor AC7 0.8.4 desde su ZIP público completo en un perfil inicialmente vacío, con el bridge publicado cargado en procesos nuevos: manifiesto y DLL ya presentes, activación correcta y otras capas conservadas. Los casos negativos de manifiesto ausente y creado después de la carga confirmaron la dependencia temporal del bootstrap. La inicialización posterior con host simulado produjo un único arranque. [Procedimiento, límites e identidades](FIRST_INSTALL_AND_BOOTSTRAP.md).

Es evidencia de empaquetado/bootstrap del consumidor AC7, no una repetición de los nueve CTests del módulo ni certificación de BL3, Edge o VR en otro PC. Esta actualización sólo cambia documentación; las fuentes nativas, ABI y artefactos de v0.1.0 permanecen iguales.
