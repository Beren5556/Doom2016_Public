# Compilación, pruebas, procedencia y mantenimiento

## Desarrollo nativo

Windows x64, Visual Studio 2022 Build Tools (MSVC C++20) y Windows SDK. Se incluyen los headers OpenXR y sus licencias para evitar depender del árbol AC7. Desde la raíz:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

`native/CMakeLists.txt` también permite compilar sólo la capa, indicando `OPENXR_INCLUDE_DIR`. No enlaza UEVR, DLSS, MinHook ni Python. El proyecto raíz añade el fixture XrSmoke y copia la DLL al directorio de su ejecutable. CTest cubre D3D11, D3D12, Index, Vive, Pico, Plus, Motion, extensiones ausentes y core 1.1. La cadena OpenXR es sintética, utiliza GPU WARP real y no inicia un runtime/visor.

Los tests comprueban composición y conservación de capas, binding/attach/sync, foco/tracking, ring saturado, gesto, cursor, recreación y dimensiones por vista. La variante extraída elimina el fixture de teclado/bridge AC7: no requiere headers UEVR. Las pruebas de ese adaptador permanecen en AC7.

## Desarrollo del host

Instalar las versiones declaradas en requirements.txt y package.json en un entorno de desarrollo. Tener Microsoft Edge instalado. El ejemplo requiere que Python encuentre `host/`; el propio script ajusta esa ruta. Ejecutar `python examples/demo_host.py`: muestra una URL local para inspeccionar la UI sin VR. No introducir su URL/token en un documento de release.

Para probar el renderer integrado, crear una Surface asociada a un proceso fixture, servir la vista y ejecutar `node host/render_surface.cjs <url>`. Comprobar publicación BGRA, entrada, token/origen, cierre y propietario único. El protocolo de resolución debe probar anchos no múltiplos de 50, externos mientras hay borrador, rechazados, respuestas tardías y ausencia de frames.

## Identidad de esta extracción

`provenance.json` conserva hashes de las seis fuentes nativas copiadas de AC7, origen y transformaciones limitadas a host/fixture. Los widgets, Surface y renderer son copias del código validado. `make_server` conserva comportamiento, retirando el transporte/CLI AC7. No se cambia la DLL instalada en el juego al crear este repositorio.

El binario original aceptado se identifica por SHA256, no por el nombre del archivo ni por un build reproducido posteriormente. Conservar separado: compilación, prueba de software, prueba física declarada y medición de rendimiento. Una recompilación no demuestra FPS, compatibilidad universal ni aceptación de otro consumidor.

## Publicación de nuevas versiones

1. Actualizar requisitos/contratos y registrar qué cambia en ABI o capacidades.
2. Implementar y ejecutar casos relevantes, conservando fuente y dependencias exactas.
3. Generar artefactos finales y comprobar esos archivos, no builds intermedios.
4. Validar instalación/retirada en un consumidor aislado y luego coordinar VR donde corresponda.
5. Actualizar VERSION, changelog, procedencia, matriz de compatibilidad y receta de consumo.
6. Publicar fuentes únicamente en privado según autorización. Cada consumidor adopta una versión concreta; no sobrescribir tags ni reemplazar assets publicados silenciosamente.

Mantener recibos sin rutas personales, perfiles ni tokens. Un fallo nuevo en AC7 no se arregla cambiando automáticamente todos los consumidores; estudiar si pertenece al núcleo o al adaptador y publicar una actualización deliberada.

Prueba del ejemplo sin UEVR: `python tests/test_demo.py`. Ver [resultado inicial](VALIDATION.md).

## Entrega 0.1.1

La versión se toma de `VERSION` en ambas entradas CMake y se incrusta en el
recurso Windows de la DLL. Ejecutar `tests/audit_release.py` sobre la fuente
operativa y `scripts/package_release.py --build build --output dist` para
generar el kit con hashes. El empaquetador no sobrescribe entregas existentes.
Auditar una extracción nueva del ZIP con `tests/audit_release.py --kit <ruta>`.
La auditoría busca identificadores de los proveedores retirados en rutas,
fuentes operativas, configuración y bytes ASCII/UTF-16 del binario. No equivale
a una demostración semántica de todas las dependencias posibles; se complementa
con la inspección de imports/exports y las pruebas del artefacto exacto.
Ver [migración y contrato](MIGRATION_0.1.1.md).

El cierre privado conserva la DLL exacta probada. El kit inicial y su recibo
permanecen como evidencia; la entrega de publicación actualiza documentación
y se genera en otra carpeta de salida. Comprobar su nueva huella, equivalencia
de los archivos operativos, extracción completa y auditoría antes de subirla.
La revisión del informe es documental (`ENTREGA_0.1.1_r2.md`); no identifica
otra DLL. Consultar [política privada](RELEASE_POLICY.md).
