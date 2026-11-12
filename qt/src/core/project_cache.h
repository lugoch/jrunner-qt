#pragma once

#include "models.h"

namespace jrunner {

struct ProjectSnapshot {
    QList<Project> projects;
    QString selectedProjectPath;
    QString selectedModulePath;
};

namespace ProjectCache {

inline constexpr int kVersion = 1;

QString path();
// An empty snapshot is valid and must not trigger another startup scan.
std::optional<ProjectSnapshot> load(const QString &filePath = path());
bool save(const ProjectSnapshot &snapshot, const QString &filePath = path());

} // namespace ProjectCache
} // namespace jrunner
