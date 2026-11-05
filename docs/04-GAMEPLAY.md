# 04 · Diseño de juego (GDD núcleo)

> Todos los números de este documento son **valores iniciales**. Viven en `Data/` (JSON) y se ajustan con playtests. El agente `game-designer` es su dueño.

## 1. Bucle principal

```
Garaje ─► Elegir coche, ruta y modo ─► Partida (2-5 min) ─► Choque o meta ─► Repetición ─► Resultados (puntos → créditos)
   ▲                                                                                                      │
   └────────────────────────────── comprar, mejorar o personalizar coches ◄───────────────────────────────┘
```

## 2. Controles

**Dos esquemas, elegibles en el menú (y por partida):**

| Esquema | Cómo funciona | Para quién |
|---|---|---|
| **Arcade (carriles)** | Izquierda o derecha = cambio de carril con asistente (física real debajo). Acelerador y freno analógicos. El "esquivar" fácil de entender, con el feeling de un coche real | Mando, teclado, jugador casual |
| **Libre (simcade)** | Dirección analógica completa, con estabilidad y tracción configurables | Volante y jugador exigente |

| Acción | Mando | Teclado | Volante |
|---|---|---|---|
| Dirección o cambio de carril | Stick izquierdo | A/D o ←/→ | Volante |
| Acelerar / frenar | RT / LT | W / S | Pedales |
| Claxon | Stick izquierdo pulsado | H | Botón |
| Ráfaga de luces | LB | F | Botón |
| Cámara | Y | C | Botón |
| Mirar atrás | RB | Espacio | Botón |
| Cambio manual (opcional) | A / X | Q / E | Levas |

## 3. Sensaciones objetivo

| Métrica | Objetivo inicial |
|---|---|
| Cambio de carril (Arcade) a 200 km/h | 0,6-0,9 s de un centro de carril al siguiente |
| Velocidad del tráfico | 90-130 km/h (camiones de 80 a 100, en los carriles derechos) |
| Velocidad punta de los coches | De 250 (coche inicial) a más de 400 km/h (hiperdeportivo) |
| Duración media de partida | 2-5 min |
| Reinicio | < 2 s |

## 4. Tráfico: catálogo de comportamientos

| Arquetipo | Comportamiento |
|---|---|
| Sedán y SUV "normal" | Sigue el límite ±10 %. Cambia de carril para adelantar, siempre con intermitente |
| Con prisa | Más rápido, cambios de carril frecuentes, distancia de seguridad corta |
| Domingo | Lento, se queda en el carril izquierdo (el clásico tapón) |
| Camión / bus | Lento, en los carriles derechos, largo (más difícil de rebasar) y con estela de aire |
| Conductor agresivo (evento) | Cambio de carril brusco, pero **siempre anunciado** (≥ 1,0 s de intermitente) |
| Patrulla (versión 2) | Fuera de alcance en la versión 1. Posible modo persecución más adelante |

La densidad sube con el tiempo de partida y con la distancia, según una curva definida en `Data/Traffic/DensityCurves.json`.

## 5. Puntuación (`Data/Gameplay/Scoring.json`)

**Puntos por distancia:** 1 punto por cada 10 m × multiplicador de velocidad.

| Velocidad | Multiplicador |
|---|---|
| < 120 km/h | × 0,5 |
| 120-200 km/h | × 1,0 |
| 200-280 km/h | × 1,5 |
| > 280 km/h | × 2,0 |

**Near miss:** `250 × factor_distancia × factor_velocidad_relativa × factor_sentido`

| Categoría | Distancia lateral mínima | Factor |
|---|---|---|
| Cerca | < 1,00 m | × 1 |
| Muy cerca | < 0,50 m | × 2 |
| Al milímetro | < 0,25 m | × 3 |

- `factor_velocidad_relativa = clamp(v_rel / 100 km/h, 0,5, 3,0)`
- `factor_sentido = × 2` contra un coche que viene de frente (modo doble sentido)
- **Enhebrar la aguja:** pasar entre dos coches de carriles adyacentes a la vez, ambos a < 1,0 m, da un bonus × 2.

**Combo:**
- Cada near miss suma 1 a la cadena si llega dentro de una ventana de 3,0 s.
- Multiplicador = `1 + 0,25 × cadena` (máximo × 5).
- Un roce o un frenazo por debajo de 120 km/h rompe la cadena.

**Choque:** un contacto a más de 25 km/h relativos termina la partida. La opción "Arcade indulgente" convierte eso en 2 roces con barra de daño antes de terminar.

## 6. Modos

| Modo | Descripción |
|---|---|
| **Infinito, un sentido** | El modo principal: sobrevive y suma |
| **Infinito, doble sentido** | Carriles en contra con más puntos y más riesgo |
| **Contrarreloj** | Puntos de control: llegar antes de que se acabe el tiempo. Cada near miss suma segundos |
| **Paseo libre** | Sin puntos ni muerte (los choques solo reinician el coche). Ideal para modo foto y Cámara Viral |
| **Reto diario** | Semilla fija para todos, clasificación (Steam), un intento con puntuación al día |
| **Misiones** | Objetivos por ruta: "10 al milímetro en una partida", "2 km por encima de 250 km/h en doble sentido"… |

## 7. Progresión

- **Créditos** según la puntuación. Se compran coches y mejoras.
- **Mejoras por coche** (5 niveles cada una): motor, turbo o compresor, transmisión, neumáticos, frenos, suspensión y aerodinámica. Cambian los parámetros físicos reales, no solo una estadística.
- **Personalización:** pintura (sólida, metalizada, perlada, mate, *wrap*), llantas, pinzas, tintado, color del interior y matrícula personalizada.
- **Desbloqueos:** rutas y modos por hitos de puntos; la Cámara Viral está disponible desde el principio porque es nuestro gancho para redes.

## 8. Plantilla de coches de la versión 1 (marcas ficticias provisionales)

> Diseños **originales** inspirados en arquetipos, no copias. Los nombres son provisionales: hay que buscar marcas registradas antes de fijarlos (ver [07-RISKS.md](07-RISKS.md)).

| # | Coche | Arquetipo | Motor | Rol |
|---|---|---|---|---|
| 1 | **Bauer R3 Sport** | Berlina deportiva compacta alemana | 6 cilindros en línea turbo | Coche inicial |
| 2 | **Aurelion Vanta S** | Berlina ejecutiva de altas prestaciones | V8 biturbo | Primeras compras |
| 3 | **Ironclad Hellion** | Muscle car americano | V8 con compresor | Sonido brutal, conducción nerviosa |
| 4 | **Volta Nine** | Berlina eléctrica hiperdeportiva | Eléctrico, 3 motores | Par instantáneo, sonido sintético |
| 5 | **Kessler Kurve R** | Coupé deportivo alemán de motor trasero | Bóxer de 6 cilindros atmosférico | El deportivo de referencia |
| 6 | **Aurelion Kaiser GT** | Gran turismo deportivo alemán | V8 biturbo | Coche de gama media alta |
| 7 | **Halcourt Regent GT** | Gran turismo de lujo británico | W12 biturbo | Lujo y estabilidad a alta velocidad |
| 8 | **Ardent Vireo** | Superdeportivo británico de motor central | V8 biturbo | Superdeportivo |
| 9 | **Vittore Saetta** | Superdeportivo italiano | V12 atmosférico | El sonido |
| 10 | **Ardent Spectre LM** | Hiperdeportivo | V8 híbrido | Final del juego |

## 9. Choques y tono

- El choque es **físico y espectacular**, con cámara lenta en la repetición, pero sin heridos visibles, sin sangre y sin peatones.
- Aviso al inicio: "Juego de fantasía. Conduce con responsabilidad en la vida real". Ver la nota de clasificación por edades en [07-RISKS.md](07-RISKS.md).

## 10. Fuera de alcance en la versión 1 (posible versión 2)

- **Motos y *lane splitting*:** necesitan un controlador de físicas propio (Chaos no resuelve bien las motos). La arquitectura (`LRVehicle`, coordenadas de carretera) lo permite más adelante.
- Policía y persecuciones; multijugador y fantasmas asíncronos; realidad virtual; consolas.
