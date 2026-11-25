# AC7 origin integration guide (adapted for module 0.1.1)

Paths below refer to the [AC7 private repository](https://github.com/Beren5556/AC7VRDLSS/tree/v0.8.3), not to this extracted tree. The local API and layout are indexed in INDEX.md.

# Módulo común Overlay/OpenXR — guía de integración 0.8.3-r2

04/10/2026. Requisito de Carlos: una misma capa para presentar los parámetros de cada mod y manejar Quest 2/Quest 3S y Pimax, con Pico como prioridad secundaria. **La gestión de mandos pertenece al módulo común**, no a cada adaptador. AC7 es la integración actual; Borderlands es el ejemplo de integración futura, todavía no implementada ni certificada.

## 1. Arquitectura y responsabilidades

```text
Mod del juego: DLSS, DLAA, Neural u otras funciones
       ↕ autoridad de valores, órdenes y confirmaciones
Adaptador del proyecto + vista de sus parámetros
       ↕ transporte local autenticado (state / command)
Host del panel → píxeles BGRA → módulo ExternalOverlayXR
       ↑ eventos de puntero       ↕ instancia/sesión OpenXR existente
       └──────────────────────── visor, mandos y runtime del usuario
```

| Pieza | Responsabilidad | Qué se reutiliza |
|---|---|---|
| `ExternalOverlayXR.dll` | Composición del panel/cursor D3D11/D3D12, perfiles de mandos, aim, gatillo, scroll, gesto de apertura, foco, cambio de perfil y fin de sesión; información del sistema/runtime | La misma DLL y su manifiesto, sin recompilar por nombre de juego o marca de visor |
| Superficie/host | Memoria compartida versionada, render fuera del juego, HTTP local autenticado y eventos de UI | `surface.py`, `render_surface.cjs`, `child_job.py`; `make_server` de `host.py` acepta el transporte del proyecto |
| Información de sesión | Consumir los datos emitidos por la capa, sin detectar ni elegir visores desde JavaScript | `web/information.js`, con los elementos HTML descritos abajo |
| Adaptador del proyecto | Habilitar la capa antes de OpenXR, iniciar/cerrar un host por PID, teclado opcional, exponer lecturas y validar/aplicar órdenes | Implementación propia; **no copiar el adaptador AC7 sin adaptar su contrato** |
| Vista del proyecto | Parámetros que realmente ofrece ese mod, unidades, rangos, textos, estado efectivo y reinicios necesarios | Diseño compartible; campos y serialización específicos del mod |

El layer no implementa DLSS ni conoce `NVIDIA_DLSS`, `Ace7Game`, UEVR, rutas de instalación o INI del juego. No hay una rama «si Pimax en AC7» ni se deduce el modelo físico de un perfil Touch emulado. Los perfiles, extensiones y acciones se resuelven dentro del layer con OpenXR. F10 es una entrada opcional del adaptador; el gesto con mandos funciona dentro del layer.

`host.py` contiene también `PipeTransport` y una entrada CLI específicos de AC7: reutilizar la factoría del servidor, no esa selección predeterminada de transporte. `run.py`, `host.ini`, instaladores, `Bridge.cpp`, `Bootstrap.cpp`, Lua y `web/adapter.js` actuales son la integración AC7. No distribuir sus rutas locales a otro proyecto.

## 2. Archivos y compilación independiente

Fuentes mínimas de la capa en `experimental/external-overlay/native/`: `OpenXRLayer.cpp`, `Surface.h`, `HoldGesture.h`, `XrInformation.h`, `XrResolution.h` y su `CMakeLists.txt` independiente. Manifiesto: `experimental/external-overlay/ExternalOverlayXR.json`. Dependencias de compilación: Windows SDK, MSVC C++20 y headers OpenXR; ninguna dependencia de fuentes AC7/UEVR/DLSS. Ejemplo para un desarrollador, sustituyendo las rutas:

```powershell
cmake -S ./native -B ./build -A x64 -DOPENXR_INCLUDE_DIR=C:/SDK/OpenXR/include
cmake --build ./build --config Release
```

El consumidor puede usar la DLL probada sin recompilarla. Mantener el manifiesto junto a la DLL: su `library_path` es relativo. Nombre de capa estable: `XR_APILAYER_EXTERNAL_control_overlay`. El paquete local de reutilización y su inventario se documentan en la entrega 0.8.3. Es un kit para integrar, no un instalador universal ni un mod de Borderlands.

## 3. Integración del arranque, sin pasos nuevos para el usuario

1. El adaptador se ejecuta como parte del arranque habitual del mod, **antes de crear la instancia OpenXR**. Añade el directorio del manifiesto a `XR_API_LAYER_PATH` y el nombre de la capa a `XR_ENABLE_API_LAYERS` en el proceso correspondiente, conservando las entradas existentes y evitando duplicados. En Windows separar directorios con `;` y nombres de capas con `;`, siguiendo el mecanismo ya comprobado del bootstrap AC7. No modificar el registro del runtime ni variables globales del sistema.
2. Crear un host para el PID que posee la sesión OpenXR. Ese host crea la superficie compartida, el servidor loopback y el renderer. El módulo encuentra la superficie por PID; no usa nombres de ejecutable. No lanzar runtimes, cambiar conexión o pedir al usuario seleccionar visor.
3. Inicializar fuera de `DllMain`; vigilar vida del proceso y terminar solamente los hijos propios mediante `child_job.py` o el equivalente del proyecto. Mantener un solo host por PID y renovación de generación al reiniciarlo.
4. En AC7, esta ruta ya la activa `ExternalOverlay/settings.ini`, `Enabled=1`, mediante el arranque heredado de test8 en el adaptador r2. No ejecutar `Start-Overlay.ps1` además del arranque automático. Otros mods deberán aportar su inicialización equivalente; no se instala nada en ellos al entregar AC7.

Si la integración del juego crea OpenXR antes de que el plugin pueda actuar, resolver ese orden en el adaptador. No solventarlo cambiando de runtime, imponiendo una lanzadera manual ni cambiando la capa común por una copia específica.

## 4. Mandos y visores: política común

La capa ofrece sugerencias de acciones para los perfiles siguientes y utiliza el perfil activo que informa el runtime. Los nombres son **perfiles de interacción, no modelos físicos certificados**.

| Perfil | Interacción |
|---|---|
| Oculus Touch | Aim derecho, gatillo analógico, thumbstick, squeeze analógico en ambas manos |
| Valve Index | Mismas acciones analógicas comunes; válido cuando el runtime expone ese perfil |
| Microsoft Motion Controller | Aim/gatillo/thumbstick; squeeze digital convertido por OpenXR a la acción float |
| HTC Vive | Aim/gatillo/trackpad; squeeze digital; scroll requiere contacto táctil activo |
| ByteDance Pico 4 | Perfil nativo sólo con `XR_BD_controller_interaction` habilitada o API OpenXR 1.1 de la aplicación |
| Meta Touch Plus | Sólo con `XR_META_touch_controller_plus` habilitada; no se presupone disponible por usar API 1.1 |

Touch/Index reutilizan los contratos de la implementación Rogue Trader validada físicamente por Carlos en Pimax con sus mandos propios y en Pico 4. Referencia: commit `0fb98a2f991075256dffd2117f9d458360caa324`, `src/native/core/Bridge.cpp:877–958`; detalle en [análisis de compatibilidad](https://github.com/Beren5556/AC7VRDLSS/blob/v0.8.3/docs/ANALISIS_PICO_PIMAX_OPENXR_20261004.md). La extensión Vive/trackpad no se presenta como identificación de aquellos mandos Pimax probados. No se conoce el modelo/runtime exacto de esa validación ni se extiende a todos los Pimax/Pico.

No copiar esos bindings al adaptador de Borderlands: ya los aporta el layer. Negocia las extensiones opcionales anunciadas por la cadena, conserva versión API/extensiones/pNext solicitados por la aplicación y sigue funcionando con perfiles core si las opcionales no están disponibles. Si una sugerencia combinada falla, reintenta la sugerencia original de la aplicación para no romper sus bindings.

El gesto usa índice y agarre de ambas manos durante 2 segundos para abrir; no vuelve a abrir hasta soltarlos. El panel visible activa aim/clic/scroll, y al ocultarlo deja de sincronizar ese conjunto interactivo. Prioridad del gesto 0; panel 1000. Cada nueva integración debe comprobar compatibilidad con las prioridades y el attach/sync propios del juego. Las acciones se añaden al attach de la aplicación y a su sync; una aplicación que no los use requiere trabajo de integración, no está cubierta automáticamente.

Al perder foco, cambiar de perfil o faltar un sync reciente durante más de un segundo se cancela la entrada anterior; al recuperar, un gatillo mantenido no debe generar clic. `xrPollEvent` observa y devuelve el mismo evento solicitado por el juego, sin drenar su cola. Los perfiles se consultan al cambiar y periódicamente (1 s), no buscando marcas por frame.

## 5. Contrato de superficie e información

ABI de superficie **v1**: se conserva el [contrato completo](https://github.com/Beren5556/AC7VRDLSS/blob/v0.8.3/experimental/external-overlay/CONTRATO.md#superficie-compartida-v1). Mapping `Local\ExternalOverlay.Surface.<PID>`, mutex `.Mutex`, propietario único `.Host`. El host publica bajo mutex; la capa usa intento no bloqueante. No cambiar offsets ni dimensiones en una integración.

- Cabecera 64 bytes; ring de 128 eventos ×24 bytes; píxeles BGRA desde 4096, textura 960×900, panel visible 672×720. Cursor nativo fuera del rectángulo visible dentro de esa textura. Eventos 1 mover, 2 pulsar, 3 soltar, 4 salir/cancelar, 5 desplazar.
- `reserved[0]`: contador de alternancia de panel desde teclado; incrementar una vez al soltar, sin autorepeat. `reserved[1]`: generación de host; `reserved[2]`: contador Close. Conservar los demás campos del contrato.
- Bloque **`XrInformation` v1**: offset 3136, tamaño 832, entre el ring y los píxeles. Definición canónica `native/XrInformation.h`, con comprobaciones de tamaño/offset al compilar. No altera la ABI v1 existente ni los píxeles.

| Offset relativo | Tipo | Significado |
|---|---|---|
| 0 | 6×u32 | versión, sesión activa, vendorId, maxLayerCount, backend (11/12), máscara de entrada |
| 24 | 2×u64 | tick Windows monotónico, XrVersion del runtime |
| 40 | char[256] | systemName devuelto por OpenXR |
| 296 | char[128] | runtimeName |
| 424 / 616 | char[192] cada uno | perfil izquierdo/derecho, terminados en NUL |
| 808 | 6×i32 | último resultado de sugerencia Touch, Index, Motion, Vive, Pico4, Plus; −1 inicial no intentado |

Máscara: 1 aim activo, 2 localización válida, 4 clic activo, 8 scroll activo, 16 acciones del gesto activas. Describe disponibilidad de acciones, no botones pulsados ni identificación física. El host copia los 832 bytes bajo mutex y rechaza versión desconocida, tick ausente/futuro o antigüedad superior a 3 segundos. Destruir sesión borra sistema/perfiles y desactiva el bloque. El resultado de un binding no demuestra que haya un mando conectado.

`POST /xr/info`, protegido con el mismo token/origen del host, devuelve `available`, `system`, `runtime`, `runtimeVersion`, `leftProfile`, `rightProfile`, `vendor`, `graphics`, `maxLayers`, `inputMask`, `bindings`, `ageMs`, `reason`. Si no hay información fresca, sólo se garantiza `available=false` y `reason`; nunca conservar en pantalla el visor de una sesión anterior. Es sólo lectura, independiente del estado/borrador de parámetros.

Para reutilizar `information.js`, proporcionar `<meta name="overlay-token" content="__TOKEN__">` y un elemento con `id="pv-headset-info"`. La vista AC7 lo envuelve en `<details id="pv-information">`; no es obligatorio para el contrato. La factoría sirve el script común, y el adaptador proporciona su propio `index.html` y `adapter.js`. Mostrar el texto de OpenXR con `textContent`, no HTML. No sustituir `systemName` por «Quest» porque el perfil sea Touch; el runtime puede emularlo.

## 6. Contrato del adaptador de parámetros

La capa OpenXR sólo lleva píxeles y eventos. El transporte del mod implementa `exchange(request)` para la factoría `make_server`; el adaptador web interpreta sus respuestas y envía las órdenes permitidas. Usar una única autoridad: el mod nativo. No escribir sus INI directamente desde el navegador.

Ejemplo orientativo de protocolo compartible (los nombres `DlssControls`/`ApplyDLSS` son de ejemplo, **no una API existente de Borderlands**):

```json
{"op":"state"}
{"protocol":1,"session":"session-id","fields":{"DlssControls":{"value":"payload-del-mod","ageMs":20}}}
{"op":"command","session":"session-id","event":"ApplyDLSS","data":"nuevo-payload","expected":{"DlssControls":"payload-del-mod"}}
{"submitted":true,"effective":false}
```

El receptor valida sesión, rangos, tipos, estado fresco y base esperada antes de aplicar. Una aceptación de transporte significa solicitado, no efectivo ni guardado. Publicar por separado configuración solicitada, estado efectivo, confirmación de guardado y necesidad de reinicio. Comparar bases antes de mutar; preservar borrador ante cambios externos; no reenviar órdenes inciertas tras timeout. La corrección test8 de confirmaciones tardías de AC7 es parte de la referencia: una confirmación propia tardía no debe convertirse en conflicto ni sobrescribir una edición posterior.

Conservar autenticación loopback, puerto efímero, token por sesión, validación Host/Origin, límites de mensajes, caducidad de órdenes y cierre ligado al PID. `make_server` permite seleccionar `ui_root` para la vista del nuevo proyecto; nunca servir rutas arbitrarias solicitadas por el cliente. Las rutas admitidas siguen siendo explícitas.

## 7. Escenarios de integración y ejemplos

El módulo no se limita a UEVR ni a Borderlands. Los próximos proyectos pueden ser mods sin UEVR y juegos PCVR nativos. Todos deben conservar la separación entre backend VR común, host/vista y autoridad nativa de parámetros.

| Escenario | Qué aporta la integración | Qué aporta el módulo común |
|---|---|---|
| Mod UEVR, como AC7 | Callback/plugin para inicializar antes de OpenXR; lectura/órdenes del mod y teclado; cierre ligado al juego | DLL de capa, panel, perfiles y entrada de mandos, información de sesión |
| Mod VR independiente, sin UEVR, con OpenXR | Inicialización propia del mod, acceso al PID/sesión, transporte de parámetros y gestión del host | La misma capa y contrato, sin incluir headers ni callbacks UEVR |
| Juego PCVR nativo con OpenXR D3D11/D3D12, más un complemento DLSS | Punto de carga admitido por ese proyecto antes de `xrCreateInstance`; adaptador a los valores/órdenes del complemento DLSS | La misma DLL conectada a la sesión existente; no crea una segunda sesión ni implementa DLSS |
| PCVR OpenVR, Vulkan/OpenGL u otra combinación | Requiere trabajo de backend; aún no compatible con esta entrega | Mantener contrato de UI/órdenes y ampliar el backend común cuando se autorice; no crear compatibilidad ficticia con un selector de visor |

### Ejemplo A: mod sin UEVR

Su inicialización habilita el manifiesto en el proceso antes de OpenXR y arranca un host propio. El host importa `Surface`, `make_server` y la gestión de hijos. El transporte `MiModTransport.exchange` implementa el contrato de su mod; `mi_vista` contiene sus controles. Esquema de integración, no script completo de arranque:

```python
surface = Surface(pid_de_la_aplicacion_openxr)
server, url = make_server(MiModTransport(pid_de_la_aplicacion_openxr),
                          surface=surface, ui_root=mi_vista)
# Servir en un hilo propio y arrancar render_surface.cjs con url.
# Vincular ambos al PID y cerrar sólo hijos propios al finalizar.
```

`render_surface.cjs` captura esa vista y traduce los eventos del layer a entrada DOM. La vista conecta sus botones/deslizadores al transporte. Ni el transporte ni la vista eligen Quest/Pimax/Pico: esto sigue resuelto por OpenXR dentro de la capa. No copiar `run.py` sin sustituir la selección `PipeTransport` AC7.

### Ejemplo B: juego PCVR nativo más complemento DLSS

El juego mantiene su instancia, sesión, render y entrada originales. Un punto de inicialización del complemento habilita la capa antes de crear esa instancia; si el juego ya la creó, no se garantiza una carga tardía y debe resolverse el punto de integración. El complemento expone estado solicitado/efectivo, calidad, nitidez y los parámetros que realmente soporte, y recibe comandos validados desde su adaptador. La capa añade panel/cursor a `xrEndFrame` y combina las acciones con las del juego; no intercepta funciones DLSS ni cambia la resolución del juego. Revisar prioridad de acciones, orden de capas y las políticas del juego. La integración y prueba concreta siguen pendientes en ese proyecto.

### Ejemplo C: aplicación futura a Borderlands

1. Terminar y validar las funciones nativas del mod (DLSS, etc.) con su autoridad de configuración.
2. Incorporar **la misma DLL de capa** y superficie/host. Aportar bootstrap automático compatible con su cadena OpenXR y ciclo de vida. No trasladar checks de Ace7Game o nightly de AC7.
3. Escribir su adaptador: publicar parámetros reales, validar cambios y transmitirlos al mod. Crear su vista con los controles que de verdad admita. El módulo común sigue manejando mandos y visor sin ramas específicas del juego.
4. Ejecutar laboratorio y prueba física con la combinación concreta de juego/runtime/visor. Documentar límites y recuperar la instalación anterior. No heredar la certificación UEVR/AC7 ni asegurar Pimax en Borderlands sólo por haber pasado AC7/Rogue Trader.

No se modifica Borderlands en 0.8.3. No se necesita que use los mismos parámetros ni versión de DLSS, pero sí una integración compatible con este contrato y Windows/OpenXR D3D11 o D3D12. Vulkan/OpenGL, OpenVR sin sesión OpenXR, varias instancias/sesiones simultáneas y un generador automático de paneles no están implementados.

## 8. Verificación mínima para el siguiente proyecto

Compilar el módulo sin fuentes del juego; comprobar carga antes de crear OpenXR y preservación de las otras capas. Verificar sugerencias/attach/sync, foco, mandos ausentes, cambio de perfil, gatillo mantenido, cierre y nueva sesión. Comprobar D3D11/D3D12 según el backend real, timeouts de swapchain, panel/cursor y límites de capas.

En el adaptador, comprobar lectura/cambios desde ambas interfaces, Apply/guardado tardío, conflicto real, recuperación después de pausa, orden incierta sin repetición, host único, arranque/cierre automáticos y rollback. En el panel, datos de visor frescos y nombres no inferidos. En físico, apertura, clic, scroll, legibilidad y coexistencia con controles del juego. Registrar separadamente laboratorio, visor y rendimiento medido.

La batería AC7 0.8.3 incluye una cadena OpenXR sintética con GPU WARP; sus variantes `--standalone` prueban consumo de superficie sin cargar `AC7_ExternalOverlay.dll`. Es evidencia de separación técnica, no ejecución de otro juego. Referencia exacta de pruebas, binarios e instalación en [ENTREGA_0.8.3.md](https://github.com/Beren5556/AC7VRDLSS/blob/v0.8.3/ENTREGA_0.8.3.md).

## 9. Resolución de salida común — ampliación r2

La resolución de salida es el primer parámetro del panel, con barra y botones −/+ de **50 píxeles de ancho por ojo**. El alto del borrador conserva la proporción; el adaptador devuelve las dimensiones que calcula su autoridad, que pueden redondearse. Los pasos se anclan al ancho vigente, incluso si no es múltiplo de 50. Editar el control no aplica cambios hasta pulsar Apply o Apply and save. El borrador no se pierde al cambiar de página.

La **lectura de las vistas presentadas pertenece a la capa común**, y no depende de UEVR. `xrBeginSession` recoge recomendaciones/límites de la configuración activa; `xrEndFrame` observa los `subImage.imageRect.extent` de la proyección original. Conserva los tamaños por vista, sin dividir el swapchain por dos ni confundir array, atlas, recomendación, resolución interna DLSS o textura del panel. El frame y sus capas originales se mantienen. La observación se hace en la posición de esta capa: otras capas posteriores pueden transformar la salida; no equivale a medir los píxeles físicos del visor.

Añadir `native/XrResolution.h` a las fuentes comunes enumeradas en el apartado 2. El bloque v1 ocupa **128 bytes en offset 3968**, sin alterar la superficie/píxeles anteriores:

| Offset relativo | Tipo | Contenido |
|---|---|---|
| 0 | 2×u32 | versión y número de vistas de la primera proyección |
| 8 | u64 | tick monotónico del último frame observado |
| 16 | 4×u32 | tipo de configuración, flags, máximo ancho/alto de swapchain del sistema |
| 32 | 4×24 bytes | por vista: ancho, alto, ancho/alto recomendados, ancho/alto máximos; seis u32 |

Flags: 1 sesión, 2 proyección presente, 4 varias proyecciones (ambigua), 8 más de cuatro vistas (datos truncados). Sin proyección se borran los tamaños anteriores. Destruir sesión invalida el bloque. `POST /xr/resolution` devuelve `available`, `views`, `viewCount`, `viewType`, `ambiguous`, `truncated`, `systemMaxWidth`, `systemMaxHeight`, `ageMs`, `reason`; rechaza bloques desconocidos o con antigüedad superior a 3 s. La vista muestra la **selección nativa y las vistas presentadas por separado**, incluso si difieren durante una transición. Si no hay frame fresco lo indica explícitamente.

### Autoridad de escritura de cada juego

La capa no redimensiona swapchains ajenos. Cada juego/mod aporta su autoridad de resolución y se conecta al mismo contrato. Esto permite juegos **sin UEVR y PCVR nativos**: sólo cambia el adaptador. El host común traduce `POST /output/state` a `exchange({"op":"output_state"})` y añade `submittedViews` desde la superficie. `POST /output/apply` transmite el cuerpo como `output_apply`, con el mismo token, origen y límites de tamaño del host.

Respuesta de ejemplo (valores ilustrativos):

```json
{"protocol":1,"session":"native-session-12","available":true,
 "canWrite":true,"canSave":true,"authority":"MiMod OpenXR",
 "width":3300,"height":3500,"minWidth":1000,"maxWidth":6000,
 "step":50,"expected":"revision-28","saved":false,
 "saveRequested":false,"restartRequired":false,"reason":""}
```

Solicitud y confirmación:

```json
{"session":"native-session-12","expected":"revision-28","width":3350,"save":true}
{"submitted":true,"effective":false,"selection":{"protocol":1,"session":"native-session-12","available":true,"canWrite":true,"canSave":true,"authority":"MiMod OpenXR","width":3350,"height":3553,"minWidth":1000,"maxWidth":6000,"step":50,"expected":"revision-29","saved":false,"saveRequested":true,"restartRequired":false}}
```

El adaptador valida sesión, versión de selección, tipos, límites y momento permitido por su motor **antes de escribir**; devuelve `error` si no puede aplicar. El token `expected` cambia ante ediciones externas, variaciones de geometría relevante o nueva sesión. `submitted` confirma la selección; nunca confirma por sí solo render efectivo o guardado. `saved` debe proceder de confirmación real de la persistencia. El transporte debe caducar peticiones que lleguen tras una pausa, como hace `PipeTransport` con `deadlineTick`; otro transporte implementará su plazo equivalente.

Si el proyecto sólo puede leer, devuelve `available=true, canWrite=false` con dimensiones y motivo. No debe simular éxito ni escribir su INI mientras otra autoridad mantiene valores distintos. Para ofrecer escritura tiene que conectar el control a la función real de cambio del motor/mod y validar su recreación de recursos. La integración de esa autoridad por juego es necesaria; UEVR no es un requisito del contrato.

### Incorporación del widget en otra vista

Servir los scripts comunes desde `make_server(..., ui_root=mi_vista)`. Incluir la meta del token, `<div id="pv-output-resolution"></div>` como primer parámetro y `/resolution.js`. La vista propia conecta su bloque único de acciones mediante:

```javascript
const request = window.overlayResolution.capture(save);
await window.overlayResolution.preflight(request); // antes de cualquier escritura
// Validar también los borradores de los restantes grupos.
// Aplicar los grupos del mod según su autoridad y comprobar sus respuestas.
await window.overlayResolution.apply(request);
```

Usar `overlayResolution.dirty` para habilitar Apply/Discard, `.availability(bloqueado)` durante operaciones y `.discard()` al descartar. Escuchar `outputdraftchange` para actualizar botones. El widget consulta el estado cada 500 ms, sincroniza cambios externos sólo sin borrador, rechaza conflictos y no repite automáticamente órdenes de resultado incierto. No copiar `web/adapter.js` AC7 para otro juego: es un ejemplo de orquestación, sus grupos/serialización son específicos.

Referencia ejecutable: `tests/resolution_test.py` implementa una autoridad PCVR independiente y arranca el **host real**; `tests/resolution_ui.cjs` usa el navegador real para recorrer barra, ±50, navegación, Apply/save, edición externa, conflicto, rechazo y pérdida de capacidad. No carga UEVR ni memoria del juego. Las pruebas `XrSmoke --standalone` comprueban también el bloque de resolución y los mandos con la DLL real, sin adaptador AC7. Son pruebas de integración de contrato, no validación física de un juego PCVR distinto.

## 10. Navegación compartida y Log

`web/navigation.js` conecta botones `data-overlay-page="parameters"` y `data-overlay-page="log"` a secciones `data-overlay-view` del mismo nombre. El bloque `#pv-actions` se muestra sólo en parámetros; Close sigue accesible. Cambiar página oculta/muestra DOM conservando controles/borradores y posición de scroll, sin recrear el host. `information.js` puede situarse dentro de Log. En AC7 se trasladan allí versión, avisos, créditos/licencias y diagnósticos que estaban al principio. No se añade captura continua ni historial ilimitado. Mensajes breves de conexión, aplicación y error permanecen junto a las acciones.

## 11. Adaptador AC7/UEVR 01143 — exclusivo de este proyecto

UEVR **sí permite cambiar su resolución OpenXR en caliente**. La API pública de plugins de 01143 no publica `OpenXR_ResolutionScale` mediante `get/set_mod_value`: pertenece a las opciones del runtime, no a los componentes de `Mod` recorridos por `PluginLoader`. Esa limitación de la API no implica reiniciar ni que la UI nativa carezca de la función.

R2 conecta `native/UevrOutput.h`, incluido sólo por `Bridge.cpp`, al mismo `ModSlider` de `OpenXR::resolution_scale`. No se compila ni distribuye en el kit común. Es una compatibilidad interna **fijada a un binario**, justificada por el código de revisión `4ee5c6b6162dee2291fc75f9dfc57667f6d45a2d` y los símbolos PDB entregados con 01143. Fuentes consultadas y salida de DIA: `experimental/external-overlay/evidence/uevr-01143-source/`. Herramienta de inspección: `tests/InspectUevrSymbols.cpp`; sólo lee el PDB, no carga UEVR.

Antes de ofrecer escritura verifica SHA256 de `UEVRBackend.dll` `1a34cf65e4c12002ec967ed167ddda4e1cc7ae1da82157d0e279597beddeed75`, CodeView GUID `B68B7741-BE31-43D4-9964-8578111FD477`/age 1 del módulo cargado, objetos/rango/nombre de la opción, sesión OpenXR activa y coincidencia con `get_hmd_width/height`. Comprueba proyección estéreo fresca, dimensiones válidas y límites. No admite 2D screen ni extreme compatibility. En otro binario queda sin capacidad de escritura; no buscar offsets por heurística.

La orden se ejecuta en Present del adaptador: intenta adquirir sin bloquear el mutex nativo de reinicialización, vuelve a leer estado y realiza compare/exchange del escalar. El ancho se convierte usando **la fórmula nativa de UEVR**, incluidas las correcciones FOV, preservando el ajuste de escala si sólo se guarda el ancho vigente. Para persistir llama a `vr.save_config`; lee `config.txt` únicamente para comprobar la escala guardada. No modifica backend, archivos UEVR ni runtime global. No exige reinicio para esta opción.

`OutputResolutionSmoke` comprueba resolución de objetos, bloqueos, aplicación/guardado/selección, conflicto, límites, sesión, caducidad, falta de proyección y discrepancia con API, con memoria sintética y el handler de producción. No carga el backend real. **Queda pendiente la prueba física de concordancia con el panel UEVR y recreación de salida durante el juego**, además de persistencia tras arranque. No se traslada esta compatibilidad interna a otras nightly ni se presenta como API universal.
