# Dependencias, licencias y procedencia

La licencia MIT raíz cubre el trabajo original de este módulo y su documentación. Las dependencias mantienen sus propios avisos; no se les aplica una licencia nueva por incluirlas.

- Khronos OpenXR headers: incluidos en `third_party/openxr`, con sus textos de licencia. La capa nativa enlaza bibliotecas del SDK Windows/D3D; Windows y su SDK no se redistribuyen aquí.
- CPython/Python Software Foundation: dependencia del host, no runtime incluido en este repositorio fuente. Conservar su LICENSE y avisos al empaquetarlo.
- Pillow: dependencia fijada en requirements.txt. Conservar licencia de Pillow y las de sus bibliotecas nativas al redistribuir.
- Node.js: dependencia del renderer. Conservar el LICENSE de la versión exacta, incluidas sus dependencias incorporadas.
- Playwright y Playwright Core: versión fijada en package.json, Apache-2.0 y avisos propios. Conservar LICENSE/NOTICE y ThirdPartyNotices cuando se empaqueten.
- Microsoft Edge: navegador instalado por separado, no incluido ni relicenciado por este proyecto.

La extracción procede de AC7 0.8.3-r2. Los contratos de mandos se apoyan en la implementación de Rogue Trader cuya prueba física en Pimax con mandos propios y Pico 4 confirmó Carlos. Esa procedencia no convierte este proyecto en un paquete de Rogue Trader ni acredita cada integración futura. Consultar provenance.json y las guías de dispositivos.

No se incluyen aquí UEVR, NVIDIA DLSS, Neural, MinHook, motores de generación de fotogramas, perfiles AC7 ni el acceso interno a UEVR. Un consumidor que incorpore otros componentes debe conservar y cumplir sus avisos por separado. Este módulo no recibe autorización para redistribuir cualquier binario del consumidor.
