# Visores, mandos y ciclo OpenXR

Prioridad de producto: Quest 2/Quest 3S y Pimax; Pico secundario. La prioridad no se implementa como selector de arranque: se utilizan runtime, extensiones y perfil activo de OpenXR. No cambiar de runtime ni pedir marca al usuario para elegir bindings. La misma DLL se integra en todos los consumidores compatibles.

| Perfil | Acciones |
|---|---|
| Oculus Touch / Valve Index | aim derecho, gatillos, sticks, squeeze analógico |
| Microsoft Motion | aim/gatillo/stick, squeeze digital convertido a float |
| HTC Vive | aim/gatillo/trackpad, squeeze digital, scroll con contacto |
| ByteDance Pico 4 | extensión XR_BD_controller_interaction o core 1.1 |
| Meta Touch Plus | extensión XR_META_touch_controller_plus requerida |

Los contratos Touch/Index proceden de la referencia Rogue Trader validada por Carlos con Pimax y sus mandos y con Pico 4. Esto no valida físicamente cada integración nueva. Vive/trackpad es cobertura adicional; no afirmar que ése sea necesariamente el mando Pimax probado. No inferir modelos exactos a partir de perfiles emulados.

## Inicialización y sesión

Habilitar la capa antes de `xrCreateInstance`, conservando la versión API, pNext y extensiones de la aplicación. La capa negocia sólo extensiones opcionales anunciadas; si no están, mantiene perfiles core. Si una sugerencia combinada falla, preserva/reintenta la sugerencia original de la aplicación.

La aplicación crea y sincroniza sus action sets; la capa añade los propios. El conjunto interactivo tiene prioridad 1000 y se activa con el panel visible. El gesto tiene prioridad 0 para coexistir. Una aplicación que no use attach/sync necesita integración adicional: no se certifica por el mero hecho de cargar la DLL.

Abrir con índice y agarre de ambas manos durante 2 segundos; requiere soltar para rearmar. F10 no pertenece al núcleo: cada consumidor puede incrementar una vez `reserved[0]` al soltar su tecla. Close incrementa `reserved[2]`. No autorepetir alternancias.

Al perder foco o tracking válido, cambiar perfil o faltar sync reciente (>1 s), cancelar la entrada anterior. Al volver, mantener el gatillo pulsado no debe producir un clic automático. `xrPollEvent` observa el evento pedido por la aplicación y lo devuelve; no drena la cola del juego. Se consulta el perfil tras cambios y periódicamente, no por nombre de hardware en cada frame.

## Recursos y fallo

La capa crea su swapchain y espacio VIEW; D3D11 usa el contexto correspondiente, D3D12 cola/lista/fence propios para subir imágenes. Esperas de imagen con timeout cero evitan bloquear el hilo del juego. Una imagen adquirida pendiente se conserva y libera una sola vez cuando está disponible. Si no hay frame del host, foco o capacidad de capas suficiente, se conserva la presentación del juego sin añadir el panel.

Destruir la sesión libera recursos propios, cancela entrada, oculta el panel y borra telemetría de identidad/resolución. Recrear sesión no debe heredar clic sostenido ni nombre de visor antiguo. Sólo se soporta una instancia/sesión activa por proceso en esta versión.
