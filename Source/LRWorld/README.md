# LRWorld

Modelo de autopista de LEFTRIGHT: coordenadas de carretera `(S, D)`, trazado procedural determinista, streaming de geometría alrededor del jugador y ambientación (cielo, luz, niebla).

- `S` (double, metros): distancia a lo largo del eje, crece en el sentido de la marcha del jugador.
- `D` (float, metros): desplazamiento lateral desde el eje, positivo hacia la **derecha**.
- Todo el código interno trabaja en metros; los centímetros de Unreal solo aparecen en la frontera (`RoadToWorld`, `WorldToRoad`, mallas).

## Clases principales

| Clase | Tipo | Para qué |
|---|---|---|
| `FLRRoadProfile`, `FLRLaneDesc`, `FLRRoadGenParams`, `FLREnvironmentPreset`, `FLRRouteDefinition`, `FLRRoadStreamingParams`, `FLRRoadDressing` | `USTRUCT` (`LRRoadTypes.h`) | Datos cargados desde JSON |
| `LRRoadLayout::BuildLanes / ComputeCrossSection / BuildMarkings / FindNearestLane` | Funciones puras (`LRRoadLayout.h`) | Carriles, sección transversal (arcenes, barreras) y marcas viales |
| `FLRRoadPath` | Clase pura (`LRRoadPath.h`) | Trazado analítico: rectas y arcos de curvatura constante con rasante parabólica. `RoadToWorld`, `WorldToRoad` (con pista de segmento, O(1) amortizado), rumbo, curvatura, pendiente, `TrimBefore` |
| `FLRRoadGenerator` | Struct pura (`LRRoadGenerator.h`) | Genera el trazado de forma determinista con un `FRandomStream`: alterna rectas y curvas suaves con transiciones escalonadas de curvatura y cambios de rasante suaves |
| `ULRRoadSubsystem` | `UTickableWorldSubsystem` (solo Game/PIE) | API central para el resto de módulos. Se reconstruye al recibir `OnRunReset`, cachea la coordenada del jugador, genera por delante, recorta por detrás y mueve el streamer |
| `ALRRoadStreamer` | Actor | Trozos (*chunks*) de ~100 m con `UProceduralMeshComponent` reutilizables: asfalto, líneas, barreras de hormigón con colisión, farolas y captafaros con `UInstancedStaticMeshComponent` |
| `ALREnvironmentActor` | Actor | Sol (luz direccional de atmósfera), `USkyAtmosphereComponent`, `USkyLightComponent` en captura en tiempo real, niebla exponencial y post-proceso sin límites |
| `ULRWorldSettings` | `UDeveloperSettings` (`config=Game`) | Rutas de los ficheros de ruta (un sentido / doble sentido), activar o no el streamer y el entorno, semilla de respaldo |

### Convenciones geométricas

- Rumbo `psi` = yaw de Unreal (de +X hacia +Y). Vector derecha = `(-sin psi, cos psi)`.
- Curvatura `k > 0` gira a la derecha (el yaw aumenta).
- Arco: `psi(s) = psi0 + k·s`, `x = x0 + (sin psi − sin psi0)/k`, `y = y0 − (cos psi − cos psi0)/k` (se evalúa con la forma de cuerda, estable para `k·s` pequeño). Curvaturas `< 1e-8` se tratan como rectas.
- Los arcos que giran más de 90° se parten en varias piezas, para que la proyección `WorldToRoad` no sea ambigua.
- Altura: `z = z0 + G0·s + ½·(G1 − G0)/L·s²` (la pendiente varía linealmente dentro del segmento y es continua entre segmentos generados).
- Posición, rumbo y altura son continuos en todas las uniones.

### Carriles

- Un sentido: `N` carriles centrados en `D = 0`, todos con `Direction = +1`. Para 4 carriles de 3,7 m: `−5,55 / −1,85 / 1,85 / 5,55`.
- Doble sentido (tráfico por la derecha, EE. UU.): carriles en contra a la izquierda (`D < 0`, `Direction = −1`) y del jugador a la derecha, separados por la mediana. Para 2+2 con mediana de 0,6 m: `−5,85 / −2,15 / 2,15 / 5,85`.
- Los índices van de 0 a N−1 de izquierda a derecha. `GetDefaultPlayerLane()` siempre devuelve un carril del sentido del jugador.

### Marcas y barreras

- Un sentido: borde izquierdo amarillo continuo, borde derecho blanco continuo, separadores blancos discontinuos (3 m de trazo, 9 m de hueco, anclados a `S` absoluta). La barrera izquierda es una barrera de mediana (en el centro de la mediana) si `MedianWidthMeters > 0`.
- Doble sentido: bordes exteriores blancos, doble línea amarilla central, sin barrera en la mediana.
- Barreras de hormigón (perfil trapezoidal) con colisión en ambos lados exteriores.

## Ficheros de datos

| Fichero | Tipo | Contenido |
|---|---|---|
| `Data/World/Route_GreyboxFreeway.json` | `RouteDefinition` | Autopista de un sentido, 4 carriles, mediana con barrera |
| `Data/World/Route_GreyboxTwoWay.json` | `RouteDefinition` | Carretera de doble sentido 2+2, mediana pintada de 0,6 m |
| `Data/World/Environment_Day.json` | `EnvironmentPreset` | Mediodía despejado (usado por las rutas greybox) |
| `Data/World/Environment_Sunset.json` | `EnvironmentPreset` | Atardecer, sol bajo y cálido, niebla volumétrica |
| `Data/World/Environment_Night.json` | `EnvironmentPreset` | Noche con luna, `bNight = true` |

Esquemas: `Data/Schema/RouteDefinition.schema.json`, `Data/Schema/RoadGenParams.schema.json`, `Data/Schema/EnvironmentPreset.schema.json`.

Las rutas activas se configuran en `ULRWorldSettings` (`[/Script/LRWorld.LRWorldSettings]` en `DefaultGame.ini`); los valores por defecto están en C++. El modo `EndlessTwoWay` usa la ruta de doble sentido; el resto, la de un sentido.

## Ciclo de vida

1. `Initialize`: se suscribe a `ULRGameEventSubsystem::OnRunReset` (antes que los demás sistemas, así el vehículo y el tráfico se reinician sobre una carretera completa).
2. `OnRunReset` → `RebuildForSeed(Seed, Mode == EndlessTwoWay)`: carga la ruta, reinicia el trazado con `LRRandom::MakeStream(Seed, RoadLayout)`, genera hasta `SpawnS + GenerateAheadM` y reconstruye toda la ventana visible (la colisión cerca del punto de salida se cocina de forma síncrona).
3. `OnWorldBeginPlay`: genera un `ALRRoadStreamer` y un `ALREnvironmentActor`. Si no hubo ningún reinicio de partida, construye la carretera con la semilla de respaldo (1).
4. `Tick`: actualiza la coordenada del jugador (`WorldToRoad` con pista), genera hasta `S + 3.000 m`, recorta antes de `S − 1.000 m` (nunca dentro de la ventana del streamer) y actualiza la ventana `[S − 300 m, S + 2.000 m]`.

## Cómo extender

- **Nueva ruta procedural:** copia un `Route_*.json`, cambia perfil, generador o colores y apunta `ULRWorldSettings` a él. Valida con `Tools/Scripts/validate-data.ps1`.
- **Nuevo ambiente:** añade `Environment_<Nombre>.json` y referencia el fichero desde `EnvironmentPresetFile` de la ruta.
- **Nuevos tipos de tramo** (puentes, túneles, rampas): añade metadatos a `FLRRoadSegment` y deja que el streamer elija la decoración por segmento; la API `(S, D)` no cambia.
- **Límites de velocidad por zona:** `GetSpeedLimitKmh(S)` ya recibe `S`; basta con añadir una tabla de zonas a la ruta.
- **Rutas de autor:** desactiva `bSpawnRoadStreamer` en `ULRWorldSettings` y construye el trazado con `FLRRoadPath::Append*` a partir de datos del mapa.

## Presupuesto de rendimiento

- Hilo de juego: `ULRRoadSubsystem::Tick` < 0,1 ms en régimen normal (una proyección con pista, generación amortizada, recorte sin realocación).
- Construcción de un *chunk*: ~0,3–0,6 ms (≈21 filas × 4 secciones + instancias). Como máximo `MaxChunkBuildsPerFrame` (2) por fotograma; a 300 km/h entra menos de un *chunk* por segundo.
- Reinicio de partida: reconstrucción completa de ~24 *chunks* en un único fotograma (objetivo < 30 ms, dentro del límite de 2 s de reinicio).
- Sin reservas de memoria por fotograma: buffers reutilizables y pool de componentes que solo crece al calentarse.
- Render: 1 malla procedural con 4 secciones + 3 ISM por *chunk* (~100 llamadas de dibujo para toda la ventana).
- Precisión: cada *chunk* se coloca en su punto de inicio y sus vértices son locales, para evitar errores de `float` lejos del origen.
- Medición: `TRACE_CPUPROFILER_EVENT_SCOPE` en `ULRRoadSubsystem::Tick`, `ULRRoadSubsystem::RebuildForSeed`, `ALRRoadStreamer::UpdateWindow` y `ALRRoadStreamer::BuildChunk`.

## Tests

`Private/Tests/` (Automation Specs, filtro `LeftRight.World`):

- `LeftRight.World.RoadPath`: ida y vuelta `RoadToWorld → WorldToRoad` en rectas y arcos (< 1 cm a varios `D`), continuidad en las uniones, partición de arcos largos, `TrimBefore` con pistas válidas.
- `LeftRight.World.RoadLayout`: centros de carril para 4 carriles en un sentido y 2+2 en doble sentido, marcas viales y barreras.
- `LeftRight.World.RoadGenerator`: determinismo (misma semilla → segmentos idénticos, también con generación incremental), semillas distintas → trazados distintos, radio ≥ `MinRadiusM`, pendiente dentro de `MaxGrade` y continua.
