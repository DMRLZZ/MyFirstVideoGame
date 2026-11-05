# 00 · Visión del juego

> Nombre en clave: **LEFTRIGHT** (prefijo de código `LR`). El nombre comercial se decide más adelante.

## La idea en una frase

Un juego de conducción en autopista **fotorrealista** en el que zigzagueas entre el tráfico a más de 250 km/h con superdeportivos y coches de lujo, en primera y tercera persona, y que **parece uno de esos vídeos virales grabados con GoPro o dashcam en las autopistas de EE. UU.**

Partidas cortas (2-5 minutos) con la lógica de "esquiva el tráfico y suma puntos" del género *traffic racer*, pero con presentación de nivel AAA.

## Qué es y qué no es

| Es | No es |
|---|---|
| Autopista de 3 a 5 carriles por sentido, con tráfico denso y creíble | Un mundo abierto tipo GTA V (no se baja del coche, no hay ciudad libre) |
| Adelantamientos al límite (near misses) que se premian con puntos y combos | Un simulador de circuito ni un juego de carreras contra rivales |
| Cámara de cabina con interior completo, cámara exterior y "cámara viral" (GoPro/dashcam) | Un juego para móvil: el fotorrealismo que buscamos exige PC |
| Coches de lujo y superdeportivos con su sonido y su detalle | Un clon: ni nombres, ni assets, ni música de Traffic Rider, GTA o Need for Speed |
| PC (Windows/Steam) en la versión 1 | Multijugador en la versión 1 |

**Por qué este alcance:** GTA V lo hicieron más de mil personas para un mundo entero. Nosotros metemos todo el presupuesto visual en **un corredor de autopista**. Esa limitación es lo que hace posible acercarse (y en algunos planos superar) ese nivel visual con un equipo pequeño más agentes.

## Pilares de diseño

Cada decisión se valida contra estos cinco pilares. Si una funcionalidad no sirve a ninguno, queda fuera.

1. **"Parece un vídeo real".** Luz, materiales, cámara y movimiento creíbles. Imitamos cómo graba una cámara real, no solo cómo se ve el mundo (ver [03-RENDERING.md](03-RENDERING.md)).
2. **"La velocidad se siente".** Campo de visión, vibración, sonido del viento, ritmo de farolas y guardarraíles: a 300 km/h tiene que dar vértigo.
3. **"Al milímetro, pero justo".** El riesgo se premia, y las muertes se sienten merecidas: el tráfico es legible (intermitentes, comportamientos coherentes) y nunca hay un muro imposible de coches.
4. **"Un coche que deseas".** Interior completo, sonido de motor propio de cada coche, pintura y detalle de revista.
5. **"Una partida más".** Reinicio en menos de 2 segundos, partidas cortas, progresión clara y retos diarios.

## Fantasía del jugador

> "Son las 2 de la madrugada, la freeway está mojada, voy a 280 en un superdeportivo, el tráfico es una pared de luces rojas y paso entre un camión y un SUV con medio metro de margen. El motor grita, el agua salpica el parabrisas y la cámara lo graba como si fuera real."

## Plataformas y requisitos

| Nivel | Hardware de referencia | Objetivo |
|---|---|---|
| Mínimo | RTX 3060 / RX 6600 XT, 16 GB RAM | 1080p, 60 fps, preset Bajo/Medio (Lumen en calidad media) |
| Recomendado | RTX 4070 / RX 7800 XT, 32 GB RAM | 1440p, 60 fps, preset Alto con DLSS/FSR en calidad |
| Cinemático | RTX 4080/5080 o superior | 4K, Lumen con ray tracing por hardware y modo foto con path tracing |

- **Versión 1:** PC Windows en Steam.
- **Después:** consolas (PS5 / Xbox Series), si la versión 1 funciona. La arquitectura no lo impide, pero no lo priorizamos.

## Ambientaciones de la versión 1 (rutas ficticias inspiradas en EE. UU.)

1. **Freeway nocturna**: autopista urbana de noche, con lluvia opcional. Es el **vertical slice** y el escaparate visual (reflejos en el asfalto mojado y cientos de luces).
2. **Costa al atardecer**: autopista junto al mar con luz dorada.
3. **Interestatal del desierto**: de día, con sol duro, calima y camiones.

## Coches

Entre 8 y 10 coches en la versión 1, todos con **marcas ficticias** inspiradas en arquetipos reales (berlina deportiva alemana, superdeportivo británico, gran turismo de lujo, V12 italiano, muscle car americano, berlina eléctrica). Las licencias de marcas reales son un riesgo legal y económico serio; ver [07-RISKS.md](07-RISKS.md) y la plantilla en [04-GAMEPLAY.md](04-GAMEPLAY.md).

## Documentos relacionados

- [01-RESEARCH.md](01-RESEARCH.md): investigación y fuentes
- [02-ARCHITECTURE.md](02-ARCHITECTURE.md): arquitectura técnica
- [03-RENDERING.md](03-RENDERING.md): plan de fotorrealismo
- [04-GAMEPLAY.md](04-GAMEPLAY.md): diseño de juego
- [05-AGENTS.md](05-AGENTS.md): modelo de trabajo multiagente
- [06-ROADMAP.md](06-ROADMAP.md): hitos y backlog
- [07-RISKS.md](07-RISKS.md): riesgos, legal y licencias
- [08-DECISIONS.md](08-DECISIONS.md): registro de decisiones (ADR)
