#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>

#include <Events.h>
#include <engine/RsGlobal.h>
#include <engine/Font.h>
#include <engine/Sprite2d.h>
#include <base/Timer.h>
#include "ibass.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <dirent.h>
#include <mutex>
#include <random>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>

MYMODCFG(net.kronos.gtasa.personalradio, PERSONAL RADIO, 0.3.6, Kronos)
NEEDGAME(com.rockstargames.gtasa)
BEGIN_DEPLIST()
    ADD_DEPENDENCY(net.rusjj.basslib)
END_DEPLIST()

namespace PersonalRadio
{
    static IBASS* bass = nullptr;
    static std::vector<std::string> tracks;
    static std::mutex stateMutex;
    static std::atomic<bool> scanning{false};
    static std::atomic<bool> stopping{false};
    static std::thread scanThread;
    static bool enabled = false;
    static bool paused = false;
    static bool trackStarted = false;
    static int currentTrack = -1;
    static uint32_t stream = 0;
    static float volume = 0.80f;
    static std::string currentTitle = "Biblioteca sin explorar";

    // Interfaz compacta: tres botones en la parte superior, centrados y movibles.
    static std::mutex uiMutex;
    static float panelCenterX = -1.0f;
    static float panelTop = -1.0f;
    static bool touchTracking = false;
    static bool touchDragging = false;
    static float touchStartX = 0.0f;
    static float touchStartY = 0.0f;
    static float touchStartCenterX = 0.0f;
    static float touchStartTop = 0.0f;

    struct PanelLayout
    {
        float left, top, right, bottom;
        float buttonTop, buttonBottom, buttonWidth, buttonGap, buttonStartX;
        float scale;
    };

    static void (*DrawRectNative)(const CRect&, const CRGBA&) = nullptr;
    static void (*FontSetOrientationNative)(unsigned char) = nullptr;
    static void (*FontSetColorNative)(CRGBA*) = nullptr;
    static void (*FontSetBackgroundNative)(unsigned char, unsigned char) = nullptr;
    static void (*FontSetWrapXNative)(float) = nullptr;
    static void (*FontSetStyleNative)(unsigned char) = nullptr;
    static void (*FontSetScaleNative)(float) = nullptr;
    static void (*FontSetProportionalNative)(unsigned char) = nullptr;
    static void (*FontSetShadowNative)(char) = nullptr;
    static void (*FontSetEdgeNative)(char) = nullptr;
    static void (*FontPrintStringNative)(float, float, unsigned short*) = nullptr;
    static void (*AsciiToGxtCharNative)(const char*, unsigned short*) = nullptr;
    static void (*RenderFontBufferNative)() = nullptr;
    static constexpr size_t MAX_TRACKS = 10000;
    static constexpr int MAX_DEPTH = 12;

    static std::string Lower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    static bool IsAudioFile(const char* name)
    {
        if (!name) return false;
        const std::string file = Lower(name);
        const char* extensions[] = { ".mp3", ".ogg", ".wav" };
        for (const char* ext : extensions)
        {
            const size_t n = file.size();
            const size_t e = std::strlen(ext);
            if (n >= e && file.compare(n - e, e, ext) == 0) return true;
        }
        return false;
    }

    static bool SkipDirectory(const std::string& parent, const char* name)
    {
        if (!name || name[0] == '.') return true;
        const std::string n = Lower(name);
        if (n == "android_unprotected" || n == "lost.dir" || n == ".thumbnails") return true;
        const size_t slash = parent.find_last_of('/');
        const std::string leaf = slash == std::string::npos ? parent : parent.substr(slash + 1);
        if (Lower(leaf) == "android" && (n == "data" || n == "obb")) return true;
        return false;
    }

    static void ScanDirectory(const std::string& path, int depth, std::vector<std::string>& found)
    {
        if (stopping.load() || depth > MAX_DEPTH || found.size() >= MAX_TRACKS) return;
        DIR* dir = opendir(path.c_str());
        if (!dir) return;

        struct dirent* entry = nullptr;
        while (!stopping.load() && (entry = readdir(dir)) != nullptr && found.size() < MAX_TRACKS)
        {
            if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0) continue;
            const std::string full = path + "/" + entry->d_name;
            struct stat st {};
            if (stat(full.c_str(), &st) != 0) continue;

            if (S_ISDIR(st.st_mode))
            {
                if (!SkipDirectory(path, entry->d_name)) ScanDirectory(full, depth + 1, found);
            }
            else if (S_ISREG(st.st_mode) && IsAudioFile(entry->d_name))
            {
                found.push_back(full);
            }
        }
        closedir(dir);
    }

    static std::string FileTitle(const std::string& path)
    {
        const size_t slash = path.find_last_of('/');
        std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
        const size_t dot = name.find_last_of('.');
        if (dot != std::string::npos) name.resize(dot);
        return name;
    }

    static void StopStreamLocked()
    {
        if (bass && stream)
        {
            bass->ChannelStop(stream);
            bass->StreamFree(stream);
        }
        stream = 0;
        trackStarted = false;
        paused = false;
    }

    static bool StartTrackLocked(int index)
    {
        if (!bass || tracks.empty()) return false;
        if (index < 0) index = 0;
        if (index >= static_cast<int>(tracks.size())) index = 0;

        StopStreamLocked();
        currentTrack = index;
        stream = bass->StreamCreateFile(false, tracks[index].c_str(), 0, 0, BASS_SAMPLE_FLOAT);
        if (!stream)
        {
            logger->Error("No se pudo abrir la pista: %s (BASS=%d)", tracks[index].c_str(), bass->ErrorGetCode());
            currentTitle = "No se pudo abrir la pista";
            return false;
        }

        bass->ChannelSetAttribute(stream, BASS_ATTRIB_VOL, volume);
        if (!bass->ChannelPlay(stream, true))
        {
            logger->Error("BASS no pudo iniciar la pista (error %d)", bass->ErrorGetCode());
            StopStreamLocked();
            currentTitle = "Error de reproduccion";
            return false;
        }
        trackStarted = true;
        paused = false;
        currentTitle = FileTitle(tracks[index]);
        logger->Info("Reproduciendo %d/%d: %s", index + 1, (int)tracks.size(), currentTitle.c_str());
        return true;
    }

    static void PlayRelativeLocked(int delta)
    {
        if (tracks.empty()) return;
        int start = currentTrack;
        if (start < 0) start = (delta > 0) ? -1 : 0;
        for (size_t attempt = 0; attempt < tracks.size(); ++attempt)
        {
            int next = (start + delta + static_cast<int>(tracks.size())) % static_cast<int>(tracks.size());
            if (StartTrackLocked(next)) return;
            start = next;
        }
        currentTitle = "No se pudo abrir ninguna pista";
    }

    static void RequestScan()
    {
        bool expected = false;
        if (!scanning.compare_exchange_strong(expected, true)) return;

        if (scanThread.joinable()) scanThread.join();
        scanThread = std::thread([]()
        {
            logger->Info("Buscando musica en el almacenamiento compartido...");
            std::vector<std::string> found;
            ScanDirectory("/storage/emulated/0", 0, found);
            if (stopping.load()) { scanning.store(false); return; }

            DIR* storage = opendir("/storage");
            if (storage)
            {
                struct dirent* entry = nullptr;
                while (!stopping.load() && (entry = readdir(storage)) != nullptr && found.size() < MAX_TRACKS)
                {
                    if (entry->d_name[0] == '.') continue;
                    if (std::strcmp(entry->d_name, "emulated") == 0 || std::strcmp(entry->d_name, "self") == 0) continue;
                    ScanDirectory(std::string("/storage/") + entry->d_name, 0, found);
                }
                closedir(storage);
            }

            if (stopping.load()) { scanning.store(false); return; }
            std::sort(found.begin(), found.end());
            found.erase(std::unique(found.begin(), found.end()), found.end());
            std::mt19937 rng(static_cast<unsigned int>(time(nullptr)));
            std::shuffle(found.begin(), found.end(), rng);

            {
                std::lock_guard<std::mutex> lock(stateMutex);
                tracks.swap(found);
                scanning.store(false);
                logger->Info("Busqueda terminada. Pistas encontradas: %d", (int)tracks.size());
                if (enabled && !tracks.empty()) PlayRelativeLocked(1);
                else if (enabled) currentTitle = "No se encontraron MP3, OGG o WAV";
            }
        });
    }

    static void ToggleRadio()
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        enabled = !enabled;
        if (!enabled)
        {
            StopStreamLocked();
            currentTitle = "Radio apagada";
            logger->Info("PERSONAL RADIO apagada.");
            return;
        }

        logger->Info("PERSONAL RADIO encendida.");
        if (tracks.empty())
        {
            currentTitle = "Buscando musica...";
            RequestScan();
        }
        else if (!stream)
        {
            PlayRelativeLocked(1);
        }
    }

    static void Previous()
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        if (!enabled) return;
        if (tracks.empty()) { RequestScan(); return; }
        PlayRelativeLocked(-1);
    }

    static void Next()
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        if (!enabled) return;
        if (tracks.empty()) { RequestScan(); return; }
        PlayRelativeLocked(1);
    }

    static void TogglePlayPause()
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        if (!bass) return;

        // El boton central inicia la radio si estaba apagada y pausa/reanuda si ya sonaba.
        if (!enabled)
        {
            enabled = true;
            logger->Info("PERSONAL RADIO iniciada.");
            if (tracks.empty())
            {
                currentTitle = "Buscando musica...";
                RequestScan();
            }
            else if (!stream)
            {
                PlayRelativeLocked(1);
            }
            return;
        }

        if (!stream)
        {
            if (tracks.empty())
            {
                currentTitle = "Buscando musica...";
                RequestScan();
            }
            else
            {
                PlayRelativeLocked(1);
            }
            return;
        }

        if (paused)
        {
            if (bass->ChannelPlay(stream, false)) paused = false;
        }
        else
        {
            bass->ChannelPause(stream);
            paused = true;
        }
    }

    static float UiScale(int height)
    {
        float scale = static_cast<float>(height) / 540.0f;
        if (scale < 0.75f) scale = 0.75f;
        if (scale > 3.0f) scale = 3.0f;
        return scale;
    }

    static void ClampPanelPositionLocked(int width, int height, float scale)
    {
        const float panelWidth = 230.0f * scale;
        const float panelHeight = 60.0f * scale;
        const float margin = 6.0f * scale;

        const float minCenter = panelWidth * 0.5f + margin;
        const float maxCenter = static_cast<float>(width) - panelWidth * 0.5f - margin;
        const float minTop = 12.0f * scale;
        const float maxTop = static_cast<float>(height) - panelHeight - margin;

        if (panelCenterX < minCenter) panelCenterX = minCenter;
        if (panelCenterX > maxCenter) panelCenterX = maxCenter;
        if (panelTop < minTop) panelTop = minTop;
        if (panelTop > maxTop) panelTop = maxTop;
    }

    static PanelLayout GetPanelLayout(int width, int height)
    {
        PanelLayout layout {};
        if (width <= 0 || height <= 0) return layout;

        layout.scale = UiScale(height);
        const float panelWidth = 230.0f * layout.scale;
        const float panelHeight = 60.0f * layout.scale;
        layout.buttonWidth = 66.0f * layout.scale;
        layout.buttonGap = 7.0f * layout.scale;
        const float buttonHeight = 48.0f * layout.scale;

        {
            std::lock_guard<std::mutex> lock(uiMutex);
            if (panelCenterX < 0.0f) panelCenterX = static_cast<float>(width) * 0.5f;
            if (panelTop < 0.0f) panelTop = static_cast<float>(height) * 0.055f;
            ClampPanelPositionLocked(width, height, layout.scale);
            layout.left = panelCenterX - panelWidth * 0.5f;
            layout.top = panelTop;
        }

        layout.right = layout.left + panelWidth;
        layout.bottom = layout.top + panelHeight;
        layout.buttonTop = layout.top + (panelHeight - buttonHeight) * 0.5f;
        layout.buttonBottom = layout.buttonTop + buttonHeight;
        const float buttonsWidth = layout.buttonWidth * 3.0f + layout.buttonGap * 2.0f;
        layout.buttonStartX = layout.left + (panelWidth - buttonsWidth) * 0.5f;
        return layout;
    }

    static int ButtonAt(const PanelLayout& layout, int x, int y)
    {
        if (x < layout.left || x > layout.right || y < layout.buttonTop || y > layout.buttonBottom)
            return -1;

        for (int i = 0; i < 3; ++i)
        {
            const float left = layout.buttonStartX + i * (layout.buttonWidth + layout.buttonGap);
            if (x >= left && x <= left + layout.buttonWidth) return i;
        }
        return -1;
    }

    static void DrawButtonText(const char* value, float x, float y)
    {
        unsigned short text[24] {};
        AsciiToGxtCharNative(value, text);
        FontPrintStringNative(x, y, text);
    }

    static void DrawOverlay()
    {
        if (!FontSetOrientationNative || !FontSetColorNative || !FontSetBackgroundNative ||
            !FontSetWrapXNative || !FontSetStyleNative || !FontSetScaleNative ||
            !FontSetProportionalNative || !FontSetShadowNative || !FontSetEdgeNative ||
            !FontPrintStringNative || !AsciiToGxtCharNative || !RenderFontBufferNative)
            return;

        const int width = RsGlobal.maximumWidth;
        const int height = RsGlobal.maximumHeight;
        if (width <= 0 || height <= 0) return;

        const PanelLayout layout = GetPanelLayout(width, height);
        bool radioEnabled = false;
        bool radioPaused = false;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            radioEnabled = enabled;
            radioPaused = paused;
        }

        // Fondo sobrio y tres zonas táctiles amplias.
        if (DrawRectNative)
        {
            const CRect panelRect(layout.left, layout.top, layout.right, layout.bottom);
            const CRGBA panelColor(8, 11, 17, 205);
            DrawRectNative(panelRect, panelColor);

            const CRGBA sideColor(42, 49, 60, 245);
            const CRGBA centerColor = radioEnabled ? CRGBA(120, 83, 25, 250) : CRGBA(54, 62, 74, 245);
            for (int i = 0; i < 3; ++i)
            {
                const float left = layout.buttonStartX + i * (layout.buttonWidth + layout.buttonGap);
                const CRect buttonRect(left, layout.buttonTop, left + layout.buttonWidth, layout.buttonBottom);
                DrawRectNative(buttonRect, i == 1 ? centerColor : sideColor);
            }
        }

        FontSetOrientationNative(ALIGN_CENTER);
        FontSetBackgroundNative(0, 0);
        FontSetWrapXNative(static_cast<float>(width) - 8.0f * layout.scale);
        FontSetScaleNative(0.62f * layout.scale);
        FontSetStyleNative(FONT_SUBTITLES);
        FontSetProportionalNative(1);
        FontSetShadowNative(1);
        FontSetEdgeNative(1);

        CRGBA textColor(255, 255, 255, 255);
        FontSetColorNative(&textColor);

        const float textY = layout.buttonTop + 11.0f * layout.scale;
        const float center1 = layout.buttonStartX + layout.buttonWidth * 0.5f;
        const float center2 = layout.buttonStartX + layout.buttonWidth + layout.buttonGap + layout.buttonWidth * 0.5f;
        const float center3 = layout.buttonStartX + 2.0f * (layout.buttonWidth + layout.buttonGap) + layout.buttonWidth * 0.5f;

        DrawButtonText("<<", center1, textY);
        DrawButtonText((radioEnabled && !radioPaused) ? "||" : ">", center2, textY);
        DrawButtonText(">>", center3, textY);
        RenderFontBufferNative();
    }

    static void HandleTouch(int type, int /*finger*/, int x, int y)
    {
        // AND_TouchEvent usa los códigos Android: DOWN=0, UP=1, MOVE=2, CANCEL=3.
        // Solo se aceptan gestos que comiencen dentro del panel visible.
        const int action = type & 0xFF;
        const int width = RsGlobal.maximumWidth;
        const int height = RsGlobal.maximumHeight;
        if (width <= 0 || height <= 0) return;

        const PanelLayout layout = GetPanelLayout(width, height);
        const float threshold = 12.0f * layout.scale;

        if (action == 0) // ACTION_DOWN
        {
            if (x < layout.left || x > layout.right || y < layout.top || y > layout.bottom)
                return;

            std::lock_guard<std::mutex> lock(uiMutex);
            touchTracking = true;
            touchDragging = false;
            touchStartX = static_cast<float>(x);
            touchStartY = static_cast<float>(y);
            touchStartCenterX = panelCenterX;
            touchStartTop = panelTop;
            return;
        }

        if (action == 2) // ACTION_MOVE
        {
            std::lock_guard<std::mutex> lock(uiMutex);
            if (!touchTracking) return;

            const float dx = static_cast<float>(x) - touchStartX;
            const float dy = static_cast<float>(y) - touchStartY;
            if (!touchDragging && (std::fabs(dx) > threshold || std::fabs(dy) > threshold))
                touchDragging = true;

            if (touchDragging)
            {
                panelCenterX = touchStartCenterX + dx;
                panelTop = touchStartTop + dy;
                ClampPanelPositionLocked(width, height, layout.scale);
            }
            return;
        }

        if (action == 1) // ACTION_UP
        {
            int pressedButton = -1;
            {
                std::lock_guard<std::mutex> lock(uiMutex);
                if (!touchTracking) return;
                if (!touchDragging) pressedButton = ButtonAt(layout, x, y);
                touchTracking = false;
                touchDragging = false;
            }

            // Ejecutar fuera del mutex visual para evitar bloqueos entre dibujo y audio.
            if (pressedButton == 0) Previous();
            else if (pressedButton == 1) TogglePlayPause();
            else if (pressedButton == 2) Next();
            return;
        }

        if (action == 3) // ACTION_CANCEL
        {
            std::lock_guard<std::mutex> lock(uiMutex);
            touchTracking = false;
            touchDragging = false;
        }
    }

    static void OnFrame()
    {
        // 0.3.3: sin consultas nativas al vehiculo ni dibujo CFont por frame.
        // La reproduccion y el avance automatico siguen funcionando.
        std::lock_guard<std::mutex> lock(stateMutex);
        if (enabled && bass && stream && !paused && !CTimer::IsPaused())
        {
            if (bass->ChannelIsActive(stream) == BASS_ACTIVE_STOPPED)
                PlayRelativeLocked(1);
        }
    }
}

ON_MOD_PRELOAD()
{
    logger->SetTag("PERSONAL RADIO");
    PersonalRadio::stopping.store(false);
}

ON_MOD_LOAD()
{
    if (!aml->GetLib("libGTASA.so"))
    {
        logger->Error("No se encontro libGTASA.so. Mod cancelado.");
        return;
    }

    void* hGTASA = aml->GetLibHandle("libGTASA.so");
    if (!hGTASA)
    {
        logger->Error("No se pudo obtener el identificador de libGTASA.so.");
        return;
    }

    // Resolver las funciones nativas mediante simbolos. Las llamadas se omiten si falta alguna.
    PersonalRadio::DrawRectNative = reinterpret_cast<void (*)(const CRect&, const CRGBA&)>(
        aml->GetSym(hGTASA, "_ZN9CSprite2d8DrawRectERK5CRectRK5CRGBA"));
    PersonalRadio::FontSetOrientationNative = reinterpret_cast<void (*)(unsigned char)>(aml->GetSym(hGTASA, "_ZN5CFont14SetOrientationEh"));
    PersonalRadio::FontSetColorNative = reinterpret_cast<void (*)(CRGBA*)>(aml->GetSym(hGTASA, "_ZN5CFont8SetColorE5CRGBA"));
    PersonalRadio::FontSetBackgroundNative = reinterpret_cast<void (*)(unsigned char, unsigned char)>(aml->GetSym(hGTASA, "_ZN5CFont13SetBackgroundEhh"));
    PersonalRadio::FontSetWrapXNative = reinterpret_cast<void (*)(float)>(aml->GetSym(hGTASA, "_ZN5CFont8SetWrapxEf"));
    PersonalRadio::FontSetStyleNative = reinterpret_cast<void (*)(unsigned char)>(aml->GetSym(hGTASA, "_ZN5CFont12SetFontStyleEh"));
    PersonalRadio::FontSetScaleNative = reinterpret_cast<void (*)(float)>(aml->GetSym(hGTASA, "_ZN5CFont8SetScaleEf"));
    PersonalRadio::FontSetProportionalNative = reinterpret_cast<void (*)(unsigned char)>(aml->GetSym(hGTASA, "_ZN5CFont15SetProportionalEh"));
    PersonalRadio::FontSetShadowNative = reinterpret_cast<void (*)(char)>(aml->GetSym(hGTASA, "_ZN5CFont21SetDropShadowPositionEa"));
    PersonalRadio::FontSetEdgeNative = reinterpret_cast<void (*)(char)>(aml->GetSym(hGTASA, "_ZN5CFont7SetEdgeEa"));
    PersonalRadio::FontPrintStringNative = reinterpret_cast<void (*)(float, float, unsigned short*)>(aml->GetSym(hGTASA, "_ZN5CFont11PrintStringEffPt"));
    PersonalRadio::AsciiToGxtCharNative = reinterpret_cast<void (*)(const char*, unsigned short*)>(aml->GetSym(hGTASA, "_Z14AsciiToGxtCharPKcPt"));
    PersonalRadio::RenderFontBufferNative = reinterpret_cast<void (*)()>(aml->GetSym(hGTASA, "_ZN5CFont16RenderFontBufferEv"));

    const bool fontReady = PersonalRadio::FontSetOrientationNative &&
        PersonalRadio::FontSetColorNative && PersonalRadio::FontSetBackgroundNative &&
        PersonalRadio::FontSetWrapXNative && PersonalRadio::FontSetStyleNative &&
        PersonalRadio::FontSetScaleNative && PersonalRadio::FontSetProportionalNative &&
        PersonalRadio::FontSetShadowNative && PersonalRadio::FontSetEdgeNative &&
        PersonalRadio::FontPrintStringNative && PersonalRadio::AsciiToGxtCharNative &&
        PersonalRadio::RenderFontBufferNative;

    PersonalRadio::bass = static_cast<IBASS*>(GetInterface("BASS"));
    if (!PersonalRadio::bass)
    {
        logger->Error("Falta BASSMod. Instala tambien libBASSMod.so.");
        return;
    }

    PersonalRadio::volume = cfg->GetFloat("Volume", 0.80f, "Settings");
    if (PersonalRadio::volume < 0.0f) PersonalRadio::volume = 0.0f;
    if (PersonalRadio::volume > 1.0f) PersonalRadio::volume = 1.0f;

    if (fontReady)
    {
        Events::drawHudEvent.after += []() { PersonalRadio::DrawOverlay(); };
        logger->Info("Panel visible activado: tres botones en la parte superior, arrastrable.");
    }
    else
    {
        logger->Error("No se resolvieron las funciones de texto; controles visuales desactivados.");
    }

    Events::touchScreenEvent.after += [](int type, int finger, int x, int y)
    {
        PersonalRadio::HandleTouch(type, finger, x, y);
    };
    Events::processScriptsEvent += []() { PersonalRadio::OnFrame(); };

    logger->Info("PERSONAL RADIO 0.3.6 cargado. Controles visibles superiores y arrastrables.");
}

ON_MOD_UNLOAD()
{
    PersonalRadio::stopping.store(true);
    if (PersonalRadio::scanThread.joinable()) PersonalRadio::scanThread.join();
    std::lock_guard<std::mutex> lock(PersonalRadio::stateMutex);
    PersonalRadio::enabled = false;
    PersonalRadio::StopStreamLocked();
    logger->Info("PERSONAL RADIO descargado.");
}
