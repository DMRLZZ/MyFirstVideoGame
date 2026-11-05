# 08 · Registro de decisiones (ADR)

Formato: contexto → decisión → alternativas descartadas → consecuencias. Cada decisión nueva se añade al final con el siguiente número. `lead-architect` es el dueño de este archivo.

---

## ADR-001 · Motor: Unreal Engine 5.8.x

- **Contexto:** queremos un nivel visual comparable a GTA V o Need for Speed, o superior. Es noviembre de 2026: UE 5.8 (junio de 2026) es la última versión mayor de UE5, y UE6 está previsto en vista previa para 2027.
- **Decisión:** UE 5.8.x (la última corrección disponible) para todo el desarrollo de la versión 1.
- **Descartadas:**
  - Unity 6 HDRP: buena calidad, pero sin un equivalente maduro a Nanite + Lumen + MegaLights para este caso, y con menos material de automoción.
  - Web (three.js/WebGPU): no llega al fotorrealismo buscado.
  - Esperar a UE6: bloquea el proyecto más de un año.
- **Consecuencias:** desarrollo en Windows, código C++ y compilación con VS 2022. Migrar a UE6 se estudiará después de la versión 1.

## ADR-002 · C++ y datos en texto; Blueprints mínimos

- **Contexto:** los agentes trabajan bien con texto y mal con `.uasset` binarios (Blueprints, materiales).
- **Decisión:**
  - Lógica en C++.
  - Datos de diseño en JSON (`Data/`) importados a DataAssets por commandlet.
  - Assets binarios creados por personas o por scripts Python versionados.
  - Blueprints solo como "pegamento" visual, sin reglas de juego.
- **Descartadas:** juego en Blueprints, porque no se puede revisar ni comparar en diffs ni editar con agentes.
- **Consecuencias:** iteración de diseño algo más lenta en el editor (se compensa con la recarga del importador). Revisiones y tests mucho mejores.

## ADR-003 · Tráfico propio con IDM + MOBIL en coordenadas de carretera

- **Contexto:** Mass Traffic (City Sample) está pensado para ciudades, casi no tiene documentación, tiene fallos conocidos en carriles adyacentes de ZoneGraph y no hay trabajo previsto por parte de Epic.
- **Decisión:** `ULRTrafficSubsystem` propio, con estructura de arrays en `(s, d)`, IDM para el movimiento longitudinal, MOBIL para los cambios de carril, pool de actores y reglas de juego justo.
- **Descartadas:**
  - Mass Traffic y ZoneGraph (riesgo y opacidad).
  - Plugins de Fab (sin verificar y sin control de las reglas de juego justo).
  - CARLA (exige un fork de UE 5.5).
- **Consecuencias:** hay que mantener más código propio, pero es simple (una autopista no tiene cruces), determinista y fácil de probar. Mass Entity se puede reconsiderar si se necesitan más de 500 coches.

## ADR-004 · Autopista por tramos propios con API de carriles única

- **Contexto:** necesitamos un modo infinito y rutas de autor fotorrealistas.
- **Decisión:**
  - `ULRRoadSubsystem` con API `(s, d)`.
  - Modo infinito con tramos reciclados desde un pool.
  - Rutas de autor con World Partition montadas por scripts y retocadas a mano.
  - Decoración de bordes con PCG.
- **Descartadas:** depender de un plugin de carreteras comercial como base (se evalúa para puentes y túneles en M5); mundo abierto.
- **Consecuencias:** tráfico, juego y audio no saben si la ruta es infinita o de autor.

## ADR-005 · Físicas: Chaos Wheeled + capa de asistencias propia

- **Contexto:** buscamos sensación "simcade" con dos esquemas: carriles asistidos y libre.
- **Decisión:** `UChaosWheeledVehicleMovementComponent` heredado, con subpasos de físicas, curvas de neumático ajustadas y una pila de asistencias propia (`LaneAssist`, ESP, TCS, dirección sensible a la velocidad).
- **Descartadas, de momento:**
  - DriveSim, Spalah y otros: se evaluarían solo si Chaos no alcanza la sensación al final de M2.
  - Modular Vehicle: experimento M1-13.
- **Consecuencias:** sin coste extra y con soporte del motor. El ajuste fino de neumáticos requiere iteración.

## ADR-006 · Marcas de coches ficticias en la versión 1

- **Contexto:** los modelos "Editorial" no se pueden usar en juegos, los fabricantes han demandado (BMW contra TurboSquid) y las licencias cuestan desde unos 5.000 USD más regalías por marca, con posibles exclusividades.
- **Decisión:** marcas, nombres y diseños originales inspirados en arquetipos, con búsqueda de marcas registradas antes de fijar nombres.
- **Descartadas:** usar marcas reales sin licencia; licenciar antes de tener tracción.
- **Consecuencias:** se pierde reconocimiento inmediato y se gana libertad (daños, personalización) y seguridad legal.

## ADR-007 · Git + Git LFS con bloqueo de archivos

- **Contexto:** los agentes trabajan con git y worktrees, y el equipo de arte es pequeño al principio.
- **Decisión:** Git + LFS, con `.uasset` y `.umap` marcados como `lockable`, ramas por tarea y PR revisada.
- **Descartadas:** Perforce desde el primer día (más fricción con los agentes). Se reconsidera para `Content/` si hay más de 5 artistas.
- **Consecuencias:** hay que vigilar el coste de almacenamiento LFS del host elegido y compartir la caché DDC entre worktrees.

## ADR-008 · Coordenadas grandes: LWC primero, recentrado como plan B

- **Contexto:** a 300 km/h se recorren unos 150 km en 30 minutos.
- **Decisión:** usar Large World Coordinates de UE5. El test M1-12 conduce 200 km a 300 km/h y mide el temblor de físicas, cámara, Niagara y materiales. Si falla, se aplica el recentrado cada 50 km (moviendo todo en un fotograma, sin corte de cámara).
- **Consecuencias:** diseño sin suposiciones de origen fijo. Nada debe guardar posiciones absolutas en `float`.

## ADR-009 · Cámara Viral como funcionalidad principal

- **Contexto:** el realismo percibido de Unrecord y similares viene en gran parte de imitar una cámara real. Además, nuestra fantasía nace de vídeos virales.
- **Decisión:** la Cámara Viral (GoPro, dashcam, móvil) es una funcionalidad de primera clase desde M3, disponible desde el principio del juego.
- **Consecuencias:** se convierte en el motor de marketing (clips para redes). Los efectos de artefacto tienen que poder desactivarse por accesibilidad.

## ADR-010 · Datos JSON cargados en tiempo de ejecución (sustituye el commandlet de importación)

- **Contexto:** la arquitectura inicial (§6) convertía los JSON en DataAssets binarios con un commandlet. Eso añade un paso, genera binarios y obliga a abrir el editor para cada cambio de balance.
- **Decisión:** los JSON de `Data/` se leen **en tiempo de ejecución** con `FLRDataLoader` (sobre `FJsonObjectConverter`) y `ULRDataSubsystem`, con recarga en caliente (`lr.Data.Reload`). `LeftRight.Build.cs` empaqueta `Data/` con `RuntimeDependencies` (UFS). Los esquemas se validan sin motor (`Tools/Data/validate_data.py`).
- **Descartadas:** commandlet JSON → DataAsset (más pasos, más binarios); DataTables desde CSV (peor para estructuras anidadas).
- **Consecuencias:** cero binarios para datos de diseño y ajuste sin recompilar. Las referencias a assets en JSON (`FSoftObjectPath`) no las descubre el *cooker*: los assets referenciados se añaden a las reglas de cocinado del Asset Manager o a `DirectoriesToAlwaysCook`.

## ADR-011 · Prototipo sin assets binarios: mundo, input y HUD desde código

- **Contexto:** los agentes no pueden editar `.uasset` y el prototipo gris debe ser jugable cuanto antes.
- **Decisión:**
  - El mundo gris (carretera con mallas procedurales, barreras, farolas, cielo, sol, niebla y post-proceso) lo genera C++ sobre el mapa vacío del motor `/Engine/Maps/Entry`.
  - Las acciones y contextos de Enhanced Input se crean con `NewObject` en tiempo de ejecución (teclado y mando).
  - El HUD se dibuja con `UCanvas` hasta la UI definitiva con CommonUI.
  - El tráfico gris usa formas básicas del motor.
- **Consecuencias:** el prototipo funciona solo con texto en el repo. Cuando lleguen los assets reales (M3) se sustituyen las mallas y el HUD, manteniendo las mismas APIs.

## ADR-012 · Audio procedural de motor en el prototipo

- **Contexto:** no hay muestras de motor con licencia todavía.
- **Decisión:** `ULRVehicleAudioComponent` hereda de `USynthComponent` y sintetiza motor (armónicos de la frecuencia de encendido), viento, rodadura y *whoosh* de near miss en el hilo de audio, con perfiles JSON por coche.
- **Consecuencias:** hay sonido desde el primer día. En M3 se añade la capa de muestras reales (MetaSounds) y el sintetizador queda como refuerzo o *fallback*.

## ADR-013 · Ajuste de dependencias entre módulos

- **Contexto:** al definir los contratos de código hicieron falta dependencias distintas a las de la tabla inicial.
- **Decisión:**
  - `LRVehicle` depende de `LRWorld` (el asistente de carril usa coordenadas de carretera).
  - `LRUI` depende de `LRCore` y `LRVehicle`: escucha el bus de eventos y no depende de `LRGameplay`.
  - `LRGameplay` depende de `LRUI`, `LRCamera`, `LRAudio`, `LRReplay` y `LRProgression`, porque configura las clases de HUD, cámara y audio.
  - `ILRRoadParticipant` (en `LRCore`) permite al tráfico leer la huella y la velocidad del jugador sin depender de `LRVehicle`.
- **Consecuencias:** el grafo sigue siendo acíclico. `LRGameplay` es el integrador.
