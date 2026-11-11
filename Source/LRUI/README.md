# LRUI

HUD de prototipo de LEFTRIGHT, dibujado con `UCanvas` desde C++ (sin assets de UMG, ADR-011). Escucha el bus de eventos de `LRCore`, lee el coche del jugador (`ALRVehicle`) y no depende de `LRGameplay`.

Depende de `LRCore`, `LRVehicle` y `SlateCore` (público), y de `Slate`, `InputCore` y `LRInput` (privado, solo para detectar el último dispositivo usado).

## Qué dibuja

| Estado de la partida | Contenido |
|---|---|
| `Garage` | Nada: lo cubre la pantalla de inicio (`SLRTitleMenu`), que abre `ALRPlayerController` |
| `Loading` | "CARGANDO" con puntos animados |
| `Countdown` | Instrumentos, puntos, indicador de esquema, panel de controles y "3, 2, 1, ¡YA!" (duración: `CountdownSeconds` de `Data/Gameplay/GameRules.json`, 3 s si falta) |
| `Running` | Velocímetro (km/h o mph), marcha, barra de RPM con zona roja, multiplicador de velocidad, puntos con separador de miles, combo con barra de tiempo, avisos de near miss, indicador de esquema, panel de controles (primeros 8 s de la primera partida) |
| `Crashed` | Destello rojo y "¡CHOQUE!" sobre los instrumentos |
| `Replay` | Etiqueta "REPETICIÓN" parpadeante |
| `Results` | Panel con puntos, distancia, near misses, mejor combo, velocidad punta, tiempo y créditos, y aviso de reinicio: "R" en teclado, "View / Share" en mando |

Los avisos de near miss muestran la categoría ("CERCA", "MUY CERCA", "¡AL MILÍMETRO!"), las etiquetas "CONTRAMANO" y "¡ENTRE DOS!" y los puntos si se conocen. Entran deslizándose, suben y se desvanecen.

## Clases principales

| Tipo | Archivo | Qué hace |
|---|---|---|
| `ALRHUD` | `Public/LRHUD.h` | El HUD. Se suscribe al bus en `BeginPlay`, se desuscribe en `EndPlay` y dibuja en `DrawHUD`. Todo está maquetado a 1080p y se multiplica por la escala (alto del lienzo / 1080). |
| `FLRHUDLabels` | `Public/LRHUD.h` | Todos los textos fijos (`LOCTEXT`, espacio de nombres `LRHUD`), creados una vez. |
| `FLRHUDFormat` | `Public/LRHUDFormat.h` | Funciones puras de texto: `FormatSpeed`, `FormatScore`, `FormatPoints`, `FormatDistanceKm`, `FormatGear`, `FormatMultiplier`, `FormatDuration`, `GetNearMissTierLabel`, `ParseSpeedUnits`. |
| `FLRHUDAnim` | `Public/LRHUDFormat.h` | Matemática pura de animación y maquetación: curvas, opacidad, contador de puntos, cuenta atrás, escala. |
| `FLRHUDPopupQueue` | `Public/LRHUDState.h` | Cola pura de avisos, de capacidad fija. Empareja cada near miss con sus puntos llegue antes o después el evento de puntuación. |
| `FLRHUDComboTracker` | `Public/LRHUDState.h` | Estado puro del combo; descuenta el tiempo entre eventos para que la barra sea continua. |
| `FLRHUDTuning`, `FLRHUDGameRules` | `Public/LRHUDTypes.h` | Datos ajustables y lectura parcial de las reglas de juego. |
| `FLRHUDInputListener` | `Private/LRHUDInputListener.h` | Preprocesador de Slate que observa las teclas sin consumirlas y alimenta `FLRInputDeviceTracker`. |

## Uso

`LRGameplay` asigna la clase en el modo de juego (`HUDClass = ALRHUD::StaticClass()`). El HUD se entera de todo por `ULRGameEventSubsystem` y por `ULRRunContextSubsystem`.

Para mostrar los puntos en un aviso, el evento `FLRScoreChangedEvent` del near miss debe llevar `Reason = "NearMiss"` y `Delta` con los puntos.

En consola: `lr.HUD.Units mph` o `lr.HUD.Units kmh`. Desde un menú de ajustes: `ALRHUD::SetSpeedUnits`.

## Datos

- `Data/UI/HUD.json` (tipo `HUDTuning`), validado por `Data/Schema/HUDTuning.schema.json`: maquetación, tamaños de letra, tiempos de animación, zona roja, separadores y colores. Los tamaños se expresan a la altura de referencia. `Sanitized()` acota los valores.
- `Data/Gameplay/GameRules.json` (de `LRGameplay`): solo se lee `CountdownSeconds`. Debe coincidir con la cuenta atrás real de la partida.

## Cómo extender

1. **Nuevo texto:** añádelo a `FLRHUDLabels` y créalo en `Build()` con `LOCTEXT`. Nunca llames a `LOCTEXT` dentro de una función de dibujo.
2. **Nuevo número en pantalla:** añade un `FText` en caché con su valor en `ALRHUD` y reconstrúyelo solo cuando cambie, en `RefreshCachedTexts` o en el manejador del evento.
3. **Nuevo parámetro:** añade el `UPROPERTY` con valor por defecto en `FLRHUDTuning`, acótalo en `Sanitized()` y añádelo al JSON y al esquema (`additionalProperties` es `false`).
4. **Nueva lógica de presentación:** va como función pura en `LRHUDFormat.h` o `LRHUDState.h`, con su test.
5. **Avisos de botones:** si cambia un mapeo en `ULRInputActions`, actualiza las filas de `FLRHUDLabels::Build()` y los textos de reinicio.

La UI definitiva con CommonUI sustituirá a este HUD manteniendo el mismo bus de eventos.

## Rendimiento

- Presupuesto: < 0,3 ms por fotograma en el hilo de juego. Se mide con `TRACE_CPUPROFILER_EVENT_SCOPE(ALRHUD::DrawHUD)`.
- Sin asignaciones por fotograma, salvo al reconstruir un `FText` cuyo valor cambia. Los textos fijos se crean una vez, y `lr.HUD.Units` se guarda en una variable mediante su callback para no copiar una cadena en cada fotograma.
- Las fuentes (`GEngine->GetLargeFont` con caché en tiempo de ejecución) se reconstruyen solo si cambia la escala.
- La cola de avisos es de capacidad fija y sus huecos se reservan al configurarla.

## Tests

- `LeftRight.UI.Format`: formato de velocidad, puntos, distancia, marcha, multiplicador, etiquetas y animación.
- `LeftRight.UI.State`: cola de avisos y emparejamiento de puntos, seguimiento del combo.
- `LeftRight.UI.Data`: carga de `HUD.json`, valores por defecto, `Sanitized`, `GameRules` y textos.
