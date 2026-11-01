# PERSONAL RADIO 0.3.2

Mod nativo experimental para **GTA San Andreas Android 2.00**, compilado para `armeabi-v7a` y cargado mediante Android Mod Loader (AML).

## Funciones de esta versión

- Reproductor independiente de las emisoras originales.
- Búsqueda de archivos locales `.mp3`, `.ogg` y `.wav` en el almacenamiento compartido al que el juego tenga acceso.
- Orden aleatorio de la biblioteca encontrada.
- Controles táctiles propios en la franja superior mientras conduces: encender/apagar, pista anterior, pausa/reanudar y pista siguiente.
- No reemplaza emisoras, no modifica los OBB y no sobrescribe archivos del juego.

## Límites conocidos

Esta versión **todavía no añade PERSONAL RADIO al selector/carrusel original de emisoras**. Se maneja mediante controles propios en pantalla.

Android 11 puede restringir el acceso directo a carpetas de música. El escáner solo encontrará los archivos que el proceso de GTA SA tenga permiso de leer. Actualmente se reconocen MP3, OGG y WAV; no M4A, AAC ni FLAC.

## Dependencias

Requiere AML compatible con GTA San Andreas Android 2.00. El paquete compilado incluye también `libBASSMod.so`, la dependencia de audio de AndroidModLoader, para que ambas bibliotecas puedan instalarse juntas. AML debe estar instalado por separado.

## Compilación

El workflow de GitHub Actions compila el proyecto sin costo y genera `PERSONAL_RADIO_ANDROID.zip`, que contiene `libPersonalRadio.so` y las instrucciones de instalación. El código fuente por sí solo no es instalable.

## Instalación de prueba

1. Descomprime `PERSONAL_RADIO_ANDROID.zip`.
2. Copia `libPersonalRadio.so` y `libBASSMod.so` directamente en la carpeta `mods` de AML para `com.rockstargames.gtasa`.
3. Comprueba que AML esté instalado y activo.
4. Inicia el juego, entra en un vehículo y usa las cuatro zonas de la parte superior de la pantalla.

No instales este archivo como APK. Es un complemento nativo `.so` para AML. La compilación correcta tampoco garantiza compatibilidad funcional: hay que probar su carga y la lectura de música en el dispositivo.
