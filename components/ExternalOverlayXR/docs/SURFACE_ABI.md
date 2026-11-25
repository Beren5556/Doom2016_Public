# ABI compartida v1 — definición operativa

Mapping `Local\ExternalOverlay.Surface.<PID>`; mutex del mismo nombre con `.Mutex`; propiedad única del host mediante `.Host`. Little endian, Windows x64. Tamaño total: `4096 + 960 * 900 * 4 = 3460096` bytes. Definiciones canónicas C++ en `native/Surface.h`, `XrInformation.h`, `XrResolution.h` y decodificación Python en `host/surface.py`.

## Distribución

| Offset | Tamaño | Contenido |
|---:|---:|---|
| 0 | 64 | Header v1 |
| 64 | 3072 | 128 eventos ×24 bytes |
| 3136 | 832 | XrInformation v1 |
| 3968 | 128 | XrResolution v1 |
| 4096 | 3456000 | Atlas BGRA 960×900 |

Header: seis u32 (`version, frame, visible, writeSequence, readSequence, status`), un u64 (`hostTick`), ocho u32 reservados. No interpretar padding distinto ni cambiar alineación. `reserved[0]` cuenta alternancias de teclado, `[1]` identifica generación del host y `[2]` cuenta Close. Los restantes son diagnóstico/estado de entrada: consultar el código al extenderlos, no reclamarlos como libres.

Evento de 24 bytes: `sequence:u32, type:u32, x:f32, y:f32, value:i32, reserved:u32`. Tipos 1 movimiento, 2 down, 3 up, 4 salir/cancelar, 5 scroll. Coordenadas en píxeles del panel visible. El productor puede sustituir el último movimiento/salida no consumido del mismo tipo; no altera transiciones de clic. Si un evento esencial desborda la cola, marca presión y se cancela el lote ambiguo. No sobrescribir clics ni reinyectar un lote incierto.

El host escribe cabecera, atlas y secuencia bajo mutex. La capa intenta adquirir con espera cero; si no puede, conserva estado previo sin bloquear el render. El host admite espera limitada. `frame` aumenta tras publicación completa; `hostTick` usa GetTickCount64. Sin frame válido reciente durante 2 s, la capa oculta el panel. Una nueva generación de host cierra/reinicia visibilidad y secuencias; no abre sola el overlay.

## Información OpenXR

Bloque relativo: 0 seis u32 (versión, sesión, vendor, máximo capas, backend, máscara de entrada); 24 dos u64 (tick y XrVersion); 40 char[256] sistema; 296 char[128] runtime; 424 y 616 char[192] perfiles; 808 seis i32 resultados de bindings. Máscara 1 aim, 2 localización, 4 clic disponible, 8 scroll, 16 acciones del gesto. Resultados −1 indican no intentado, no ausencia de mando.

No exponer char buffers sin asegurar NUL ni usarlos como HTML. Sistema y runtime son nombres distintos. Un perfil Touch puede proceder de emulación y no identifica Quest. Sesión destruida borra identidad; versión desconocida, tick futuro/ausente o antigüedad >3 s producen `available=false`.

## Resolución

Offset relativo 0: versión y viewCount u32. 8: tick u64. 16: tipo de configuración, flags, ancho/alto máximos de swapchain u32. 32: cuatro entradas de seis u32: ancho, alto, ancho/alto recomendados, ancho/alto máximos. Flags 1 sesión, 2 proyección, 4 múltiples proyecciones, 8 más de cuatro vistas.

Los rectángulos proceden de la primera proyección original del frame, antes de añadir los quads propios. No representan el tamaño total de una textura ni se dividen entre dos; conservan arrays y vistas asimétricas. Sin proyección se borran dimensiones previas. Varias proyecciones hacen ambigua la interpretación; se informa, no se fusionan silenciosamente. El consumidor decide si admite escritura en esa configuración.

La lectura observa la posición de esta capa dentro de la cadena. Una transformación posterior puede cambiar lo entregado al runtime. Recomendación, selección del motor, rectángulo presentado y panel físico son conceptos distintos. Mostrar «esperando frame» cuando caduca, no conservar una cifra antigua como vigente.

## Compatibilidad de cambios

No hay espacio libre después del bloque de resolución antes de píxeles. Una nueva extensión necesita un mecanismo versionado deliberado, no aumentar la estructura y escribir encima del atlas. Mantener static_assert de tamaño/offset y casos Python/C++ cruzados. La versión de cada bloque es independiente de la versión comercial del módulo.
