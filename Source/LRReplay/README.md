# LRReplay

Repetición de la partida: guarda en un búfer circular los últimos segundos (60 s a 30 Hz por defecto) del coche del jugador y del tráfico cercano, y permite consultar cualquier instante con interpolación. La reproducción visual (actores fantasma, cámaras cinemáticas, modo foto) todavía no está implementada: este módulo solo graba y evalúa.

## Clases principales

| Tipo | Archivo | Qué hace |
|---|---|---|
| `FLRReplaySettings` | `Public/LRReplayTypes.h` | Ajustes del grabador: duración (s), frecuencia (Hz), radio del tráfico (m) y máximo de vehículos por fotograma. |
| `FLRReplayFrame` | `Public/LRReplayTypes.h` | Un instante grabado: `Time`, `PlayerTransform`, `PlayerRoadCoord`, `PlayerSpeedKmh`, `EngineRpm`, `SteeringInput`, `ThrottleInput`, `BrakeInput`, `Gear` y `Traffic` (instantáneas `FLRTrafficVehicleSnapshot` ordenadas por `Id`). |
| `FLRReplayBuffer` | `Public/LRReplayBuffer.h` | Lógica pura (sin `UObject` ni mundo). Búfer circular con capacidad fija, preasignado en `Configure`. `Add`, `Clear`, `Num`, `Duration`, `GetFrame` (orden cronológico) y `EvaluateAt` (interpolación). |
| `FLRReplaySampleClock` | `Public/LRReplayBuffer.h` | Reloj de muestreo a frecuencia fija. Tras un tirón descarta el exceso: no genera ráfagas de fotogramas repetidos. |
| `ULRReplaySubsystem` | `Public/LRReplaySubsystem.h` | `UTickableWorldSubsystem` (solo Game y PIE). Graba automáticamente según el estado de la partida y expone `EvaluateAt`. |

### Comportamiento automático de `ULRReplaySubsystem`

- `OnRunReset`: para la grabación y vacía el búfer.
- Estado `Running`: empieza a grabar (o reanuda sobre el mismo búfer si ya tenía fotogramas).
- `Crashed` o cualquier otro estado: para la grabación y conserva el búfer para el estado `Replay`.

Cada muestra copia la transformación del jugador (si el peón es un `ALRVehicle`, también velocidad, rpm, dirección aplicada, acelerador, freno y marcha) y pide a `ULRTrafficSubsystem::GetSnapshot` los vehículos a ±250 m en `s`. Si hay más de 64, se quedan los más cercanos en `s`.

### Interpolación (`EvaluateAt`)

- `TimeFromStart` se mide desde el fotograma más antiguo y se limita a `[0, Duration()]`.
- Posición y escala del jugador con *lerp*; rotación con *slerp* (camino más corto, sin saltos en ±180°).
- Escalares (velocidad, rpm, entradas) con *lerp*. La marcha y las luces se toman del fotograma más cercano.
- Tráfico: emparejado por `Id` con una mezcla lineal de las dos listas ordenadas. Un vehículo que solo aparece en un fotograma se muestra mientras ese fotograma sea el más cercano (alpha < 0,5 o alpha >= 0,5).

## Datos

- `Data/Replay/Replay.json` (tipo `ReplaySettings`) es **opcional**: si no existe se usan los valores por defecto de C++ (`DurationSeconds` 60, `SampleRateHz` 30, `TrafficRadiusMeters` 250, `MaxTrafficVehicles` 64). Todavía no hay archivo ni esquema en `Data/` (ver la lista de pendientes).

## Cómo extender

- **Nuevo dato por fotograma** (por ejemplo, luces del jugador o estado de ruedas): añade el campo a `FLRReplayFrame` con valor por defecto, cópialo en `CopyScalars` e interpólalo en `InterpolateFrames` (`LRReplayBuffer.cpp`), y rellénalo en `ULRReplaySubsystem::CaptureFrame`.
- **Reproducción:** un consumidor (por ejemplo, un `ALRReplayDirector` futuro) llama a `EvaluateAt` cada fotograma con un `FLRReplayFrame` reutilizado y coloca actores fantasma con `ULRRoadSubsystem::RoadToWorld(S, D)` para el tráfico y `PlayerTransform` para el jugador.
- **Cámara lenta:** basta con avanzar `TimeFromStart` más despacio; la interpolación es continua.

## Tests

`Private/Tests/LRReplayBufferSpec.cpp` (`LeftRight.Replay.Buffer.*`), sin mundo:

- orden cronológico tras dar la vuelta al anillo y rechazo de tiempos no crecientes;
- duración con 0, 1 y N fotogramas, y saturación a la capacidad;
- puntos medios de posición, rotación (incluido el paso por ±180°) y escalares, y extremos limitados;
- emparejamiento del tráfico por `Id`, vehículos que aparecen o desaparecen, y límite por fotograma;
- memoria: `GetAllocatedSize()` no crece tras el calentamiento y los arrays se reutilizan;
- reloj de muestreo: 30 Hz estables y sin ráfagas tras un tirón.

## Presupuesto de rendimiento

- **CPU:** < 0,1 ms por tick de juego (una muestra cada 33 ms: copia de unos 64 × 48 bytes y una ordenación in situ). Medido con `TRACE_CPUPROFILER_EVENT_SCOPE(ULRReplaySubsystem::Tick)` y `ULRReplaySubsystem::CaptureFrame`.
- **Memoria:** 1800 fotogramas × 64 vehículos ≈ 5,5 MB, reservados una sola vez al iniciar el mundo. Sin asignaciones tras el calentamiento (el array auxiliar de tráfico solo crece hasta el pico de vehículos dentro del radio).
- `EvaluateAt`: búsqueda binaria O(log n) y mezcla O(m) del tráfico; sin asignaciones si se reutiliza el fotograma de salida.
