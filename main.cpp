#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>

#include <Events.h>
#include <engine/Font.h>
#include <engine/RsGlobal.h>
#include <entity/PlayerPed.h>
#include <base/Timer.h>
#include "ibass.h"

#include <algorithm>
#include <atomic>
#include <cctype>
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

MYMODCFG(net.kronos.gtasa.personalradio, PERSONAL RADIO, 0.3.4, Kronos)
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

    static void TogglePause()
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        if (!enabled || !bass || !stream) return;
        if (paused)
        {
            bass->ChannelPlay(stream, false);
            paused = false;
        }
        else
        {
            bass->ChannelPause(stream);
            paused = true;
        }
    }

    static void DrawOverlay()
    {
        // No consultar el vehiculo desde el callback de dibujo.
        // Se muestra siempre para confirmar visualmente que el mod esta cargado.
        char line[256];
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            std::snprintf(line, sizeof(line), "PERSONAL RADIO [%s]  |  << PREV  |  %s  |  NEXT >>\\n%s",
                enabled ? "ON" : "OFF", paused ? "PLAY" : "PAUSE", currentTitle.c_str());
        }

        GxtChar text[256]{};
        AsciiToGxtChar(line, text);
        const float scale = static_cast<float>(RsGlobal.maximumHeight) / 540.0f;
        CFont::SetScale(scale * 0.78f);
        CFont::SetColor(CRGBA(255, 255, 255, 255));
        CFont::SetFontStyle(FO_FONT_STYLE_HEADING);
        CFont::SetEdge(1);
        CFont::SetOrientation(ALIGN_CENTER);
        CFont::SetProportional(1);
        CFont::SetBackground(0, 1);
        CFont::PrintString(0.5f * RsGlobal.maximumWidth, 0.035f * RsGlobal.maximumHeight, text);
        CFont::RenderFontBuffer();
    }

    static void HandleTouch(int type, int /*finger*/, int x, int y)
    {
        // Evita consultar el vehiculo desde callbacks de entrada o render.
        // Esa consulta es la principal sospechosa del cierre al subir al auto.
        if (type != 2) return;
        const int width = RsGlobal.maximumWidth;
        const int height = RsGlobal.maximumHeight;
        if (y > height * 0.15f) return;

        if (x < width * 0.25f) ToggleRadio();
        else if (x < width * 0.50f) Previous();
        else if (x < width * 0.75f) TogglePause();
        else Next();
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

    PersonalRadio::bass = static_cast<IBASS*>(GetInterface("BASS"));
    if (!PersonalRadio::bass)
    {
        logger->Error("Falta BASSMod. Instala tambien libBASSMod.so.");
        return;
    }

    PersonalRadio::volume = cfg->GetFloat("Volume", 0.80f, "Settings");
    if (PersonalRadio::volume < 0.0f) PersonalRadio::volume = 0.0f;
    if (PersonalRadio::volume > 1.0f) PersonalRadio::volume = 1.0f;

    Events::drawHudEvent.after += []() { PersonalRadio::DrawOverlay(); };
    Events::touchScreenEvent.after += [](int type, int finger, int x, int y)
    {
        PersonalRadio::HandleTouch(type, finger, x, y);
    };
    Events::processScriptsEvent += []() { PersonalRadio::OnFrame(); };

    logger->Info("PERSONAL RADIO 0.3.4 cargado. Indicador visible y controles tactiles superiores.");
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
