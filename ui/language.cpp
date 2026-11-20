// SPDX-License-Identifier: LGPL-2.1-or-later
#include "language.h"

#include <QCoreApplication>
#include <QLibraryInfo>

bool Language::setRussian(bool enable) {
    if (enable == installed_) {
        return true;
    }
    if (enable) {
        if (translator_.isEmpty() && !translator_.load(":/i18n/planedraft_ru.qm")) {
            return false;
        }
        if (qt_.isEmpty() &&
            !qt_.load("qtbase_ru", QCoreApplication::applicationDirPath() + "/translations") &&
            !qt_.load("qtbase_ru", QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
            return false;
        if (!QCoreApplication::installTranslator(&qt_))
            return false;
        installed_ = QCoreApplication::installTranslator(&translator_);
        if (!installed_)
            QCoreApplication::removeTranslator(&qt_);
    } else {
        QCoreApplication::removeTranslator(&translator_);
        QCoreApplication::removeTranslator(&qt_);
        installed_ = false;
    }
    return installed_ == enable;
}
