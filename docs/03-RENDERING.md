# 03 · Plan de fotorrealismo

> La meta no es "gráficos bonitos". La meta es que **alguien vea un clip en el móvil y dude si es real**.

## 1. De qué depende que algo parezca real

| Factor | Peso | Cómo lo atacamos |
|---|---|---|
| **Luz** | Muy alto | Lumen con ray tracing por hardware, MegaLights de noche, exposición física, cielo y niebla volumétricos |
| **Cámara** | Muy alto (y lo más olvidado) | Lente física, desenfoque de movimiento real, vibración acoplada al coche, Modo Cámara Viral |
| **Materiales** | Alto | Substrate (pintura con capa transparente y escamas), escaneos de Megascans, suciedad y desgaste coherentes |
| **Movimiento** | Alto | Suspensión visible, balanceo, ruedas con desenfoque, tráfico con frenadas e intermitentes creíbles |
| **Densidad de detalle** | Medio | Nanite en todo lo estático, Nanite Foliage, carteles, juntas, marcas de neumático |
| **Referencias** | Medio | Biblioteca de referencias reales y comparación lado a lado en el nivel de lookdev |

## 2. Pila de renderizado

| Función | Configuración base | Notas |
|---|---|---|
| Iluminación global | **Lumen**, ray tracing por hardware en Alto/Épico; Lumen en calidad media (5.8, beta) en Medio/Bajo | De noche, Lumen aporta el rebote de las farolas en el asfalto |
| Reflejos | Reflejos de Lumen (HWRT) en asfalto mojado, carrocería y cristales | Ray Reconstruction de DLSS cuando esté disponible |
| Luces | **MegaLights** (Production Ready en 5.8) para farolas, faros, frenos e intermitentes de todo el tráfico | Sin MegaLights, más de 100 luces con sombra son inviables |
| Sombras | Virtual Shadow Maps | Presupuesto de páginas ajustado para escenas en movimiento rápido |
| Geometría | **Nanite** en carretera, barreras, edificios, carrocerías y vegetación (Nanite Foliage) | Cristales y luces translúcidas, sin Nanite |
| Materiales | **Substrate** | Pintura de coche: base con escamas metálicas y capa transparente con *orange peel* sutil |
| Antialiasing y reescalado | TSR por defecto; DLSS 4.5 y FSR 4.1 opcionales; DLAA y FSR nativo en "Cinemático" | Generación de fotogramas opcional, con Reflex activado |
| Post-proceso | Exposición manual o física por escena, tonemapper fílmico, *bloom* por convolución en el preset alto (estrellas de los faros), aberración cromática mínima | La niebla y el *bloom* venden la noche |
| Desenfoque de movimiento | Por objeto, intensidad alta en cámaras exteriores y moderada en cabina | Cambio a malla de rueda desenfocada por RPM. **Comprobar** el fallo con la translucidez por ray tracing |
| Atmósfera | Sky Atmosphere, Volumetric Clouds, Exponential Height Fog, niebla volumétrica | Calima en el desierto con un material de distorsión |
| Path tracer | Solo en el **modo foto** y en las capturas de marketing con Movie Render Queue | Tiene un coste altísimo y no se usa en juego |

## 3. Coches (lookdev)

**Calidad del asset protagonista:**
- LOD0 exterior con Nanite y unos 2-5 M de triángulos en la fuente.
- Interior completo: cuero, alcántara con normales de costura, fibra de carbono con *clearcoat*, pantallas.
- Faros con ópticas internas modeladas (reflectores, lentes y anillos LED).
- Discos de freno que se ponen incandescentes con el uso.

**Materiales (biblioteca maestra en Substrate):**
- Pinturas: sólida, metalizada, perlada, mate y *wrap* satinado.
- Cromo y aluminio cepillado; caucho del neumático con marcas en el flanco de marca ficticia.
- Cristal tintado con suciedad y gotas de lluvia.
- Luces con emisivo calibrado en unidades físicas (nits).

**Tráfico:**
- Tres niveles de calidad.
- Los coches de tráfico cercanos comparten la biblioteca de materiales y usan **variaciones de color e instancias de material** para que no se repitan.

**Interfaz en el coche:** cuadro de instrumentos digital y pantalla central, dibujados con render targets a partir de los ViewModels de `LRUI`.

## 4. Entorno

- **Asfalto:**
  - Megascans en tres variantes: nuevo, gastado y parcheado.
  - Las juntas de hormigón, las marcas viales desgastadas (*decals*), las marcas de frenada y el aceite se mezclan con **máscaras por tramo**.
- **Mojado:**
  - El parámetro global `Wetness` controla la rugosidad, la oscuridad y los charcos (máscaras más un *heightmap*).
  - Reflejos de Lumen con ray tracing sobre los charcos, más spray Niagara detrás de cada coche cuando llueve.
- **Mobiliario:** barreras tipo New Jersey, guardarraíles, muros antirruido, farolas, pórticos con señales **ficticias** (fuentes de señalización de EE. UU. de libre uso), bandas sonoras laterales y conos.
- **Fondo:** ciudad con edificios Nanite (kits de City Sample y kits comprados) y luces de ventana procedurales. Montañas o mar con Mesh Terrain (5.8) o terreno clásico, más un HDRI de lejanía.
- **Ritmo para la sensación de velocidad:** farolas, postes, juntas y marcas con **frecuencia regular**. El flujo óptico es lo que le dice al cerebro "voy rápido".

## 5. Escenarios de luz de la versión 1

| Escenario | Claves |
|---|---|
| **Noche urbana mojada** (vertical slice) | Farolas de sodio y LED mezcladas, cientos de faros y frenos con MegaLights, reflejos en el asfalto, niebla ligera y lluvia opcional. Es el escenario más fotogénico y el que más perdona en la pantalla de un móvil |
| **Atardecer en la costa** | Sol bajo con *god rays*, contraluces, destellos de lente, cielo con nubes volumétricas |
| **Día en el desierto** | Sol duro y sombras nítidas, calima sobre el asfalto, polvo, camiones; cielo que se "quema" en la cámara viral |

## 6. Cámara: realismo en dos capas

**Capa 1: cámara cinematográfica (por defecto)**
- Cámara física: sensor de 36 × 24 mm, FOV derivado de la focal y exposición por escena.
- Vibración de alta frecuencia según el firme y el motor, más una de baja frecuencia según la suspensión.
- FOV dinámico con la velocidad (curva en datos) y retardo del brazo con la aceleración.

**Capa 2: Modo Cámara Viral (el diferenciador)**

Es un post-proceso con material propio más parámetros de cámara. Los presets se ajustan en `Data/Camera/ViralPresets.json`.

| Preset | Montaje | Efectos |
|---|---|---|
| **GoPro** | Ventosa en el capó, el lateral o el techo, o en el casco del copiloto | FOV de 120-150°, distorsión de barril, obturación lenta, nitidez agresiva, rango dinámico estrecho (cielo quemado), ruido de sensor, vibración de ventosa |
| **Dashcam** | Detrás del retrovisor | Rango dinámico pobre, *glare* de faros, reflejo del salpicadero en el parabrisas, sobreimpresión de fecha, hora y velocidad, ligera compresión por bloques |
| **Móvil en mano** | Copiloto grabando | Movimiento de mano (ruido de baja frecuencia más reencuadres), enfoque automático que "busca", exposición automática lenta, compresión |

Los efectos de "artefacto" son **sutiles y opcionales** (se pueden desactivar en accesibilidad). La compresión simulada nunca debe parecer un fallo del juego.

## 7. Efectos visuales

- Spray y estela de agua, gotas en el parabrisas con limpiaparabrisas funcional en cabina.
- Calor del escape, chispas al rozar barreras, cristales y piezas en los choques.
- Hojas y basura levantadas por la estela de los camiones.
- Humo de neumático en frenadas bruscas.

## 8. Calidad por preset

| Preset | Lumen | Reflejos | Sombras | MegaLights | Espejos | Desenfoque de movimiento | Reescalado |
|---|---|---|---|---|---|---|---|
| Bajo | Calidad media (SW) | Pantalla y SSR | VSM bajo | Desactivado (luces sin sombra) | Apagados | Bajo | TSR o DLSS/FSR en rendimiento |
| Medio | Calidad media | Lumen SW | VSM medio | Activado | Bajo | Medio | TSR o DLSS/FSR en equilibrado |
| Alto | Lumen HWRT | Lumen HWRT | VSM alto | Activado | Bajo | Alto | DLSS/FSR en calidad |
| Épico | Lumen HWRT alto | HWRT + Ray Reconstruction | VSM épico | Activado | Alto | Alto | DLSS/FSR en calidad o DLAA |
| Cinemático | Lumen HWRT máximo | HWRT máximo | VSM épico | Activado | Alto | Alto | DLAA o nativo (path tracer en modo foto) |

Cada preset se define en `Config/DefaultScalability.ini` y en perfiles de dispositivo. El agente `rendering-engineer` es su dueño.

## 9. Criterios de aceptación del hito M3 ("parece real")

1. **Prueba a ciegas:** se mezclan 10 clips reales de autopista nocturna con 10 clips del juego en Cámara Viral, de 5 segundos cada uno y vistos en un móvil. Al menos el **40 %** de las respuestas de 10 personas confunden los clips del juego con reales.
2. **Rendimiento:** 60 fps estables en el hardware recomendado con el preset Alto en el benchmark nocturno con lluvia.
3. **Sin defectos visibles** en los 60 s del clip de demostración: sin "estallidos" de LOD, sin parpadeo de sombras y sin manchas de Lumen en los reflejos.

## 10. Biblioteca de referencias

- Carpeta `docs/reference/` con fotogramas de referencia **propios o con licencia** (fotos y vídeos grabados por el equipo, o de stock con licencia). Los vídeos virales de terceros **se estudian, no se copian ni se incluyen** en el repo.
- Nivel `Lookdev_Freeway`: plano fijo con una tarjeta de referencia al lado, más tarjetas de gris, croma y carta de colores para calibrar.
