# LRAudio

Sonido del coche del jugador, **100 % procedural**: funciona desde el primer día sin un solo archivo de audio (ADR-012). Sintetiza motor, viento, rodadura, juntas de dilatación, *whoosh* de near miss y golpe de choque en el hilo de audio, con un perfil JSON por coche.

Depende de `LRCore`, `LRVehicle`, `AudioMixer` y, de forma privada y solo para leer, de `LRTraffic` (para saber a qué lado quedó el coche adelantado).

## Clases principales

| Tipo | Archivo | Qué hace |
|---|---|---|
| `ULRVehicleAudioComponent` | `Public/LRVehicleAudioComponent.h` | `USynthComponent` del coche. Cada tick lee RPM, acelerador, velocidad y `S` del `ALRVehicle` y los publica para el hilo de audio. Carga el perfil y lo recarga si el coche cambia de definición o con `lr.Data.Reload`. |
| `ULRAudioSubsystem` | `Public/LRAudioSubsystem.h` | `UWorldSubsystem`. Escucha `OnNearMiss`, `OnCrash`, `OnCameraModeChanged` y `OnRunReset` y los reenvía al componente del jugador. **Añade el componente él solo** si el peón es un `ALRVehicle` que no lo tiene. |
| `FLREngineAudioProfile` | `Public/LRAudioTypes.h` | Todos los valores ajustables. `Sanitized()` los acota y `LoadById()` carga el JSON con su alternativa. |
| `FLRVehicleSynth` | `Public/DSP/LRVehicleSynth.h` | Lógica pura: el sintetizador completo. Se prueba sin mundo ni motor de audio. |
| `FLRVehicleSynthBridge` | `Public/DSP/LRVehicleSynthBridge.h` | Lógica pura: el cruce sin bloqueos entre el hilo de juego y el de audio. |
| `LRSynthDSP`, `FLROscillatorBank`, `FLRBiquad`, `FLROnePoleLowPass`… | `Public/DSP/LRSynthDSP.h` | Lógica pura: osciladores, filtros, ruido, saturación suave y paneo. |

Uso desde otro módulo (normalmente no hace falta, porque el subsistema lo hace solo):

```cpp
ULRVehicleAudioComponent* VehicleAudio = ULRVehicleAudioComponent::FindOrAddTo(Vehicle);
```

## Capas de sonido

| Capa | Cómo se genera | De qué depende |
|---|---|---|
| **Motor** | Parciales sinusoidales de la frecuencia de encendido (`rpm / 60 * cilindros / 2`), incluido el medio orden que da el "lope" del V8, más ruido de admisión y de escape, variación por ciclo y saturación suave | RPM, carga, perfil |
| **Viento** | Ruido en banda con ganancia proporcional a `v²`, y ráfagas lentas. Canales izquierdo y derecho decorrelacionados | Velocidad |
| **Rodadura** | Ruido grave filtrado | Velocidad |
| **Juntas** | Un golpe cada `JointSpacingMeters` de `S` y otro cuando pasa el eje trasero | `S`, velocidad, batalla |
| **Whoosh** | Ráfaga de ruido en banda con barrido descendente, paneada hacia el lado del coche adelantado | Velocidad relativa y holgura lateral del near miss |
| **Choque** | Ráfaga de ruido más un golpe grave. Si es grave (`bSevere`), corta el motor hasta la siguiente partida | Velocidad del impacto |

**Mezcla de cabina:** cuando `FLRCameraModeChangedEvent::bInterior` es verdadero, el motor pasa por un paso bajo, baja el viento y sube la vibración de la rodadura, con un fundido de `MixSmoothingSeconds`.

## Hilos

```
Hilo de juego                         FLRVehicleSynthBridge                 Hilo de audio
TickComponent ── SetParams ──────────▶ std::atomic por parámetro ──────────▶ Render
TriggerCrash... ── PushEvent ────────▶ cola SPSC de tamaño fijo ───────────▶ HandleEvent
SubmitProfile (reserva memoria) ─────▶ puntero atómico "pendiente" ────────▶ SwapProfile
ReclaimRetiredProfile (libera) ◀────── puntero atómico "retirado" ◀──────── perfil anterior
```

- El hilo de audio **nunca reserva ni libera memoria, ni se bloquea**. Recibe el perfil intercambiando contenidos y devuelve el anterior al hilo de juego para que lo libere.
- El sonido se genera con un `ISoundGenerator` privado (`CreateSoundGenerator`), la vía que el motor recomienda para componentes nuevos: el hilo de audio no toca ningún `UObject`, y el puente es de propiedad compartida, así que el componente puede destruirse mientras el mezclador termina un búfer.
- Los parámetros se suavizan dentro del hilo de audio, muestra a muestra. Coeficientes y ganancias se recalculan cada 32 muestras.

## Datos

`Data/Audio/<id>.json`, tipo `EngineAudioProfile` (esquema en `Data/Schema/EngineAudioProfile.schema.json`). Los campos que faltan conservan el valor por defecto de C++.

| Archivo | Motor | Lo usa |
|---|---|---|
| `EngineAudio_Default.json` | Genérico. Tiene **todos** los campos, así que sirve de plantilla | Alternativa si falta un perfil |
| `engine_i6_turbo.json` | 6 en línea turbo: suave y limpio | `bauer_r3_sport` |
| `engine_v8_biturbo.json` | V8 biturbo: grave, amortiguado por los turbos, mucho aire | `aurelion_vanta_s` |
| `engine_v8_supercharged.json` | V8 *muscle* con compresor: mucho medio orden, áspero, silbido del compresor | `ironclad_hellion` |
| `engine_v8_supercar.json` | V8 de cigüeñal plano: sin lope, brillante y agudo | Ningún coche todavía |
| `engine_v10_na.json` | V10 atmosférico: frecuencia de encendido muy alta, raspa | `ardent_vireo` |

## Cómo extender

- **Nuevo perfil de motor:** copia `EngineAudio_Default.json`, ponle como `id` y `Id` el nombre del archivo, y apunta a él desde `AudioProfile` en `Data/Vehicles/<coche>.json`. Valida con `Tools/Scripts/validate-data.ps1`. Se escucha en caliente con `lr.Data.Reload`.
- **Nuevo parámetro:** añádelo con su valor por defecto y `ClampMin` / `ClampMax` en `FLREngineAudioProfile`, y también en `Sanitized()` y en el esquema (`additionalProperties` es `false`, así que el validador rechaza lo que no esté declarado).
- **Nueva capa continua:** añade su estado a `FLRVehicleSynth`, sus coeficientes a `UpdateControl` y su muestra a `Render`, sin reservar memoria. Si necesita un dato nuevo del coche, añádelo a `FLRVehicleSynthParams` y a los atómicos del puente.
- **Nuevo efecto puntual:** añade un valor a `FLRVehicleSynthEvent::EType`, trátalo en `HandleEvent` y envíalo con `PushEvent`.
- **Muestras reales (M3):** ADR-012 prevé una capa de MetaSounds; este sintetizador quedaría como refuerzo o alternativa.

## Tests

`Private/Tests/`, sin mundo: `LeftRight.Audio.DSP.*` (salida acotada, mapeo RPM a Hz, estabilidad de filtros), `LeftRight.Audio.Synth.*` (cada capa, seguridad numérica, juntas, paneo, cabina), `LeftRight.Audio.Bridge.*` (cruce entre hilos, ciclo de vida del perfil) y `LeftRight.Audio.Profile.*` (todos los JSON cargan y todos los coches apuntan a un perfil que existe).

```powershell
Tools/Scripts/test.ps1 -Filter LeftRight.Audio
```

## Presupuesto de rendimiento

- **Hilo de audio:** alrededor del 1 % de un núcleo por coche a 48 kHz en estéreo (0,9 % medido con un ejecutable de prueba fuera del motor, en Release). Medido con `TRACE_CPUPROFILER_EVENT_SCOPE(FLRVehicleSynthBridge::Render)`.
- **Hilo de juego:** muy por debajo de los 0,5 ms que `docs/02-ARCHITECTURE.md §8` asigna al audio. Son unas cuantas escrituras atómicas por tick (`ULRVehicleAudioComponent::TickComponent`). Solo lee disco al cambiar de perfil.
- **Memoria:** unos pocos KB por coche, sin reservas por tick. Solo se reserva al cambiar de perfil y en la consulta de tráfico de cada near miss, que es un evento puntual.
