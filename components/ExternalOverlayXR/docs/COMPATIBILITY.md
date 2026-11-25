# Compatibilidad y aceptación del módulo 0.1.1

| Combinación | Evidencia | Estado |
|---|---|---|
| Común Windows x64/OpenXR/D3D11 y D3D12 | DLL exacta, 9/9 CTests con cadena sintética y GPU WARP | PASS offline |
| Host y widgets comunes | Servidor real y Edge, autenticación, resolución, Log, Apply/Discard | PASS offline |
| AC7 0.8.5-r2 | ZIP del consumidor incluye la DLL 0.1.1 exacta; Carlos indica «Todo perfecto» el 07/10/2026 | Integración aceptada por el usuario |
| Publicación del Layer 0.1.1 | Carlos da por OK la versión y ordena publicación privada | Cierre autorizado |
| Otros juegos, runtimes o visores | Sin nueva evidencia específica en este cierre | Validación propia pendiente |

DLL aceptada/entregada:
`da4bc917d12ad01e3d869e4492cba6104288178b24d6982919ac0667406fd885`.
Nombre de layer `XR_APILAYER_EXTERNAL_control_overlay`, implementación de
manifiesto 2; Surface, información y resolución ABI v1.

El ZIP AC7 0.8.5-r2 consultado tiene SHA-256
`1faa3e55748fc261a7fbc455a0040568f7412e549203d0d7d9bc5ba723c15f92`.
Su bridge arranca Python/Node/host del perfil mediante rutas relativas, sin
setup ni `host.ini`; las pruebas del consumidor cubren extracción limpia,
otra ruta con espacios/acentos y vida del proceso. Esa solución de arranque
no forma parte de la DLL común.

La aceptación es una declaración de Carlos para esta integración. No se
añaden mediciones de FPS ni una certificación nueva de otras nightly, juegos
o headsets. AC7 mantiene UEVR Nightly 01143 exacta, revisión
`4ee5c6b6162dee2291fc75f9dfc57667f6d45a2d`.

Las dos variantes de generación dejan de ser dependencias internas de AC7.
Si se usan, Carlos indica hacerlo externamente. El Layer común conserva los
contratos normales de encadenamiento OpenXR, sin instalar ni configurar esos
proveedores; el funcionamiento de cada combinación externa debe comprobarse
por separado.
