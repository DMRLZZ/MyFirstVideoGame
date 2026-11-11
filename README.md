<p align="center">
  <img src="docs/media/logo_banner.png" alt="LEFTRIGHT" width="100%">
</p>

<p align="center">
  <b>Esquiva el tráfico de la autopista a más de 250 km/h, al milímetro.</b><br>
  Juego de conducción para PC hecho en Unreal Engine 5.8 y C++.
</p>

<p align="center">
  <a href="https://github.com/DMRLZZ/MyFirstVideoGame/releases/latest"><b>⬇ Descargar para Windows</b></a>
  &nbsp;·&nbsp;
  <a href="#cómo-se-juega">Controles</a>
  &nbsp;·&nbsp;
  <a href="#compilar-desde-el-código">Compilar</a>
  &nbsp;·&nbsp;
  <a href="docs/00-VISION.md">Visión</a>
</p>

---

## Qué es

Una autopista sin fin, tráfico denso y un superdeportivo. Pasas entre los coches con el menor margen posible, sumas puntos por cada adelantamiento al límite y encadenas combos hasta que un choque acaba la partida. Partidas cortas, reinicio inmediato, "una más".

<p align="center">
  <img src="docs/media/bauer_r3_sport_front34.png" alt="Bauer R3 Sport" width="32%">
  <img src="docs/media/aurelion_kaiser_gt_front34.png" alt="Aurelion Kaiser GT" width="32%">
  <img src="docs/media/ardent_vireo_front34.png" alt="Ardent Vireo" width="32%">
</p>

- **Cinco coches** con su motor, su peso y su manejo: Bauer R3 Sport, Aurelion Vanta S, Aurelion Kaiser GT, Ironclad Hellion y Ardent Vireo.
- **Tráfico con cabeza:** los coches aceleran, frenan y cambian de carril como conductores de verdad (modelos IDM y MOBIL).
- **Autopista procedural** entre colinas, con árboles, rocas, pórticos de señales y pasos elevados, generada sin fin.
- **Dirección suave** pensada para teclado y mando, más los esquemas por carriles y simulación.
- **Música propia** sin derechos de terceros, y la tuya: copia tus MP3 en una carpeta y suenan en el juego.
- **Cámara exterior y de cabina**, HUD con puntos, combos y velocidad, y menú de pausa con ajustes.

Todas las marcas de coches son ficticias. Los coches, las texturas, el escenario y la música se generan por código en este repositorio.

## Instalar y jugar

1. Descarga `LeftRight-Windows.zip` de la [última versión](https://github.com/DMRLZZ/MyFirstVideoGame/releases/latest).
2. Descomprímelo en cualquier carpeta (clic derecho → **Extraer todo**).
3. Abre **`LeftRight.exe`**.

Si Windows muestra "Windows protegió su PC", pulsa **Más información → Ejecutar de todas formas**: el juego no está firmado digitalmente. Si falta el runtime de Visual C++, el propio juego ofrece instalarlo la primera vez.

**Para desinstalar,** borra la carpeta. Los ajustes quedan en `%LOCALAPPDATA%\LeftRight` y tu música en `Documentos\LeftRight\Music`.

### Requisitos

| | Equipo de prueba | Recomendado |
|---|---|---|
| Sistema | Windows 11 de 64 bits | Windows 10 u 11 de 64 bits |
| GPU | NVIDIA Quadro RTX 3000, 6 GB | RTX 4070 o superior |
| CPU | Intel Core i7-10750H | 8 núcleos o más |
| RAM | 32 GB | 16 GB o más |
| Disco | 1 GB libre | SSD |

En el equipo de prueba, un portátil, el juego mantiene unos 60 fps a 1080p con resolución dinámica.

## Cómo se juega

| Acción | Teclado | Mando |
|---|---|---|
| Acelerar | W | RT |
| Frenar | S | LT |
| Moverse a los lados | A / D | Stick izquierdo |
| Freno de mano | Espacio | B / Círculo |
| Cambiar de cámara | C | Y / Triángulo |
| Mirar atrás | B | RB |
| Cambiar de coche | V | Cruceta arriba |
| Siguiente canción | M | Cruceta abajo |
| Reiniciar | R | View / Share |
| Menú y ajustes | Esc | Menu / Options |

**Tu música:** en el menú (Esc → Ajustes → *Añadir mi música*) se abre la carpeta `Documentos\LeftRight\Music`. Copia ahí tus MP3, M4A, WAV o FLAC y pulsa *Buscar música nueva*.

## Compilar desde el código

Necesitas Windows, [Unreal Engine 5.8](https://www.unrealengine.com/), Visual Studio 2022 con C++ y [Git LFS](https://git-lfs.com/).

```powershell
git lfs install
git clone https://github.com/DMRLZZ/MyFirstVideoGame.git
cd MyFirstVideoGame
$env:UE_ROOT = "C:\Program Files\Epic Games\UE_5.8"

Tools/Scripts/build.ps1           # compila el editor
Tools/Scripts/test.ps1            # tests automáticos
Tools/Scripts/package.ps1         # build de Windows en Packaged/
```

La arquitectura, los presupuestos de rendimiento y las decisiones están en [`docs/`](docs/). Empieza por [00-VISION](docs/00-VISION.md) y [02-ARCHITECTURE](docs/02-ARCHITECTURE.md).

## Estado

Prototipo jugable (hito M1). La conducción, el tráfico, la puntuación, el menú y la música funcionan; el aspecto fotorrealista final llega con los assets del hito M3. Lo que viene está en el [roadmap](docs/06-ROADMAP.md).

---

<p align="center">
  <img src="docs/media/logo_icon.png" alt="LR" width="96"><br>
  <sub>Copyright LEFTRIGHT. Todos los derechos reservados.</sub>
</p>
