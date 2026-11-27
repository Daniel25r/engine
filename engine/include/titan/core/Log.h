#pragma once
// Registro de mensajes del motor. Cada mensaje tiene nivel, categoría y marca de tiempo.
// Los "sinks" reciben cada mensaje (consola del editor, archivo, logcat...).
// Se guardan los últimos mensajes en memoria para poder mostrarlos más tarde.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace titan {

enum class LogLevel { Info, Warning, Error };

struct LogMessage {
    LogLevel level;
    std::string category;
    std::string text;
    double seconds; // desde que arrancó el proceso
};

class Log {
public:
    using Sink = std::function<void(const LogMessage&)>;

    static void write(LogLevel level, const char* category, const char* fmt, ...)
#if defined(__GNUC__)
        __attribute__((format(printf, 3, 4)))
#endif
        ;

    static int addSink(Sink sink);
    static void removeSink(int id);
    // Mensajes recientes (como máximo 1000).
    static std::vector<LogMessage> recent();
    // Activa/desactiva la salida estándar de la plataforma (stdout / logcat).
    static void setPlatformOutput(bool enabled);
    // Copia todos los mensajes a un archivo de texto (para enviar un informe de errores desde un teléfono).
    // Si ya existe y pasa de maxBytes, se renombra a <ruta>.anterior y se empieza uno nuevo.
    // Los avisos y errores se escriben al disco al momento (sobreviven a un cierre inesperado).
    static bool openFile(const std::string& path, size_t maxBytes = 1024 * 1024);
    static void closeFile();
};

} // namespace titan

#define TITAN_INFO(cat, ...) ::titan::Log::write(::titan::LogLevel::Info, cat, __VA_ARGS__)
#define TITAN_WARN(cat, ...) ::titan::Log::write(::titan::LogLevel::Warning, cat, __VA_ARGS__)
#define TITAN_ERROR(cat, ...) ::titan::Log::write(::titan::LogLevel::Error, cat, __VA_ARGS__)
