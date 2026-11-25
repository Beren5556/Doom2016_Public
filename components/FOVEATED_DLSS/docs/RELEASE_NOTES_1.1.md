# FoveatedDLSS 1.1

Versión aceptada por Carlos el 12 de octubre de 2026: «La damos por buena».
Motor 1.1.0; API C++ 4; esquema de configuración 3, con lectura de 1/2/3;
contrato de control 2 para ExternalOverlayDLSS. Tag: v1.1.0. Repositorio privado.

La región de foveado se define mediante cuatro porcentajes independientes:
superior, inferior, derecha e izquierda. Representan un único contorno en el
plano común de los dos ojos solapados. 20/20/20/20 produce un círculo;
20/20/0/0, una elipse horizontal. Se conservan forma, transición, ajustes de
calidad, periferia, región Neural enlazada/independiente e indicador.

El menú nuevo retira los bloqueos, ratio/reparto, ancho/alto dependientes y
centros manuales por ojo. El host proporciona los mapas de proyección binocular;
los ajustes antiguos mantienen su geometría hasta aplicar el nuevo borrador.

## Paquetes privados

| Archivo | Contenido |
|---|---|
| FoveatedDLSS-1.1.0-Core-win-x64-Release.zip | Configuración, planificación y geometría, sin dependencia NVIDIA |
| FoveatedDLSS-1.1.0-D3D11-win-x64-Release.zip | Core y operaciones/pipeline D3D11 |
| FoveatedDLSS-1.1.0-NGX-win-x64-Release.zip | Core, D3D11 y backend NGX; conserva el aviso NVIDIA |
| SHA256SUMS.txt | Huellas de los tres ZIP anteriores |

Son paquetes de desarrollo con librerías estáticas, cabeceras, exports CMake,
documentación y consumidor independiente. Usar C++20/MSVC x64 y
`find_package(DlssFoveado 1.1 CONFIG REQUIRED)`. NGX requiere el SDK como input
de desarrollo; está conservado con su licencia en este repositorio privado.
El runtime NGX y el proveedor/modelo Neural los suministra el host.
Las fuentes completas, shaders, scripts, pruebas, dependencias de desarrollo y
evidencias están en el tag y sus archivos de código fuente de GitHub.

Estos ZIP no son perfiles UEVR ni plugins instalables directamente en un juego.
La implementación de menú/puente pertenece a ExternalOverlayDLSS; cada host
integra después las versiones compatibles. Contrato detallado:
[CONTROL_CONTRACT_2.0.md](CONTROL_CONTRACT_2.0.md).

## Verificación

Core: 2/2 CTest; D3D11 y NGX: 4/4. Consumidor independiente compilado y ejecutado
contra cada ZIP extraído: 1/1 por paquete. CPU/WARP comprueban geometría,
solape, recorte/composición/indicador y pipeline estéreo con Neural antes/después.
Los binarios de la release son exactamente los identificados en
`evidence/packages-1.1.0.json`; no se recompilan para esta publicación.

La aceptación de la versión y su publicación no acreditan pruebas físicas nuevas
en un visor. La calibración, inferencia real y validación del host siguen siendo
parte de su integración. No se modifica la instalación aceptada de BL3.
