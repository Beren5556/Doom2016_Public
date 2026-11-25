# Primera instalación: archivos presentes antes del bootstrap

La instalación del consumidor debe dejar disponibles el manifiesto y la DLL de la capa **antes de ejecutar el código que prepara su activación**. Comprobar sólo que existen después de inicializar el plugin no demuestra que estuvieran disponibles cuando se necesitaban.

## Orden obligatorio

1. El instalador, importador o preparación previa despliega `ExternalOverlayXR.json`, `ExternalOverlayXR.dll` y los ajustes de activación requeridos por el consumidor. El `library_path` del manifiesto debe resolver a la DLL realmente instalada. En la distribución de referencia ambos archivos están juntos y la ruta de la DLL es relativa.
2. El bootstrap del consumidor comprueba esos archivos y prepara `XR_API_LAYER_PATH` y `XR_ENABLE_API_LAYERS` únicamente en el proceso objetivo, conservando otras capas. Debe ocurrir antes de crear la instancia OpenXR.
3. La integración inicializa el host fuera del bloqueo de carga de DLLs. Configura sus rutas y dependencias mediante el procedimiento de instalación del consumidor; después mantiene un host por PID.
4. El juego crea su instancia/sesión habitual y la capa consume la superficie del host.

**Si el bootstrap está en `DllMain`, los archivos deben existir antes de `LoadLibrary` del plugin.** Extraerlos más tarde desde `plugin_initialize` llega después de esa comprobación. Si se necesita extracción durante el arranque, realizarla en una preparación anterior y explícita, antes del bootstrap y de OpenXR. No trasladar extracción pesada, lanzamiento del host ni inicialización de VR al bloqueo de carga.

Volver a llamar a `LoadLibrary` para una DLL ya cargada no vuelve a ejecutar su `DLL_PROCESS_ATTACH`. Crear entonces el manifiesto no constituye una recuperación validada de la activación. El consumidor debe ordenar correctamente su instalación y su punto de entrada; el módulo común no corrige automáticamente el bootstrap de otro proyecto.

## Manifiesto, host y Edge son requisitos distintos

El manifiesto permite descubrir la capa; `library_path` permite cargar su binario. Las variables de activación conectan el bootstrap con el loader OpenXR. El host requiere sus propias rutas, Python/Pillow, Node/Playwright y Edge según el empaquetado del consumidor.

Tener Edge instalado no suple un manifiesto ausente cuando se prepara la activación. Tampoco prueba que el layer se haya habilitado. Separar estos fallos al diagnosticar: primero verificar archivos y activación en el momento de carga, después configuración y arranque del host.

## Prueba de aceptación de primera instalación

Usar el paquete final distribuido, en una carpeta nueva sin perfil previo, manifiesto residual ni rutas del equipo de desarrollo. Registrar su hash antes de extraerlo. No reutilizar una instalación ya preparada como única prueba.

- Antes de cargar el plugin, comprobar manifiesto, DLL referenciada y ajustes necesarios; registrar que ya existen.
- Cargar el plugin en un proceso nuevo y observar la activación desde el bootstrap, antes de inicializar el host o crear OpenXR. Comprobar que conserva otras capas.
- Inicializar después el plugin con un host simulado para probar un único arranque y rechazo de duplicados sin lanzar un juego ni VR.
- Contrastar con casos negativos: manifiesto ausente al cargar, y manifiesto creado después de cargar. Su resultado debe explicar claramente qué dependencia temporal tiene ese consumidor.
- Para la entrega completa, ejecutar además el setup del host y sus pruebas específicas en el destino nuevo. Un host simulado no valida Edge, composición VR ni la sesión física.

Estas comprobaciones pertenecen al consumidor y a su paquete. El hecho de que el módulo compile o sus fixtures OpenXR pasen no garantiza que otro proyecto lo haya instalado a tiempo.

## Evidencia de referencia: paquete AC7 0.8.4

El 05/10/2026 se extrajo el ZIP público completo de AC7 0.8.4 en un perfil aislado inicialmente vacío y se cargó su bridge real en procesos nuevos. El ZIP ya contenía el manifiesto, la DLL de la capa y `settings.ini`; la activación ocurrió antes de disponer de `host.ini`, ejecutar setup o iniciar Edge. Después, la inicialización del plugin con un host simulado produjo un único arranque.

| Caso comprobado | Resultado observado |
|---|---|
| ZIP completo, archivos desplegados antes de cargar | Capa activada; otras capas conservadas |
| Manifiesto ausente al cargar | Capa sin activar |
| Manifiesto creado tras cargar y segunda llamada a `LoadLibrary` | Capa seguía sin activar |
| Inicialización del plugin después de la carga correcta | Host simulado arrancó una vez; duplicado rechazado |

El paquete AC7 evita el problema concreto de extracción tardía porque despliega ambos archivos desde la importación. Su `Setup-Overlay.cmd` sigue siendo necesario una vez para configurar las rutas del host. No se modificaron el perfil real, el paquete publicado o los binarios.

[Identidades y resultados saneados](validation/first-install-20261005.json). Se usaron el bridge y layer publicados, no una recompilación. No se creó una instancia OpenXR real ni se inició juego, VR o Edge. Esta evidencia valida el orden de archivos/carga de AC7; no certifica BL3 ni otro consumidor o PC. El fallo comunicado por Carlos desde BL3 motivó esta comprobación, pero no se reprodujo ni corrigió BL3 en el repositorio común.
