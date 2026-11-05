# 06 · Hoja de ruta y backlog

> **Duraciones orientativas** para un equipo pequeño (1-3 personas más agentes). El cuello de botella real no será el código, sino los **assets** (coches y entorno) y el **ajuste fino** (manejo, sensación, luz). Cada hito termina cuando cumple sus criterios de salida, no en una fecha.

| Hito | Objetivo | Duración orientativa |
|---|---|---|
| **M0** · Fundaciones | Proyecto, repo, scripts, tests y datos funcionando | 1-2 semanas |
| **M1** · Prototipo gris | ¿Es divertido esquivar tráfico? | 3-5 semanas |
| **M2** · Sensación de velocidad | Que 300 km/h den vértigo | 2-4 semanas |
| **M3** · Vertical slice fotorrealista | 60 s de "freeway nocturna" que parezcan reales | 6-12 semanas (depende de los assets) |
| **M4** · Contenido y progresión | Garaje, coches, modos, guardado | 6-10 semanas |
| **M5** · Mundo | 3 rutas, clima, hora del día, doble sentido | 6-10 semanas |
| **M6** · Pulido | Volante, accesibilidad, idiomas, optimización, QA | 4-8 semanas |
| **M7** · Lanzamiento | Steam, demo, logros, clasificaciones | 4-6 semanas |

---

## M0 · Fundaciones

**Criterio de salida:** un `LeftRight.uproject` vacío que compila desde la línea de comandos, pasa un test de ejemplo, importa un JSON de prueba a un DataAsset, y la CI (o el script local) lo ejecuta todo en verde.

| ID | Tarea | Dueño | Depende de |
|---|---|---|---|
| M0-01 | Instalar UE 5.8.x, VS 2022 (C++ para juegos) y el SDK de Windows; definir `UE_ROOT` | **Persona** | — |
| M0-02 | Crear el proyecto C++ `LeftRight` (plantilla vacía, sin contenido de ejemplo) y los módulos esqueleto de §4 de la arquitectura | build-release-engineer | M0-01 |
| M0-03 | `.gitignore` y `.gitattributes` (LFS, `lockable`), probar `git lfs lock`, elegir el host remoto | build-release-engineer | M0-02 |
| M0-04 | `Tools/Scripts/build.ps1`, `test.ps1`, `package.ps1`, `run-python.ps1` | build-release-engineer | M0-02 |
| M0-05 | Arnés de Automation Specs más un spec de ejemplo por módulo | qa-perf-engineer | M0-04 |
| M0-06 | `Data/Schema/` y `validate-data.ps1` (Python + `jsonschema`) | editor-tools-engineer | M0-02 |
| M0-07 | Commandlet `LRImportData` (JSON → `UPrimaryDataAsset`) más su test | editor-tools-engineer | M0-06 |
| M0-08 | Activar **Unreal MCP** (experimental) en `127.0.0.1`, documentar las herramientas en `docs/UNREAL_MCP.md` y probar un script de solo lectura | editor-tools-engineer | M0-02 |
| M0-09 | `LRCore`: `ULRGameEventSubsystem`, structs de eventos, Gameplay Tags y categorías de log | lead-architect | M0-02 |
| M0-10 | Estándares de código (convenciones de Epic más las nuestras) en `docs/CODING_STANDARDS.md` | lead-architect | — |
| M0-11 | Runner de CI (opcional en M0, obligatorio en M3) | build-release-engineer | M0-04 |

## M1 · Prototipo gris: "¿es divertido?"

**Criterio de salida:**
- Partida de 5 minutos en una autopista gris infinita de 4 carriles, con tráfico que cambia de carril con intermitente, near miss que puntúan, choque que termina la partida y reinicio en menos de 2 s.
- **Más de 90 fps** en la máquina de desarrollo.
- Playtest con al menos 5 personas: lo juegan más de una vez sin que se lo pidan, y ninguna muerte se percibe como "injusta".

| ID | Tarea | Dueño | Depende de |
|---|---|---|---|
| M1-01 | `ULRRoadSubsystem`: coordenadas `(s, d)`, `RoadToWorld` y `WorldToRoad`, más specs de precisión | world-builder | M0-09 |
| M1-02 | Tramos grises y streaming infinito determinista con pool | world-builder | M1-01 |
| M1-03 | `ALRVehicle` con Chaos (malla provisional: plantilla de vehículos o City Sample), subpasos de físicas y `ULRVehicleDefinition` desde JSON | vehicle-physics-engineer | M0-07 |
| M1-04 | Asistente de carril (Arcade) y modo Libre, con dirección sensible a la velocidad | vehicle-physics-engineer | M1-01, M1-03 |
| M1-05 | `ULRTrafficSubsystem`: IDM en un sentido, generación y reciclado, pool de `ALRTrafficVehicle` | traffic-ai-engineer | M1-01 |
| M1-06 | MOBIL con intermitentes y reglas de juego justo (TTC, que siempre haya salida) | traffic-ai-engineer | M1-05 |
| M1-07 | Detección de near miss y su evento en el bus | traffic-ai-engineer | M1-05, M0-09 |
| M1-08 | Estados de partida, `ULRScoreComponent` y combos desde `Scoring.json` | gameplay-engineer | M1-07 |
| M1-09 | Choque del tráfico (paso a física) y fin de partida | traffic-ai-engineer + gameplay-engineer | M1-05, M1-08 |
| M1-10 | Cámaras chase, capó y cabina provisional | camera-engineer | M1-03 |
| M1-11 | Enhanced Input (mando y teclado) y HUD mínimo con ViewModel | ui-engineer | M1-08 |
| M1-12 | **Experimento:** 200 km a 300 km/h, comprobar el temblor con Large World Coordinates (ADR-008) | qa-perf-engineer | M1-02, M1-03 |
| M1-13 | **Experimento:** Chaos Wheeled frente a Modular Vehicle (1-2 días, informe) | vehicle-physics-engineer | M1-03 |
| M1-14 | **Experimento:** PCG en tiempo de ejecución frente a variantes horneadas para los bordes (presupuesto) | world-builder | M1-02 |
| M1-15 | Specs: carriles, IDM, MOBIL, near miss y puntuación; test funcional de 10 min de tráfico | qa-perf-engineer | M1-05…M1-08 |
| M1-16 | Valores iniciales en `Data/` y guion del playtest | game-designer | M1-08 |

## M2 · Sensación de velocidad

**Criterio de salida:** en un playtest a ciegas, la gente describe la velocidad como "rapidísima" y "controlable". En cabina, ninguna persona de 5 se marea en una partida de 5 minutos con los ajustes por defecto.

| ID | Tarea | Dueño |
|---|---|---|
| M2-01 | FOV dinámico, retardo del brazo, vibración por firme y motor (curvas en datos) | camera-engineer |
| M2-02 | Cabeza en cabina con modelo muelle-amortiguador por fuerzas G y mirada anticipada | camera-engineer |
| M2-03 | Ajuste de manejo por arquetipo (3 coches provisionales) y curvas de neumático | vehicle-physics-engineer |
| M2-04 | MetaSound de motor v1 (muestras provisionales) y perfil de audio por coche | audio-engineer |
| M2-05 | Viento, rodadura, juntas rítmicas y *whoosh* de pass-by con Doppler | audio-engineer |
| M2-06 | Objetos de ritmo en los bordes (farolas, postes, juntas) en los tramos grises | world-builder |
| M2-07 | Configuración del desenfoque de movimiento y prototipo de rueda desenfocada | rendering-engineer |
| M2-08 | Vibración del mando según velocidad, near miss y choque | ui-engineer |
| M2-09 | Playtest de sensación y ajuste de datos | game-designer |

## M3 · Vertical slice fotorrealista: "Freeway nocturna"

**Criterio de salida:** los tres criterios de [03-RENDERING.md §9](03-RENDERING.md) (prueba a ciegas al 40 %, 60 fps en el preset Alto en el hardware recomendado y ningún defecto en el clip de 60 s), más un tráiler de 60 s grabado.

| ID | Tarea | Dueño |
|---|---|---|
| M3-01 | **Comprar o encargar** el coche protagonista ficticio con interior completo (especificación técnica de `rendering-engineer`) | **Persona** |
| M3-02 | **Comprar** el kit de autopista y entorno nocturno (Megascans y Fab) y registrarlo en `THIRD_PARTY.md` | **Persona** |
| M3-03 | Hardware de lookdev (ver arquitectura §12) | **Persona** |
| M3-04 | Biblioteca de materiales maestros Substrate (pintura, cristal, caucho, cromo, luces) por scripts y ajuste fino | rendering-engineer + artista técnico |
| M3-05 | Presets de calidad (Bajo a Cinemático), Lumen HWRT y MegaLights | rendering-engineer |
| M3-06 | Ruta nocturna de autor (unos 8 km) con World Partition, montada por scripts y retocada a mano | world-builder + artista de entorno |
| M3-07 | `ALRSkyController` de noche, mojado, charcos y lluvia Niagara | world-builder |
| M3-08 | Luces del tráfico compatibles con MegaLights, variaciones de material y LOD lejano con emisivos | traffic-ai-engineer |
| M3-09 | Interior del coche: instrumentos diegéticos, espejos (`SceneCapture`) y limpiaparabrisas con gotas | camera-engineer + ui-engineer |
| M3-10 | **Cámara Viral** (GoPro, dashcam, móvil) | camera-engineer |
| M3-11 | Integración de DLSS 4.5 y FSR 4.1 (verificar compatibilidad con 5.8) y Reflex | rendering-engineer |
| M3-12 | Benchmark nocturno, línea base de rendimiento en la CI y pasada de optimización | qa-perf-engineer |
| M3-13 | Muestras reales del motor protagonista (licencia o grabación) y mezcla interior/exterior | **Diseñador de sonido** + audio-engineer |
| M3-14 | Prueba a ciegas y grabación del tráiler (Movie Render Queue desde una repetición) | game-designer + camera-engineer |

## M4 · Contenido y progresión

| ID | Tarea | Dueño |
|---|---|---|
| M4-01 | 6-8 coches más (encargo de arte), cada uno con su JSON, manejo y audio | Persona + vehicle-physics-engineer + audio-engineer |
| M4-02 | Garaje: compra, mejoras con efecto físico y personalización | gameplay-engineer + ui-engineer |
| M4-03 | Guardado versionado y migraciones | gameplay-engineer |
| M4-04 | Modos contrarreloj, paseo libre y misiones | gameplay-engineer |
| M4-05 | Repetición (búfer circular) y modo foto | camera-engineer |
| M4-06 | Menús completos con CommonUI y ajustes (gráficos, audio, controles, unidades) | ui-engineer |
| M4-07 | Economía y curva de progresión simuladas (script) | game-designer |

## M5 · Mundo

| ID | Tarea | Dueño |
|---|---|---|
| M5-01 | Ruta "Costa al atardecer" y kit de tramos | world-builder + artista |
| M5-02 | Ruta "Interestatal del desierto" con calima y polvo | world-builder + artista |
| M5-03 | Hora del día y clima dinámico por ruta | world-builder |
| M5-04 | Modo doble sentido (carriles en contra en la simulación de tráfico y su puntuación) | traffic-ai-engineer + gameplay-engineer |
| M5-05 | Variedad del modo infinito: puentes, túneles, rampas y obras | world-builder |
| M5-06 | Reto diario con semilla por fecha | gameplay-engineer |

## M6 · Pulido

| ID | Tarea | Dueño |
|---|---|---|
| M6-01 | Soporte de volante y force feedback | ui-engineer + vehicle-physics-engineer |
| M6-02 | Accesibilidad: daltonismo, intensidad de vibración de cámara, desactivar efectos de la Cámara Viral, subtítulos de interfaz | ui-engineer + camera-engineer |
| M6-03 | Localización en español e inglés (String Tables) | ui-engineer |
| M6-04 | Optimización por preset y precompilación de shaders (PSO cache) para evitar tirones | rendering-engineer + qa-perf-engineer |
| M6-05 | Informes de fallos (Sentry o BugSplat) y telemetría anónima opcional | build-release-engineer |
| M6-06 | Ronda de QA completa y triaje de errores | qa-perf-engineer |

## M7 · Lanzamiento

| ID | Tarea | Dueño |
|---|---|---|
| M7-01 | Steamworks: logros, clasificaciones y guardado en la nube | build-release-engineer + gameplay-engineer |
| M7-02 | Página de Steam, tráiler y capturas (Cámara Viral = contenido para redes) | **Persona** + camera-engineer |
| M7-03 | Demo para Steam Next Fest (1 ruta, 2 coches) | Todos |
| M7-04 | Clasificación por edades (IARC), EULA y revisión legal de nombres | **Persona y asesor legal** |
| M7-05 | Build Shipping firmada y pipeline de parches | build-release-engineer |
