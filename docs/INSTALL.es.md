# Instalar Spider-Man 2 (Xbox) en la Switch

[![English](https://img.shields.io/badge/lang-English-1c2233?style=for-the-badge)](INSTALL.md)
[![Español](https://img.shields.io/badge/idioma-Espa%C3%B1ol-dc1e2e?style=for-the-badge)](INSTALL.es.md)

[← Volver al README](../README.es.md)

> [!TIP]
> **¿Nuevo en el homebrew de Switch?** Primero necesitas una consola con firmware personalizado (Atmosphère)
> y el Homebrew Menu. La guía de la comunidad [switch.hacks.guide](https://switch.hacks.guide/) explica cómo,
> paso a paso (en inglés). Las palabras que se usan en esta página se explican en el [glosario](#glosario).

El port se hace con **tu propia copia** del juego, de una de dos formas. La rápida: la
[página de instalación](#la-página-de-instalación), en el navegador de tu móvil o computador, desbloquea el `.nro`
bloqueado de la release con tu juego, en un minuto. La otra: [compilarlo](#compilar-el-port) a partir de tu juego en
un computador con Windows o Linux. De cualquier forma el resultado es un `.nro` hecho con tu copia, que va a la
tarjeta SD junto con tu juego.

- [Cómo conseguir tu juego](#cómo-conseguir-tu-juego)
  - [Con una Xbox modificada](#con-una-xbox-modificada)
  - [Los formatos que lee el port](#los-formatos-que-lee-el-port)
- [La página de instalación](#la-página-de-instalación)
- [Compilar el port](#compilar-el-port) (en lugar de la página de instalación)
  - [En Windows](#en-windows)
  - [En Linux](#en-linux)
  - [Instalar las herramientas a mano](#instalar-las-herramientas-a-mano)
  - [En Windows, a mano](#en-windows-a-mano)
  - [Lo que dice el compilador](#lo-que-dice-el-compilador)
- [Copiarlo a la tarjeta SD](#copiarlo-a-la-tarjeta-sd)
- [La primera vez](#la-primera-vez)
- [Partidas](#partidas)
- [Actualizar](#actualizar)
- [Solución de problemas](#solución-de-problemas)
- [Reportar un error](#reportar-un-error)
- [Glosario](#glosario)

## Cómo conseguir tu juego

Necesitas *Spider-Man 2* para la **Xbox original**, la edición cuyo `default.xbe` tiene el SHA-1
`853bcb253c5baea2d7e2d9b59e5888b4c87bb402` (ID de título `4156002B`, versión 4, la edición de EE. UU.). Desde la
0.4.0 también sirve otra edición si su código de juego es el mismo (`b3d6d36a707840a9dcc047f0302b41edc61dfee2`: el
programa en sí, sin la cabecera que nombra su región), como puede serlo una europea. El compilador, la página de
instalación y el port lo comprueban, así que no tienes que averiguarlo tú. El port nunca descarga nada: usa la
copia que le das.

### Con una Xbox modificada

Una unidad de DVD de PC normal no puede leer los discos de juegos de la Xbox original. La forma habitual de
copiar tu propio disco es una Xbox modificada:

1. En la Xbox, una herramienta como [DVD2Xbox](https://consolemods.org/wiki/Xbox:DVD2Xbox) copia el disco al
   disco duro de la Xbox, como archivos ("Copy DVD/CD to Harddisk") o como ISO ("ISO Ripper", una imagen
   completa del disco dividida en partes).
2. Copia esa carpeta o esos archivos a tu computador por la red, con un programa de FTP como FileZilla (los
   dashboards de una Xbox modificada traen un servidor FTP).

Copiar un disco con un PC solo es posible con unidades muy particulares; la [wiki de Redump](http://wiki.redump.org/)
(en inglés) tiene los detalles. Sea cual sea la herramienta, el resultado son los archivos del juego
(`default.xbe` y la carpeta `data`) o una imagen de disco, y el port acepta las dos cosas.

### Los formatos que lee el port

| Lo que tienes | ¿Funciona? |
|---|---|
| Los archivos del disco: `default.xbe` y la carpeta `data` | ✅ |
| `.iso` o `.xiso`: una XISO recortada (como la que hacen `extract-xiso`, `xdvdfs` o Repackinator) | ✅ |
| `.iso`: un volcado completo del disco (con la partición de video al principio, como los de Redump) | ✅ |
| `.cci`: comprimida, como la hace Repackinator para Cerbios | ✅ |
| `.cso`: comprimida (versión 2, LZ4), como la hacen Repackinator, `ciso` o `xdvdfs` para StellarOS | ✅ |
| Cualquiera de las anteriores dividida en partes: `nombre.1.iso`, `nombre.2.iso`, ... (también `.1.cci`, `.1.cso`) | ✅ pon todas las partes en la carpeta |
| `.zip`, `.7z`, `.rar` | ❌ extrae primero la imagen o la carpeta que traen |
| Una imagen de disco de Xbox 360, o un CSO como los de la PSP (versión 1, zlib) | ❌ |

Si una herramienta dividió tu imagen con otros nombres, renombra las partes a `nombre.1.iso`, `nombre.2.iso`,
etc., en orden. En una tarjeta SD con FAT32 ningún archivo puede pasar de 4 GB: un volcado completo del disco
tiene que ir dividido, o convertido en una XISO recortada o en un `.cci` / `.cso` (este juego ocupa unos 1,7 GB
como XISO y 1,5 GB comprimido). La [guía de herramientas ISO de ConsoleMods](https://consolemods.org/wiki/Xbox:ISO_Extraction_%26_Repacking)
(en inglés) lista las herramientas que convierten entre estos formatos.

## La página de instalación

**[santibros.github.io/SpiderMan2-2004-NX](https://santibros.github.io/SpiderMan2-2004-NX/)**, en un móvil Android o un computador, en cualquier
navegador reciente (Chrome, Firefox, Edge, Safari, Samsung Internet; en iPhone no se ha probado).

1. **Elige tu juego:** tu imagen de disco (`.iso`, `.xiso`, `.cci`, `.cso`; si está dividida, selecciona todas sus
   partes a la vez) o solo su `default.xbe`. La página lee el `default.xbe` del juego y comprueba que sea la
   edición compatible o (para la 0.4.0 y posteriores) otra edición con su código de juego. Todas las versiones
   aceptan los mismos archivos.
2. **Elige la versión**, o deja la más reciente, que ya está elegida. El menú tiene todas desde la 0.0.8 y dice cuál
   es la recomendada (la más reciente), cuáles funcionan en una Switch con problemas conocidos (0.1.0 a 0.3.0) y
   cuáles son pruebas tempranas que no arrancan en ella (0.0.8 a 0.0.10). Debajo están los cambios principales de
   la versión, un enlace a todas sus notas y qué tener en cuenta: antes de la 0.2.3 las partidas guardadas no
   cargan, y las pruebas tempranas van en una carpeta propia, `switch/SpiderMan2Xbox-<versión>`, para que nunca
   toquen tus partidas. `?v=0.2.3` al final de la dirección de la página la abre con esa versión elegida.
3. **Descarga `SpiderMan2Xbox-SD.zip`.** La página desbloquea el port con tu `default.xbe` y lo pone en un zip con
   la carpeta `switch` para tu tarjeta SD. Tarda unos segundos.
4. Sigue con [Copiarlo a la tarjeta SD](#copiarlo-a-la-tarjeta-sd).

Tu juego nunca se sube a ningún sitio: la página solo descarga el port bloqueado y hace el resto en tu navegador.
Sin el `default.xbe` del juego compatible el port bloqueado no se puede abrir, y por eso puede estar en las
releases. En un computador, `python3 tools/lock_nro.py unlock SpiderMan2Xbox-<versión>.nro.locked default.xbe
SpiderMan2Xbox.nro` hace lo mismo.

> [!TIP]
> En un móvil, la página puede leer el juego desde el almacenamiento del móvil, o desde la tarjeta SD de la Switch
> en un lector de tarjetas (los adaptadores USB-C funcionan en la mayoría de móviles Android). Si el navegador no
> ve el archivo, cópialo primero a Descargas en el móvil.

## Compilar el port

En lugar de usar la página de instalación, el port se puede compilar a partir de tu juego en un computador. El
compilador crea `SpiderMan2Xbox.nro` a partir de tu propia copia: comprueba tu copia, recompila el código
del juego y compila el port, en 10 a 30 minutos según el computador, más unos 10 minutos la primera vez para
instalar sus herramientas. Necesita unos 2 GB de espacio, y sitio para una copia de tu juego cuando también lo
pone en la carpeta de la tarjeta SD.

Primero descarga `SpiderMan2-2004-NX-<versión>.zip` de la página de [Releases](../../../releases) (en
**Assets**, no "Source code") y descomprímelo.

### En Windows

1. **Haz doble clic en `build-windows.cmd`** en la carpeta que descomprimiste. Si Windows dice "Windows
   protegió su PC", haz clic en **Más información** y luego en **Ejecutar de todas formas**: es un script del zip,
   no un programa firmado.
2. **Solo la primera vez, si WSL no está instalado** (WSL es el Linux que Windows 10 y 11 pueden instalar,
   gratis, de Microsoft): ofrece instalarlo, y Windows pide permiso. Luego reinicia el computador, abre
   **Ubuntu** una vez desde el menú Inicio para elegir un nombre de usuario y una contraseña, y vuelve a hacer
   doble clic en `build-windows.cmd`.
3. **Elige tu juego** en la ventana que se abre: tu imagen de disco (la primera parte, si está dividida), o
   `default.xbe` de los archivos del disco. También puedes arrastrar tu juego sobre `build-windows.cmd`.
4. **Espera.** La primera vez instala las herramientas dentro de Linux y pide una vez la contraseña de Linux que
   elegiste; luego compila.
5. Al terminar, se abre la carpeta **`SD card`** junto a `build-windows.cmd`. Su carpeta `switch` tiene el
   port y tu juego, lista para [la tarjeta SD](#copiarlo-a-la-tarjeta-sd).

El compilador para Windows es nuevo en la 0.1.0. Si se detiene, su mensaje dice por qué, y
[los mismos pasos a mano](#en-windows-a-mano) siempre funcionan.

### En Linux

En Ubuntu o Debian, un comando en la carpeta que descomprimiste, indicando dónde está tu juego (una imagen de
disco, la primera parte de una dividida, la carpeta del juego o `default.xbe`):

```sh
bash build.sh --copy-game "/ruta/a/Spider-Man 2.iso"
```

La primera vez ofrece instalar lo que necesita (git, cmake, ninja, g++ y las herramientas de Switch de
devkitPro, con `sudo`; `--yes` las instala sin preguntar). `--copy-game` pone también tu juego en la carpeta de
la tarjeta SD que crea, `out/sd/switch`; sin eso, copias el juego tú. En otra distribución,
[instala las herramientas a mano](#instalar-las-herramientas-a-mano) antes.

### Instalar las herramientas a mano

Lo que `build.sh` instala solo en Ubuntu y Debian (otras distribuciones tienen los mismos paquetes con nombres
parecidos):

1. **Las herramientas de compilación:**
   ```sh
   sudo apt install git cmake ninja-build g++ wget
   ```
2. **Las herramientas de Switch de devkitPro** ([instrucciones de devkitPro](https://devkitpro.org/wiki/devkitPro_pacman), en inglés):
   ```sh
   wget https://apt.devkitpro.org/install-devkitpro-pacman
   chmod +x ./install-devkitpro-pacman
   sudo ./install-devkitpro-pacman
   sudo dkp-pacman -Sy
   sudo dkp-pacman -S switch-dev switch-mesa
   ```
   Pulsa Enter para aceptar todos los paquetes que ofrece. `switch-mesa` es OpenGL para la Switch, con el que
   dibuja el renderizador por GPU.

### En Windows, a mano

Lo que hace `build-windows.cmd`, paso a paso:

1. **Instala WSL con Ubuntu.** Abre PowerShell como administrador, escribe `wsl --install` y reinicia el
   computador cuando lo pida. En el siguiente inicio se abre Ubuntu y te pide un nombre de usuario y una
   contraseña (son solo para este Linux).
2. **Copia la carpeta del port dentro de Linux** (compilar dentro de Linux es mucho más rápido que en `C:`).
   Tus unidades de Windows están en `/mnt/`: `C:\Users\Tu\Downloads` es `/mnt/c/Users/Tu/Downloads`. En Ubuntu:
   ```sh
   cp -r "/mnt/c/Users/Tu/Downloads/SpiderMan2-2004-NX-0.1.0" ~/
   cd ~/SpiderMan2-2004-NX-0.1.0
   ```
3. **Compila**, indicando dónde está tu juego en la unidad de Windows. La primera vez instala las herramientas
   (en WSL también crea el enlace `/etc/mtab` que necesita el instalador de devkitPro):
   ```sh
   bash build.sh --copy-game "/mnt/c/Users/Tu/Juegos/Spider-Man 2.iso"
   ```
4. **Encuentra el resultado en Windows**: en el Explorador de archivos, escribe `\\wsl$` en la barra de
   direcciones y abre `Ubuntu\home\<tu usuario>\SpiderMan2-2004-NX-0.1.0\out\sd`.

### Lo que dice el compilador

Los mensajes del compilador están en inglés:

| Mensaje | Qué hacer |
|---|---|
| `Missing: ...` y `Install them now?` | Pulsa Enter (o `Y`) y escribe tu contraseña: las instala. En una distribución sin `apt`, [instálalas a mano](#instalar-las-herramientas-a-mano). |
| `still missing after installing` | La instalación de arriba dice qué falló (a menudo, no hay internet). Prueba otra vez, o [instálalas a mano](#instalar-las-herramientas-a-mano). Si devkitPro está en otro lugar que no sea `/opt/devkitpro`, ejecuta antes `export DEVKITPRO=/donde/esté`. |
| `this default.xbe is not the release the port supports` | Tu copia es otra edición del juego, con otro código de juego (otra versión, o una edición compilada aparte). El port solo funciona con el código de juego para el que se hizo: abre un issue con el SHA-1 y el código de juego que mostró, si quieres que esa edición sea compatible. |
| `... is a compressed archive` | Extrae la imagen de disco o la carpeta del juego del `.zip`, `.7z` o `.rar`, e indica esa al compilador. |
| `default.xbe could not be read from ...` | El mensaje dice por qué (no es una imagen de Xbox, es una imagen de Xbox 360, falta una parte de una imagen dividida...). |
| `the PC tools did not build` / `the Switch build failed` | El registro que nombra (`build-tools.log`, `build-switch.log`) dice por qué. Adjúntalo a un issue si no está claro. |
| `WSL cannot see ...` (Windows) | Pon tu juego en una unidad de este computador (no en una carpeta de red) y vuelve a intentarlo. |
| `the build stopped` (Windows) | Los mensajes justo encima, de Linux, dicen por qué: son los mismos de esta tabla. |

## Copiarlo a la tarjeta SD

1. Copia la carpeta `switch` a la **raíz** de tu tarjeta SD: la de `SpiderMan2Xbox-SD.zip` (descomprímelo, o
   ábrelo y arrastra la carpeta), o la que creó el compilador (`SD card` en Windows, `out/sd` en Linux). Se une a
   la carpeta `switch` que ya está ahí, y queda `switch/SpiderMan2Xbox/`.
2. Si tu juego todavía no está dentro (con la página de instalación nunca lo está; tampoco si compilaste en Linux
   sin `--copy-game`), cópialo en `switch/SpiderMan2Xbox/game/`: tu imagen de disco (todas sus partes, si está
   dividida), o `default.xbe` y la carpeta `data`, de forma que queden `game/default.xbe` y `game/data/game.ini`.
   La página de instalación solo necesita `default.xbe`, pero el juego lo necesita todo.

> [!CAUTION]
> El `.nro` desbloqueado contiene código hecho a partir de tu copia del juego. Guárdalo para ti: no lo compartas,
> no lo publiques ni lo subas a ningún sitio.

## La primera vez

Mantén **R** mientras abres cualquier juego: se abre el Homebrew Menu en vez del juego (title takeover). Elige
**Spider-Man 2 (Xbox)**.

> [!WARNING]
> No lo abras desde el Álbum: ahí el homebrew solo recibe unos 440 MB de memoria, y el juego necesita unos
> 1,3 GB. El port te avisa si se abrió así.

Una pantalla azul dice **Loading...** hasta que el juego dibuja su primera imagen. Desde ahí, el juego lo
dibuja el chip gráfico de la Switch, imagen por imagen. El juego sale en el idioma de la consola (más abajo).

- **Salir:** mantén **+ y −** a la vez durante un segundo.
- **Sin sonido:** mantén **L** mientras arranca el port (hasta que aparezca la pantalla azul).
- **El renderizador antiguo por CPU:** mantén **R** mientras arranca el port. Muestra una imagen fija cada
  pocos segundos, pero no usa el chip gráfico: pruébalo si la imagen sale mal o si el port se detiene justo
  después de la pantalla azul, y dilo en tu reporte.
- **Sin hilo de renderizado:** mantén **ZR** mientras arranca el port. El chip gráfico recibe entonces su
  trabajo desde el propio hilo del juego: más lento, pero sirve para saber si un problema viene del hilo de
  renderizado.

Los botones mantenidos al arrancar valen solo para ese arranque; el [menú de depuración](#el-menú-de-depuración)
guarda las mismas opciones para todos.

## El menú de depuración

Mantén **ZL + ZR** y pulsa **−** (en cualquier orden) para abrir los ajustes del port sobre el juego; la misma
combinación, **B** o **+** lo cierran. **Arriba/Abajo** elige un ajuste, **Izquierda/Derecha** o **A** lo
cambian, y también funciona la pantalla táctil. Mientras está abierto, el juego no recibe los botones (sigue
corriendo: pulsa **+** antes para pausarlo). Cada ajuste tiene una línea de ayuda, y una nota naranja cuando
el valor elegido cuesta algo (calor, batería, velocidad). La línea de abajo muestra las imágenes por segundo, el
tiempo de cada imagen y las llamadas de dibujo de ese momento. El menú está en inglés.

- **Language** (idioma): Auto (el de la consola), English, Español, Français, Deutsch o Italiano, desde el
  siguiente arranque.
- **Voices** (voces): Auto (las del idioma, si tu copia las tiene; si no, las de tu copia) o cualquier idioma del
  que tu copia tenga voces, desde el siguiente arranque. El menú dice cuáles.
- **Resolution** (resolución): 480p (la de la Xbox), 720p o 1080p: todo el juego y sus videos dibujados con esos
  píxeles. 720p llena la pantalla portátil píxel a píxel; 1080p, para jugar en el dock, se dibuja más grande y se
  reduce (en modo portátil dibuja 720p, la de la pantalla). Más trabajo para la GPU, que el reloj Auto de la GPU
  sube: si bajan las imágenes por segundo, baja un paso.
- **Screen** (pantalla): 4:3 con los lados negros, desenfocados, con telaraña o con la telaraña en los colores del
  traje (rojo, azul y negro), en un televisor CRT de los 2000 (**2000s CRT TV**: plateado, altavoces estéreo,
  entradas A/V, tubo de pantalla plana) o de los 70 (**70s CRT TV**: un mueble de madera con perillas y altavoz, la
  imagen con esquinas redondeadas y líneas de barrido), o 16:9, la cámara panorámica oculta del propio juego: **16:9, wide view** (vista amplia), con más
  ciudad a los lados y el HUD, los menús y los videos en 4:3 en el centro, o **16:9, close view** (vista cercana),
  con Spider-Man y la ciudad un tercio más grandes y el HUD y los menús un décimo más grandes. En las dos, las
  cinemáticas con franjas de cine llenan el ancho.
- **CRT filter** (filtro CRT): Off, Light o Strong, la imagen como en un televisor CRT antiguo (líneas de barrido,
  las franjas roja, verde y azul de la pantalla, un brillo suave, los bordes más oscuros; Strong además curva el
  cristal), con cualquier opción de Screen.
- **Upscaling** (escalado: Smooth o Sharp) y **Texture filtering** (filtrado: Standard o anisotrópico de 4x a 16x;
  el anisotrópico es mucho más trabajo para el chip gráfico en la ciudad, así que prueba Standard primero si bajan
  las imágenes por segundo).
- **Vibration** (vibración): Off, Low, Medium o High, en los Joy-Con o un Pro Controller (la Switch Lite no tiene
  vibración propia). Si el juego deja una vibración encendida, se apaga a los 2 segundos.
- **CPU, GPU y memory clock** (relojes): Auto (el de fábrica del port), Stock (los de la consola), o una de las
  velocidades que usan los perfiles de rendimiento de Nintendo (hasta 1785 MHz de CPU, 768 MHz de GPU, 1600 MHz de
  memoria). Auto es 1785 MHz de CPU y 1600 MHz de memoria, y para la GPU 460,8 MHz a 480p en modo portátil y
  768 MHz a 720p y en el dock. Los relojes de la consola vuelven mientras el port está en el menú HOME y al
  cerrarlo. Stock gasta menos batería.
- **Frame-rate counter** (contador): apagado, FPS, o completo (también el tiempo de cada imagen, las llamadas de
  dibujo, los relojes y la memoria), arriba a la izquierda.
- **Sound volume** (volumen), **Renderer** (GPU o CPU), **Render thread** (hilo de renderizado) y el
  **Start-up log** (las últimas líneas del registro en la pantalla de arranque en vez de "Loading...", para
  una foto si el juego no arranca).

Los cambios se guardan al momento en `switch/SpiderMan2Xbox/settings.ini`. Para volver a los ajustes
recomendados, elige **Restore recommended settings**, o borra el archivo.

## Idioma

El juego toma el idioma de la consola (Configuración del sistema → Sistema → Idioma). En inglés, español (de
España o de Latinoamérica), francés (de Francia o de Canadá), alemán o italiano, sus menús y subtítulos salen en
ese idioma, con los textos que trae cualquier disco (el italiano bien desde la 0.4.0); en cualquier otro idioma, en
inglés. Para jugar en otro idioma que el de la consola, cambia **Language** en el menú de depuración y vuelve a
arrancar el port.

Las voces son las de tu copia: el disco de cada edición solo trae las suyas. El disco de EE. UU. trae voces en
inglés; los europeos, en francés, alemán y español (el doblaje de España) uno, en italiano otro. Con **Voices** en
Auto los personajes hablan el idioma de los menús si tu copia tiene esas voces; si no, las de tu copia. O pon
**Voices** en cualquier idioma que tenga tu copia, aparte del de los textos (voces en español con textos en inglés,
por ejemplo). El menú de depuración dice qué voces tiene tu copia.

## Partidas

Tus partidas están en `switch/SpiderMan2Xbox/save/`: las partidas del juego en `save/progress/` (lo que la Xbox
guarda en `E:\UDATA` para este juego) y sus datos propios en `save/data/` (`E:\TDATA`). Haz una copia de la
carpeta `save` de vez en cuando, y vuelve a copiarla para restaurar; las actualizaciones no la tocan. El port
también guarda una copia en `sdmc:/config/SpiderMan2Xbox/save/`, al día unos segundos después de cada guardado:
si al arrancar `save/progress` no tiene partidas (por ejemplo, una actualización que reemplazó toda la carpeta), la
copia vuelve sola. Para empezar de cero, elige **New Game** en el menú de guardado del juego, o borra las dos. El
resto del disco duro de la Xbox (sus unidades de caché) y la lista de shaders están en `cache/`, que se puede
borrar cuando quieras: se vuelve a crear al arrancar.

Antes de la 0.2.3 el juego guardaba pero no podía cargar sus partidas ("UNABLE TO LOAD SPIDER-MAN 2 SAVED GAME").
Las partidas se escribían completas, y la 0.2.3 las carga.

## Actualizar

Abre otra vez la [página de instalación](#la-página-de-instalación) (siempre tiene la versión más nueva, y todas las anteriores en su menú de versiones) y copia
la carpeta `switch` del zip nuevo encima de la vieja, o compila la versión nueva igual que la anterior y copia su
carpeta `out/sd/switch`. `game/` y `save/` se quedan como están, y también la lista de shaders de `cache/`. Las versiones anteriores a la 0.1.0 lo guardaban todo en `hdd/`: en el primer
arranque de la 0.1.0 las partidas que tenga pasan a `save/`, `hdd/` se convierte en `cache/` y `shaders.cache` pasa
a su interior. Si una partida está en los dos sitios, la de `hdd/` se queda donde está y se usa, y el registro lo dice.

Para volver a una versión anterior, elígela en el menú de versiones de la página de instalación y copia la carpeta
`switch` de su zip encima de la misma forma. Antes de la 0.2.3 las partidas guardadas no cargan: primero haz una
copia de `save/`. Las pruebas tempranas 0.0.8 a 0.0.10 vienen en una carpeta propia, `switch/SpiderMan2Xbox-<versión>`,
que también necesita tu juego en su `game/`: en la carpeta principal su `hdd/` quedaría junto a tu `save/`, y una
versión más nueva podría seguir usando la partida de `hdd/`.

## Solución de problemas

Los mensajes de la pantalla están en inglés:

| Lo que ves | Qué hacer |
|---|---|
| **El compilador se detiene al instalar las herramientas** ("the build stopped" justo después de los paquetes de Ubuntu) | No pudo descargar las herramientas de devkitPro; desde la 0.2.3 dice por qué, y lo intenta tres veces. Comprueba que Linux tenga internet y vuelve a abrir el compilador; una VPN, un antivirus o una red que bloquee `apt.devkitpro.org` también lo causa, así que prueba otra red. A mano, en Ubuntu: `wget https://apt.devkitpro.org/install-devkitpro-pacman`, `chmod +x install-devkitpro-pacman`, `sudo ./install-devkitpro-pacman`, `sudo dkp-pacman -S switch-dev switch-mesa`, y luego el compilador otra vez. |
| **Game not found** | Tu juego no está en `switch/SpiderMan2Xbox/game/`, o está en una forma que el port no lee. La pantalla lista los archivos que encontró y por qué no puede usar cada uno. Mira [los formatos](#los-formatos-que-lee-el-port). |
| **The data folder is missing** | El contenido de `data` está directamente en `game/`. Crea ahí una carpeta llamada `data` y mueve a ella `game.ini`, `movies`, `packs` y `sound`. |
| **A different release of the game** | El código de juego de tu copia no es aquel a partir del cual se compiló el port (la pantalla dice sus regiones, su SHA-1 y su código de juego). Lo más probable es que se cierre: pulsa **+** para volver. Normalmente el compilador lo detecta; esto pasa cuando el `.nro` se compiló a partir de otra copia. |
| **Another game or version** | La imagen que está en `game/` es de otro juego, o de otra versión de este. |
| **Not enough memory in this mode** | Abre el port con **R + un juego**, no desde el Álbum. |
| **The game did not start** | El archivo más nuevo de `logs/` dice por qué. |
| **La pantalla se congela** | Toma una foto de la pantalla, reinicia la consola (mantén POWER 12 segundos) y envía la carpeta `logs`. Luego prueba una vez manteniendo **L** al arrancar (sin sonido), otra manteniendo **ZR** (sin hilo de renderizado) y otra manteniendo **R** (el renderizador por CPU), y cuenta con cuáles se sigue congelando. Si se congela en "Loading...", activa **Start-up log** en el [menú de depuración](#el-menú-de-depuración) (o en `settings.ini`: `startup_log=on`), para que la foto muestre hasta dónde llegó. |
| **La imagen sale mal** (falta, negra, parpadea, colores raros) | Toma una foto y envía la carpeta `logs`. Luego prueba manteniendo **R** al arrancar (el renderizador por CPU): si así la imagen sale bien, el problema está en el renderizador por GPU. |
| **Va lento** | Activa el contador en el [menú de depuración](#el-menú-de-depuración) y revisa ahí que los relojes estén en Auto y el filtrado en Standard. A 720p o 1080p, prueba una resolución menor. Envía la carpeta `logs`: cada 5 segundos el registro tiene líneas `perf:` que dicen en qué se va el tiempo. |
| **No vibra** | La Switch Lite no tiene motor de vibración: usa Joy-Con o un Pro Controller. Revisa que **Vibration** no esté en Off en el [menú de depuración](#el-menú-de-depuración), y que la vibración del control esté activada en la configuración de la consola. |
| **El juego sale en otro idioma** | Sigue el idioma de la consola, que se lee al arrancar. Cambia **Language** en el [menú de depuración](#el-menú-de-depuración) y vuelve a arrancar el port. Las voces son las de tu copia: el disco de EE. UU. solo trae las inglesas (ver [Idioma](#idioma)). |
| **Da tirones** (paradas cortas al jugar) | Sobre todo la primera vez que pasas por un sitio, mientras el driver gráfico prepara los shaders que necesita; se guardan en `cache/shaders.cache` y la siguiente vez se preparan al arrancar. Si siguen, envía la carpeta `logs`: una línea `hitch:` señala cada tirón y en qué se fue el tiempo. |
| **Una pantalla de error de Atmosphère** | Envía el archivo más nuevo de `atmosphere/crash_reports/` y la carpeta `logs`. |

## Reportar un error

Abre un [issue](../../../issues/new/choose) y adjunta la carpeta `switch/SpiderMan2Xbox/logs` (comprimida en
zip), con `crash.txt` si lo hay, y una foto de la pantalla si ayuda. Di qué versión del port usas, qué versión
de firmware y de Atmosphère, y en qué formato está tu juego. **Nunca adjuntes archivos del juego** ni el `.nro`
que compilaste. Puedes escribir en español.

## Glosario

| Palabra | Significado |
|---|---|
| **XBE** | El formato de los programas de la Xbox. `default.xbe` es el programa del juego; el port se hace a partir de él, y se desbloquea con él. |
| **.nro bloqueado** | El port de la release, cifrado (AES-256-GCM) con una clave hecha del `default.xbe` compatible (desde la 0.4.0, de su código de juego, que comparten las otras ediciones del mismo código): no sirve sin el juego. La página de instalación lo desbloquea. |
| **XISO** | Una imagen de disco con solo la partición del juego de un disco de Xbox. |
| **Volcado completo del disco** | Una imagen de todo el disco (primero la partición de video, luego la del juego), como las de Redump. |
| **CCI**, **CSO** | Imágenes de disco de Xbox comprimidas, hechas por Repackinator, que leen Cerbios (CCI) y StellarOS (CSO). |
| **Imagen dividida** | Una imagen cortada en partes (`nombre.1.iso`, `nombre.2.iso`, ...) para que cada una quepa en el límite de 4 GB de FAT32. |
| **FAT32**, **exFAT** | Los dos sistemas de archivos que puede tener una tarjeta SD en la Switch. FAT32 no admite archivos de más de 4 GB. |
| **Title takeover** | Abrir el Homebrew Menu manteniendo R mientras abres un juego: así el homebrew recibe la memoria del juego. |
| **emuMMC** | Una copia del sistema de la consola en la tarjeta SD, para que el homebrew no toque el sistema real. |
| **`.nro`** | Un programa homebrew para la Switch. |
| **devkitPro** | Las herramientas gratuitas con las que se compila el homebrew de Switch. |
| **WSL** | Subsistema de Windows para Linux: un Linux de verdad dentro de Windows. |
| **Recompilación estática** | Traducir el código de un programa a otro procesador de antemano, en vez de mientras se ejecuta. |
