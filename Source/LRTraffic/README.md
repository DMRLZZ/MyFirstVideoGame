# LRTraffic

Tráfico de autopista de LEFTRIGHT (`docs/02-ARCHITECTURE.md §5.2`): hasta 200 coches simulados en coordenadas de carretera `(S, D)` con IDM (seguimiento) y MOBIL (cambios de carril), reglas de juego justo hacia el jugador, detección de *near miss* y una representación con actores reutilizables cerca y una malla instanciada lejos.

- Toda la simulación es **pura** (sin `UObject` ni mundo) y está en `Public/Sim/`: se prueba sin motor gráfico.
- Unidades SI dentro (metros, segundos, m/s, m/s²). Los centímetros solo aparecen al colocar actores (`ULRRoadSubsystem::RoadToWorld`).
- Determinista: toda la aleatoriedad sale de `LRRandom::MakeStream(Seed, TrafficSpawn)` y `LRRandom::MakeStream(Seed, TrafficBehavior)`. Mismo seed y mismas entradas del jugador → mismo estado bit a bit (`FLRTrafficSim::ComputeChecksum`).

## Clases principales

| Clase / función | Tipo | Para qué |
|---|---|---|
| `FLRIdmParams`, `IdmAcceleration` | Pura (`Sim/LRIdm.h`) | Intelligent Driver Model. Aceleración limitada a `[-9 m/s², a_max]`; hueco mínimo 0,1 m |
| `FLRMobilParams`, `FLRMobilInput`, `EvaluateMobil` | Pura (`Sim/LRMobil.h`) | Incentivo `(ã_c − a_c) + p·[(ã_n − a_n) + (ã_o − a_o)] > umbral ∓ sesgo_derecha`; seguridad `ã_n ≥ −b_safe` y huecos delante/detrás ≥ `MinGap` |
| `FLRTrafficSim` | Clase pura (`Sim/LRTrafficSim.h`) | Simulación en *struct of arrays* con paso fijo (60 Hz). Listas por carril ordenadas por `S` (inserción, casi ordenadas), orden global "de delante hacia atrás", fases de cambio de carril `None → Signaling → Moving`, generación y eliminación, choques |
| `FLRNearMissTracker` | Clase pura (`Sim/LRNearMissTracker.h`) | Por cada coche con solape longitudinal con el jugador guarda la distancia lateral mínima y la velocidad relativa máxima; al terminar el solape sin contacto emite `FLRNearMissEvent` (nivel, sentido contrario, enhebrar la aguja) |
| `FLRTrafficProfile` y estructuras asociadas | `USTRUCT` (`LRTrafficTypes.h`) | Datos: arquetipos, perfiles de conductor, curva de densidad, ventanas, juego justo, tiempos de cambio de carril, umbrales de near miss |
| `FLRTrafficVehicleSnapshot` | `USTRUCT` (`LRTrafficTypes.h`) | Vista de solo lectura de un coche (repetición, minimapa, audio) |
| `ULRTrafficSubsystem` | `UTickableWorldSubsystem` (solo Game/PIE) | Dueño de la simulación: acumulador de paso fijo (máx. 4 pasos por frame), jugador como obstáculo, near miss al bus de eventos, reinicio en `OnRunReset`, accidentes en `OnCrash`, pool de actores y malla lejana |
| `ALRTrafficVehicleActor` | Actor (pool de ~60) | Coche *grey-box* hecho con formas básicas del motor (carrocería, cabina, 4 ruedas, luces de freno e intermitentes), color por instancia dinámica de `BasicShapeMaterial`, colisión cinemática que bloquea al jugador y genera *hits*; `EnterWreckMode` pasa a física simulada |

### Simulación (`FLRTrafficSim::Step`)

1. Guarda `PrevS / PrevD` (interpolación de render), descuenta los temporizadores de accidente y elimina los coches fuera de `[playerS − SimBehindM, playerS + SimAheadM]`.
2. Reconstruye las listas por carril (compactación + ordenación por inserción) y el orden global.
3. Calcula aceleraciones IDM con el estado al principio del paso. Un coche en fase `Moving` sigue al líder más restrictivo de los dos carriles. El jugador es líder u obstáculo, pero nunca decide.
4. Cambios de carril: MOBIL cada ~0,5 s por coche (fases desfasadas), solo en `[playerS − 150, playerS + 600]`. Intermitente 1,0-2,0 s, desplazamiento lateral con *smootherstep* en 2,5-4,5 s; el yaw sale de la velocidad lateral. Mientras se mueve, el coche cuenta en **los dos** carriles.
5. Integración longitudinal de delante hacia atrás con dos límites exactos: no solapar al líder de ningún carril ocupado y no cerrar la última salida (ver "Siempre hay salida").
6. Generación de coches nuevos según la curva de densidad.
7. Luces (freno, intermitentes) y yaw.

Los carriles en contra (`Direction = −1`) avanzan hacia `S` decreciente con su propio orden de líderes; las velocidades se guardan como módulo (≥ 0).

### Reglas de juego justo

- **TTC:** ningún coche se cambia al carril del jugador por delante de él si el tiempo hasta colisión es menor que `FairnessTtcSeconds` (1,2 s), ni a menos de `MinGapToPlayerM`. Excepción: el evento "conductor agresivo" (`AggressiveEventChance` del perfil), que siempre pone el intermitente al menos `AggressiveMinBlinkSeconds` (≥ 1,0 s).
- **Generación:** solo entre `playerS + 600` y `playerS + 1200` m, o por detrás fuera de cámara (180-240 m) si el jugador va más lento que el tráfico. El relleno inicial al reiniciar la partida es la única excepción (empieza a 60 m y deja libre el carril del jugador 150 m).
- **Siempre hay salida:** cada coche del sentido del jugador bloquea el carril más cercano a su centro; nunca hay una ventana de 40 m (`SolvabilityWindowM`) con todos los carriles del jugador bloqueados. Se comprueba al generar, al aprobar un cambio de carril, cuando un cambio cruza la mitad del carril y en cada avance (el morro no puede entrar donde cerraría la última salida; los coches lo anticipan con un "obstáculo fantasma" IDM). Los accidentes no cuentan.
- **Camiones y buses:** más lentos, nacen en los carriles derechos, tienen sesgo extra a la derecha y nunca usan el carril izquierdo en carreteras de 3 o más carriles.

### Accidentes

`ULRTrafficSubsystem` escucha `OnCrash`. Si `OtherActor` es uno de sus actores, el coche queda en la simulación como obstáculo parado durante `WreckLifetimeSeconds` (10 s; los seguidores frenan con IDM) mientras su actor pasa a física simulada con un impulso proporcional a la masa y a la velocidad de impacto (`WreckImpulseScale`). Después se eliminan los dos. Ese coche no puede dar near miss.

### Near miss

Por frame, con las posiciones interpoladas que ve el jugador: para cada coche con solape longitudinal se guarda `min(|dp − dv| − (wp + wv)/2)` y la velocidad relativa máxima. Al terminar el solape sin contacto, con distancia < 1,0 m y velocidad relativa > 20 km/h, se emite `FLRNearMissEvent` con nivel `Close` (< 1,0 m), `VeryClose` (< 0,5 m) o `Razor` (< 0,25 m), `bOncoming` y `bThreadTheNeedle` (otro paso válido por el lado contrario terminado hace menos de `ThreadWindowSeconds`).

## Ficheros de datos

| Fichero | Tipo | Contenido |
|---|---|---|
| `Data/Traffic/TrafficProfile_Default.json` | `TrafficProfile` | Arquetipos `sedan`, `hatchback`, `suv`, `pickup`, `van`, `truck`, `bus`; conductores `normal`, `hurry`, `sunday`, `heavy`; curva de densidad 8 → 26 coches/km/carril en 7 minutos; ventanas, juego justo, cambios de carril y near miss |

Esquema: `Data/Schema/TrafficProfile.schema.json` (valídalo con `Tools/Scripts/validate-data.ps1`). Las claves de `data` son exactamente los nombres de los `UPROPERTY`; lo que falte conserva el valor por defecto de C++. La consola `lr.Data.Reload` recarga el perfil y reinicia el tráfico con la semilla actual.

## Uso desde otros módulos

```cpp
if (ULRTrafficSubsystem* Traffic = ULRTrafficSubsystem::Get(this))
{
	TArray<FLRTrafficVehicleSnapshot> Snapshot;   // reutiliza el array entre frames
	Traffic->GetSnapshot(Snapshot, PlayerS - 200.0, PlayerS + 600.0);
	const bool bIsTraffic = Traffic->IsTrafficActor(HitActor);
}
```

- Los near miss llegan por `ULRGameEventSubsystem::OnNearMiss`.
- `ResetTraffic(Seed)` se llama solo al recibir `OnRunReset`. Como los delegados multicast de Unreal se ejecutan en orden inverso de suscripción, si la carretera todavía no se ha reconstruido para la nueva partida, el reinicio se hace al principio del siguiente tick.
- `SetSimulationEnabled(false)` congela el tráfico (por ejemplo, en la repetición); `SetDensityScale` escala la curva de densidad.

## Cómo extender

- **Nuevo arquetipo o conductor:** solo JSON (`Archetypes`, `DriverProfiles`). Un arquetipo puede forzar su conductor con `DriverProfile`.
- **Nuevo evento de tráfico:** añade la lógica en `FLRTrafficSim` (puro) y su test en `Private/Tests/LRTrafficSimSpec.cpp`; nunca rompas las tres reglas de juego justo (hay tests que las vigilan).
- **Modelos reales de coches:** sustituye las formas básicas de `ALRTrafficVehicleActor::ApplyArchetype` por mallas referenciadas desde el JSON del arquetipo; la colisión cinemática no cambia.
- **Consultas espaciales:** `FLRTrafficSim::GetLaneOccupancy(S0, S1, Counts)` y los accesores por slot (`GetS`, `GetD`, `GetSpeed`...).

## Tests

`Private/Tests/` (Automation Specs, `LeftRight.Traffic.*`, sin mundo):

- `LRIdmSpec.cpp`: converge a `v0` en carretera libre; se para detrás de un obstáculo parado sin solapar.
- `LRMobilSpec.cpp`: inseguro → no cambia; con incentivo → cambia; la cortesía reduce cambios; sesgo a la derecha.
- `LRTrafficSimSpec.cpp`: 10 minutos simulados sin solapes en un carril, sin NaN ni velocidades negativas y sin ventanas bloqueadas; determinismo por checksum; ventanas de generación; doble sentido; regla TTC; accidentes; carga de `TrafficProfile_Default.json`.
- `LRNearMissTrackerSpec.cpp`: niveles, sentido contrario, enhebrar la aguja, contacto → sin evento.

```powershell
Tools/Scripts/test.ps1 -Filter LeftRight.Traffic
```

## Presupuesto de rendimiento

- **≤ 1,0 ms del hilo de juego** para 200 coches simulados (simulación + near miss + actualización de actores). Medido con `TRACE_CPUPROFILER_EVENT_SCOPE` en `ULRTrafficSubsystem::Tick`, `UpdatePresentation`, `UpdateNearMisses`, `FLRTrafficSim::Step`, `Accelerations`, `UpdateLaneChanges`, `Integrate` y `RebuildLaneLists`.
- Sin reservas de memoria por tick: los arrays de la simulación se reservan en `Configure` (`MaxVehicles`), y los arrays temporales del subsistema se reutilizan entre frames. Las instancias lejanas solo crecen (marca de agua) y las libres se esconden con escala cero.
- Vecinos por búsqueda binaria en listas por carril; la comprobación de salida avanza por los carriles con saltos (pocas iteraciones por consulta).
- ~60 actores visibles a la vez; el resto, una sola `UInstancedStaticMeshComponent` sin colisión.
