# Contexto conjunto de proyección y render temporal

18/10/2026. Instrumentación de laboratorio opt-in, unida a la sonda GPU por operación. No activa DLSS ni el consumidor privado DLAA. No añade llamadas de render, envíos, esperas GPU ni escrituras a estructuras del motor. El registro CPU y el inventario de shaders tienen coste; no hay medida de FPS ni promesa de coste nulo.

## Activación coordinada

Sólo en el entorno hijo del launcher conservado, durante una prueba acordada con Carlos:

- `KHARVOX_TEMPORAL_AUDIT_PATH`: ruta absoluta nueva al JSONL GPU.
- `KHARVOX_TEMPORAL_CONTEXT=1`: genera `<audit>.context.jsonl`.
- `KHARVOX_SFS_CAPTURE_DIRECTORY`: directorio nuevo para originales SPIR-V y variantes de pipeline; permite investigar productores de HDR y exposición.

Ejecutar primero `scripts/check_temporal_launch.py`, comprobar juego/launcher cerrados y pareja DLL/sello coherente. Iniciar el launcher únicamente cuando Carlos esté listo. Confirmar en el log `CPU temporal context enabled` y la existencia de ambos encabezados. Crear `<audit>.arm` cuando Carlos confirme una escena jugable. El contexto usa exactamente el armado de la sonda GPU y la retirada ya existente de SFS.

El contexto termina tras 12 intervalos retirados con serial distinto; la sonda GPU mantiene sus 32 intervalos observados. No repetir una captura para acreditar otra vez el funcionamiento general de VR. Al terminar, analizar conjuntamente ambos JSONL e inventario de shaders con `scripts/analyze_temporal_context.py`.

## Datos y límites

El registro conserva metadatos antes del armado: imágenes/vistas reales tras expansión SFS, render passes originales y multiview, referencias de attachments por subpass, dependencias, framebuffers, shaders/variantes de pipelines, layouts y estado canónico de descriptores. Arrays, escrituras que continúan en otro binding y copias conservan las celdas actuales. Liberación/reset y reutilización de handles no heredan celdas antiguas. Las asignaciones conservan la forma del layout aunque se destruya su handle.

Semántica de referencia: [actualizaciones consecutivas y arrays](https://docs.vulkan.org/guide/latest/descriptor_arrays.html) y [copias de descriptores](https://docs.vulkan.org/refpages/latest/refpages/source/VkCopyDescriptorSet.html), documentación de Khronos.

Durante el intervalo registra dibujos y su generación de command buffer, offsets dinámicos originales, estado de viewport/scissor, enlaces de compute, dispatch, push constants, barreras Vulkan 1.0 transformadas, identidades de transferencias y llamadas de envío (resultado, semáforos, etapas, valores timeline). Un envío aceptado no acredita ejecución ni retirada. El analizador enlaza dibujos con los tokens GPU existentes y resuelve las ubicaciones de salida por las referencias del subpass; el orden del framebuffer por sí solo no basta.

Los hooks existentes del motor observan Pj, P0, V, VP, matriz del modelo seleccionado y resultado de la llamada original. La función original se ejecuta exactamente una vez. El modelo ya exige su huella completa; el observador adicional exige también los 0x3d9 bytes del constructor de vistas (`7f811e4e80479921`, FNV-1a). No instala un nuevo hook. Lecturas fallidas quedan nulas. Copias de vista de 0x11b0 bytes nunca se leen en los offsets V/VP posteriores. Los flotantes se guardan como palabras IEEE exactas.

Se observan las primeras 512 llamadas de modelo y 64 constructores de vista por intervalo; las omitidas se cuentan en `sampledOut`. No se ha asociado todavía una matriz CPU con el consumo de un shader de geometría. Las matrices permiten comparar ramas y fases, no declarar automáticamente jitter apto para NGX.

Límites independientes: 32 MiB de texto de metadatos, 96 MiB de texto de eventos, 100.000 objetos y 250.000 eventos; los contenedores tienen sobrecoste adicional. Shapes de layouts/descriptores tienen límites propios. Toda pérdida, excepción contenida o captura parcial impide declarar completo el transporte. Al agotarse eventos se detiene la observación; el fichero se vacía en la siguiente retirada o al cerrar.

No cubre alias de memoria, regiones exactas de transferencias, ejecución de secundarios, sync2 ni descriptores template/push. Los snapshots de descriptores representan actualizaciones CPU, no la estabilidad hasta consumo GPU. Los candidatos a productores se ordenan por observación CPU, nunca se presentan como orden de ejecución. El inventario original de shaders tiene sus propios límites; comprobar que existen los hashes usados. No se fija preexposición por el nombre de una variable ni por el denominador 128 del TAA.

## Validación

`doom-temporal-context`: arrays/copia/destrucción/reutilización, 2.000 registros concurrentes, armar/parar, capacidades, captura parcial, excepciones contenidas, lectura condicionada por huella y límites de las copias de vista. No ejecuta código de DOOM.

`doom-temporal-audit-runtime-gpu`: NativeSfs real, shader original local identificado y perfil instalado; constantes y ojos sintéticos en RTX4090. Ahora incluye el contexto y envíos de la fixture por el mismo helper de registro. 12 intervalos CPU y 14 operaciones GPU asociadas; conserva las 32 tandas GPU y sus comprobaciones anteriores. No equivale a probar el DLL en el visor.

Regresión formal SFS: 15/15; variantes guías/auditoría/resolve compiladas y validadas SPIR-V. `scripts/check_temporal_context.py`: 17 comprobaciones, incluidas asociación ambigua, pipeline/offset alterados, truncado y referencias de attachments permutadas. `jitterQualified`, `preExposureQualified`, `producerSynchronizationQualified` y `dlssReady` siguen false.

El siguiente paso es observar esta instrumentación en una única sesión coordinada, estudiar los originales de los productores y resolver la asociación de matrices, exposición y sincronización antes de insertar NGX. DLAA nativo continúa siendo la primera integración prevista; overlay común y foveado siguen pendientes de adopción posterior.

## Resultado vivo y diagnóstico de pérdidas

La sesión `live-temporal-context-20261018-210117` ya está terminada: 32 pares GPU,
12 intervalos CPU, 36 constructores de vista y 6.144 modelos observados. El análisis
adicional `scripts/analyze_doom_live_context.py` verifica P·(V·M), el control con la
proyección contraria y las fases frente a ambos ojos del TAA. Reconstruye candidatos
a productores según el orden de command buffers del envío aceptado, incluyendo
posibles escrituras storage de compute. No convierte ese orden en prueba de
ejecución GPU ni de cobertura de píxeles. Las 12 asociaciones de fase concuerdan;
11 tienen prefijo de envío completo y barrera color→lectura de fragmento observada.
El envío del último intervalo queda fuera de la ventana CPU.

La versión usada en esa sesión acumuló 82 pérdidas/avisos y ninguna fila `gap`.
`gap()` contaba avisos antes del armado sin conservar sus motivos; el límite de
metadatos tampoco distinguía causa. El archivo histórico no permite reconstruir
los 82 motivos ni atribuirlos exactamente al prearmado. No son 82 muestras GPU
perdidas. Su final `interval-limit`, cero excepciones y tamaños observados no
acreditan la integridad de metadatos anteriores al armado.

La corrección añade `lossAccounting:2` al encabezado schema1 y `lossReasons` al
final, con contadores `beforeArm`/`afterArm`, capacidades diferenciadas de objetos,
bytes de metadatos, número de eventos y bytes de eventos. Conserva máximos de
metadatos/objetos. El mapa admite hasta 63 motivos más `other-reasons`, con claves
de hasta 128 bytes. El contador global sigue incluyendo todas las incidencias;
no se silencian ni se recalifican retrospectivamente. `sampledOut` sigue separado.
La instrumentación sigue opt-in, sin escrituras al motor ni llamadas GPU nuevas.
