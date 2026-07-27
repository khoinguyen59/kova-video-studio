// SPDX-License-Identifier: Apache-2.0 AND MIT
// SPDX-FileCopyrightText: 2024 Vietnamese Normalizer Contributors

#pragma once

#include <QHash>
#include <QString>

namespace vietnorm::detail {

struct DictionaryStore {
    QHash<QString, QString> acronyms;
    QHash<QString, QString> foreignWords;

    static DictionaryStore builtIn();
    static bool loadCsv(const QString &path, bool acronymFile,
                        DictionaryStore &store, QString *error);
    static bool loadDirectory(const QString &directory,
                              DictionaryStore &store, QString *error);
};

} // namespace vietnorm::detail
