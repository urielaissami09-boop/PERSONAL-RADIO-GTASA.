# PERSONAL RADIO 0.3.6

Código fuente de un mod nativo para GTA San Andreas Android 2.00, ABI `armeabi-v7a`.

La compilación se realiza gratis con GitHub Actions. El workflow descarga las fuentes de soporte de AML, el SDK Android de GTA San Andreas y la interfaz de BASSMod. Después compila `libPersonalRadio.so` y lo empaqueta junto con `libBASSMod.so` en `PERSONAL_RADIO_ANDROID.zip`.

La interfaz de esta versión es un panel compacto en la parte superior central, con tres botones para anterior, reproducir/pausar y siguiente. El panel admite arrastre táctil. El dibujo se activa únicamente cuando se resuelven las funciones nativas necesarias.

Todavía no añade una emisora al selector original de radios. Requiere AML y BASSMod instalados y debe probarse en el dispositivo antes de considerarla funcional.

Para instalar, copia únicamente `libPersonalRadio.so` y `libBASSMod.so` en la carpeta `mods` de AML. Los README y archivos de instrucciones no van dentro de esa carpeta.

Última actualización: la compilación está configurada en `.github/workflows/build.yml`.
