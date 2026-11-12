#include "debug_log.h"

#include <QProcessEnvironment>
#include <cstdio>

namespace jrunner {

bool debugLogEnabled()
{
    static const bool enabled = QProcessEnvironment::systemEnvironment()
        .value(QStringLiteral("JRUNNER_DEBUG")) == QLatin1String("1");
    return enabled;
}

void debugLog(const QString &category, const QString &message)
{
    if (!debugLogEnabled())
        return;
    std::fprintf(stderr, "[jrunner:%s] %s\n",
                 category.toUtf8().constData(),
                 message.toUtf8().constData());
}

} // namespace jrunner
