# DOOM Neural Post 1.1-dev1

Procesador opcional de las imágenes finales de ambos ojos, adaptado del auxiliar ES2 y de nr-bridge/Deus Ex. Se ejecuta después de DLAA/DLSS/foveado y de las manos integradas, **antes de xrReleaseSwapchainImage y xrEndFrame**. Remleo es una capa OpenXR externa instalada por Carlos: recibe las imágenes finales después de este punto. No se instala ni modifica Remleo, OFXR, el runtime global o el juego.

## Configurar

Editar `DOOMNeuralPost.ini`, junto a `KharvoxLayer.dll`. Sus 26 opciones están comentadas, con rangos y dependencias. Se leen al arrancar DOOM; cerrar el juego para editar y volver a abrir. El panel B3 sigue controlando DLAA/DLSS/foveado; su Neural antiguo permanece bloqueado. La nueva ruta se activa exclusivamente con `Enabled=1` en el INI. Para desactivarla, `Enabled=0` y reiniciar. Si falta el INI o tiene valores inválidos, Neural queda desactivado y se conserva la ruta original.

La entrega empieza con escala 60 %, intensidad 90 %, estilo Cinemático y una pasada. Son valores de partida para la prueba, no una promesa de rendimiento. Región 100 % procesa todo el ojo. El foveado de este procesador es fijo e independiente de la reconstrucción DLSS. No activar dos proveedores Neural simultáneamente.

## Arquitectura y límites

1. La copia XR normal termina antes del procesamiento Neural. Con Neural activo, esta ruta requiere completar la copia antes de liberar imágenes; con Neural apagado se conserva la política anterior.
2. Se importan en Vulkan los recursos D3D12 de un atlas privado mediante un handle Win32, identificando la GPU por LUID. Las copias preservan los bytes RGBA/BGRA, sin convertir accidentalmente sRGB a lineal. Se transfieren los rectángulos reales de ambos ojos y se restauran layouts/propiedad.
3. El auxiliar `DOOMNeuralWorker.exe` inicializa NGX en otro proceso. Las imágenes permanecen en GPU. Las esperas CPU de fences verifican el intercambio entre APIs; es una primera implementación síncrona, no un transporte de latencia cero. Incluye copias adicionales cuyo coste debe medirse en partida.
4. El par fuente aporta identidad, tiempo, pose y FOV. Los vectores son estimaciones del giro y la profundidad es constante: no se extraen profundidad real, traslación ni movimiento de objetos. Una imagen repetida reutiliza el resultado. Menús/cinemáticas suspenden el efecto; tamaño/FOV/generación y discontinuidades reinician historial.
5. Solo se publica el par después de éxito. Falta de modelo o fallo ordinario conserva el original y desactiva reintentos durante esa sesión. Un timeout/pérdida de dispositivo con trabajo GPU pendiente exige reiniciar; no se garantiza recuperación de un dispositivo perdido ni se liberan imágenes como si hubieran terminado.

Solo admite RGBA8/BGRA8, dos ojos de igual tamaño y rectángulos de al menos 64×64. El atlas máximo es 16384×16384. Las capas HUD separadas quedan fuera; manos, armas y texto integrados sí pueden cambiar. Procesa los fotogramas fuente del mod; la interpolación externa ocurre después. La coherencia visual binocular, los destellos históricos, la calidad, FPS y latencia con Remleo quedan pendientes de prueba física.

## Diagnóstico y recuperación

El log KHARVOX incluye `[NEURAL-POST] pre-release applied=1` cuando se completa la ruta. Los logs acotados del auxiliar van a `%TEMP%/KHARVOX-Diagnostics/NeuralPost`. No confundir selección INI con aplicación real: una pantalla de menú no evalúa el modelo.

La instalación conserva una copia de los archivos sustituidos y un recibo con huellas. La herramienta `scripts/neural_release.py restore` restaura esa base con DOOM cerrado; requiere que los archivos nuevos no hayan cambiado desde la instalación. `Enabled=0` permite conservar el resto de la nueva versión sin procesamiento Neural.

El modelo es la copia privada existente del usuario, no una integración oficial ni una autorización para distribuirlo. Procedencia y licencias: [PROVENANCE.md](PROVENANCE.md), `vendor/nr-bridge/PROVENANCE.upstream.md` y `vendor/`.
