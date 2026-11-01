# PERSONAL RADIO 0.3.6

Mod nativo experimental para **GTA San Andreas Android 2.00**, compilado para `armeabi-v7a` y cargado mediante Android Mod Loader (AML).

## Funciones de esta versión

- Reproductor independiente de las emisoras originales.
- Búsqueda de archivos locales `.mp3`, `.ogg` y `.wav` en el almacenamiento compartido al que el juego tenga acceso.
- Orden aleatorio de la biblioteca encontrada.
- Panel pequeño y visible en la parte superior central de la pantalla.
- Tres botones táctiles: pista anterior, reproducir/pausar y pista siguiente.
- El botón central inicia la radio, pausa y reanuda la reproducción.
- El panel se puede arrastrar con el dedo para cambiarlo de lugar.
- Los toques fuera del panel no activan la radio.
- No reemplaza emisoras, no modifica los OBB y no sobrescribe archivos del juego.

## Límites conocidos

Esta versión **todavía no añade PERSONAL RADIO al selector/carrusel original de emisoras**. Se maneja mediante un panel propio en pantalla.

Android 11 puede restringir el acceso directo a carpetas de música. El escáner solo encontrará los archivos que el proceso de GTA SA tenga permiso de leer. Actualmente se reconocen MP3, OGG y WAV; no M4A, AAC ni FLAC.

La interfaz usa funciones de dibujo y texto nativas del juego. Si alguno de esos símbolos no está disponible, el mod evita activar el dibujo para reducir el riesgo de cierre. La compilación debe probarse en el dispositivo.

## Dependencias

Requiere AML compatible con GTA San Andreas Android 2.00. El paquete compilado incluye también `libBASSMod.so`, la dependencia de audio de AndroidModLoader, para que ambas bibliotecas puedan instalarse juntas. AML debe estar instalado por separado.

## Instalación

1. Descomprime `PERSONAL_RADIO_ANDROID.zip`.
2. Copia únicamente `libPersonalRadio.so` y `libBASSMod.so` en la carpeta `mods` de AML para `com.rockstargames.gtasa`.
3. No copies los archivos README ni los documentos de texto dentro de `mods`.
4. Inicia GTA SA y entra en una partida. El panel debe aparecer arriba, en el centro.
5. Toca `<<` para la pista anterior, `>` para iniciar/reanudar o `||` para pausar, y `>>` para la siguiente. Arrastra el panel para moverlo.

No instales este archivo como APK. Es un complemento nativo `.so` para AML. La compilación correcta tampoco garantiza compatibilidad funcional: hay que probar su carga, los botones y la lectura de música en el dispositivo.
