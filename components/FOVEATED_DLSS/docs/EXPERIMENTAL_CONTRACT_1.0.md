# Extensión optativa 1 — motor 1.2.1

Current1.3.0 migration and opt-in consumer policy: [MIGRATION_1.3.0.md](MIGRATION_1.3.0.md). Generic central-DLAA capability below is not enabled by the DLSS-only consumer policy.

Adopción de exp6 y del ajuste final aprobado por Carlos para B3 VR 0.9.7.
La implementación procede del snapshot identificado en
`evidence/adoption-exp6-1.2.json`. No cambia la geometría ni las proyecciones de
1.1.0. El espacio, centro y unidades de ambos ojos siguen siendo comunes.

## Versiones y migración

Motor 1.2.1 (corrige la identidad de 1.2.0); API C++ base 4;
configuración base 3, lectura 1/2/3; control Layer 2;
`dlss_foveado::experimental::Version == 1`. Los 22 campos de la configuración
base conservan orden, valores y unidades. Los ajustes nuevos viajan aparte.
Los nombres C++/CMake existentes se conservan, incluido `FixedPipeline`.

El contrato es de fuentes, no ABI binaria. Recompilar host y todos sus backends
con los mismos headers/bibliotecas. CMake usa `SameMinorVersion`: solicitar
`find_package(DlssFoveado 1.2 CONFIG REQUIRED)`. Los consumidores 1.1 no se
actualizan silenciosamente. Las opciones nuevas están desactivadas por defecto.
El host debe negociar expresamente la extensión antes de usarla; base 4 por sí
sola no significa que un proveedor soporte Dynamic.

`PipelineSettings` añade `centralDlaa`, `dynamic`, `inner`, `outer`.
`ReconstructionInput` añade `preparedSlot` (central 0..2, periferia siempre 0).
`ReconstructionBackend::Statistics()` tiene implementación predeterminada vacía;
el proveedor NGX cuenta contextos creados/evaluados/liberados. Un backend externo
que quiera Dynamic debe separar historiales por `(viewId, branch, preparedSlot)`
y anunciar al menos ocho contextos. Los backends antiguos deben mantener Dynamic
deshabilitado hasta implementar esa semántica, aunque acepten la API base 4.

## Ajustes y contrato con el Layer

El adaptador del host traduce JSON a `experimental::Settings`; el motor no incluye
un parser JSON, transporte, persistencia ni UI. La forma aceptada es:

```json
{
  "version": 1,
  "mode": 2,
  "inner": [3000, 3000, 3000, 3000],
  "outer": [1000, 1000, 1000, 1000],
  "targetMicros": 11100,
  "centralDlaa": false,
  "sustainedOverloadMs": 1000,
  "recoveryDelayMs": 1000
}
```

| Campo | Semántica / validación |
|---|---|
| mode | 0 Disabled, 1 Fixed, 2 Dynamic; activación explícita |
| inner / outer | Cuatro enteros T/B/R/L, puntos básicos del diámetro completo; 0..4500, múltiplos de 100 |
| relación de límites | `outer[i] <= inner[i]`: inner es la zona mínima de calidad, outer la máxima; límites iguales válidos |
| targetMicros | 5000..50000; respaldo cuando no hay cadencia válida del runtime |
| centralDlaa | Booleano; requiere escena nativa, AA nativo y SR periférico temporal |
| sustainedOverloadMs | 100..10000 ms; defecto 1000; sobrecarga continua antes de contraer |
| recoveryDelayMs | 100..10000 ms; defecto 1000; ventana saludable de recuperación con cadencia |

Las dos esperas se configuran **solo en JSON**. El Layer no muestra controles
para ellas y debe conservarlas al editar, aplicar, guardar, restaurar o cambiar
de modo. Presets antiguos de extensión 1 con seis o siete campos reciben 1000
para cada espera ausente; campos desconocidos, versiones desconocidas y valores
inválidos se rechazan. No se reescriben archivos al cargar. El host persiste
perfiles y presets independientes por modo y selecciona el guardado al arrancar.

El contrato de host/Layer ya utilizado por el laboratorio anuncia
`experimentalVersion: 1`, `dynamicWithNeural: false`, y capacidades
`dynamicFoveation`, `independentModePresets`, `centralDlaa`, `stereoGpuInterval`
solo cuando el host realmente las suministra. Conserva `controlVersion: 2`,
revisiones y resultados del contrato base. Apply/ApplySave adjunta `experimental`
con los ajustes a su sobre; el `packet` base permanece
`revision|generalRevision|config`. El snapshot `experimental` versión 1 incluye
`selectedMode`, `profiles[3]`, `presets[3]` y telemetría. Cada perfil contiene
`config` (cadena base) y `settings`; preset no guardado es null.

La integración del host y el Layer son entregas distintas. No activar la extensión
con un Layer antiguo que no conserve ambos campos; fijar las revisiones de ambos
productores. Las vistas del Layer no requieren encender indicadores del juego.
Neural + Dynamic está excluido tanto en el controlador como en el pipeline.
Fixed + Neural antes/después conserva su funcionamiento.

## Control, preparación e historial

Validar ajustes antes de `Configure`, mantener un único propietario en el hilo
de render y alimentar `Sample` con secuencia, generación y observación frescas.
El host suministra medición asíncrona y llama `FramePeriod` con la cadencia real
del runtime cuando está disponible (4..50 ms). Ese objetivo prevalece sobre el
valor manual. No confundir intervalos de presentación con coste GPU puro.
Los tests simulados a 72/90/120 Hz no son mediciones de visores.

No usar muestras repetidas, ajenas a la generación, futuras, no finitas o con
antigüedad superior a 300 ms. `Stabilize` abre una ventana de 2 segundos; un hueco
o cambio de generación reinicia la observación. `ReportChangeCpuMs` bloquea
ajustes después de un coste superior al objetivo; Apply permite reintentar.
La espera confirma evidencia, no acelera la rampa: el movimiento entre niveles
adyacentes mantiene 300 ms, redondeando el contorno a pasos de 1%.

Tres recortes centrales preparados por ojo (máximo, intermedio, mínimo) más una
periferia por ojo: hasta ocho contextos. Recortes idénticos comparten un slot.
La primera evaluación prepara las variantes con entradas reales del ojo; las
siguientes seleccionan el menor recorte que contiene la máscara. El coste de
preparación no se considera una garantía de rendimiento ni debe ocultarse como
una evaluación ordinaria. Mantener los recursos vivos hasta retirar el trabajo.

Solo el movimiento automático de los cuatro cortes centrales dentro de límites
ya preparados conserva epoch/generación e historial periférico. Apply, cambio
de límites, calidad, modo, Neural, proyección, dimensiones o cámara exige la
reconfiguración/reset correspondiente. No adaptar a cada paso el epoch como si
fuese un cambio de configuración: eso reiniciaría los historiales.

Al cambiar de contexto central se sigue evaluando el saliente con **las entradas
actuales de ese ojo** durante la estabilización del entrante. La transición dura
16 frames; contempla inversiones de dirección. No reutiliza píxeles de frames
antiguos, no comparte historial entre ojos ni añade texturas grandes/contextos.
Durante la transición hay hasta dos evaluaciones centrales por ojo; estable,
una. Los resets reales siguen funcionando. El coste real de esta evaluación
adicional no está medido en esta entrega.

## DLAA central, Neural y memoria

`centralDlaa` requiere entrada de escena nativa (`input == output` observado),
`ldrNativeAA`, periferia temporal y escala periférica inferior a 100%. El centro
se prepara a resolución nativa de su reconstrucción, incluido supersampling;
la periferia reducida usa SR hasta la salida completa. No convertir una captura
reducida en supuesta escena nativa cambiando únicamente los metadatos.

Neural-after usa `GuideCrop` para preparar directamente profundidad/movimiento
regionales, conservando los dos mapeos nearest y su clamp. Evita imágenes guía
intermedias de salida completa. El proveedor Neural sigue siendo del host.

`NgxOptions::budgetReservePercent` conserva el nombre del laboratorio, pero es
un interruptor de protección: 0 desactivado, 1..99 activado, no un porcentaje
reservado. Consulta presupuesto local real y crecimiento no local >256 MiB
sobre la base. Rechaza nuevas asignaciones bajo presión; recursos preparados
toleran picos menores de 1 segundo. Un presupuesto no disponible no se inventa.
Errores reales de creación/driver siguen fallando; no hay reintento ilimitado.
Los mensajes incluyen uso/presupuesto. `Stop` y retirada por consultas GPU
mantienen su contrato asíncrono; no destruir el backend mientras esté ocupado.

## Alcance de las pruebas

CPU: contratos base, geometría, controlador, esperas, cadencia simulada,
rechazo de muestras, recuperación y bloqueo Neural. WARP: pipeline, guías,
composición, memoria simulada y transición exp6 de 148 pares con modelo temporal
deliberadamente incorrecto después de reset, entradas actuales, inversión de
dirección, recursos acotados y reset real. Comparaciones de gradientes cubren
ambos ojos y formas. NGX se compila/enlaza; no se ejecuta inferencia real aquí.
La prueba manual exp6 de Carlos pertenece a su DLL exacta en el laboratorio,
no acredita rendimiento ni funcionamiento universal de estos paquetes.
