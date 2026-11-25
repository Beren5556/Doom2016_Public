# Diagnóstico sin alterar el proyecto consumidor

| Síntoma | Comprobar primero | Acción justificada |
|---|---|---|
| No aparece panel | Capa habilitada antes de instancia, mismo PID, host activo, frame/tick | Corregir integración de arranque; no cambiar runtime por ensayo |
| Funciona en el PC de desarrollo pero falla en primera instalación | Manifiesto y DLL existentes antes del bootstrap, `library_path`, ajustes de activación y perfil residual | Reproducir desde el paquete final en perfil vacío; desplegar archivos antes de cargar el plugin si el bootstrap está en `DllMain`. Ver [orden de primera instalación](FIRST_INSTALL_AND_BOOTSTRAP.md) |
| Host no inicia | Rutas, dependencias, permisos, Edge, estado de habilitación | Rehacer sólo configuración propia del host con juego cerrado |
| Panel desaparece | HostTick/estado, foco, reinicio de generación, presión del ring | Revisar logs propios y vida del proceso; no reproducir clics |
| Puntero sin clic | Perfil activo, binding resultado, aim, gatillo armado y sync | Verificar acciones reales; no identificar marca por un perfil emulado |
| Scroll Vive no responde | Contacto del trackpad y umbral | Mantener comprobación touch; no mapearlo como thumbstick a ciegas |
| Apply bloqueado | Sesión fresca, capacidades, fallo nativo, petición pendiente | Leer estado y resolver el motivo, sin reenvío automático |
| Conflicto de borrador | Cambio en UI nativa o recreación de sesión | Conservar borrador, comparar, ofrecer Discard/revisión |
| Selección distinta de vistas | Transición de recursos, capas posteriores, viewport real | Mostrar ambas; comprobar autoridad/orden de capas |
| Guardado pendiente | Acknowledgement real de persistencia | Esperar/leer; HTTP 200 no demuestra escritura a disco |
| Proyección ambigua | Más de una projection layer o multivista | Publicar bandera y limitar escritura según adaptador |
| Otra instancia del host | Mutex .Host existente | Rechazar duplicado; no reiniciar el host válido |

## Evidencias mínimas útiles

Recoger versión/commit/hash del módulo y consumidor, API gráfica, system/runtime, perfiles activos, resultados de sugerencias, flags de entrada, edad de información, vistas recomendadas/presentadas, estado de la orden y generación/sesión. No recoger un perfil entero ni logs personales si basta una publicación acotada. No incluir tokens HTTP o credenciales.

En AC7, las fuentes de estado pertenecen a su bridge/renderer y el nombre de log lo decide su host. En otro motor no inventar los mismos nombres. Las capturas de panel sirven para UI; no prueban ubicación pre-TAA, calidad visual por ojo o FPS.

AC7 0.8.5-r2 arranca el host desde rutas relativas a su DLL sin setup ni
`host.ini`. Ante un log que pida ese archivo, comprobar primero la identidad
del bridge y el ZIP utilizados: corresponde al arranque previo. No crear
un INI manual para ocultar que el paquete/bridge de la versión actual no se
ha instalado. Ver [migración 0.1.1](MIGRATION_0.1.1.md).

## Límites de recuperación

La aplicación mantiene sus recursos y sesión. El panel puede ocultarse y el host puede terminar; no se fuerza cierre del juego. Ante un archivo propio modificado después de instalar, preservar y revisar antes de rollback. No tocar configuraciones NVIDIA, shaders o mods ajenos para reparar un fallo de interfaz.

Las nubes/horizonte de AC7 tienen una investigación separada iniciada tras aceptar 0.8.3-r2. La observación de Carlos indica que también aparece con el perfil UEVR básico. No atribuirla al módulo ni anunciar solución sin evidencia; no mezclar esa investigación con la estabilidad de esta extracción.
