# Integrar un nuevo consumidor de principio a fin

## Decidir el punto de entrada

Identificar la API VR y gráfica real del proyecto, el PID que posee la sesión y un punto de inicialización anterior a `xrCreateInstance`. Esta entrega soporta Windows/OpenXR/D3D11 o D3D12. Un título OpenVR no pasa a ser OpenXR por copiar el manifiesto. Tampoco basta con cargar la capa después de que el juego haya creado su instancia.

En un plugin UEVR, el plugin puede preparar la activación y el host según su ciclo de vida. En un mod independiente, hacerlo desde su inicialización admitida. En un juego PCVR nativo, usar el punto de carga del complemento que añade las funciones; el juego conserva su instancia y acciones. No crear una segunda sesión VR ni reemplazar el backend global.

## Fijar la dependencia

Elegir una release del módulo, registrar su tag/commit, DLL SHA256, manifest y versión de bloques. La release vigente es [v0.1.1 privada](https://github.com/Beren5556/ExternalOverlayXR/releases/tag/v0.1.1); las identidades se publican en la ficha y el recibo del repositorio. Mantenerla junto a los artefactos del consumidor. Si se recompila, registrar herramienta/SDK/dependencias y probar ese nuevo archivo. La coincidencia de fuentes no implica coincidencia de binario. `provenance.json` describe el origen 0.1.0; AC7 0.8.5-r2 consume la DLL 0.1.1 exacta, identificada en `publication-0.1.1.json` del repositorio.

## Incorporar capa y host

1. Desplegar `ExternalOverlayXR.json` y `ExternalOverlayXR.dll` antes del bootstrap que prepara su activación. Mantenerlos juntos cuando el library_path sea relativo. Si el bootstrap se ejecuta en `DllMain`, deben existir antes de cargar el plugin: extraerlos en `plugin_initialize` sería demasiado tarde. Seguir [primera instalación y orden de carga](FIRST_INSTALL_AND_BOOTSTRAP.md).
2. Preparar sólo en el proceso objetivo `XR_API_LAYER_PATH` y `XR_ENABLE_API_LAYERS`, preservando las entradas existentes y la detección de otras capas. No escribir variables globales ni cambiar el registro del runtime.
3. Crear/iniciar un host por PID y su Surface antes de que la capa necesite píxeles. En el servidor: `make_server(MiTransporte(pid), surface=surface, ui_root=mi_vista)`.
4. Ejecutar el renderer con la URL retornada y NODE_PATH del runtime empaquetado. Asociar Node/Edge a ChildJob y terminarlo al salir el PID.
5. Validar duplicados, reinicio de host, ausencia de UI, ventana sin foco y destrucción de sesión. Un fallo del panel no debe alterar el render ni la configuración del juego.

La integración AC7 0.8.5-r2 resuelve el arranque mediante rutas relativas al
perfil, sin setup ni `host.ini`. Es responsabilidad de su bridge y no una
función nueva de la DLL común. Mantener esta receta automática en un port y
comprobarla desde una instalación vacía y una ruta diferente.

## Conectar parámetros y resolución

Localizar la autoridad que ya usa el menú nativo del juego/mod. Definir qué hilo puede leer/escribir, cómo se recrean recursos, límites del motor y señal de persistencia. El adaptador implementa output_state/output_apply; la vista coloca el widget primero y conecta Apply/Discard. Si una opción no está soportada, declararla no disponible con motivo.

No copiar `UevrOutput.h` de AC7 a otro motor ni asumir los offsets de otra nightly. En AC7 es un acceso interno restringido a un binario concreto; no forma parte de la API común. Una API propia del motor es preferible cuando exista y esté validada. La capa sólo observa la salida; no redimensiona imágenes ajenas como sustituto del setter del motor.

## Prueba mínima del consumidor

Preparar un perfil recuperable. Ejecutar lectura de estado, conflicto de borrador, Apply/save, respuesta tardía, sesión perdida y límites antes de VR. Probar acción desde UI nativa y desde overlay, confirmando una sola autoridad. En la sesión física coordinada: inicio habitual, gesto, clic/scroll, Log, correspondencia de resolución, cambios en caliente y guardado tras reinicio. Validar cada API gráfica/runtime/visor que se anuncie, registrando las combinaciones realmente probadas.

Añadir una primera instalación desde el paquete final en un perfil vacío: verificar archivos antes de cargar el plugin y activación antes de OpenXR. Contrastar manifiesto ausente/tardío para detectar dependencias ocultas por archivos de sesiones anteriores. Edge y el setup del host se comprueban por separado; no sustituyen esta prueba.

## Actualizar o retirar

Con juego cerrado, inventariar archivos propios y ajenos antes de instalar. Sustituir sólo artefactos propios; conservar preferencias, shaders y mods. Mantener copia de los reemplazados y comprobar hashes después. Rollback debe rechazar archivos que el usuario haya editado desde la instalación y conservar su elección de activación. No utilizar un rollback de otra versión.

La extracción no integra aún Borderlands ni otro motor. Al incorporar el siguiente consumidor, documentar aquí su contrato, capacidades, prueba física y diferencias reales, sin crear una copia divergente de bindings/entrada.
