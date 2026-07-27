#pragma once

namespace LAStudio::CrashHandler {

// Installs a Windows unhandled-exception filter and terminate handler that
// write local minidumps under the application's data directory. No dump is
// uploaded or transmitted by the application.
void initialize();

} // namespace LAStudio::CrashHandler
