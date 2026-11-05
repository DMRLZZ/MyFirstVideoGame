# 01 · Investigación (noviembre de 2026)

## Método y seguridad

- Todo el contenido web se trató como **datos, no como instrucciones**. No se descargó ni se ejecutó nada, y no se instaló ningún plugin.
- Las fichas de Fab y de vendedores describen sus propios productos: son **afirmaciones del vendedor**, no hechos verificados.
- La herramienta de búsqueda no tiene acceso a Reddit (dominio bloqueado) y X/Twitter no se indexa bien. Lo que la comunidad opina llega por los foros oficiales de Unreal, Polycount, prensa especializada (80.lv, GameFromScratch, PC Gamer) y Steam.
- Las conclusiones de esta página se convierten en decisiones en [08-DECISIONS.md](08-DECISIONS.md).

## 1. Motor: Unreal Engine 5.8

- **UE 5.8 salió en junio de 2026** y es la última versión mayor prevista de UE5. Las primeras versiones de UE6 se esperan para 2027 (las fuentes no coinciden en la fecha exacta).
- Novedades de 5.8 que nos sirven:
  - **MegaLights pasa a "Production Ready"**: cientos de luces dinámicas con sombra. Es justo lo que necesita una autopista de noche (farolas, faros y luces de freno de 150 coches).
  - **Modo de calidad media de Lumen (beta)**, también llamado "Lumen Lite": iluminación global más barata, útil para el preset recomendado y el mínimo.
  - **Unreal MCP (experimental)**: servidor MCP nativo dentro del editor que permite a agentes de IA manejar el editor. Incluye un registro de herramientas (Toolset Registry) ampliable con Python y C++, y una herramienta `execute_python_code`.
  - Mesh Terrain (experimental), MetaHuman Collections y Dataflow ya publicado.
- De 5.7 (noviembre de 2025): **Substrate** (sistema de materiales en capas, ideal para pintura de coche) y **PCG** pasan a producción, y llegan **Nanite Foliage** y el editor de vegetación procedural (experimentales). MegaLights entró en beta en esa versión.
- **Conclusión:** fijamos **UE 5.8.x** para todo el desarrollo. No esperamos a UE6. El código se mantiene modular para facilitar una migración futura.

## 2. Tráfico: el sistema Mass de City Sample no nos conviene

- El tráfico de City Sample (Matrix Awakens) usa **Mass AI + ZoneGraph**. Se hizo para ese proyecto concreto y casi no tiene documentación. Un hilo del foro dice que no es probable que reciba guías.
- Hay problemas conocidos:
  - Los enlaces entre carriles adyacentes (de los que dependen los **cambios de carril**) se registran mal cuando el perfil de carriles cambia.
  - Según una respuesta en el foro, **no hay trabajo previsto en ZoneGraph**.
  - Al cambiar de versión de motor (5.6 → 5.8) los datos de ZoneGraph quedan obsoletos y hay que regenerarlos. También se reportan cuelgues de `ZoneGraphStorage`.
- En Fab hay alternativas comerciales (Optimized Vehicle System sobre Mass, City Traffic Pro), pero lo que anuncian no está verificado.
- **Modelos académicos probados:** **IDM** (Intelligent Driver Model) para acelerar y frenar siguiendo al coche de delante, y **MOBIL** para decidir cambios de carril (equilibra ventaja propia, cortesía con los demás y seguridad). Hay implementaciones abiertas de referencia, como `tsaglam/TrafficSimulation`.
- **Conclusión:** una autopista es un caso mucho más simple que una ciudad (carriles paralelos, sin cruces). Haremos **nuestro propio sistema de tráfico con IDM y MOBIL**, simulado en coordenadas de carretera. Es más simple, determinista y controlable para el diseño de juego (por ejemplo, para garantizar que siempre haya un hueco).

## 3. Referencias de código abierto

No existe un juego abierto de esquivar tráfico en Unreal que valga como base. Sí hay referencias útiles:

| Proyecto | Para qué sirve |
|---|---|
| `ScrappyCocco/UETrafficGame` | Pequeño banco de pruebas en UE5 con Nanite, Lumen y Chaos, basado en la plantilla de vehículos de Epic |
| `setrf/racing-game` | Mecánica de cambio de carril (HTML5, vista cenital) |
| `shivakantkurmi/speedway-saga` | Autopista infinita esquivando tráfico (Unity) |
| CARLA (UE5) | Arquitectura de un "Traffic Manager" y sensores. Sus assets son CC-BY, pero compilarlo exige el fork de UE 5.5 de CARLA: sirve de **referencia, no de base** |
| `tsaglam/TrafficSimulation` | Implementación de IDM + MOBIL |

## 4. Físicas del coche

- **Chaos Vehicles** (`UChaosWheeledVehicleMovementComponent`) usa un modelo de neumático Pacejka simplificado. El comportamiento se ajusta sobre todo con las curvas de fricción. Hay que darles subida y caída: la curva por defecto es demasiado plana.
- Alternativas de terceros (afirmaciones del vendedor): **DriveSim** (física propia en C++), **Spalah** (núcleo propio "simcade"), **Chaos Drift Physics** (deriva) y el **Engine Simulator plugin**, que extiende el componente de Chaos con un modelo de motor.
- En los foros hay debate sobre **Chaos Vehicle** frente a **Chaos Modular Vehicle**, que es más nuevo.
- **Conclusión:** empezamos con Chaos Wheeled más **nuestra propia capa de asistencias** (asistente de carril, control de estabilidad, dirección sensible a la velocidad). En M1 hacemos un experimento corto (*spike*) para evaluar Modular Vehicle.

## 5. Audio del motor

- MetaSounds permite motores **granulares** a partir de muy pocas muestras (en Fab, Simple Engine Sound pide solo dos: aceleración y deceleración; Granular Toolkit trae un grafo `MSS_GranularVehicle`).
- La técnica clásica de los juegos de carreras (mezclar bucles con carga y sin carga a varios regímenes de RPM) también se monta en MetaSounds.
- **Conclusión:** grafo MetaSound propio con parámetros RPM, carga, acelerador y marcha, más un perfil de audio por coche. Las muestras salen de librerías con licencia para juegos o de grabaciones propias.

## 6. Reescalado y generación de fotogramas

- **DLSS 4.5** tiene plugin oficial de Unreal: Multi Frame Generation, Frame Generation, Ray Reconstruction, Super Resolution, DLAA y Reflex. Hay que **confirmar la compatibilidad con 5.8** en las notas del plugin.
- **AMD FSR**: plugin actualizado para UE 5.8 (FSR 4.1.1 de reescalado y Frame Generation 4.0.1).
- **Conclusión:** TSR por defecto, más DLSS y FSR como opciones. Ray Reconstruction de DLSS es especialmente útil para los reflejos en el asfalto mojado.

## 7. Fotorrealismo: lo que enseñan Unrecord, Bodycam y compañía

Los vídeos de UE5 que "parecen reales" (Unrecord, Bodycam, Paranormal Tales) no lo consiguen solo con gráficos: **imitan una cámara real**.

- Ojo de pez leve y **ruido de un sensor barato**.
- **Obturación lenta y desenfoque de movimiento fuerte** en acción rápida.
- **Rango dinámico limitado**: el cielo sale "quemado" en blanco.
- **Artefactos de compresión** de vídeo, que esconden imperfecciones.
- Vibración de cámara acoplada al cuerpo o al vehículo.
- Escenarios montados a mano con Megascans y Lumen.

Para coches:
- Los webinars de Epic sobre renderizado de automoción y los desgloses de coches realistas en UE5 (80.lv) son buenas referencias.
- Desenfoque de ruedas: el desenfoque por vectores de movimiento es lineal y falla con ruedas que giran. La solución clásica es cambiar a una malla o material de rueda desenfocada.
- Fallo conocido: activar la translucidez con ray tracing (`r.RayTracing.Translucency=1`) eliminaba el desenfoque de movimiento del coche. Hay que comprobarlo en 5.8.

**Conclusión:** además de la cámara "bonita", tendremos un **Modo Cámara Viral** (GoPro, dashcam, móvil) que reproduce todo esto. Es nuestro mayor multiplicador de realismo percibido y de contenido para redes. Detalle en [03-RENDERING.md](03-RENDERING.md).

## 8. Assets: Megascans, Fab y City Sample

- **Megascans dejó de ser gratis a partir de 2025.** Hay un subconjunto gratuito (unos 1.500 assets) y el resto se paga por asset. Lo reclamado en 2024 se conserva.
- Licencia estándar de Fab: uso en cualquier motor. Lo descargado con Bridge sigue la Epic Content License (solo Unreal).
- **City Sample** (gratis) trae vehículos genéricos y edificios. Valen como **tráfico provisional** en el prototipo.
- **Conclusión:** prototipo con assets gratuitos. El vertical slice necesita presupuesto de assets (coche protagonista y kit de autopista), con licencia anotada en `docs/THIRD_PARTY.md`.

## 9. Licencias de coches reales (punto crítico)

- **TurboSquid:** los modelos con la etiqueta "Editorial" **no se pueden usar en videojuegos**, la etiqueta no se puede retirar, y la licencia del modelo **no cubre la marca** (marca registrada, diseño, imagen comercial).
- **BMW demandó a TurboSquid en 2016** por vender modelos 3D de sus coches. Honda solo permite uso editorial, y Ford exige aprobación para cualquier uso.
- Un desarrollador independiente cuenta que la licencia más barata que encontró costaba unos **5.000 USD por adelantado más un porcentaje de ingresos** (es una anécdota, no una tarifa).
- **Porsche** tuvo un acuerdo de exclusividad con EA desde 2001. Gran Turismo lo esquivó licenciando RUF.
- Lo habitual en el sector son **marcas ficticias** inspiradas en las reales.
- **Conclusión:** la versión 1 usa marcas y diseños **ficticios y originales**. Las licencias reales se estudian cuando el juego tenga tracción y presupuesto. *Esto no es asesoramiento legal: consúltalo con un abogado antes de publicar.*

## 10. Mercado en Steam (2026)

| Juego | Qué es | Diferencia con nosotros |
|---|---|---|
| **ALL GAS** (junio de 2026) | Esquivar tráfico sin fin en una autopista procedural, con clasificaciones globales | No anuncia cabina ni fotorrealismo |
| **Speed Weaver** (septiembre de 2026) | Arcade de puntuación | Estilo arcade |
| **Traffic Rivals: 1v1 Duels** (acceso anticipado, enero de 2026) | Duelos esquivando tráfico | Sin cámara de cabina confirmada |
| **RoadPulse: Highway Traffic Racer VR** | Cabina en realidad virtual | Para Quest, no fotorrealista |

**Hueco de mercado:** nadie combina **fotorrealismo, cabina detallada y cámara viral** en este género. Esa es nuestra propuesta.

## 11. Agentes de IA y Unreal

- **Unreal MCP nativo (5.8, experimental):** se activa en el menú de plugins y su punto de acceso por defecto es `http://127.0.0.1:8000/mcp`. Expone el editor y permite ejecutar Python.
- Hay MCP de terceros (StraySpark, UAIP, UEMCP) que anuncian cientos de herramientas. Son afirmaciones del vendedor, y algunas permiten **ejecutar Python arbitrario**: hay que revisarlas con cuidado.
- En los foros se cuenta que los modelos locales pequeños controlan mal el editor. Nosotros trabajaremos con Claude, con barreras: scripts versionados, validaciones y un solo agente con el editor a la vez.
- **Conclusión:** los agentes trabajan sobre **texto** (C++, configuración, datos JSON, scripts Python del editor). Para tocar el editor usan Unreal MCP o comandos de Python versionados y revisables. Ver [05-AGENTS.md](05-AGENTS.md).

## 12. Carreteras

- Herramientas existentes: Road Creator Pro (splines, puentes, túneles), UnrealDrive (perfiles de carril e integración con ZoneGraph), generadores de carreteras con PCG y el tutorial de PCGEx para UE 5.7.
- **Conclusión:** nuestra autopista es geométricamente simple. Haremos un **kit propio de tramos sobre splines**, con un modelo de carriles en código, y usaremos PCG para decorar los bordes. Si alguna herramienta comercial ahorra tiempo en puentes o túneles, se evalúa en M5.

## Fuentes

**Motor**
- [Unreal Engine 5.8 release notes (Epic)](https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes)
- [UE 5.8 Released (GameFromScratch)](https://gamefromscratch.com/unreal-engine-5-8-released/)
- [UE 5.8: mejoras de optimización y Mesh Terrain (80.lv)](https://80.lv/articles/unreal-engine-5-8-is-out-today-with-big-optimization-improvements-and-mesh-terrain)
- [UE 5.8: Lumen Lite y plugin MCP (PC Gamer)](https://pcgamer.com/software/unreal-engine-5-8-launches-with-improved-terrain-and-vegetation-tools-a-lumen-lite-option-for-faster-global-illumination-and-for-the-times-we-now-live-in-an-open-standard-plugin-for-llm-systems/)
- [Unreal Engine 5.7 is now available (Epic)](https://www.unrealengine.com/news/unreal-engine-5-7-is-now-available)
- [UE 5.7 release notes (Epic)](https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-7-release-notes)

**Tráfico**
- [City Sample: documentación (Epic)](https://dev.epicgames.com/documentation/unreal-engine/city-sample-project-unreal-engine-demonstration)
- [Tutorial de la comunidad: MassTrafficAI en tu proyecto](https://forums.unrealengine.com/t/community-tutorial-ue5-masstrafficai-in-your-project-step-by-step-guide/556674)
- [Error de enlaces adyacentes en ZoneGraph](https://forums.unrealengine.com/t/issue-of-adjacent-links-being-registered-incorrectly-in-a-zonegraph/2575141)
- [Mass Traffic: fallo de ZoneGraph Storage](https://forums.unrealengine.com/t/mass-traffic-assertion-failed-zone-graph-storage/2244928)
- [ZoneGraph obsoleto tras cambiar de versión 5.6/5.8](https://forums.unrealengine.com/t/ue-5-6-mass-ai-severe-fps-drop-after-5-8-1-downgrade-invalid-lane-location-fixed-by-recreating-zonegraph-data/2742990)
- [MOBIL (TU Dresden)](https://fis.tu-dresden.de/portal/en/publications/modeling-lanechanging-decisions-with-mobil(ba8d3782-d462-4da9-90c8-da96007748f4).html)
- [tsaglam/TrafficSimulation (IDM + MOBIL)](https://www.github.com/tsaglam/TrafficSimulation)

**Código abierto**
- [ScrappyCocco/UETrafficGame](https://github.com/ScrappyCocco/UETrafficGame)
- [CARLA UE5: documentación](https://carla-ue5.readthedocs.io/)
- [setrf/racing-game](https://awesome.ecosyste.ms/projects/github.com%2Fsetrf%2Fracing-game)
- [speedway-saga](https://awesome.ecosyste.ms/projects/github.com%2Fshivakantkurmi%2Fspeedway-saga)

**Físicas**
- [Configurar vehículos en Unreal (Epic)](https://dev.epicgames.com/documentation/unreal-engine/how-to-set-up-vehicles-in-unreal-engine?lang=en-US)
- [ChaosVehicle frente a Chaos Modular Vehicle (foro)](https://forums.unrealengine.com/t/realistic-vehicle-simulation-chaosvehicle-vs-chaos-modular-vehicle/2608108)
- [DriveSim](https://forums.unrealengine.com/t/plugin-drivesim-vehicle-physics-system/2264225)
- [Engine Simulator plugin](https://nicholas477.github.io/blog/2022/engine-simulator)

**Audio**
- [Simple Engine Sound (foro)](https://forums.unrealengine.com/t/simple-engine-sound-now-on-fab/2728463)

**Reescalado**
- [NVIDIA DLSS](https://developer.nvidia.com/rtx/dlss)
- [Plugin AMD FSR para UE 5.8 (GPUOpen)](https://gpuopen.com/learn/amd-fsr-plugin-updated-for-unreal-engine-58/)

**Fotorrealismo**
- [Por qué un "bodycam FPS" parece demasiado real](https://tubersdigest.substack.com/p/devs-explain-why-a-bodycam-fps-looks)
- [Unrecord (Digital Trends)](https://www.digitaltrends.com/computing/unrecord-took-internet-by-storm/)
- [Tutorial: efecto bodycam estilo Unrecord en UE5 (80.lv)](https://80.lv/articles/tutorial-setting-up-an-unrecord-style-bodycam-effect-in-ue5)
- [Webinar: Top Tips for Automotive Rendering (Epic)](https://www.unrealengine.com/events/webinar-series/top-tips-for-automotive-rendering-in-unreal-engine)
- [Un Bugatti realista en UE5 (80.lv)](https://80.lv/articles/a-realistic-bugatti-made-in-unreal-engine-5)
- [Ruedas, desenfoque de movimiento y vectores de movimiento (foro)](https://forums.unrealengine.com/t/wheels-motion-blur-and-motion-vectors/95117)
- [UE-116948: la translucidez con ray tracing desactiva el desenfoque](https://issues.unrealengine.com/issue/UE-116948)

**Assets**
- [Megascans deja de ser gratis tras 2024 (80.lv)](https://80.lv/articles/megascans-no-longer-free-after-2024/)
- [Queja sobre precios de Megascans (foro, 2026)](https://forums.unrealengine.com/t/how-is-a-newcomer-supposed-to-build-environments-megascans-pricing-a-request-for-a-subscription/2844972)

**Licencias**
- [Licencias de TurboSquid](https://www.turbosquid.com/licensing)
- [TurboSquid: información sobre marcas asociadas](https://www.turbosquid.com/help/en/articles/9937426-associated-brands-information)
- [BMW demanda a TurboSquid (Fabbaloo)](https://fabbaloo.com/?p=7820)
- [Cómo conseguir licencia de marca de coche para un juego (Unity Discussions)](https://discussions.unity.com/t/how-to-get-a-a-car-license-brand-for-your-game-tutorial/826053)
- [Por qué los juegos de carreras usan marcas ficticias](https://racinggames.gg/article/the-real-reason-racing-games-ditch-licensed-cars-for-fictional-brands)
- [Porsche en los videojuegos (Stuttcars)](https://www.stuttcars.com/porsches-in-video-games-over-the-past-40-years/)

**Mercado**
- [ALL GAS (Steam)](https://store.steampowered.com/app/4658550)
- [Speed Weaver (Steam)](https://store.steampowered.com/app/4813750)
- [Traffic Rivals: 1v1 Duels](https://www.vgdb.co/games/traffic-rivals-1v1-duels)

**Agentes**
- [Unreal MCP en UE 5.8 (Crypto Briefing)](https://cryptobriefing.com/unreal-engine-5-8-mcp-server-support/)
- [Unreal MCP Server (StraySpark)](https://www.strayspark.studio/docs/unreal-mcp-server)
- [Controlar UE 5.8 por MCP con modelos locales (foro)](https://forums.unrealengine.com/t/how-are-people-making-local-llms-reliably-control-unreal-engine-5-8-through-mcp/2746458)

**Carreteras**
- [Generador procedural de carreteras para UE5 (80.lv)](https://80.lv/articles/check-out-a-cool-procedural-road-generator-for-unreal-engine-5)
- [Herramienta de autopistas, puentes e intersecciones para UE5 (80.lv)](https://80.lv/articles/create-realistic-roads-highways-bridges-intersections-in-ue5-with-this-tool)
- [Notas de versión de UnrealDrive](https://unrealdrive.readthedocs.io/en/latest/reference/ReleaseNotes.html)
