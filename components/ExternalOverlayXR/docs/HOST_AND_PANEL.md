# Host, panel, dependencias y distribución

## Qué se ejecuta

El juego carga sólo la DLL nativa. La integración inicia un host Python asociado al PID OpenXR y un proceso Node que controla Edge headless. Edge y sus hijos pertenecen al trabajo de Windows creado por `ChildJob`; cerrar ese trabajo termina sólo esos procesos propios. No matar procesos Edge del usuario ni procesos por nombre global.

La superficie se identifica por PID, no por marca de visor ni título del juego. Su mutex `.Host` impide dos propietarios. El host abre el proceso objetivo con permiso de sincronización, detecta su fin y cierra sus hijos. Una integración que omita ese ciclo puede dejar procesos huérfanos aunque el panel funcione.

## Dependencias fijadas en la extracción

| Dependencia | Versión utilizada | Finalidad |
|---|---|---|
| Windows x64 y MSVC runtime | Entorno nativo de compilación | DLL, D3D11/D3D12 y sincronización |
| CPython | 3.12.14 | Host HTTP, proceso, memoria compartida |
| Pillow | 12.3.0 | PNG → BGRA |
| Node.js | 24.19.0 | Renderer fuera del juego |
| Playwright / Core | 1.62.1 | Edge headless y entrada DOM |
| Microsoft Edge | Instalación del equipo | Motor de interfaz |
| OpenXR SDK headers | Copia registrada en third_party | Compilación, no runtime alternativo |

`requirements.txt` y `package.json` fijan las dependencias de desarrollo. Un consumidor puede empaquetarlas o instalarlas durante su instalación; no descargarlas mientras está arrancando el juego. No depender de un workspace de Codex, PATH personal o una caché de desarrollo en la distribución.

## Servidor común

`make_server(transport, port=0, surface=None, ui_root=None)` devuelve `(server, url)`. El transporte implementa `exchange(dict) -> dict`. Ejecutar `serve_forever` en un hilo del host. `port=0` obtiene un puerto libre; la URL contiene un token de sesión generado con `secrets`. No publicar ese token en documentación, telemetría remota o logs compartidos.

`ui_root` suministra `index.html` y `adapter.js` del consumidor. El servidor mantiene rutas explícitas para los widgets comunes. No convierte un path arbitrario de petición en una ruta de disco. La extracción elimina `PipeTransport` y el CLI de AC7: no existe aquí un transporte UEVR predeterminado.

La vista incluye `<meta name="overlay-token" content="__TOKEN__">`; el servidor sustituye ese marcador. Toda petición de control usa `X-Overlay-Token`, validación Host/Origin y loopback. El adaptador debe añadir sus propias comprobaciones de sesión/revisión/rango antes de escribir. Autenticación HTTP no sustituye validación del motor.

## Interfaz reutilizable

- `information.js`: requiere `#pv-headset-info`; muestra texto escapado, rechaza información caducada y no infiere hardware físico.
- `resolution.js`: requiere `#pv-output-resolution` y la meta del token; crea barra/±50 y expone `window.overlayResolution`.
- `navigation.js`: conecta `data-overlay-page` con `data-overlay-view`; conserva DOM/borradores y scroll, oculta acciones en Log.
- `render_surface.cjs`: recibe URL local autenticada; traduce el ring a DOM y captura imágenes. La captura visible es aproximadamente cada 100 ms, y oculta cada 750 ms; no equivale al FPS VR.

El consumidor decide etiquetas/grupos del panel. Ofrecer únicamente los controles que implemente su motor. Mantener confirmaciones y errores de acciones junto a los controles; Log recoge detalle, identidad y diagnóstico. Close sólo oculta el panel. No debe cerrar el juego ni guardar parámetros de manera implícita.

## Empaquetado y rutas

AC7 0.8.5-r2 distribuye módulos del host, runtime Python/Pillow aislado,
Node/Playwright, HTML/JS y las DLL probadas. Su bridge descubre los componentes
por rutas relativas a su propia DLL y arranca el host automáticamente sin
setup ni `host.ini`. Esta función pertenece al consumidor; el común aporta
la factoría del servidor y el contrato de superficie. La receta antigua de
AC7 0.8.3 con INI absoluto se conserva únicamente en la guía de origen.

Bytecode está ligado a la versión de Python y no es cifrado. Los recursos HTML/JS son necesarios para ejecutar el panel. Las fuentes nativas y el material de desarrollo permanecen en privado según la política del proyecto. Retener las licencias de cada runtime y biblioteca, incluso si se elimina material de tests al empaquetar.

Al mover el perfil, comprobar que el adaptador resuelve los componentes
relativos a la nueva ubicación. Probar desde una carpeta distinta,
preferiblemente con espacios/acentos, y sin Python/Node globales. El host debe
producir un frame, rechazar duplicados y terminar al finalizar el PID objetivo.
Después probar su combinación física con el juego.
