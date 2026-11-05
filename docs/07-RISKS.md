# 07 · Riesgos, legal y licencias

> Nada de esto es asesoramiento legal. Antes de publicar, consúltalo con un abogado especializado en propiedad intelectual y videojuegos.

## 1. Riesgos principales

| # | Riesgo | Probabilidad | Impacto | Mitigación |
|---|---|---|---|---|
| R1 | **Assets fotorrealistas caros o lentos de conseguir** (coches con interior, kit de autopista) | Alta | Muy alto | Vertical slice con **un solo** coche protagonista y una sola ruta. Prototipo con assets gratuitos. Presupuesto de arte definido antes de M3 |
| R2 | **Marcas de coches reales** (demandas o cartas de cese) | Alta si se usan | Muy alto | Marcas y diseños ficticios en la versión 1 (ADR-006). Búsqueda de marcas registradas para los nombres. Licencias solo con tracción y abogado |
| R3 | **Rendimiento:** Lumen HWRT, MegaLights y 150 coches a 60 fps | Media | Alto | Presupuestos por sistema, benchmark en la CI desde M3, presets con Lumen en calidad media, LOD del tráfico, DLSS/FSR |
| R4 | **"No es divertido"** pese a verse bien | Media | Muy alto | M1 es gris y su único objetivo es la diversión. No se pasa a M3 sin playtest positivo |
| R5 | **Muertes injustas** por tráfico impredecible | Media | Alto | Intermitentes obligatorios, regla de TTC, comprobación de salida, tests funcionales |
| R6 | **Mareo en cabina** | Media | Medio | FOV configurable, vibración y balanceo ajustables, horizonte estable, test de mareo en M2 |
| R7 | **Experimentales del motor** (Unreal MCP, Lumen en calidad media, Nanite Foliage) inestables | Media | Medio | Ninguna funcionalidad experimental en el camino crítico de juego. Cada una tiene alternativa estable |
| R8 | **Precisión a gran distancia** (más de 100 km) | Baja | Medio | Large World Coordinates, test de 200 km y recentrado como plan B (ADR-008) |
| R9 | **Agentes que rompen cosas en paralelo** | Media | Medio | Propiedad por módulo, revisión obligatoria, tests en verde, un solo agente con el editor a la vez |
| R10 | **Inyección de prompts y código de terceros malicioso** | Baja | Alto | Contenido externo como dato, `researcher` de solo lectura, revisión y aprobación humana de plugins (05-AGENTS §7) |
| R11 | **Hardware de desarrollo insuficiente** para el lookdev | Alta (portátil actual) | Medio | Máquina de 16 GB de VRAM antes de M3 |
| R12 | **Migración a UE6** a mitad de proyecto | Media | Medio | Nos quedamos en 5.8 hasta la versión 1. C++ modular, sin APIs obsoletas, sin depender de plugins abandonados |
| R13 | **Mercado:** otros juegos de esquivar tráfico en 2026 | Media | Medio | Diferenciarse por fotorrealismo, cabina y Cámara Viral. Demo pronto en Next Fest |

## 2. Propiedad intelectual

- **No copiamos juegos existentes.** Nada de nombres, logos, interfaz, música, assets ni diseños de niveles de Traffic Rider, Traffic Racer, GTA, Need for Speed ni similares. Compartimos género y mecánica general (esquivar tráfico), que no son exclusivos de nadie.
- **Coches:**
  - Diseños originales inspirados en *arquetipos* (por ejemplo, "coupé alemán de motor trasero"), no réplicas reconocibles de un modelo concreto. Sin logos, parrillas icónicas ni nombres de modelo reales.
  - Un artista debe recibir el encargo de **diseño original**, no de "copiar un McLaren".
  - Los modelos con etiqueta "Editorial" (TurboSquid y similares) **están prohibidos** en el proyecto.
- **Si en el futuro se licencian marcas reales:** contrato por fabricante, aprobación de cómo se representa el coche (los fabricantes suelen limitar los daños visibles), coste por adelantado más regalías y posibles exclusividades. Es un proyecto aparte, con abogado.
- **Señales de tráfico, matrículas y ciudades:** ficticias. Las fuentes de señalización de EE. UU. de libre uso se pueden usar comprobando su licencia.
- **Música:** solo con licencia para juegos y streaming (que no provoque reclamaciones en Twitch o YouTube: es clave para la viralidad).
- **Assets:** cada asset de terceros se registra en `docs/THIRD_PARTY.md` (origen, licencia, fecha y quién lo compró). Megascans y Fab bajo su licencia vigente en el momento de la compra.

## 3. Clasificación y tono

- Conducción temeraria sin violencia explícita: clasificación estimada **PEGI 7-12 / ESRB E10+-T**. Se confirma con el cuestionario IARC en M7.
- Sin peatones, sin sangre y sin heridos visibles en los choques.
- Aviso de responsabilidad en el arranque y en la tienda. La comunicación nunca anima a imitarlo en vías reales.

## 4. Motor y costes

- **Unreal Engine:** sujeto a la EULA de Epic vigente (históricamente, un 5 % de regalías a partir del primer millón de USD de ingresos brutos por producto). Hay que revisar la versión actual antes de publicar.
- Sumar al presupuesto: assets (coches y entorno), librerías de sonido con licencia, plugins (DLSS y FSR son gratuitos), hardware, alojamiento de LFS y CI, Steam Direct, y asesoría legal.
