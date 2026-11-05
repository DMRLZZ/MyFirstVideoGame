# LRInput

Módulo de entrada de LEFTRIGHT: **Enhanced Input construido en tiempo de ejecución** (sin archivos `.uasset`), con mapeos de teclado **y** mando para todas las acciones, detección de cambio de carril para el esquema Arcade y seguimiento del último dispositivo usado para los avisos de botones del HUD.

Depende de `LRCore`, `EnhancedInput` e `InputCore`. No depende de ningún otro módulo de juego: el `ALRPlayerController` (`LRGameplay`) es quien lo usa.

## Clases principales

| Tipo | Archivo | Qué hace |
|---|---|---|
| `FLRInputTuning` | `Public/LRInputTypes.h` | Parámetros ajustables (zonas muertas, curva de dirección, umbrales de cambio de carril). Se carga de `Data/Input/Input.json`. `Sanitized()` los acota a rangos seguros. |
| `ELRInputDevice` | `Public/LRInputTypes.h` | `KeyboardMouse` o `Gamepad`. |
| `ULRInputActions` | `Public/LRInputActions.h` | `Create(Outer, Tuning)` crea con `NewObject` las 14 `UInputAction` y los contextos `DrivingContext` y `MenuContext`. También `LoadTuning`, `ApplyTuning`, `GetAllActions` y `GetPromptKey` (tecla a mostrar en el HUD según el dispositivo). |
| `FLRLaneChangeDetector` | `Public/LRLaneChangeDetector.h` | Lógica pura: convierte el eje de dirección en peticiones de cambio de carril (−1/0/+1) con histéresis y repetición. |
| `FLRInputCurves` | `Public/LRInputCurves.h` | Funciones puras: zona muerta, curva de respuesta, cadena completa de stick y gatillo, `ClampSteer`. |
| `FLRInputDeviceTracker` | `Public/LRInputDeviceTracker.h` | Lógica pura: último dispositivo usado (con `FKey::IsGamepadKey`), con umbral para ignorar la deriva de los sticks. |

## Mapeos

| Acción | Tipo | Teclado | Mando |
|---|---|---|---|
| Throttle | Axis1D | W, ↑ | Gatillo derecho (zona muerta de gatillo) |
| Brake | Axis1D | S, ↓ | Gatillo izquierdo (zona muerta de gatillo) |
| Steer | Axis1D (acumulativo) | D, → = +1; A, ← = −1 | Stick izquierdo X (zona muerta, curva, inversión opcional) |
| Handbrake | Bool (mantener) | Espacio | Botón derecho (B / Círculo) |
| LaneLeft / LaneRight | Bool (pulsar) | Q / E | Cruceta izquierda / derecha |
| CameraCycle | Bool (pulsar) | C | Botón superior (Y / Triángulo) |
| LookBack | Bool (mantener) | B | Gatillo superior derecho (RB) |
| Horn | Bool (mantener) | H | Clic del stick izquierdo |
| Pause | Bool (pulsar) | Esc, P | Start / Menú |
| Restart | Bool (pulsar) | R | Back / Vista |
| ToggleControlScheme | Bool (pulsar) | T | Gatillo superior izquierdo (LB) |
| MenuConfirm (menú) | Bool (pulsar) | Enter, Espacio | Botón inferior (A / Cruz) |
| MenuBack (menú) | Bool (pulsar) | Esc, Retroceso | Botón derecho (B / Círculo) |

Notas:
- Los valores de `Steer`, `Throttle` y `Brake` **ya llegan procesados** (zona muerta, curva e inversión). No vuelvas a aplicar `FLRInputCurves` sobre ellos; esas funciones son para fuentes crudas (volante, previsualizaciones).
- `Steer` es acumulativo (A + D = 0). Usa `FLRInputCurves::ClampSteer` porque tecla + stick puede superar 1.
- Prioridades sugeridas: `DrivingContextPriority = 0` y `MenuContextPriority = 1`.

## Uso típico (en el player controller)

```cpp
FLRInputTuning Tuning;
ULRInputActions::LoadTuning(Tuning);                  // si falla, se quedan los valores por defecto
InputActions = ULRInputActions::Create(this, Tuning); // guardar en un UPROPERTY
Subsystem->AddMappingContext(InputActions->DrivingContext, ULRInputActions::DrivingContextPriority);
LaneDetector.Configure(Tuning);
// En el tick, esquema Arcade:
const int32 Dir = LaneDetector.Update(FLRInputCurves::ClampSteer(SteerValue), DeltaSeconds);
if (Dir != 0) { Vehicle->RequestLaneChange(Dir); }
```

Para recargar en caliente (`lr.Data.Reload`): `LoadTuning`, `ApplyTuning`, `LaneDetector.Configure` y después `Subsystem->RequestRebuildControlMappings()`.

## Datos

- `Data/Input/Input.json` (tipo `InputTuning`), validado por `Data/Schema/InputTuning.schema.json`.
- Campos: `StickDeadZone` (0,15), `TriggerDeadZone` (0,05), `SteeringExponent` (1,5), `LaneChangeThreshold` (0,55), `LaneChangeReleaseThreshold` (0,3), `LaneChangeRepeatDelaySeconds` (0,35 s), `bInvertSteering` (false).

## Cómo extender

1. **Nueva acción:** añade un `UPROPERTY TObjectPtr<UInputAction>` en `ULRInputActions`, créala en `Build()` con `MakeAction`, mapea **siempre** una tecla de teclado y otra de mando, y añádela a `GetAllActions`. Actualiza el test `LeftRight.Input.Actions` (número de acciones y teclas).
2. **Nuevo parámetro ajustable:** añade el `UPROPERTY` con valor por defecto en `FLRInputTuning`, acótalo en `Sanitized()`, añádelo al JSON y al esquema (`additionalProperties` es `false`).
3. **Nuevo contexto** (Replay, PhotoMode): créalo en `Build()` como `MenuContext` y documenta su prioridad.
4. **Reasignación de controles:** pendiente; se hará sobre `UEnhancedInputUserSettings` con estas acciones.

## Rendimiento

- Coste por fotograma prácticamente nulo: `FLRLaneChangeDetector::Update` y `FLRInputDeviceTracker::NotifyKey` son O(1), sin asignaciones de memoria.
- `ULRInputActions::Create` asigna memoria una sola vez al crear el controlador. `GetPromptKey` recorre unos 40 mapeos; llámalo solo cuando cambie el dispositivo (usa `GetChangeCount`), no en cada fotograma.
- Presupuesto: < 0,01 ms por fotograma en el hilo de juego.

## Tests

- `LeftRight.Input.LaneChangeDetector`: disparo único, histéresis, repetición, fotogramas largos.
- `LeftRight.Input.Curves`: zona muerta, curva, cadenas de stick y gatillo, `Sanitized`.
- `LeftRight.Input.DeviceTracker`: clasificación de teclas, cambios de dispositivo, deriva analógica.
- `LeftRight.Input.Actions`: `Create()` construye todas las acciones con su `ValueType`, mapeos de teclado y mando, modificadores, triggers y carga del JSON.
