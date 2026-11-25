# Contrato de control 2 — cuatro lados

Especificación aprobada por Carlos el 11 de octubre de 2026. El 12 de octubre
Carlos fija la versión como **FoveatedDLSS 1.1**: motor **1.1.0**, API C++ **4**,
configuración **3** (lee 1/2/3). Corrige la denominación provisional 0.4.0.
La designación no supone una publicación ni una actualización automática de BL3.
ExternalOverlayDLSS implementa el menú y el puente en su propio proyecto.
Motor 1.2.0 conserva este contrato base y añade la extensión optativa 1:
[Dynamic y DLAA central](EXPERIMENTAL_CONTRACT_1.0.md). El descriptor anuncia
la versión real del motor; 1.1.0 en la tabla identifica la introducción del contrato.

## Semántica y menú

Un único contorno fijo en el plano binocular común, con las proyecciones de los
dos ojos superpuestas y alineadas. Arriba/abajo/izquierda/derecha son direcciones
del usuario, nunca nasal/temporal ni porcentajes independientes por ojo.

Cada porcentaje recorta desde ese extremo del diámetro base. Se conserva el
centro (0.5, 0.5); radio de cada lado = 0.5 − porcentaje/100. Los dos ejes del
plano común tienen la misma unidad. Cada cuadrante usa sus dos radios: para una
elipse q = sqrt((dx/radioHorizontal)^2 + (dy/radioVertical)^2), para rectángulo
q = max(abs(dx)/radioHorizontal, abs(dy)/radioVertical). Interior q <= 1.
No desplazar una elipse simétrica para simular cuatro radios diferentes.

Ejemplos en orden **superior, inferior, derecha, izquierda**:

| Porcentajes | Contorno con forma elipse |
|---|---|
| 20 / 20 / 20 / 20 | Círculo de diámetro 60% |
| 20 / 20 / 0 / 0 | Elipse horizontal: ancho 100%, alto 60% |
| 0 / 0 / 20 / 20 | Elipse vertical: ancho 60%, alto 100% |
| 35 / 5 / 20 / 20 | Menor radio superior, mayor inferior |
| 20 / 20 / 35 / 5 | Menor radio derecho, mayor izquierdo |

Rango por lado 0–45%, paso recomendado 1%, precisión del contrato 0.01%.
El límite evita radios nulos. 0% conserva ese radio completo; no significa
desactivar el foveado. Transición 0–50%: fuera del interior, peso
1 − smoothstep(0, transición/100, q−1). La transición expande cada radio;
no cambia los cuatro porcentajes del interior. El indicador dibuja q=1.

Menú: activar; forma (óvalo/rectángulo); superior, inferior, izquierda, derecha;
transición; supersampling y preset central; periferia espacial/temporal, escala
y preset cuando corresponda; Neural enlazado a SR o región independiente con
los mismos cuatro lados/forma/transición; indicador SR/Neural/ambos; aplicar,
aplicar y guardar, descartar. Neural depende del proveedor/capacidades del host.
Se eliminan del menú nuevo lockHeight, verticalRatio, upperShare, linkedAxes,
ancho/alto y centros X/Y por ojo. neuralLinked se conserva.

## Descubrimiento y transporte

Hereda del [contrato 1](CONTROL_CONTRACT_1.0.md) las secciones de sesión,
revisiones uint64 como cadenas decimales, comandos, resultados, persistencia,
deduplicación, conflictos, desconexión y actividad confirmada de ambos ojos.
Los cambios de esta especificación prevalecen sobre las tablas/versiones v1:

| Campo del descriptor | Valor |
|---|---|
| controlVersion | 2 |
| engineVersion / sourceContract | 1.1.0 / 4 |
| configReadVersions / configWriteVersion | [1,2,3] / 3 |
| geometryMode | binocularFourSides |
| backend / placement | D3D11 / fixed |
| capacidades nuevas | fourSideRegions, binocularProjection |
| capacidades eliminadas en este modo | nasalHeightControls |

Mantener las demás capacidades v1 cuando sean utilizables. available solo es
true cuando también existe una proyección binocular válida suministrada por el
host; si falta, unavailableReason explica «Proyección binocular no disponible».
El Layer no calcula ni inventa esa calibración. No activar v2 frente a un host
v1 ni enviar esquema 3 a la integración BL3 antigua. Un cliente puede conservar
su compatibilidad v1 en un camino separado, sin reinterpretar campos.

Envelope controlVersion=2; eventos DLSS_FOVEADO.Apply / ApplySave / Controls
y paquete revision|generalRevision|config sin renombrar. Config <=1024 bytes
ASCII; comando <=1100 bytes. Sin campos vacíos, signos, espacios, valores fuera
de rango, campos sobrantes o bool distinto de 0/1. Aplicar no guarda en disco.

## Esquema 3: exactamente 22 enteros separados por punto y coma

| Índice | Campo | Rango/valor |
|---|---|---|
| 0 | schema | 3 |
| 1 | enabled | 0/1 |
| 2 | central.shape | 0 Rectangle, 1 Ellipse |
| 3 | central.topBps | 0–4500 |
| 4 | central.bottomBps | 0–4500 |
| 5 | central.rightBps | 0–4500 |
| 6 | central.leftBps | 0–4500 |
| 7 | central.transitionBps | 0–5000 |
| 8 | periphery | 0 Spatial, 1 Temporal |
| 9 | peripheralScaleBps | 2500–10000 |
| 10 | peripheralPreset | 0 Auto, 10, 11, 12, 13 |
| 11 | centralPreset | 0 heredar, 10, 11, 12, 13 |
| 12 | supersampleBps | 10000–20000 |
| 13 | showRegion | 0/1 |
| 14 | neuralLinked | 0/1 |
| 15 | neural.shape | 0 Rectangle, 1 Ellipse |
| 16 | neural.topBps | 0–4500 |
| 17 | neural.bottomBps | 0–4500 |
| 18 | neural.rightBps | 0–4500 |
| 19 | neural.leftBps | 0–4500 |
| 20 | neural.transitionBps | 0–5000 |
| 21 | indicatorRegions | 0 SR, 1 Neural, 2 ambas |

100 puntos básicos = 1%. Orden de serialización **T/B/R/L**; la UI puede mostrar
izquierda antes que derecha si enlaza por nombre. Default ambos contornos:
20/20/20/20, elipse, transición 10%. neuralLinked conserva la región independiente
guardada mientras utiliza la central. Otros valores por defecto no cambian.

Ejemplo válido, activado, mostrando el contorno SR:

```text
12|8|3;1;1;2000;2000;2000;2000;1000;0;7500;0;0;10000;1;1;1;2000;2000;2000;2000;1000;0
```

## Migración explícita, sin modificar el archivo al leer

Parse de esquemas 1/2 conserva GeometryMode::Legacy y la geometría antigua.
Encode de esa configuración sigue produciendo esquema 2. No hay una conversión
exacta general de los centros/óvalos nasales antiguos al nuevo contorno binocular.

Con snapshot antiguo, el host añade:

```json
{"migration":{"kind":"legacyToFourSides","requiresApply":true,"draftConfig":"3;..."}}
```

draftConfig es un Config válido completo, obtenido mediante FourSideDraft():
conserva activar, formas, transiciones y todos los ajustes de calidad/Neural/
indicador; propone 20% en los cuatro lados de ambas regiones y retira centros y
dependencias antiguas. Mostrar «Configuración anterior activa; aplica los cuatro
lados para cambiarla». No afirmar equivalencia visual. Este aviso acompaña los
botones existentes; no añade una confirmación modal. Editar prepara el borrador;
Apply cambia la geometría en memoria; ApplySave también escribe esquema 3 con
la persistencia del host. Descartar conserva el snapshot antiguo y restablece su
borrador propuesto. Nunca migrar/guardar al conectar, mover un slider o cerrar.
Si falta draftConfig válido para un snapshot antiguo, deshabilitar edición con
motivo. Snapshot esquema 3 se edita directamente y no lleva migration.

Un host v2 acepta del Layer únicamente Apply/ApplySave con esquema 3. La lectura
1/2 es para persistencia y snapshots; no habilita comandos antiguos en una sesión
v2. Validación de Config más negociación obligatoria en el adaptador.

## Obligaciones de integración del host

API 4 añade a cada ViewGeometry un BinocularMapping explícito: homografía 3×3
row-major de píxeles de salida locales al ojo al plano común normalizado, con
spaceId y revision no nulos e iguales para la pareja. Origen superior izquierdo,
Y hacia abajo, sin offsets de atlas ni jitter temporal. El host deriva la matriz
de sus proyecciones y convenciones verificadas; NasalSide no es una calibración.
Plano común con centro fijo (0.5,0.5) y unidades iguales en X/Y. Se rechazan mapas
ausentes, degenerados, no finitos o con horizonte dentro de una vista.
Se conserva un único contorno; cada ojo muestra la porción correspondiente.
Un cambio de geometría, mapa, vista o tamaño requiere nuevo epoch y reset de
historiales de ambos ojos/proveedores. El Layer no envía matrices.

El motor realiza recorte conservador, máscara e indicador con este mapa. No es
una calibración óptica, modelo de profundidad/IPD ni soporte automático de un
visor. Adaptador de host y comprobación binocular física quedan pendientes en
cada integración. No anunciar actividad por recibir un comando o por compilar.

Pruebas del Layer: tabla de 22 campos y límites; ejemplos 20/20/20/20 y 20/20/0/0;
T/B/R/L sin intercambio; enlazar/desenlazar Neural sin perder valores; migración
solo al aplicar; ninguna escritura al descartar; negociación v1/v2; proyección
ausente; y todas las pruebas de sesión, revisiones y resultados del contrato 1.
