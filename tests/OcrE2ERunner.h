#pragma once

namespace LAStudio {

// Deliberately separate from QtTest: this is an opt-in, code-only acceptance
// runner which drives the same production OCR and Dubbing controllers used by
// the desktop application.  It must never be invoked by CI with fixture data.
bool isOcrE2EInvocation(int argc, char *argv[]);
int runOcrE2E(int argc, char *argv[]);
bool isOcrE2EArtifactReportInvocation(int argc, char *argv[]);
int runOcrE2EArtifactReport(int argc, char *argv[]);

} // namespace LAStudio
