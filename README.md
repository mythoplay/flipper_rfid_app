# FlippeRFID (Flipper Zero)

App externa para Flipper Zero con:

- UI visual (menu, vista de capturas y escritura de tag)
- ciclo de inventario periodico
- guardado de tags en SD
- arquitectura por modulos (`driver` comun + implementaciones)

## Estructura

- `application.fam`: metadata de la app
- `fm504_rfid_app.c`: UI y flujo principal (usa interfaz de driver)
- `rfid_driver.*`: interfaz comun para modulos RFID (escalable)
- `RfidModuleFm504`: modulo funcional actual
- `RfidModuleRe40` (Zebra): base integrada, comandos aun no implementados
- `fm504_uart.*`: transporte UART (stub)
- `fm504_protocol.*`: validacion/parsing/comandos (placeholder)
- `fm504_reader.*`: implementacion FM504 (inventory/write)
- `storage_tags.*`: persistencia en `/ext/apps_data/fm504_rfid/tags.txt`

## Arquitectura escalable

- La UI no llama directo a `fm504_*`.
- La UI usa `rfid_driver_*` (capa comun).
- La app permite seleccionar modulo desde UI (`FM504` o `RE40`).
- Para agregar otro lector, implementa su bloque en `rfid_driver.c` y no hace falta tocar pantallas.

## Mapeo de pines FM504 -> Flipper

1. `GND reader` -> `GND Flipper`
2. `TX reader` -> `RX Flipper`
3. `RX reader` -> `TX Flipper`
4. `EN` -> `PA7` del Flipper (`gpio_ext_pa7`, pin 2 del header GPIO, linea MOSI)
5. `VCC reader` -> fuente externa (recomendado en UHF)

### Control de energia por software (`EN`)

- `Start Scan`: pone `EN` en HIGH, espera estabilizacion y arranca lectura.
- `Stop Scan`: pone `EN` en LOW.
- Al salir de la app o cambiar de modulo: `EN` vuelve a LOW.
- En `Write Tag`: habilita temporalmente `EN` para escribir y luego lo deshabilita.

## Importante

Con el proyecto Python validado (`FM-503-UHF-RFID-Reader-main`) se ajusto la app al modelo de comando ASCII:

- formato general: `<LF><CMD><args><CR>`
- lectura EPC (single): `<LF>R1,0,<words><CR>`
- lectura TID: `<LF>R2,<addr>,<words><CR>`
- inventario multi EPC: `<LF>U<max><CR>` (referencia, no implementado aun)
- ACK de escritura/lock: respuesta contiene `<OK>`

Actualmente en la app:

- `FM503`/`FM504`/`FM505` suelen compartir familia de comandos, pero valida en tu firmware real.
- `fm504_uart.c` sigue como stub para no bloquear compilacion/flujo UI.
- menu principal:
  - `Scan`
  - `Write Tag`
  - `Module`
  - `Read Mode`
  - `TX Power`
  - `Read Rate (ms)`
  - `Acerca de`
- pantalla `Scan` compacta:
  - cabecera: `Scan > <MODE> <PWR>dB <RATE>ms`
  - lectura en area grande
  - botones: `Save`, `Start/Stop`, `Clear`
- EPC parseado desde respuesta `R` asumiendo formato `CRC16 + PC + EPC`
- TID parseado desde respuesta `R` como bloque hex
- `W` exacto puede variar segun firmware; hoy va con `W1,2,<len_words>,<epc_hex>`.

Debes completar estas funciones según el manual del FM-504:

1. `fm504_uart_open`, `fm504_uart_send`, `fm504_uart_read`
2. validar `fm504_protocol_make_write_epc_cmd` contra traza real del FM504
3. si usas `U` multi-tag, agregar parser de lista por lineas

## Flujo en la app

1. `Iniciar escaneo`: activa inventario periodico
2. `Ver capturas`: muestra EPC o TID segun modo seleccionado
3. `Escribir EPC`: ingresa HEX y envia escritura
4. `Guardar capturas`: guarda CSV simple en SD
5. `Limpiar capturas`

## Build (ufbt)

Si tienes `ufbt`:

```bash
cd /Users/fernando/Source_Code/FlipperZero_RFID/flipper_rfid_app
ufbt build
ufbt launch
```

## Siguiente paso recomendado

Primero integra UART real y confirma que recibes una trama cruda del FM-504.
Con una captura real de respuesta, ajusta parser EPC/TID
