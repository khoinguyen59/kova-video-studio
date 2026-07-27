// SPDX-License-Identifier: Apache-2.0 AND MIT
// SPDX-FileCopyrightText: 2024 Vietnamese Normalizer Contributors

#pragma once

namespace vietnorm {

enum class Profile {
    Compatibility023,
    SafeVietnameseTtsV1,
};

struct NormalizationOptions {
    Profile profile = Profile::SafeVietnameseTtsV1;
    bool enablePreprocessing = true;
    bool enableTransliteration = false;
};

} // namespace vietnorm
