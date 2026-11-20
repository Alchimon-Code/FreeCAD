// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <QTranslator>

// English is the source language; the Russian .qm is an embedded resource.
class Language {
  public:
    bool setRussian(bool enable);
    [[nodiscard]] bool russian() const { return installed_; }

  private:
    QTranslator translator_, qt_;
    bool installed_ = false;
};
