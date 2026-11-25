# Contrato del adaptador y resolución

## Extensión Vulkan experimental local

La rama `codex/vulkan-pilot` sobre v0.1.1 añade Vulkan/Vulkan2; no sustituye una
release publicada ni certifica un visor. ABI1 intacta. Backend100 se presenta como
Vulkan en el host. El build necesita `VULKAN_INCLUDE_DIR`, sólo headers; carga
`vulkan-1.dll` desde System32 y toma prestados dispositivo/cola de la sesión.

El consumidor debe serializar todas sus operaciones sobre esa cola, incluido
`xrEndFrame`, con la misma exclusión Vulkan. El mutex interno no protege los
envíos del juego. Dispositivo/cola deben vivir hasta destruir la sesión/layer.
Se usa BGRA8 sRGB, staging coherente y fence privados. EndFrame consulta la
finalización sin esperar y libera la imagen XR una sola vez. Una subida pendiente
puede retrasar la primera presentación; nunca se publica antes de completar.
En fallo de cola se detiene la reutilización; la destrucción retira GPU y puede
esperar. No crea otro dispositivo ni incorpora código de un consumidor.

La prueba usa OpenXR simulado y verifica píxeles en GPU real. Las pruebas D3D11 y
D3D12 permanecen. El runtime real, bootstrap del manifiesto/host y sincronización
del consumidor necesitan su propia adopción; compilar no activa el layer.

## Autoridad única

El adaptador recibe solicitudes, comprueba estado y llama al motor/mod. No mantiene un INI rival ni delega writes sin validación al navegador. Un cambio realizado desde el menú nativo debe aparecer en las siguientes lecturas. Una selección solicitada, una salida ya renderizada y un valor persistido se publican por separado.

El servidor llama a `transport.exchange(request)`. Las excepciones se traducen a error HTTP; también se admite un objeto con `error`. El transporte debe limitar plazos y mensajes y no repetir mutaciones tras timeout. AC7 implementa una petición por conexión named pipe y caducidad GetTickCount64; otro motor puede usar otro transporte siempre que conserve esas garantías.

## Rutas

| POST | Uso |
|---|---|
| `/api` | Objeto de protocolo específico del consumidor |
| `/output/state` | `exchange({op: output_state})`, con vistas presentadas añadidas por el host |
| `/output/apply` | Solicitud validada, reenviada como output_apply |
| `/xr/info` | Información común fresca de la superficie |
| `/xr/resolution` | Vistas de proyección observadas |
| `/xr/input` | Eventos de entrada y vida del proceso |
| `/xr/frame` | Publicación PNG del panel |
| `/xr/close` | Contador Close; no termina el juego |

Todas las rutas de control exigen token y las comprobaciones de origen del host. El cuerpo de órdenes está limitado a 16000 bytes. Las rutas XR necesitan una superficie suministrada al servidor.

## output_state v1

Campos operativos: `protocol=1`, `session` string, `available`, `canWrite`, `canSave`, `authority`, `width`, `height`, `minWidth`, `maxWidth`, `step=50`, `expected`, `saved`, `saveRequested`, `restartRequired`, `reason`. Width/height son la selección leída de la autoridad; `submittedViews` procede separadamente de la capa. Valores positivos enteros. Publicar capacidades verdaderas: si no hay setter válido, `canWrite=false` y un motivo.

`expected` es un token opaco de comparación. Debe cambiar al variar la selección, geometría relevante o sesión. No devolver siempre `1` ni basarlo exclusivamente en un contador HTTP. `session` identifica la vida del receptor; una reconexión no permite reproducir órdenes de una sesión anterior. Si el motor tiene recreaciones internas, incluir también su identidad en el token.

## output_apply v1

Ejemplo de petición: `{"session":"engine-12","expected":"r28","width":3350,"save":true}`. El adaptador valida sesión y token contra lectura fresca; tipos, límites y capacidad; y el punto seguro de ejecución. Relee o usa compare-and-swap si puede cambiar entre comprobación y escritura. El alto se deriva con la geometría real del motor, no de una relación supuesta universal.

Éxito de selección: `{"submitted":true,"effective":false,"selection":{...nuevo output_state...}}`. La respuesta necesita la nueva revisión y las dimensiones que realmente seleccionó el motor. `effective=false` evita confundir la respuesta con la recreación del render; las vistas presentadas confirman qué está saliendo. `saved=true` sólo tras confirmación de persistencia real. «Save requested» no equivale a «saved».

## Widget y acción compartida

La barra y ±50 modifican el mismo borrador. Los pasos se anclan al ancho original incluso si no es múltiplo de 50. El preview mantiene proporción, pero el readback tiene autoridad sobre el redondeo. No crear swapchains mientras se arrastra el slider.

1. Capturar `request = overlayResolution.capture(save)`.
2. Ejecutar `await overlayResolution.preflight(request)` antes de cualquier escritura de cualquier grupo.
3. Validar los otros grupos y aplicar según su autoridad. Si un grupo falla, detener los siguientes y describir lo completado.
4. Ejecutar `await overlayResolution.apply(request)` y presentar la confirmación.
5. `dirty` controla Apply/Discard; `availability(busy)` bloquea edición durante órdenes; `discard()` restaura la selección vigente. Escuchar `outputdraftchange`.

Sin borrador, lecturas externas sincronizan controles. Con borrador, se conserva lo editado y se rechaza una base obsoleta. Una respuesta incierta no se reenvía: leer estado, reconciliar si demuestra la selección solicitada y conservar la incertidumbre si no. Una revisión diferente con el ancho esperado permite reconocer confirmación tardía sin repetir la orden. No llamar al mismo setter de nuevo como comprobación.

## Parámetros adicionales

El módulo no define los parámetros de render de los juegos. Puede usarse `{op:state}` con `protocol, session, fields` y `{op:command, event, data, expected}`, como AC7, o un contrato propio documentado. Mantener siempre base comparada, límites, respuesta solicitada/efectiva/persistida y errores visibles. La UI no debe ofrecer opciones que el motor no implementa.

El ejemplo incluido usa un motor en memoria para mostrar el cableado completo. No debe copiarse su almacenamiento simulado como implementación de un motor real. Ver también los ejemplos y campos completos de la guía original de AC7.
