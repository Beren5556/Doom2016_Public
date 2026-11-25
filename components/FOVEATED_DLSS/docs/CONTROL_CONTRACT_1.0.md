# Contrato de control para ExternalOverlayDLSS

**Contrato de control 1 — especificación para el puente Layer/host.**
Motor existente 0.3.0; API C++ 3; configuración 2, lectura 1/2.
Destino: ExternalOverlayDLSS 0.1.3. No modifica el procesamiento del motor.
El puente descrito aquí aún no está implementado en BL3 ni en el Layer.

## 1. Límites y correspondencia existente

El Layer presenta y edita; el host valida, aplica en su punto seguro, persiste
y comunica resultados; el motor valida Config y procesa la pareja de ojos.
El Layer no invoca funciones C++ del motor, carga NGX ni escribe directamente
`foveation-v1.ini`. El transporte local lo proporciona el proyecto Layer y su
adaptador de host. Esta especificación define mensajes lógicos; no crea puertos,
servicios, registros globales ni una nueva dependencia de IPC para el motor.

| Operación / dato | Implementación actual de referencia |
|---|---|
| Aplicar | `DLSS_FOVEADO.Apply` |
| Aplicar y guardar | `DLSS_FOVEADO.ApplySave` |
| Configuración y revisiones | `DLSS_FOVEADO.Controls` |
| Estado legible | `DLSS_FOVEADO.Status` |
| Resultado legible de guardado | `DLSS_FOVEADO.Saved` |
| Diagnóstico legible | `DLSS_FOVEADO.Details` |

Fuente: `references/bl3/FoveationController.h`, copia histórica del controlador
aceptado. Los nombres/eventos y el paquete existente se conservan. Descubrimiento,
correlación y resultados estructurados son responsabilidad del futuro puente;
no se debe anunciar que el controlador actual ya los exporta.

## 2. Descubrimiento antes de habilitar controles

Al conectar, el puente entrega un descriptor con estos datos:

| Campo | Tipo y significado |
|---|---|
| `controlVersion` | Entero, exactamente 1 para este contrato |
| `sessionId` | Identidad opaca no vacía de esta sesión del host; cambia al reiniciar/reconectar el puente |
| `hostId`, `hostVersion` | Identidad y versión declaradas por el host, sin rutas locales |
| `engineVersion`, `sourceContract` | `0.3.0` y `3` para esta base; no confundir con controlVersion |
| `configReadVersions`, `configWriteVersion` | `[1,2]` y `2` |
| `backend` | `D3D11` |
| `placement` | `fixed` |
| `operations` | `apply` y `applySave` solo si el host las ofrece |
| `geometryMode` | `nasalOval` si el host declara las mitades nasales; `symmetric` en el modo no nasal |
| `capabilities` | Funciones admitidas por la intersección de motor, host y proveedores seleccionados |

La lista de capacidades usa nombres estables: `fixedFoveation`, `rectangle`,
`ellipse`, `transition`, `centralSupersampling`, `spatialPeriphery`,
`temporalPeriphery`, `indicator`, `nasalHeightControls`, `neuralRegions`,
`neuralBefore`, `neuralAfter`. No anunciar Neural si falta el proveedor del host;
los tres últimos no se deducen del SDK. El host solo anuncia las funciones
utilizables en su integración. El modo nasal procede del host, no del visor.

`available` (booleano) y `unavailableReason` (texto) describen disponibilidad
actual, separada del soporte: estar compilado no demuestra que DLSS esté listo
ni que haya una pareja de vistas válida. El host vuelve a enviar descriptor y
snapshot si cambian proveedores/capacidades; los borradores se revalidan.

El Layer reconoce explícitamente controlVersion, sourceContract y esquema de
escritura antes de habilitar cambios. No usa comparaciones de texto del número
del motor para adivinar compatibilidad. Versión incompatible: mostrar motivo y
deshabilitar edición. Ausencia de descriptor: no presentar controles operativos.
Capacidades desconocidas se ignoran; no se activan modos por aproximación.

## 3. Snapshot, edición y unidades

El snapshot contiene `sessionId`, `revision`, `generalRevision`, `config`,
`runtime` y el último resultado de comando, si existe.

`revision` y `generalRevision` se transportan como **cadenas decimales uint64**,
para que una UI JavaScript no pierda precisión. Para enviar un comando ambas
deben ser positivas. El controlador puede publicar generalRevision=0 durante
inicio; en ese estado el Layer espera. `config` es el texto exacto de Config,
máximo 1024 bytes ASCII; no JSON de floats ni porcentajes redondeados.

El paquete de `.Controls`, `.Apply` y `.ApplySave` es el existente:

```text
revision|generalRevision|config
```

Máximo 1100 bytes para comandos, exactamente dos `|`. Config tiene 24 campos
para esquema 1 o 30 para esquema 2. Se rechazan campos vacíos, valores inválidos,
signos, desbordamientos, versiones desconocidas y separadores adicionales.
La validación definitiva es `Config::Parse`/`Config::Valid` del motor.

El Layer conserva por separado el snapshot y su borrador. Editar un deslizador
no envía Apply ni escribe disco. Aplicar usa las revisiones de las que nació el
borrador. Descartar vuelve al snapshot más reciente. Un conflicto de revisiones
requiere revisar el borrador; no hay reintentos que sobrescriban cambios ajenos.

### Campos del esquema 2, índices desde cero

| Índice | Campo | Valores/unidades |
|---|---|---|
| 0 | schema | 2 |
| 1 | enabled | 0/1 |
| 2 | central.shape | 0 Rectangle, 1 Ellipse |
| 3–4 | central.widthBps, heightBps | 1000–10000 |
| 5 | central.transitionBps | 0–5000 |
| 6 | central.linkedAxes | 0/1 |
| 7 | periphery | 0 Spatial, 1 Temporal |
| 8 | peripheralScaleBps | 2500–10000 |
| 9–10 | peripheralPreset, centralPreset | 0, 10, 11, 12 o 13 |
| 11 | supersampleBps | 10000–20000 |
| 12–15 | leftX, leftY, rightX, rightY | 0–10000, coordenadas normalizadas por ojo |
| 16 | showRegion | 0/1 |
| 17 | neuralLinked | 0/1 |
| 18 | neural.shape | 0 Rectangle, 1 Ellipse |
| 19–20 | neural.widthBps, heightBps | 1000–10000 |
| 21 | neural.transitionBps | 0–5000 |
| 22 | neural.linkedAxes | 0/1 |
| 23 | indicatorRegions | 0 SR, 1 Neural, 2 ambas |
| 24 | central.lockHeight | 0/1 |
| 25–26 | central.verticalRatioBps, upperShareBps | 0–10000 |
| 27 | neural.lockHeight | 0/1 |
| 28–29 | neural.verticalRatioBps, upperShareBps | 0–10000 |

10000 puntos básicos = 100%; la UI muestra porcentaje dividiendo por 100.
Preset central 0 hereda el host; periférico 0 es NGX Auto. No reinterpretar enums.
Esquema 1 conserva los primeros 24 campos, con índice 0=1; migración en memoria:
ambas alturas bloqueadas, ratio 10000 y reparto superior 5000. ApplySave escribe 2.

En `nasalOval`, widthBps es campo horizontal retenido. lockHeight conserva toda
la altura y los valores guardados de ratio/reparto. Desbloqueado:
reducción = (1 − ancho) × ratio; arriba = reducción × reparto superior;
abajo = reducción − arriba. La UI calcula la parte inferior como complemento,
sin añadir otro campo persistente. 80% / 90% / 70–30 conserva 82% de altura.
heightBps/linkedAxes mantienen su significado para `symmetric`; no se eliminan
del formato al mostrar controles nasales. Los centros existentes son posiciones
fijas avanzadas, no gaze. neuralLinked reutiliza toda la geometría central y
conserva los ajustes independientes para cuando se desenlace.

## 4. Comando y resultado estructurado del puente

Sobre el paquete existente, el adaptador de transporte asocia estos metadatos:

```json
{
  "controlVersion": 1,
  "sessionId": "identidad-opaca-del-host",
  "requestId": "identidad-opaca-del-comando",
  "event": "DLSS_FOVEADO.ApplySave",
  "packet": "12|8|2;1;1;8000;6000;1000;1;0;7500;0;0;10000;5000;5000;5000;5000;1;1;1;6000;6000;1000;1;0;0;9000;7000;1;10000;5000"
}
```

Es una representación lógica: el transporte puede serializar esos campos según
su infraestructura; el valor de `packet` no cambia. IDs son cadenas no vacías,
acotadas a 128 bytes UTF-8. Cada conexión del Layer mantiene un único comando
pendiente. El host sigue siendo autoridad aun con otros clientes/Lua conectados.

Validar sesión, versión, evento, capacidades, tamaño, Config y ambas revisiones
antes de encolar. Volver a comprobar revisiones al consumir en el punto seguro
del host, como ya hace Tick. No llamar al renderer desde el hilo de transporte.
El comando nunca se declara activo por el mero éxito de Submit.

El puente correlaciona `requestId` y devuelve:

| Resultado | Significado |
|---|---|
| `queued` | Submit aceptó la cola; aún puede fallar la comprobación de Tick |
| `accepted` | Tick incorporó Config a requested y avanzó revision |
| `rejected` | No se incorporó esta petición; incluye código de error y snapshot vigente |

El resultado final incluye `revision` y `persistence`: `notRequested`, `saved`
o `failed`, más un mensaje si falló. Un fallo de escritura no revierte la
configuración solicitada en memoria: esa es la conducta del controlador existente.
Apply no cambia la configuración guardada; ApplySave usa escritura temporal y
sustitución controlada por el host. No cambiar el nombre foveation-v1.ini.

Códigos de rechazo del puente: `incompatibleVersion`, `wrongSession`,
`unsupportedOperation`, `unavailableCapability`, `invalidPacket`, `invalidConfig`,
`notReady`, `busy`, `revisionConflict`. Una entrada inválida no toca configuración,
disco ni GPU. Un rechazo por capacidad no convierte automáticamente a otro modo.

El adaptador debe obtener resultados estructurados en el punto de aplicación y
guardado. No interpretar frases localizadas de `.Status`/`.Saved` como códigos.
Los eventos legibles existentes continúan disponibles para el panel Lua.

## 5. Estado real y desconexión

`runtime` contiene `requestedRevision`, `appliedRevision` (cadenas uint64,
appliedRevision puede ser "0"), `active`, `message` y `details`. Active solo
se anuncia cuando la revisión solicitada está confirmada y los dos ojos se han
procesado/publicado conforme al host. No equivale a `accepted` ni a `saved`.

Una revisión aceptada reinicia appliedRevision/active mientras espera resultados.
Si falla un ojo, el host conserva su imagen ordinaria para la pareja y describe
el motivo. La UI puede mostrar solicitado/pendiente sin anunciar foveación activa.
Deshabilitar el modo es Apply con enabled=0, no desinstalar ni apagar procesos.

Desconectar deshabilita el envío y marca el snapshot como no vigente. No cambia
por sí solo el último estado aplicado ni guarda nada. Al reconectar: nuevo
descriptor y snapshot, invalidación de peticiones pendientes y revisión explícita
del borrador. No repetir ApplySave automáticamente; ante timeout, el resultado
es desconocido hasta consultar el snapshot/resultado, no un fracaso demostrado.
Una repetición del mismo requestId dentro de la sesión devuelve el resultado
conservado o busy si sigue pendiente, sin volver a aplicar/guardar. La sesión
debe renovarse antes de descartar la deduplicación de IDs ya aceptados.

## 6. Comprobaciones para la integración posterior

El proyecto Layer y el adaptador del host deben verificar: descubrimiento/versiones;
UI derivada de capacidades; round-trip sin alterar los 30 campos; v1→v2 al guardar;
conflicto de cada revisión antes/después de encolar; cola ocupada; rechazo de
payload inválido; guardado fallido con requested conservado; aceptación distinta
de actividad binocular; desconexión/reconexión; duplicados sin doble guardado;
coexistencia con el panel Lua y conservación de ajustes al bloquear/desenlazar.

La matriz visual/VR de cada host se valida posteriormente con versiones fijadas.
Este documento no afirma que el nuevo puente ya exista ni requiere cambiar la
funcionalidad de renderizado aceptada para poder empaquetar el motor.
