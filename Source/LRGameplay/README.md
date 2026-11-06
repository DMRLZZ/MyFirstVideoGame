# LRGameplay

Reglas de la partida de LEFTRIGHT: ciclo de vida de la partida, puntuación y combos, modo de juego y controlador del jugador. Las reglas viven en lógica pura y los valores en `Data/Gameplay/` (ver `docs/04-GAMEPLAY.md`).

Depende de todos los módulos de juego (`LRCore`, `LRWorld`, `LRTraffic`, `LRVehicle`, `LRInput`, `LRCamera`, `LRUI`, `LRAudio`, `LRReplay`, `LRProgression`) y de `EnhancedInput`. Ningún módulo depende de él: los demás se enteran de todo por el bus de eventos de `LRCore`.

## Clases principales

| Tipo | Archivo | Qué hace |
|---|---|---|
| `FLRScoringRules`, `FLRSpeedTier` | `Public/LRScoring.h` | Valores de puntuación (`Scoring.json`). |
| `FLRScoreCalculator`, `FLRScoreState` | `Public/LRScoring.h` | Lógica pura: `AddDistance`, `ApplyNearMiss`, `Tick`, `BreakCombo`, consulta de multiplicador de velocidad. |
| `FLRRunStateMachine` | `Public/LRRunStateMachine.h` | Lógica pura: valida las transiciones de estado. |
| `FLRGameRules`, `FLRRumbleTuning` | `Public/LRGameRules.h` | Reglas de la partida (`GameRules.json`). |
| `FLRGameRulesLogic` | `Public/LRGameRules.h` | Lógica pura: qué significa un choque, carril inicial, semilla, argumentos de consola. |
| `ULRScoreComponent` | `Public/LRScoreComponent.h` | Componente de `ALRGameState`. Acumula puntos mientras el estado es `Running`, emite `OnScoreChanged`, `OnComboChanged` y `OnSpeedTierChanged`, y genera el `FLRRunResult`. |
| `ALRGameState` | `Public/LRGameState.h` | Dueño de `ULRScoreComponent`. |
| `ALRGameModeEndless` | `Public/LRGameModeEndless.h` | Crea el coche, arranca y reinicia partidas, decide cuándo termina una. Es el único que escribe en `ULRRunContextSubsystem`, junto con el cambio de esquema del controlador. |
| `ALRPlayerController` | `Public/LRPlayerController.h` | Enhanced Input, envío de controles al coche, cámara, pausa y vibración. |

## Flujo de una partida

```
Garage ─► Loading ─► Countdown ─► Running ─► Crashed ─► (Replay) ─► Results ─► Countdown (reinicio) | Garage
                         ▲           │           │                     │
                         └───────────┴───────────┴─────── reinicio ────┘
```

1. **Arranque.** El mapa es `/Engine/Maps/Entry`, vacío: el mundo se construye desde código. Un tick después de `StartPlay` (la escena de física empieza después de `StartPlay`), `ALRGameModeEndless` crea el `ALRVehicle`, carga el coche seleccionado en `ULRProgressionSubsystem` (si falla, `DefaultVehicleId`), lo posee, lo registra con `SetPlayerPawn` y le añade el `ULRVehicleAudioComponent`.
2. **`StartRun(semilla)`.** Llama a `ULRRunContextSubsystem::BeginNewRun`, y la carretera, el tráfico, la puntuación, las cámaras y el audio se reinician al recibir `OnRunReset`. No se recarga el mapa. Después, `ResetForRun` coloca el coche en `GetSpawnS()` a `StartSpeedKmh`, con la entrada desactivada.
3. **`Countdown`.** Dura `CountdownSeconds`. El coche sigue rodando por inercia y el asistente lo mantiene en su carril. El HUD lee la misma duración de `GameRules.json`.
4. **`Running`.** Se activa la entrada y empieza a contar la puntuación.
5. **Choque.** `FLRGameRulesLogic::EvaluateCrash` decide qué significa cada contacto (ver la tabla). Si termina la partida: `SetWrecked`, estado `Crashed`, resultado registrado en `ULRProgressionSubsystem`, créditos copiados en `FLRRunResult.CreditsEarned`, `OnRunFinished` y, tras `ResultsDelaySeconds`, `Results`.
6. **Reinicio.** `RestartRun()` funciona en cualquier estado posterior a la cuenta atrás. Usa una semilla nueva: del reloj o, en `Daily`, la de la fecha UTC. Una partida abandonada en `Running` no se registra.

### Qué significa un contacto

| Situación | Resultado |
|---|---|
| Normal, contacto `bSevere` (`CrashImpactKmh`, 25 km/h) | Fin de la partida |
| Normal, contacto leve | Roce: rompe el combo |
| Arcade indulgente, impacto ≤ `ScrapeMaxImpactKmh` | Roce: rompe el combo y no consume nada |
| Arcade indulgente, impacto mayor con roces disponibles | Consume uno de `ScrapesAllowed` y rompe el combo |
| Arcade indulgente, sin roces disponibles | Fin de la partida |
| Arcade indulgente, impacto ≥ `ForgivingFatalImpactKmh` | Fin de la partida |
| `FreeRide`, contacto `bSevere` | Tras `CrashRecoverySeconds`, el coche vuelve a la carretera `FreeRideResetForwardMeters` más adelante |

### Puntuación

- **Distancia:** 1 punto cada 10 m × multiplicador de velocidad. Solo cuenta carretera nueva: ir marcha atrás y volver a pasar no da puntos.
- **Near miss:** `250 × distancia (1/2/3) × clamp(v_rel / 100, 0,5, 3) × sentido contrario (2) × enhebrar (2) × combo`.
- **Combo:** el multiplicador es `1 + 0,25 × cadena`, con un máximo de × 5. Un near miss se paga con el multiplicador vigente y después hace crecer la cadena: el primero paga × 1 y el siguiente, si llega en la ventana de 3 s, × 1,25. El multiplicador que ve el jugador es el que recibirá el próximo near miss.
- **Rompen el combo:** cualquier roce y bajar de 120 km/h. Por debajo de esa velocidad un near miss paga, pero no inicia cadena.
- **`FreeRide`** no da puntos; solo registra distancia y velocidad punta.

## Comandos de consola

| Comando | Qué hace |
|---|---|
| `lr.Restart` | Reinicia la partida con una semilla nueva (en cualquier estado posterior a la cuenta atrás) |
| `lr.Mode <EndlessOneWay\|EndlessTwoWay\|FreeRide\|Daily>` | Reinicia en ese modo, desde cualquier estado |
| `lr.Vehicle <id>` | Carga `Data/Vehicles/<id>.json` en el coche y reinicia. Solo se guarda como selección si el perfil tiene ese coche |
| `lr.ControlScheme <Arcade\|Simcade>` | Cambia el esquema de control sin reiniciar |
| `lr.Data.Reload` | (De `LRCore`) Vuelve a leer `Scoring.json` y `GameRules.json`. No se aplica a `Input.json`, que solo se lee al crear el controlador |

## Archivos de datos

| Archivo | Tipo | Esquema |
|---|---|---|
| `Data/Gameplay/Scoring.json` | `ScoringRules` | `Data/Schema/ScoringRules.schema.json` |
| `Data/Gameplay/GameRules.json` | `GameRules` | `Data/Schema/GameRules.schema.json` |

`ALRHUD` lee `CountdownSeconds` de `GameRules.json`. `ALRPlayerController` lee `Data/Input/Input.json` (de `LRInput`).

## Entrada

`Config/DefaultInput.ini` no define las clases de Enhanced Input, así que el controlador las fija él mismo: `OverridePlayerInputClass = UEnhancedPlayerInput` y crea un `UEnhancedInputComponent` en `SetupInputComponent`.

En Arcade, el eje de dirección se limita con `FLRInputCurves::ClampSteer` y pasa por `FLRLaneChangeDetector` en `PlayerTick`. También se envía al coche, que solo lo usa por debajo de la velocidad mínima del asistente. `Pause` lleva `bTriggerWhenPaused` para poder reanudar, y `bShouldPerformFullTickWhenPaused` sigue en `false`.

## Cómo extender

- **Nuevo valor ajustable:** añade el `UPROPERTY` con su valor por defecto, acótalo en `Sanitized()` y añádelo al JSON y al esquema (`additionalProperties` es `false`).
- **Nuevo estado o transición:** cámbialo en `FLRRunStateMachine::CanTransition` y añade el caso a `LRRunStateMachineSpec.cpp`.
- **Nuevo modo:** añade el valor en `ELRGameModeType` (`LRCore`) y en `FLRGameRulesLogic::ParseGameMode`. Si cambia las reglas de los choques, hazlo en `EvaluateCrash`.
- **Nueva fuente de puntos:** añade la función pura a `FLRScoreCalculator` con su test, y emite `OnScoreChanged` con un `Reason` propio desde `ULRScoreComponent`.

## Tests

Todos son de lógica pura, sin mundo:

- `LeftRight.Gameplay.Scoring`: distancia en cada tramo, fórmula de near miss (sentido contrario y enhebrar incluidos), crecimiento, tope, caducidad y rotura del combo, y lectura del JSON.
- `LeftRight.Gameplay.RunStateMachine`: transiciones válidas e inválidas y reinicios.
- `LeftRight.Gameplay.GameRules`: lectura del JSON, decisión de cada choque, carril inicial, argumentos de consola y semillas.

## Rendimiento

- `ULRScoreComponent::TickComponent` debe tardar < 0,05 ms y no asigna memoria. Se mide con `TRACE_CPUPROFILER_EVENT_SCOPE`.
- El modo de juego no tiene tick: funciona con temporizadores y eventos.
- El controlador solo trabaja en `PlayerTick` para el detector de cambio de carril. Cada evento de entrada hace un `Cast` y una llamada.
