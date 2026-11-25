# Adopción de ExternalOverlayXR 0.1.1 y retirada de generación en AC7

07/10/2026. Petición de Carlos: una nueva versión del Layer totalmente libre
de OFXR en sus dos variantes; avisar a «AC7 VR 0.8.4» cuando esté preparada para
que cree la tarea 0.8.5 y adapte AC7 eliminando también ambos proveedores.

## Resultado del análisis

Base común verificada contra GitHub privado: `Beren5556/ExternalOverlayXR`,
commit `25fba663cabd6d19671bfae083da4ea32c4d0e96`, posterior a v0.1.0.
Ese árbol **ya no contiene motores, controles, configuración ni carga de
proveedores de generación**. Se retiraron cinco referencias textuales de
código/documentación. El código de resolución de funciones globales OpenXR
se conserva: es un contrato genérico de la cadena, no un motor de generación.

En la base AC7 0.8.4 los proveedores reales eran **OFXR Classic 0.2.1 / V116** y
**OFXR Fork Djules75 0.2.10.1 / V412**. Cambiar sólo la DLL del panel no los
retira del consumidor. La comprobación de la 0.1.1 acredita únicamente el
común y su kit; no presentar AC7 0.8.4 como limpio ni instalado con esta versión.

## Contrato de 0.1.1

| Elemento | Identidad |
|---|---|
| Módulo y host | 0.1.1 |
| Nombre del layer | `XR_APILAYER_EXTERNAL_control_overlay`, conservado |
| Manifiesto | `ExternalOverlayXR.json`, implementation_version 2 |
| DLL | `ExternalOverlayXR.dll`, recurso de versión 0.1.1 |
| Surface / información / resolución | ABI v1; estructuras y offsets conservados |
| Gesto, mandos, composición | Código funcional conservado |
| Fuente | Desarrollo en `codex/layer-0.1.1-no-ofxr`; cierre en main y `v0.1.1` |
| Publicación | Repositorio privado `Beren5556/ExternalOverlayXR`, main y tag `v0.1.1`; Carlos autoriza el cierre el 07/10/2026 |
| Kit publicado | `ExternalOverlayXR-0.1.1.zip`, asset de la release privada; identidad por `SHA256SUMS.json` y `publication-0.1.1.json` |

Usar la huella de la DLL en `docs/validation/release-0.1.1.json` y el informe
`ENTREGA_0.1.1_r2.md`. La huella del ZIP inicial se conserva en el informe y
recibo originales; el ZIP de publicación actualiza documentación y tiene una
identidad propia. La DLL no cambia. No reutilizar el hash aceptado del origen 0.1.0.

El kit contiene DLL, manifiesto, host, widgets, ejemplo, contratos, lista de
dependencias y licencias. No es un perfil AC7 ni distribuye Python, Node,
Playwright, Pillow o Edge. El consumidor debe mantener su empaquetado y setup
de esas dependencias. Conservar manifiesto y DLL antes del bootstrap del plugin.

## Integración AC7 realizada

Carlos aceptó AC7 0.8.5-r2 con «Todo perfecto» el 07/10/2026 y autorizó
después el cierre privado de este módulo. El ZIP de ese consumidor contiene
la DLL 0.1.1 con SHA-256
`da4bc917d12ad01e3d869e4492cba6104288178b24d6982919ac0667406fd885`.
La coincidencia se ha comprobado leyendo directamente el ZIP 0.8.5-r2.

La retirada interna de ambos proveedores se implementa en el consumidor.
La indicación de uso vigente de Carlos es que OFXR, si se desea, se use
externamente. El común conserva el encadenamiento normal de capas OpenXR;
no instala ni habilita un proveedor externo y su compatibilidad física
particular necesita evidencia propia.

El bridge de AC7 0.8.5-r2 localiza Python, Node y el host dentro del perfil
mediante rutas relativas a su DLL. Arranca automáticamente, sin setup ni
`host.ini`; se comprobó desde un ZIP limpio y otra ruta con espacios/acentos.
Ese código vive en AC7, no en el núcleo común. Al portar la integración,
trasladar esa responsabilidad al adaptador del nuevo consumidor y validar
que publica píxeles, evita duplicados y termina al salir su proceso.

El mapa siguiente documenta lo que se trasladó a la tarea 0.8.5 desde la
base 0.8.4; sus rutas no representan archivos aún presentes en el consumidor
actual. Conservar la evidencia inicial y las releases históricas.

## Mapa de retirada para la tarea AC7 0.8.5

Inspección sobre la fuente privada de AC7 0.8.4,
commit `786d64837758524383ad040a0c10b2327d6477ac`. Las rutas de esta tabla son
relativas al repositorio **AC7VRDLSS**, no al repositorio del módulo.

| Área | Elementos que revisar/retirar en 0.8.5 |
|---|---|
| Bootstrap y negociación | `src/OfxrBootstrap.cpp`, `.h`; carga temprana, manifiesto privado, registro de proveedores y exports exclusivos |
| Funciones compartidas del bootstrap | Conservar y trasladar la inicialización legítima del módulo y `NVIDIA_DLSS_PrepareProfile`/extracción de dependencias si el consumidor sigue usándolas; no borrar DllMain a ciegas |
| Configuración y contratos | `src/OfxrSettings.h`, `OfxrContract.h`, `OfxrReceipt.h`, `tools/OfxrConfig.cpp`; secciones Classic/Fork y opciones heredadas |
| Render y estados | `src/DirectTemporal.h`, `FlightGate.h`, `NVIDIA_DLSS.cpp`; recibos, elegibilidad, estados, callbacks y textos; conservar historiales DLSS/Neural y cambios entre pares |
| Menú UEVR | `scripts/NVIDIA_DLSS.lua`; campos, Apply/Save, eventos y respuestas exclusivos de ambos proveedores |
| Overlay consumidor | `experimental/external-overlay/native/Bridge.cpp`, `AC7_ExternalOverlay.lua`, `web/adapter.js`, `web/index.html`, `build_ui.py` y plantilla `reference/approved.html`; impedir que regenerar la UI reponga controles |
| Compilación | CMake raíz/overlay, includes OpenXR obtenidos actualmente de `third_party/ofxr-djules75/external/OpenXR-SDK/include`; apuntar a headers independientes |
| Payload y distribución | Revisar `tools/pack.py`, inputs `build-bundle`, recursos incrustados y selección de payload; no incluir DLLs, carpetas, manifiestos, INIs, licencias o fuentes correspondientes exclusivamente a los proveedores retirados |
| Ajustes/instalación | `config/settings.ini`, documentación y generadores; migrar sólo claves/archivos propios con recuperación. No borrar componentes de terceros ni modificar registro/backend global |
| Pruebas | Retirar tests exclusivos del motor eliminado y adaptar los mixtos (`DlssAdapterSmoke`, `VRModeSmoke`, `BundleSmoke`, `PluginHost`, `PanelLuaSmoke`, bridge/host/pipe/UI). Mantener o sustituir la cobertura de render que siga siendo necesaria |
| Documentación | Guías de configuración, instalación, solución de problemas y licencias de la nueva release; conservar releases/tags/evidencia históricos identificados como tales |

El `src/DirectTemporal.h` inspeccionado de 0.8.4 emitía recibos de pares, y el
bootstrap alojaba también una función de preparación del perfil. Esta retirada
requiere separar responsabilidades; no es suficiente borrar nombres o poner
Enabled=false. Los scripts de empaquetado y plantillas deben quedar limpios
antes de generar la DLL y el perfil 0.8.5 finales.

## Criterios de aceptación de AC7 0.8.5

- Adoptar exactamente el nuevo kit/huella, conservando configuración, shaders,
  mods ajenos y la instalación anterior recuperable.
- Ambas variantes ausentes de fuentes operativas, controles, esquema de
  configuración, eventos, carga, manifiestos, recursos incrustados y paquete.
- Primera instalación en perfil vacío y migración desde 0.8.4; ninguna variante
  vuelve a extraerse aunque estuviera activada en la configuración antigua.
- DLSS/DLAA/Neural, preset, escalas, Apply/Save, resolución, mandos, overlay,
  nubes y herramientas existentes conservan sus contratos; no alterar el
  pendiente privado de ocultación de cabina como parte de esta retirada.
- Compilar y probar la DLL final del consumidor. Los tests del común no
  sustituyen la batería del render ni una sesión física coordinada con Carlos.
- Mantener UEVR Nightly 01143 exacta, revisión
  `4ee5c6b6162dee2291fc75f9dfc57667f6d45a2d`. Sin juego/VR autónomos, publicación
  ni cambios a otros consumidores.

## Reproducir la entrega del común

Compilar y ejecutar los nueve CTests como explica `BUILD_TEST_RELEASE.md`.
Ejecutar `python tests/test_demo.py` con las dependencias fijadas; después:

```powershell
python tests/audit_release.py --receipt test-output/source-audit.json
python scripts/package_release.py --build build --output dist
python tests/audit_release.py --kit dist/ExternalOverlayXR-0.1.1 --receipt test-output/delivery-audit.json
```

El empaquetador rechaza sobrescribir una entrega existente. Auditar también
una extracción nueva del ZIP. El informe debe distinguir código compilado,
host/navegador, pruebas OpenXR sintéticas, kit íntegro e integración física.
