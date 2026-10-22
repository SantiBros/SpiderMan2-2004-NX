# Instalar Spider-Man 2 (Xbox) en la Switch

[![English](https://img.shields.io/badge/lang-English-1b2a4a?style=for-the-badge)](INSTALL.md)
[![Español](https://img.shields.io/badge/idioma-Espa%C3%B1ol-c8202f?style=for-the-badge)](INSTALL.es.md)

[← Volver al README](../README.es.md)

> [!TIP]
> **¿Nuevo en el homebrew de Switch?** Primero necesitas una consola con firmware personalizado (Atmosphère)
> y el Homebrew Menu. La guía de la comunidad [switch.hacks.guide](https://switch.hacks.guide/) explica cómo,
> paso a paso (en inglés). Las palabras que se usan en esta página se explican en el [glosario](#glosario).

El port se compila a partir de **tu propia copia** del juego, una vez, en tu computador: el resultado es un
`.nro` hecho para ti. Luego va a la tarjeta SD junto con tu juego.

- [Cómo conseguir tu juego](#cómo-conseguir-tu-juego)
  - [Con una Xbox modificada](#con-una-xbox-modificada)
  - [Los formatos que lee el port](#los-formatos-que-lee-el-port)
- [Compilar el port](#compilar-el-port)
  - [En Linux](#en-linux)
  - [En Windows con WSL](#en-windows-con-wsl)
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
`853bcb253c5baea2d7e2d9b59e5888b4c87bb402` (ID de título `4156002B`, versión 4). El compilador lo comprueba,
así que no tienes que averiguarlo tú. El port nunca descarga nada: usa la copia que le das.

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

## Compilar el port

El compilador, `build.sh`, necesita unas herramientas gratuitas la primera vez (unos 10 minutos, una sola vez)
y luego un solo comando: comprueba tu copia, recompila el código del juego y compila `SpiderMan2Xbox.nro`, en
10 a 30 minutos según el computador. Necesita unos 2 GB de espacio.

Primero descarga `SpiderMan2-2004-NX-<versión>.zip` de la página de [Releases](../../../releases) y descomprímelo, o
clona el repositorio con git.

### En Linux

En Ubuntu o Debian (otras distribuciones tienen los mismos paquetes con nombres parecidos):

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
3. **Compila**, en la carpeta que descomprimiste, indicando dónde está tu juego (una imagen de disco, la
   primera parte de una dividida, la carpeta del juego o `default.xbe`):
   ```sh
   bash build.sh "/ruta/a/Spider-Man 2.iso"
   ```

### En Windows con WSL

WSL ejecuta un Linux de verdad dentro de Windows 10 y 11, y el compilador funciona ahí.

1. **Instala WSL con Ubuntu.** Abre PowerShell como administrador, escribe `wsl --install` y reinicia el
   computador cuando lo pida. En el siguiente inicio se abre Ubuntu y te pide un nombre de usuario y una
   contraseña (son solo para este Linux).
2. **En Ubuntu**, prepara WSL para devkitPro y luego haz los pasos 1 y 2 de [En Linux](#en-linux):
   ```sh
   sudo ln -s /proc/self/mounts /etc/mtab
   ```
3. **Copia la carpeta del port dentro de Linux** (compilar dentro de Linux es mucho más rápido que en `C:`).
   Tus unidades de Windows están en `/mnt/`: `C:\Users\Tu\Downloads` es `/mnt/c/Users/Tu/Downloads`.
   ```sh
   cp -r "/mnt/c/Users/Tu/Downloads/SpiderMan2-2004-NX-0.1.0" ~/
   cd ~/SpiderMan2-2004-NX-0.1.0
   ```
4. **Compila**, indicando dónde está tu juego en la unidad de Windows (solo se lee):
   ```sh
   bash build.sh "/mnt/c/Users/Tu/Juegos/Spider-Man 2.iso"
   ```
5. **Encuentra el resultado en Windows**: en el Explorador de archivos, escribe `\\wsl$` en la barra de
   direcciones y abre `Ubuntu\home\<tu usuario>\SpiderMan2-2004-NX-0.1.0\out\sd`.

### Lo que dice el compilador

Los mensajes del compilador están en inglés:

| Mensaje | Qué hacer |
|---|---|
| `missing: ...` | Instala lo que nombra (paso 1). |
| `devkitPro's Switch tools were not found` | Haz el paso 2. Si devkitPro está en otro lugar que no sea `/opt/devkitpro`, ejecuta antes `export DEVKITPRO=/donde/esté`. |
| `this default.xbe is not the release the port supports` | Tu copia es otra edición del juego (de otra región o versión). El port solo funciona con la edición para la que se hizo: abre un issue con el SHA-1 que mostró, si quieres que esa edición sea compatible. |
| `... is a compressed archive` | Extrae la imagen de disco o la carpeta del juego del `.zip`, `.7z` o `.rar`, e indica esa al compilador. |
| `default.xbe could not be read from ...` | El mensaje dice por qué (no es una imagen de Xbox, es una imagen de Xbox 360, falta una parte de una imagen dividida...). |
| `the PC tools did not build` / `the Switch build failed` | El registro que nombra (`build-tools.log`, `build-switch.log`) dice por qué. Adjúntalo a un issue si no está claro. |

## Copiarlo a la tarjeta SD

1. Copia la carpeta `out/sd/switch` a la **raíz** de tu tarjeta SD. Se une a la carpeta `switch` que ya está
   ahí, y queda `switch/SpiderMan2Xbox/`.
2. Copia tu juego en `switch/SpiderMan2Xbox/game/`: tu imagen de disco (todas sus partes, si está dividida),
   o `default.xbe` y la carpeta `data`, de forma que queden `game/default.xbe` y `game/data/game.ini`.

> [!CAUTION]
> El `.nro` que compilaste contiene código hecho a partir de tu copia del juego. Guárdalo para ti: no lo
> compartas, no lo publiques ni lo subas a ningún sitio.

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
- **CPU, GPU y memory clock** (relojes): de fábrica, o una de las velocidades que usan los perfiles de
  rendimiento de Nintendo (hasta 1785 MHz de CPU, 768 MHz de GPU, 1600 MHz de memoria). Los relojes de la consola
  vuelven mientras el port está en el menú HOME y al cerrarlo. Si el contador se queda por debajo de 30 en
  sitios con mucho movimiento, prueba primero un reloj de CPU más alto.
- **Frame-rate counter** (contador): apagado, FPS, o completo (también el tiempo de cada imagen, las llamadas de
  dibujo, los relojes y la memoria), en la franja negra a la izquierda de la imagen.
- **Sound volume** (volumen), **Renderer** (GPU o CPU), **Render thread** (hilo de renderizado) y el
  **Start-up log** (las últimas líneas del registro en la pantalla de arranque en vez de "Loading...", para
  una foto si el juego no arranca).

Los cambios se guardan al momento en `switch/SpiderMan2Xbox/settings.ini`. Para volver a los ajustes
recomendados, elige **Restore recommended settings**, o borra el archivo.

## Idioma

El juego toma el idioma de la consola (Configuración del sistema → Sistema → Idioma). En inglés, español (de
España o de Latinoamérica), francés (de Francia o de Canadá), alemán o italiano, sus menús y subtítulos salen en
ese idioma, con los textos de tu disco; en cualquier otro idioma, en inglés. Las voces son en inglés en todos los
idiomas: el disco solo trae las inglesas. Para jugar en otro idioma que el de la consola, cambia **Language** en
el menú de depuración y vuelve a arrancar el port.

## Partidas

Tus partidas están en `switch/SpiderMan2Xbox/save/`: las partidas del juego en `save/progress/` (lo que la Xbox
guarda en `E:\UDATA` para este juego) y sus datos propios en `save/data/` (`E:\TDATA`). Haz una copia de la
carpeta `save` de vez en cuando, y vuelve a copiarla para restaurar; las actualizaciones no la tocan. El resto del
disco duro de la Xbox (sus unidades de caché) y la lista de shaders están en `cache/`, que se puede borrar cuando
quieras: se vuelve a crear al arrancar.

## Actualizar

Compila la versión nueva igual que la anterior y copia su carpeta `out/sd/switch` encima de la vieja. `game/` y
`save/` se quedan como están. Las versiones anteriores a la 0.1.0 lo guardaban todo en `hdd/`: en el primer
arranque de la 0.1.0 las partidas que tenga pasan a `save/`, `hdd/` se convierte en `cache/` y `shaders.cache` pasa
a su interior. Si una partida está en los dos sitios, la de `hdd/` se queda donde está y se usa, y el registro lo dice.

## Solución de problemas

Los mensajes de la pantalla están en inglés:

| Lo que ves | Qué hacer |
|---|---|
| **Game not found** | Tu juego no está en `switch/SpiderMan2Xbox/game/`, o está en una forma que el port no lee. La pantalla lista los archivos que encontró y por qué no puede usar cada uno. Mira [los formatos](#los-formatos-que-lee-el-port). |
| **The data folder is missing** | El contenido de `data` está directamente en `game/`. Crea ahí una carpeta llamada `data` y mueve a ella `game.ini`, `movies`, `packs` y `sound`. |
| **A different release of the game** | Tu copia no es la edición a partir de la cual se compiló el port. Lo más probable es que se cierre: pulsa **+** para volver. Normalmente el compilador lo detecta; esto pasa cuando el `.nro` se compiló a partir de otra copia. |
| **Another game or version** | La imagen que está en `game/` es de otro juego, o de otra versión de este. |
| **Not enough memory in this mode** | Abre el port con **R + un juego**, no desde el Álbum. |
| **The game did not start** | El archivo más nuevo de `logs/` dice por qué. |
| **La pantalla se congela** | Toma una foto de la pantalla, reinicia la consola (mantén POWER 12 segundos) y envía la carpeta `logs`. Luego prueba una vez manteniendo **L** al arrancar (sin sonido), otra manteniendo **ZR** (sin hilo de renderizado) y otra manteniendo **R** (el renderizador por CPU), y cuenta con cuáles se sigue congelando. Si se congela en "Loading...", activa **Start-up log** en el [menú de depuración](#el-menú-de-depuración) (o en `settings.ini`: `startup_log=on`), para que la foto muestre hasta dónde llegó. |
| **La imagen sale mal** (falta, negra, parpadea, colores raros) | Toma una foto y envía la carpeta `logs`. Luego prueba manteniendo **R** al arrancar (el renderizador por CPU): si así la imagen sale bien, el problema está en el renderizador por GPU. |
| **Va lento** | Activa el contador en el [menú de depuración](#el-menú-de-depuración) y prueba ahí un reloj de CPU más alto. Envía la carpeta `logs`: cada 5 segundos el registro tiene una línea `perf:` que dice en qué se va el tiempo. |
| **El juego sale en otro idioma** | Sigue el idioma de la consola, que se lee al arrancar. Cambia **Language** en el [menú de depuración](#el-menú-de-depuración) y vuelve a arrancar el port. Las voces siempre son en inglés. |
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
| **XBE** | El formato de los programas de la Xbox. `default.xbe` es el programa del juego; el port se compila a partir de él. |
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
