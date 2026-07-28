#include "HardwareManager.h"
#include "Logger.h"
#include <QDebug>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QSysInfo>
#include <QStringList>
#include <QSet>
#include <QThreadPool>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#include <intrin.h>
#include <dxgi1_6.h>
#include <vector>

#pragma comment(lib, "dxgi.lib")
#endif

namespace LAStudio {

namespace {

QStringList requiredCpuFeatures(const QVariantMap &runtime)
{
    QStringList values;
    const QVariant raw = runtime.value(QStringLiteral("requiredCpuFeatures"));
    for (const QVariant &value : raw.toList()) {
        const QString feature = value.toString().trimmed().toUpper();
        if (!feature.isEmpty()) values.append(feature);
    }
    if (values.isEmpty() && raw.metaType().id() == QMetaType::QString) {
        for (const QString &value : raw.toString().split(u',', Qt::SkipEmptyParts)) {
            const QString feature = value.trimmed().toUpper();
            if (!feature.isEmpty()) values.append(feature);
        }
    }
    values.removeDuplicates();
    return values;
}

QSet<QString> detectedCpuFeatures(const QString &flags)
{
    QSet<QString> features;
    for (const QString &flag : flags.split(u' ', Qt::SkipEmptyParts)) {
        features.insert(flag.trimmed().toUpper());
    }
    return features;
}

bool versionAtLeast(const QString &actual, const QString &minimum)
{
    const auto parse = [](QString value, QList<int> *parts) {
        value = value.trimmed();
        if (value.startsWith(u'v', Qt::CaseInsensitive)) value.remove(0, 1);
        const QStringList rawParts = value.split(u'.', Qt::KeepEmptyParts);
        if (rawParts.isEmpty() || rawParts.size() > 4) return false;
        for (const QString &part : rawParts) {
            bool ok = false;
            const int number = part.toInt(&ok);
            if (!ok || number < 0) return false;
            parts->append(number);
        }
        while (parts->size() < 4) parts->append(0);
        return true;
    };

    QList<int> actualParts;
    QList<int> minimumParts;
    if (!parse(actual, &actualParts) || !parse(minimum, &minimumParts)) return false;
    for (int i = 0; i < actualParts.size(); ++i) {
        if (actualParts.at(i) != minimumParts.at(i)) return actualParts.at(i) > minimumParts.at(i);
    }
    return true;
}

#ifdef Q_OS_WIN
bool osEnablesAvxState()
{
    int cpuInfo[4] = {0};
    __cpuidex(cpuInfo, 1, 0);
    const bool hasOsXsave = (static_cast<unsigned int>(cpuInfo[2]) & (1u << 27)) != 0;
    const bool hasAvx = (static_cast<unsigned int>(cpuInfo[2]) & (1u << 28)) != 0;
    return hasOsXsave && hasAvx && (_xgetbv(0) & 0x6) == 0x6;
}

bool osEnablesAvx512State()
{
    return osEnablesAvxState() && (_xgetbv(0) & 0xe6) == 0xe6;
}

QString driverVersionString(const LARGE_INTEGER &version)
{
    const quint32 high = static_cast<quint32>(version.HighPart);
    const quint32 low = version.LowPart;
    return QStringLiteral("%1.%2.%3.%4")
        .arg(HIWORD(high)).arg(LOWORD(high)).arg(HIWORD(low)).arg(LOWORD(low));
}

struct GpuInventory {
    QVariantList gpus;
    double vramTotal = 0.0;
    bool canPollVramUsage = false;
};

bool queryIntelIntegratedInventory(GpuInventory *inventory)
{
    bool foundIntelAdapter = false;
    QSet<QString> seenNames;
    for (DWORD index = 0;; ++index) {
        DISPLAY_DEVICEW device{};
        device.cb = sizeof(device);
        if (!EnumDisplayDevicesW(nullptr, index, &device, 0)) break;
        if ((device.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) != 0) continue;

        const QString name = QString::fromWCharArray(device.DeviceString).trimmed();
        if (name.isEmpty()
            || name.contains(QStringLiteral("Microsoft"), Qt::CaseInsensitive)) {
            continue;
        }
        if (!name.contains(QStringLiteral("Intel"), Qt::CaseInsensitive)) {
            return false;
        }

        const QString key = name.toCaseFolded();
        if (seenNames.contains(key)) continue;
        seenNames.insert(key);
        QVariantMap gpu;
        gpu.insert(QStringLiteral("name"), name);
        gpu.insert(QStringLiteral("vram"), 0.0);
        inventory->gpus.append(gpu);
        foundIntelAdapter = true;
    }
    return foundIntelAdapter;
}

bool isIntelIntegratedAdapter(const DXGI_ADAPTER_DESC1 &desc)
{
    return QString::fromWCharArray(desc.Description)
               .contains(QStringLiteral("Intel"), Qt::CaseInsensitive)
        && desc.DedicatedVideoMemory < 1024ull * 1024ull * 1024ull;
}

GpuInventory queryGpuInventory()
{
    GpuInventory inventory;
    IDXGIFactory1 *factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                  reinterpret_cast<void **>(&factory)))) {
        return inventory;
    }

    IDXGIAdapter1 *adapter = nullptr;
    for (UINT index = 0;
         factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND;
         ++index) {
        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            adapter->Release();
            continue;
        }

        QVariantMap gpu;
        gpu.insert(QStringLiteral("name"), QString::fromWCharArray(desc.Description));
        gpu.insert(QStringLiteral("vram"),
                   static_cast<double>(desc.DedicatedVideoMemory)
                       / (1024.0 * 1024.0 * 1024.0));
        LARGE_INTEGER driverVersion{};
        if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driverVersion))) {
            gpu.insert(QStringLiteral("driverVersion"), driverVersionString(driverVersion));
        }
        inventory.vramTotal += gpu.value(QStringLiteral("vram")).toDouble();
        inventory.gpus.append(gpu);

        // DXGI's dynamic local-memory query is unreliable on UMA Intel adapters.
        // The UI does not need that number to select a runtime, so do not poll it.
        if (!isIntelIntegratedAdapter(desc)) {
            inventory.canPollVramUsage = true;
        }
        adapter->Release();
    }
    factory->Release();
    return inventory;
}

double queryDiscreteVramUsage()
{
    double vramUsed = 0.0;
    IDXGIFactory1 *factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                  reinterpret_cast<void **>(&factory)))) {
        return vramUsed;
    }

    IDXGIAdapter1 *adapter = nullptr;
    for (UINT index = 0;
         factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND;
         ++index) {
        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            || isIntelIntegratedAdapter(desc)) {
            adapter->Release();
            continue;
        }

        IDXGIAdapter3 *adapter3 = nullptr;
        if (SUCCEEDED(adapter->QueryInterface(__uuidof(IDXGIAdapter3),
                                              reinterpret_cast<void **>(&adapter3)))) {
            DXGI_QUERY_VIDEO_MEMORY_INFO memInfo{};
            if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(
                    0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memInfo))) {
                vramUsed += static_cast<double>(memInfo.CurrentUsage)
                    / (1024.0 * 1024.0 * 1024.0);
            }
            adapter3->Release();
        }
        adapter->Release();
    }
    factory->Release();
    return vramUsed;
}
#endif

} // namespace

static HardwareManager* s_instance = nullptr;
static QMutex s_instanceMutex;

HardwareManager::HardwareManager(QObject *parent) : QObject(parent) {
    s_instance = this;
    detectHardware();

    m_usageTimer = new QTimer(this);
    connect(m_usageTimer, &QTimer::timeout, this, &HardwareManager::updateResourceUsage);
    m_usageTimer->start(2000);
}

HardwareManager* HardwareManager::instance() {
    QMutexLocker locker(&s_instanceMutex);
    if (!s_instance) {
        s_instance = new HardwareManager();
    }
    return s_instance;
}

HardwareManager* HardwareManager::create(QQmlEngine *, QJSEngine *) {
    return instance();
}

QVariantMap HardwareManager::runtimeCompatibility(const QVariantMap &runtime) const {
    const QString id = runtime.value(QStringLiteral("id")).toString().toLower();
    const QString name = runtime.value(QStringLiteral("name")).toString().toLower();
    const QString label = runtime.value(QStringLiteral("label")).toString().toLower();
    const QString asset = runtime.value(QStringLiteral("asset")).toString().toLower();
    const QString haystack = QStringList{id, name, label, asset}.join(QStringLiteral(" "));
    const QStringList requiredFeatures = requiredCpuFeatures(runtime);
    const QSet<QString> detectedFeatures = detectedCpuFeatures(m_cpuFlags);
    QStringList missingFeatures;
    for (const QString &feature : requiredFeatures) {
        if (!detectedFeatures.contains(feature)) missingFeatures.append(feature);
    }

    QVariantMap result;
    result.insert(QStringLiteral("compatible"), true);
    result.insert(QStringLiteral("kind"), QStringLiteral("cpu"));
    result.insert(QStringLiteral("title"), QStringLiteral("Compatible"));
    result.insert(QStringLiteral("detail"), QStringLiteral("Runs on the detected CPU."));

    const QString disabledReason = runtime.value(QStringLiteral("disabledReason")).toString().trimmed();
    if (!disabledReason.isEmpty()) {
        result.insert(QStringLiteral("compatible"), false);
        result.insert(QStringLiteral("title"), QStringLiteral("Unavailable"));
        result.insert(QStringLiteral("detail"), disabledReason);
        return result;
    }

    if (!missingFeatures.isEmpty()) {
        result.insert(QStringLiteral("compatible"), false);
        result.insert(QStringLiteral("title"), QStringLiteral("Unavailable"));
        result.insert(QStringLiteral("detail"),
                      QStringLiteral("Requires CPU feature(s): %1.").arg(missingFeatures.join(QStringLiteral(", "))));
        return result;
    }

    const QString minimumDriver = runtime.value(QStringLiteral("minDriverVersion")).toString().trimmed();
    const auto hasMinimumDriver = [this, &minimumDriver](const auto &matchesGpu) {
        if (minimumDriver.isEmpty()) return true;
        for (const QVariant &gpuValue : m_gpus) {
            const QVariantMap gpu = gpuValue.toMap();
            if (!matchesGpu(gpu)) continue;
            const QString driver = gpu.value(QStringLiteral("driverVersion")).toString();
            if (versionAtLeast(driver, minimumDriver)) return true;
        }
        return false;
    };

    if (haystack.contains(QStringLiteral("cuda"))) {
        const bool hasNvidiaGpu = std::any_of(m_gpus.cbegin(), m_gpus.cend(), [](const QVariant &gpuValue) {
            const QVariantMap gpu = gpuValue.toMap();
            return gpu.value(QStringLiteral("name")).toString().contains(QStringLiteral("NVIDIA"), Qt::CaseInsensitive);
        });
        const bool driverCompatible = hasMinimumDriver([](const QVariantMap &gpu) {
            return gpu.value(QStringLiteral("name")).toString().contains(QStringLiteral("NVIDIA"), Qt::CaseInsensitive);
        });

        result.insert(QStringLiteral("kind"), QStringLiteral("cuda"));
        result.insert(QStringLiteral("compatible"), hasNvidiaGpu && driverCompatible);
        result.insert(QStringLiteral("title"), hasNvidiaGpu && driverCompatible ? QStringLiteral("Compatible") : QStringLiteral("Unavailable"));
        result.insert(QStringLiteral("detail"), !hasNvidiaGpu
            ? QStringLiteral("Requires an NVIDIA CUDA-capable GPU.")
            : !driverCompatible
            ? QStringLiteral("Requires NVIDIA driver version %1 or newer.").arg(minimumDriver)
            : QStringLiteral("NVIDIA GPU detected."));
        return result;
    }

    if (haystack.contains(QStringLiteral("vulkan"))) {
        const bool hasHardwareGpu = !m_gpus.isEmpty();
        const bool driverCompatible = hasMinimumDriver([](const QVariantMap &) { return true; });
        bool isCompatible = hasHardwareGpu && driverCompatible;
        QString detail = QStringLiteral("Hardware GPU detected.");

        // Intel integrated GPUs have driver bugs/limitations that crash the Omnivoice Vulkan backend
        if (haystack.contains(QStringLiteral("omnivoice"))) {
            bool hasIntelGpu = std::any_of(m_gpus.cbegin(), m_gpus.cend(), [](const QVariant &gpuValue) {
                const QVariantMap gpu = gpuValue.toMap();
                return gpu.value(QStringLiteral("name")).toString().contains(QStringLiteral("Intel"), Qt::CaseInsensitive);
            });
            if (hasIntelGpu) {
                isCompatible = false;
                detail = QStringLiteral("OmniVoice Vulkan backend is unstable and disabled on Intel integrated GPUs.");
            }
        }

        if (hasHardwareGpu && !driverCompatible) {
            detail = QStringLiteral("Requires a GPU driver version %1 or newer.").arg(minimumDriver);
        }

        result.insert(QStringLiteral("kind"), QStringLiteral("vulkan"));
        result.insert(QStringLiteral("compatible"), isCompatible);
        result.insert(QStringLiteral("title"), isCompatible ? QStringLiteral("Compatible") : QStringLiteral("Unavailable"));
        result.insert(QStringLiteral("detail"), isCompatible ? detail : (hasHardwareGpu ? detail : QStringLiteral("Requires a hardware GPU with Vulkan support.")));
        return result;
    }

    if (haystack.contains(QStringLiteral("hip")) || haystack.contains(QStringLiteral("radeon"))) {
        const bool hasAmdGpu = std::any_of(m_gpus.cbegin(), m_gpus.cend(), [](const QVariant &gpuValue) {
            const QString gpuName = gpuValue.toMap().value(QStringLiteral("name")).toString();
            return gpuName.contains(QStringLiteral("AMD"), Qt::CaseInsensitive) ||
                gpuName.contains(QStringLiteral("Radeon"), Qt::CaseInsensitive);
        });
        const bool driverCompatible = hasMinimumDriver([](const QVariantMap &gpu) {
            const QString name = gpu.value(QStringLiteral("name")).toString();
            return name.contains(QStringLiteral("AMD"), Qt::CaseInsensitive)
                || name.contains(QStringLiteral("Radeon"), Qt::CaseInsensitive);
        });
        result.insert(QStringLiteral("kind"), QStringLiteral("hip"));
        result.insert(QStringLiteral("compatible"), hasAmdGpu && driverCompatible);
        result.insert(QStringLiteral("title"), hasAmdGpu && driverCompatible ? QStringLiteral("Compatible") : QStringLiteral("Unavailable"));
        result.insert(QStringLiteral("detail"), !hasAmdGpu
            ? QStringLiteral("Requires a supported AMD Radeon GPU.")
            : !driverCompatible
            ? QStringLiteral("Requires AMD driver version %1 or newer.").arg(minimumDriver)
            : QStringLiteral("AMD Radeon GPU detected."));
        return result;
    }

    if (haystack.contains(QStringLiteral("sycl"))) {
        const bool hasIntelGpu = std::any_of(m_gpus.cbegin(), m_gpus.cend(), [](const QVariant &gpuValue) {
            return gpuValue.toMap().value(QStringLiteral("name")).toString()
                .contains(QStringLiteral("Intel"), Qt::CaseInsensitive);
        });
        result.insert(QStringLiteral("kind"), QStringLiteral("sycl"));
        result.insert(QStringLiteral("compatible"), hasIntelGpu);
        result.insert(QStringLiteral("title"), hasIntelGpu ? QStringLiteral("Compatible") : QStringLiteral("Unavailable"));
        result.insert(QStringLiteral("detail"), hasIntelGpu
            ? QStringLiteral("Intel GPU detected.")
            : QStringLiteral("Requires a supported Intel GPU."));
        return result;
    }

    if (haystack.contains(QStringLiteral("openvino"))) {
        const bool hasIntelDevice = m_cpuName.contains(QStringLiteral("Intel"), Qt::CaseInsensitive) ||
            std::any_of(m_gpus.cbegin(), m_gpus.cend(), [](const QVariant &gpuValue) {
                return gpuValue.toMap().value(QStringLiteral("name")).toString()
                    .contains(QStringLiteral("Intel"), Qt::CaseInsensitive);
            });
        result.insert(QStringLiteral("kind"), QStringLiteral("openvino"));
        result.insert(QStringLiteral("compatible"), hasIntelDevice);
        result.insert(QStringLiteral("title"), hasIntelDevice ? QStringLiteral("Compatible") : QStringLiteral("Unavailable"));
        result.insert(QStringLiteral("detail"), hasIntelDevice
            ? QStringLiteral("Intel CPU or GPU detected.")
            : QStringLiteral("Requires a supported Intel CPU or GPU."));
        return result;
    }

    const bool supportedCpu = m_cpuArchitecture.compare(QStringLiteral("x86_64"), Qt::CaseInsensitive) == 0
        || m_cpuArchitecture.compare(QStringLiteral("arm64"), Qt::CaseInsensitive) == 0;
    result.insert(QStringLiteral("compatible"), supportedCpu);
    result.insert(QStringLiteral("title"), supportedCpu ? QStringLiteral("Compatible") : QStringLiteral("Unavailable"));
    result.insert(QStringLiteral("detail"), supportedCpu
        ? QStringLiteral("Uses system RAM and CPU instructions.")
        : QStringLiteral("Requires a supported 64-bit CPU."));
    return result;
}

void HardwareManager::detectHardware() {
#ifdef Q_OS_WIN
    // CPU Name
    int cpuInfo[4] = { -1 };
    char cpuBrandString[0x40];
    memset(cpuBrandString, 0, sizeof(cpuBrandString));
    __cpuid(cpuInfo, 0x80000002);
    memcpy(cpuBrandString, cpuInfo, sizeof(cpuInfo));
    __cpuid(cpuInfo, 0x80000003);
    memcpy(cpuBrandString + 16, cpuInfo, sizeof(cpuInfo));
    __cpuid(cpuInfo, 0x80000004);
    memcpy(cpuBrandString + 32, cpuInfo, sizeof(cpuInfo));
    m_cpuName = QString::fromLatin1(cpuBrandString).trimmed();

    // CPU Architecture
    SYSTEM_INFO sysInfo;
    GetNativeSystemInfo(&sysInfo);
    if (sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64)
        m_cpuArchitecture = "x86_64";
    else if (sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64)
        m_cpuArchitecture = "ARM64";
    else if (sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL)
        m_cpuArchitecture = "x86";
    else
        m_cpuArchitecture = "Unknown";

    // AVX-family instructions need both CPU support and OS-enabled extended
    // register state. Advertising them from CPUID alone can select a runtime
    // that faults on a system where XMM/YMM/ZMM state is not enabled.
    QStringList flags;
    const bool avxEnabled = osEnablesAvxState();
    __cpuidex(cpuInfo, 1, 0);
    if (avxEnabled && (static_cast<unsigned int>(cpuInfo[2]) & (1u << 28))) flags << "AVX";

    __cpuidex(cpuInfo, 7, 0);
    if (avxEnabled && (static_cast<unsigned int>(cpuInfo[1]) & (1u << 5))) flags << "AVX2";
    if (osEnablesAvx512State() && (static_cast<unsigned int>(cpuInfo[1]) & (1u << 16))) flags << "AVX512";
    
    m_cpuFlags = flags.join(" ");

    // RAM Total
    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memStatus)) {
        m_ramTotal = static_cast<double>(memStatus.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0);
    }

#else
    m_cpuName = QSysInfo::prettyProductName();
    m_cpuArchitecture = QSysInfo::currentCpuArchitecture();
    m_cpuFlags = "Unknown";
#endif
    // CPU and RAM calls are cheap and need to be immediately available to the
    // runtime chooser. DXGI can synchronously block in a graphics driver, so it
    // is deliberately isolated from the GUI thread below.
    scheduleGpuDetection();
}

void HardwareManager::scheduleGpuDetection()
{
#ifdef Q_OS_WIN
    if (m_gpuDetectionComplete || m_gpuDetectionInFlight) return;
    // GPU enumeration is advisory: local execution is CPU-only and no picker
    // interaction may wait for a display-driver call.  EnumDisplayDevices can
    // block just as DXGI can on some Intel Iris drivers, so *all* inventory
    // work belongs on a worker thread.  The UI starts with CPU compatibility
    // and refreshes after this queued result arrives.
    m_gpuDetectionInFlight = true;
    const QPointer<HardwareManager> guard(this);
    QThreadPool::globalInstance()->start([guard] {
        GpuInventory inventory;
        if (!queryIntelIntegratedInventory(&inventory)) {
            inventory = queryGpuInventory();
        }
        if (!guard) return;
        QMetaObject::invokeMethod(guard.data(), [guard, inventory] {
            if (guard) {
                guard->completeGpuDetection(inventory.gpus, inventory.vramTotal,
                                            inventory.canPollVramUsage);
            }
        }, Qt::QueuedConnection);
    });
#else
    completeGpuDetection({}, 0.0, false);
#endif
}

void HardwareManager::completeGpuDetection(const QVariantList &gpus, double vramTotal,
                                           bool canPollVramUsage)
{
    if (m_gpuDetectionComplete) return;
    m_gpuDetectionInFlight = false;
    m_gpuDetectionComplete = true;
    m_gpus = gpus;
    m_vramTotal = vramTotal;
    m_canPollVramUsage = canPollVramUsage;

    Logger::info(QStringLiteral("Hardware"), QStringLiteral("Hardware Detection Finished:"));
    Logger::info(QStringLiteral("Hardware"), QStringLiteral("  CPU: %1 (%2)").arg(m_cpuName, m_cpuArchitecture));
    Logger::info(QStringLiteral("Hardware"), QStringLiteral("  CPU Flags: %1").arg(m_cpuFlags));
    Logger::info(QStringLiteral("Hardware"), QStringLiteral("  RAM Total: %1 GB").arg(m_ramTotal, 0, 'f', 2));
    Logger::info(QStringLiteral("Hardware"), QStringLiteral("  GPUs Count: %1, VRAM Total: %2 GB").arg(m_gpus.size()).arg(m_vramTotal, 0, 'f', 2));
    for (int i = 0; i < m_gpus.size(); ++i) {
        QVariantMap gpu = m_gpus.at(i).toMap();
        Logger::info(QStringLiteral("Hardware"), QStringLiteral("    GPU %1: %2 (%3 GB VRAM)").arg(i).arg(gpu["name"].toString()).arg(gpu["vram"].toDouble(), 0, 'f', 2));
    }

    emit hardwareInfoChanged();
}

void HardwareManager::updateResourceUsage() {
    updateCpuUsage();
    updateRamUsage();
    scheduleVramUsageUpdate();
    emit resourceUsageChanged();
}

void HardwareManager::updateCpuUsage() {
#ifdef Q_OS_WIN
    FILETIME idleTime, kernelTime, userTime;
    if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        unsigned long long idle = (static_cast<unsigned long long>(idleTime.dwHighDateTime) << 32) | idleTime.dwLowDateTime;
        unsigned long long kernel = (static_cast<unsigned long long>(kernelTime.dwHighDateTime) << 32) | kernelTime.dwLowDateTime;
        unsigned long long user = (static_cast<unsigned long long>(userTime.dwHighDateTime) << 32) | userTime.dwLowDateTime;

        if (m_lastIdleTime > 0) {
            unsigned long long idleDiff = idle - m_lastIdleTime;
            unsigned long long kernelDiff = kernel - m_lastKernelTime;
            unsigned long long userDiff = user - m_lastUserTime;
            unsigned long long totalDiff = kernelDiff + userDiff;

            if (totalDiff > 0) {
                m_cpuUsage = 100.0 * (totalDiff - idleDiff) / totalDiff;
            }
        }

        m_lastIdleTime = idle;
        m_lastKernelTime = kernel;
        m_lastUserTime = user;
    }
#endif
}

void HardwareManager::updateRamUsage() {
#ifdef Q_OS_WIN
    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memStatus)) {
        m_ramUsed = static_cast<double>(memStatus.ullTotalPhys - memStatus.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
    }
#endif
}

void HardwareManager::scheduleVramUsageUpdate() {
#ifdef Q_OS_WIN
    if (!m_gpuDetectionComplete || !m_canPollVramUsage || m_vramUsageProbeInFlight) return;
    m_vramUsageProbeInFlight = true;
    const QPointer<HardwareManager> guard(this);
    QThreadPool::globalInstance()->start([guard] {
        const double vramUsed = queryDiscreteVramUsage();
        if (!guard) return;
        QMetaObject::invokeMethod(guard.data(), [guard, vramUsed] {
            if (guard) guard->completeVramUsageUpdate(vramUsed);
        }, Qt::QueuedConnection);
    });
#endif
}

void HardwareManager::completeVramUsageUpdate(double vramUsed)
{
    m_vramUsageProbeInFlight = false;
    if (!qFuzzyCompare(m_vramUsed + 1.0, vramUsed + 1.0)) {
        m_vramUsed = vramUsed;
        emit resourceUsageChanged();
    }
}

} // namespace LAStudio
